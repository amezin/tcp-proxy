#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>

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

    std::vector<pollfd> pollfds;
    bidirectional_connection conn(std::move(client_sock), std::move(dest_sock));

    while (conn.setup_pollfds(pollfds)) {
        if (poll(pollfds.data(), pollfds.size(), -1) < 0) {
            perror("poll");
            return EXIT_FAILURE;
        }

        conn.handle_pollfds(pollfds);
        pollfds.clear();
    }

    return EXIT_SUCCESS;
}
