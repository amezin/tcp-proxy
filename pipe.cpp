#include "pipe.h"

#include <cassert>

my_pipe::my_pipe(size_t size)
    : buffer(size),
      buffer_begin(buffer.data()),
      buffer_end(buffer.data() + size),
      data_begin(buffer_begin),
      data_end(buffer_begin),
      data_wraps(false)
{
}

my_pipe::~my_pipe()
{
}

size_t my_pipe::available_read() const
{
    auto size = (data_wraps ? buffer_end : data_end) - data_begin;
    assert(size >= 0);
    return size_t(size);
}

size_t my_pipe::available_write() const
{
    auto size = (data_wraps ? data_begin : buffer_end) - data_end;
    assert(size >= 0);
    return size_t(size);
}

void my_pipe::read(ptrdiff_t size)
{
    assert(size >= 0);
    assert(size_t(size) <= available_read());

    data_begin += size;

    if (data_begin == buffer_end) {
        assert(data_wraps);
        data_wraps = false;
        data_begin = buffer_begin;
    }
}

void my_pipe::written(ptrdiff_t size)
{
    assert(size >= 0);
    assert(size_t(size) <= available_write());

    data_end += size;

    if (data_end == buffer_end) {
        assert(!data_wraps);
        data_wraps = true;
        data_end = buffer_begin;
    }
}
