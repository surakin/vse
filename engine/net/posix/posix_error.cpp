#include "net/posix/posix_net.h"

#include "os/last_error.h"

namespace engine_placeholder::net::posix
{

ErrorCode ErrorFromErrno(int error)
{
    switch (error)
    {
    case 0: return ErrorCode::Ok;
    case EAGAIN:
#if EAGAIN != EWOULDBLOCK
    case EWOULDBLOCK:
#endif
    case EINPROGRESS:
    case EALREADY: return ErrorCode::WouldBlock;
    case ECONNREFUSED: return ErrorCode::Refused;
    case ECONNRESET:
    case ECONNABORTED: return ErrorCode::Reset;
    case ETIMEDOUT: return ErrorCode::TimedOut;
    case ENETUNREACH:
    case EHOSTUNREACH:
    case ENETDOWN:
    case EHOSTDOWN:
    case ENETRESET: return ErrorCode::Unreachable;
    case EADDRINUSE: return ErrorCode::AddressInUse;
    case EADDRNOTAVAIL: return ErrorCode::AddressNotAvailable;
    case ENOTCONN: return ErrorCode::NotConnected;
    case EISCONN: return ErrorCode::AlreadyConnected;
    case EMSGSIZE: return ErrorCode::MessageTooLarge;
    case EPIPE:
    case ESHUTDOWN: return ErrorCode::Closed;
    case ECANCELED: return ErrorCode::Cancelled;
    case EBADF:
    case ENOTSOCK:
    case EINVAL:
    case EFAULT:
    case EDESTADDRREQ: return ErrorCode::Invalid;
    case EAFNOSUPPORT:
    case EPROTONOSUPPORT:
    case EPROTOTYPE:
    case EOPNOTSUPP:
#if ENOTSUP != EOPNOTSUPP
    case ENOTSUP:  // its own number on macOS (45 against 102); the same as EOPNOTSUPP on Linux
#endif
    case ENOPROTOOPT: return ErrorCode::Unsupported;
    case EMFILE:
    case ENFILE:
    case ENOBUFS:
    case ENOMEM: return ErrorCode::LimitExceeded;
    default: return ErrorCode::Other;
    }
}

}  // namespace engine_placeholder::net::posix
