// The macOS half of the POSIX socket backend (Phase 26 Day 326): the flags and calls that differ from Linux.
//
// macOS has no SOCK_NONBLOCK, SOCK_CLOEXEC or accept4, so a socket is created (or accepted) and its two flags are set
// right after, before the call returns. Between the two there is a window in which another thread that forks and
// execs would pass the descriptor to its child; macOS gives no way to close it for a plain socket. The engine itself
// starts children only for death tests (posix_spawn); an application that forks while sockets are being opened should
// use POSIX_SPAWN_CLOEXEC_DEFAULT (an Apple extension), which passes only the descriptors it names. A write to a
// vanished peer is kept from raising SIGPIPE by SO_NOSIGPIPE on every socket, as MSG_NOSIGNAL does not exist.

#include <fcntl.h>
#include <sys/socket.h>

#include "net/posix/posix_net.h"
#include "os/last_error.h"
#include "os/os.h"

namespace engine_placeholder::net::posix
{

namespace
{

// Makes `fd` non-blocking, close-on-exec and free of SIGPIPE; closes it and returns -1 (errno kept) when the system
// refuses one of them.
int Prepare(int fd)
{
    if (fd < 0)
    {
        return fd;
    }
    const int one = 1;
    const int status = ::fcntl(fd, F_GETFL);
    if (::fcntl(fd, F_SETFD, FD_CLOEXEC) != 0 || status < 0 || ::fcntl(fd, F_SETFL, status | O_NONBLOCK) != 0 ||
        ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one)) != 0)
    {
        const int error = os::LastError();
        os::Close(fd);
        os::SetLastError(error);
        return -1;
    }
    return fd;
}

}  // namespace

int OpenSocketFd(int domain, int type)
{
    return Prepare(::socket(domain, type, 0));
}

int AcceptFd(int listener, sockaddr* address, socklen_t* length)
{
    // An accepted socket takes the listener's O_NONBLOCK on macOS, but not its close-on-exec flag; both are set again,
    // with SO_NOSIGPIPE.
    const int fd = ::accept(listener, address, length);
    if (fd < 0)
    {
        return fd;
    }
    const int prepared = Prepare(fd);
    if (prepared < 0 && os::LastError() == EINVAL)
    {
        // A connection the peer reset before it was accepted refuses socket options (EINVAL once it can neither send
        // nor receive): it is an aborted connection, which the caller skips.
        os::SetLastError(ECONNABORTED);
    }
    return prepared;
}

int SendFlags()
{
    return 0;  // SO_NOSIGPIPE, set on every socket, does what MSG_NOSIGNAL does on Linux
}

ErrorCode SendErrorCode(int error)
{
    // A send on a connection the peer is tearing down fails with EPROTOTYPE on macOS, where Linux says EPIPE.
    return error == EPROTOTYPE ? ErrorCode::Closed : ErrorFromErrno(error);
}

}  // namespace engine_placeholder::net::posix
