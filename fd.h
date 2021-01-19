#pragma once

class fd
{
private:
    int value;

public:
    static const int INVALID_VALUE = -1;

    explicit fd(int value = INVALID_VALUE) : value(value)
    {}

    fd(const fd &) = delete;
    fd &operator =(const fd &) = delete;

    ~fd()
    {
        close();
    }

    fd(fd &&rhs) : value(rhs.value)
    {
        rhs.value = INVALID_VALUE;
    }

    fd &operator =(fd &&rhs)
    {
        close();
        value = rhs.value;
        rhs.value = INVALID_VALUE;
        return *this;
    }

    int get() const
    {
        return value;
    }

    bool valid() const
    {
        return value >= 0;
    }

    static fd wrap(int value)
    {
        return fd(value);
    }

    bool close();
};
