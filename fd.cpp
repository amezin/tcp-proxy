#include "fd.h"

#include <cstdio>

#include <fcntl.h>
#include <unistd.h>

bool fd::close()
{
    if (!valid()) {
        return true;
    }

    if (::close(value) != 0) {
        value = INVALID_VALUE;
        perror("close");
        return false;
    }

    value = INVALID_VALUE;
    return true;
}

bool fd::make_nonblocking()
{
    int flags = fcntl(value, F_GETFL, 0);
    if (flags != -1 && fcntl(value, F_SETFL, flags | O_NONBLOCK) == 0) {
        return true;
    }

    perror("fcntl(F_SETFL)");
    return false;
}
