#include "file_descriptor.hpp"
#include <system_error>
#include <cerrno>
#include <utility>
#include <string>
#include <fcntl.h>
#include <unistd.h>

FileDescriptor::FileDescriptor(const char* path, int flags)
: fd(open(path, flags))
{
    if (this->fd == -1) {
        throw std::system_error(
            errno,
            std::generic_category(),
            "(FileDescriptor) Open file " + std::string(path)
        );
    }
}

FileDescriptor::FileDescriptor(int fd)
: fd(fd)
{}

FileDescriptor::FileDescriptor(FileDescriptor&& other) noexcept
: fd(std::exchange(other.fd, -1))
{}

auto FileDescriptor::operator=(FileDescriptor&& other) noexcept
-> FileDescriptor&
{
    if (this->fd != -1) {
        close(this->fd);
    }

    this->fd = std::exchange(other.fd, -1);
    return *this;
}

FileDescriptor::operator int() const
{
    return this->fd;
}

FileDescriptor::~FileDescriptor()
{
    if (this->fd != -1) {
        close(this->fd);
        this->fd = -1;
    }
}
