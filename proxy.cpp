#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include <sys/signalfd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>

#include "fd.h"
#include "bidirectional_connection.h"

fd tcp_socket(int address_family)
{
    fd sock = fd::wrap(socket(address_family, SOCK_STREAM, IPPROTO_TCP));
    if (!sock.valid()) {
        perror("socket");
    }

    return sock;
}

bool enable_keepalive(fd &sock)
{
    int on = 1;
    if (setsockopt(sock.get(), SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on)) != 0) {
        perror("setsockopt(SO_KEEPALIVE)");
        return false;
    }

    return true;
}

std::unique_ptr<sockaddr, decltype(&free)> temporary_sockaddr_buf(socklen_t addrlen)
{
    return std::unique_ptr<sockaddr, decltype(&free)>(static_cast<sockaddr *>(malloc(addrlen)), &free);
}

void print_sockaddr(const char *msg, const sockaddr *addr, socklen_t addrlen)
{
    char host[NI_MAXHOST], serv[NI_MAXSERV];
    int err = getnameinfo(addr, addrlen, host, sizeof(host), serv, sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV);
    if (err == 0) {
        fprintf(stderr, "%s: %s:%s\n", msg, host, serv);
    } else {
        fprintf(stderr, "%s: <%s>\n", msg, gai_strerror(err));
    }
}

std::unique_ptr<bidirectional_connection>
setup_connection(const fd &listening_sock, socklen_t listener_addrlen, const addrinfo &dest_addr)
{
    socklen_t client_addrlen = listener_addrlen;
    auto client_addr_buf = temporary_sockaddr_buf(client_addrlen);

    fd client_sock = fd::wrap(accept(listening_sock.get(), client_addr_buf.get(), &client_addrlen));
    if (!client_sock.valid()) {
        perror("accept");
        return nullptr;
    }

    print_sockaddr("New client", client_addr_buf.get(), client_addrlen);

    fd dest_sock = tcp_socket(dest_addr.ai_family);
    if (!dest_sock.valid()) {
        return nullptr;
    }

    if (!client_sock.make_nonblocking() || !dest_sock.make_nonblocking()) {
        return nullptr;
    }

    if (!enable_keepalive(client_sock) || !enable_keepalive(dest_sock)) {
        return nullptr;
    }

    if (connect(dest_sock.get(), dest_addr.ai_addr, dest_addr.ai_addrlen) != 0) {
        if (errno != EINPROGRESS) {
            perror("connect");
            return nullptr;
        }
    }

    return std::unique_ptr<bidirectional_connection>(
        new bidirectional_connection(std::move(client_sock), std::move(dest_sock))
    );
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

    print_sockaddr("Resolved destination", dest_addr->ai_addr, dest_addr->ai_addrlen);

    fd listening_sock = tcp_socket(listen_addr->ai_family);
    if (!listening_sock.valid()) {
        return EXIT_FAILURE;
    }

    if (bind(listening_sock.get(), listen_addr->ai_addr, listen_addr->ai_addrlen) != 0) {
        perror("bind");
        return EXIT_FAILURE;
    }

    print_sockaddr("Listening on", listen_addr->ai_addr, listen_addr->ai_addrlen);

    if (listen(listening_sock.get(), 1000) != 0) {
        perror("listen");
        return EXIT_FAILURE;
    }

    sigset_t quit_signals;
    sigemptyset(&quit_signals);
    sigaddset(&quit_signals, SIGINT);
    sigaddset(&quit_signals, SIGTERM);

    fd sigfd = fd::wrap(signalfd(-1, &quit_signals, SFD_NONBLOCK));
    if (!sigfd.valid()) {
        perror("signalfd");
        return EXIT_FAILURE;
    }

    if (sigprocmask(SIG_BLOCK, &quit_signals, NULL) != 0) {
        perror("sigprocmask");
        return EXIT_FAILURE;
    }

    std::vector<pollfd> pollfds;
    std::vector<std::unique_ptr<bidirectional_connection>> prev_connections, connections;

    for (;;) {
        pollfds.clear();

        {
            pollfd signal_pfd;
            signal_pfd.fd = sigfd.get();
            signal_pfd.events = POLLIN;
            signal_pfd.revents = 0;
            pollfds.push_back(signal_pfd);
        }

        {
            pollfd accept_pfd;
            accept_pfd.fd = listening_sock.get();
            accept_pfd.events = POLLIN;
            accept_pfd.revents = 0;
            pollfds.push_back(accept_pfd);
        }

        connections.swap(prev_connections);
        connections.clear();

        for (auto &conn : prev_connections) {
            if (conn->setup_pollfds(pollfds)) {
                connections.push_back(std::move(conn));
            }
        }

#ifdef TRACE
        fprintf(stderr, "%zu connections\n", connections.size());
#endif

        if (poll(pollfds.data(), pollfds.size(), -1) < 0) {
            perror("poll");
            return EXIT_FAILURE;
        }

        for (auto &conn : connections) {
            conn->handle_pollfds(pollfds);
        }

        if (pollfds[0].revents & POLLIN) {
            signalfd_siginfo info;

            if (read(sigfd.get(), &info, sizeof(info)) == sizeof(info)) {
                psignal(int(info.ssi_signo), "Exiting");
            } else {
                perror("read (signalfd)");
            }

            return EXIT_SUCCESS;
        }

        if (pollfds[1].revents & POLLIN) {
            auto conn = setup_connection(listening_sock, listen_addr->ai_addrlen, *dest_addr);
            if (conn) {
                connections.push_back(std::move(conn));
            }
        }
    }
}
