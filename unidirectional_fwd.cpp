#include "unidirectional_fwd.h"

#include <cstdio>
#include <cstring>

#include <sys/socket.h>

short unidirectional_forwarder::source_events() const
{
    return (recv_down || buffer.full()) ? 0 : POLLIN;
}

short unidirectional_forwarder::destination_events() const
{
    return (send_down || buffer.empty()) ? 0 : POLLOUT;
}

void unidirectional_forwarder::recv(int sock)
{
    auto nrecv = ::recv(sock, buffer.write_pointer(), buffer.available_write(), 0);

    if (nrecv > 0) {
        buffer.written(nrecv);
#ifdef TRACE
        fprintf(stderr, "fd=%d received %zd\n", sock, nrecv);
#endif
    } else {
        recv_down = true;
#ifdef TRACE
        fprintf(stderr, "fd=%d recv down\n", sock);
#endif

        if (nrecv < 0) {
            perror("recv");
        }
    }
}

void unidirectional_forwarder::send(int sock)
{
    auto nsent = ::send(sock, buffer.read_pointer(), buffer.available_read(), MSG_NOSIGNAL);

    if (nsent > 0) {
        buffer.read(nsent);
#ifdef TRACE
        fprintf(stderr, "fd=%d sent %zd\n", sock, nsent);
#endif
    } else {
        send_down = true;
        recv_down = true;
#ifdef TRACE
        fprintf(stderr, "fd=%d send down\n", sock);
#endif

        perror("send");
    }
}

void unidirectional_forwarder::handle_events(const pollfd *source, const pollfd *destination)
{
    if (source && (source->revents & source_events())) {
        recv(source->fd);
    }

    if (destination && (destination->revents & destination_events())) {
        send(destination->fd);
    }

    if (destination && !send_down && recv_down && buffer.empty()) {
        send_down = true;

#ifdef TRACE
        fprintf(stderr, "fd=%d shutdown(SHUT_WR)\n", destination->fd);
#endif

        if (shutdown(destination->fd, SHUT_WR) != 0) {
            perror("shutdown");
        }
    }
}
