#pragma once

#include <vector>

#include <poll.h>

#include "fd.h"
#include "unidirectional_fwd.h"

class bidirectional_connection
{
private:
    fd client_sock, dest_sock;
    unidirectional_forwarder client_to_dest, dest_to_client;

    static const size_t INVALID_INDEX = size_t(-1);
    size_t client_pollfd_index = INVALID_INDEX, dest_pollfd_index = INVALID_INDEX;

    size_t setup_pollfd(fd &sock, const unidirectional_forwarder &reader, const unidirectional_forwarder &writer, std::vector<pollfd> &pollfds);
    const pollfd *get_pollfd(size_t index, const std::vector<pollfd> &pollfds, const fd &expected_sock);

public:
    bidirectional_connection(fd &&client_sock, fd &&dest_sock);
    ~bidirectional_connection();

    bool setup_pollfds(std::vector<pollfd> &pollfds);
    void handle_pollfds(const std::vector<pollfd> &pollfds);
};
