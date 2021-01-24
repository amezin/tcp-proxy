#pragma once

#include <poll.h>

#include "pipe.h"

class unidirectional_forwarder
{
private:
    static const size_t BUFFER_SIZE = 4096;

    my_pipe buffer{BUFFER_SIZE};
    bool recv_down = false;
    bool send_down = false;

    void recv(int sock);
    void send(int sock);

public:
    bool source_shut_down() const
    {
        return recv_down;
    }

    short source_events() const;

    bool destination_shut_down() const
    {
        return send_down;
    }

    short destination_events() const;

    void handle_events(const pollfd *source, const pollfd *destination);
};
