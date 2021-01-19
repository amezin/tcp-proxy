#include "fd.h"

#include <cstdio>

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
