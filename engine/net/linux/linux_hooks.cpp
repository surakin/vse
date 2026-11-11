// The Linux half of the POSIX socket backend: the flags and calls that differ between operating systems.

#include <sys/socket.h>

#include "net/posix/posix_net.h"

namespace engine_placeholder::net::posix
{

int OpenSocketFd(int domain, int type)
{
    return ::socket(domain, type | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
}

int AcceptFd(int listener, sockaddr* address, socklen_t* length)
{
    return ::accept4(listener, address, length, SOCK_NONBLOCK | SOCK_CLOEXEC);
}

int SendFlags()
{
    return MSG_NOSIGNAL;
}

int TruncationFlag()
{
    return MSG_TRUNC;
}

}  // namespace engine_placeholder::net::posix
