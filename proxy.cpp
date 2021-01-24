#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>

#include "fd.h"

static const size_t BUFFER_SIZE = 4096;

fd tcp_socket(int address_family)
{
    fd sock = fd::wrap(socket(address_family, SOCK_STREAM, IPPROTO_TCP));
    if (!sock.valid()) {
        perror("socket");
    }

    return sock;
}

void socket_perror(const fd &sock, const char *msg)
{
    int error = 0;
    socklen_t errlen = sizeof(error);
    if (getsockopt(sock.get(), SOL_SOCKET, SO_ERROR, (void *)&error, &errlen) != 0) {
        perror("getsockopt");
    }
    if (error) {
        fprintf(stderr, "%s: %s\n", msg, strerror(error));
    }
}

typedef std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addrinfo_ptr;

addrinfo_ptr parse_host_port(const char *host, const char *port, bool passive)
{
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = (passive ? AI_PASSIVE : 0) | AI_NUMERICSERV;

    addrinfo *res = nullptr;
    int error = getaddrinfo(host, port, &hints, &res);
    addrinfo_ptr ptr(res, &freeaddrinfo);

    if (error) {
        fprintf(stderr, "Can't parse host '%s' or port '%s'\n", host, port);

        if (error == EAI_SYSTEM) {
            perror("getaddrinfo");
        } else {
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(error));
        }
    }

    return ptr;
}

enum {
    ARG_SELF,
    ARG_LISTEN_ADDR,
    ARG_LISTEN_PORT,
    ARG_DEST_HOST,
    ARG_DEST_PORT,
    ARG_C
};

int main(int argc, char *argv[])
{
    if (argc != ARG_C) {
        fprintf(stderr, "Invalid number of arguments, expected %d, got %d\n", ARG_C, argc);
        fprintf(stderr, "Usage: %s listen_address listen_port destination_host destination_port\n", argv[ARG_SELF]);
        return EXIT_FAILURE;
    }

    auto listen_addr = parse_host_port(argv[ARG_LISTEN_ADDR], argv[ARG_LISTEN_PORT], true);
    if (!listen_addr) {
        return EXIT_FAILURE;
    }

    auto dest_addr = parse_host_port(argv[ARG_DEST_HOST], argv[ARG_DEST_PORT], false);
    if (!dest_addr) {
        return EXIT_FAILURE;
    }

    fd listening_sock = tcp_socket(listen_addr->ai_family);
    if (!listening_sock.valid()) {
        return EXIT_FAILURE;
    }

    if (bind(listening_sock.get(), listen_addr->ai_addr, listen_addr->ai_addrlen) != 0) {
        perror("bind");
        return EXIT_FAILURE;
    }

    if (listen(listening_sock.get(), 1000) != 0) {
        perror("listen");
        return EXIT_FAILURE;
    }

    fd client_sock = fd::wrap(accept(listening_sock.get(), nullptr, nullptr));
    if (!client_sock.valid()) {
        perror("accept");
        return EXIT_FAILURE;
    }

    fd dest_sock = tcp_socket(dest_addr->ai_family);
    if (!dest_sock.valid()) {
        return EXIT_FAILURE;
    }

    if (connect(dest_sock.get(), dest_addr->ai_addr, dest_addr->ai_addrlen) != 0) {
        perror("connect");
        return EXIT_FAILURE;
    }

    if (!client_sock.make_nonblocking() || !dest_sock.make_nonblocking()) {
        return EXIT_FAILURE;
    }

    std::vector<char> buffer(BUFFER_SIZE);
    char *buffer_begin = buffer.data();
    char *buffer_end = buffer.data() + buffer.size();
    char *read_ptr = buffer_begin;
    char *write_ptr = buffer_begin;
    bool read_down = false;

    enum {
        POLLFD_CLIENT_SOCK,
        POLLFD_DEST_SOCK,
        POLLFD_COUNT
    };

    pollfd pollfds[POLLFD_COUNT];

    auto &client_sock_pollfd = pollfds[POLLFD_CLIENT_SOCK];
    client_sock_pollfd.fd = client_sock.get();

    auto &dest_sock_pollfd = pollfds[POLLFD_DEST_SOCK];
    dest_sock_pollfd.fd = dest_sock.get();

    for (;;) {
        client_sock_pollfd.events = short((read_ptr < buffer_end && !read_down) ? POLLIN : 0);
        dest_sock_pollfd.events = short((write_ptr < read_ptr) ? POLLOUT : 0);

        if (poll(pollfds, nfds_t(POLLFD_COUNT), -1) < 0) {
            perror("poll");
            return EXIT_FAILURE;
        }

        if (client_sock_pollfd.revents & POLLERR) {
            socket_perror(client_sock, "client socket");
            return EXIT_FAILURE;
        }

        if (dest_sock_pollfd.revents & (POLLERR | POLLHUP)) {
            socket_perror(dest_sock, "dest socket");
            return EXIT_FAILURE;
        }

        if (client_sock_pollfd.revents & POLLIN) {
            auto nread = recv(client_sock.get(), read_ptr, size_t(buffer_end - read_ptr), 0);
            if (nread < 0) {
                perror("recv");
                return EXIT_FAILURE;
            }
            if (nread == 0) {
                read_down = true;
            }
            read_ptr += nread;
        }

        if (dest_sock_pollfd.revents & POLLOUT) {
            auto nsent = send(dest_sock.get(), write_ptr, size_t(read_ptr - write_ptr), MSG_NOSIGNAL);
            if (nsent <= 0) {
                perror("send");
                return EXIT_FAILURE;
            }
            write_ptr += nsent;
        }

        if (write_ptr == buffer_end) {
            read_ptr = buffer_begin;
            write_ptr = buffer_begin;
        }

        if (write_ptr == read_ptr && read_down) {
            if (shutdown(dest_sock.get(), SHUT_WR) != 0) {
                perror("shutdown");
                return EXIT_FAILURE;
            }

            return EXIT_SUCCESS;
        }
    }

    return EXIT_SUCCESS;
}
