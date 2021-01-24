#include "bidirectional_connection.h"

#include <cassert>
#include <cstdio>
#include <cstring>

#include <sys/socket.h>

#ifdef TRACE

static void trace_pollfd(const char *prefix, const pollfd &pfd)
{
    fprintf(
        stderr,
        "%s: fd=%d events=%s%s%s%s revents=%s%s%s%s\n",
        prefix,
        pfd.fd,
        (pfd.events & POLLIN) ? "POLLIN," : "",
        (pfd.events & POLLOUT) ? "POLLOUT," : "",
        (pfd.events & POLLERR) ? "POLLERR," : "",
        (pfd.events & POLLHUP) ? "POLLHUP," : "",
        (pfd.revents & POLLIN) ? "POLLIN," : "",
        (pfd.revents & POLLOUT) ? "POLLOUT," : "",
        (pfd.revents & POLLERR) ? "POLLERR," : "",
        (pfd.revents & POLLHUP) ? "POLLHUP," : ""
    );
}

#else

static void trace_pollfd(const char *, const pollfd &)
{}

#endif

static void socket_perror(const fd &sock, const char *msg)
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

bidirectional_connection::bidirectional_connection(fd &&client_sock, fd &&dest_sock)
    : client_sock(std::move(client_sock)), dest_sock(std::move(dest_sock))
{
}

bidirectional_connection::~bidirectional_connection()
{
}

bool bidirectional_connection::setup_pollfds(std::vector<pollfd> &pollfds)
{
    client_pollfd_index = setup_pollfd(client_sock, client_to_dest, dest_to_client, pollfds);
    dest_pollfd_index = setup_pollfd(dest_sock, dest_to_client, client_to_dest, pollfds);

    return client_pollfd_index != INVALID_INDEX || dest_pollfd_index != INVALID_INDEX;
}

size_t bidirectional_connection::setup_pollfd(fd &sock, const unidirectional_forwarder &reader, const unidirectional_forwarder &writer, std::vector<pollfd> &pollfds)
{
    if (!sock.valid()) {
        return INVALID_INDEX;
    }

    if (reader.source_shut_down() && writer.destination_shut_down()) {
        sock.close();
        return INVALID_INDEX;
    }

    pollfd pfd;
    pfd.fd = sock.get();
    pfd.events = reader.source_events() | writer.destination_events();
    pfd.revents = 0;
    pollfds.push_back(std::move(pfd));
    return pollfds.size() - 1;
}

void bidirectional_connection::handle_pollfds(const std::vector<pollfd> &pollfds)
{
    auto client_pollfd = get_pollfd(client_pollfd_index, pollfds, client_sock);
    auto dest_pollfd = get_pollfd(dest_pollfd_index, pollfds, dest_sock);

    if (client_pollfd) {
        trace_pollfd("client_pollfd", *client_pollfd);
        if (client_pollfd->revents & POLLERR) {
            socket_perror(client_sock, "client socket");
        }
    }

    if (dest_pollfd) {
        trace_pollfd("dest_pollfd", *dest_pollfd);
        if (dest_pollfd->revents & POLLERR) {
            socket_perror(dest_sock, "dest socket");
        }
    }

    client_to_dest.handle_events(client_pollfd, dest_pollfd);
    dest_to_client.handle_events(dest_pollfd, client_pollfd);
}

const pollfd *bidirectional_connection::get_pollfd(size_t index, const std::vector<pollfd> &pollfds, const fd &expected_sock)
{
    (void)expected_sock;

    if (index == INVALID_INDEX) {
        assert(!expected_sock.valid());
        return nullptr;
    }

    const pollfd &pfd = pollfds[index];
    assert(pfd.fd == expected_sock.get());
    return &pfd;
}
