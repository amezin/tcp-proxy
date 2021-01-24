#pragma once

#include <vector>

class my_pipe
{
private:
    std::vector<char> buffer;
    char *const buffer_begin;
    char *const buffer_end;
    char *data_begin;
    char *data_end;
    bool data_wraps;

public:
    explicit my_pipe(size_t size);
    ~my_pipe();

    char *read_pointer()
    {
        return data_begin;
    }

    char *write_pointer()
    {
        return data_end;
    }

    size_t available_read() const;
    size_t available_write() const;

    void read(ptrdiff_t size);
    void written(ptrdiff_t size);

    bool empty() const
    {
        return data_begin == data_end && !data_wraps;
    }

    bool full() const
    {
        return data_begin == data_end && data_wraps;
    }
};
