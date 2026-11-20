#include "net/windows/wsa_error.h"

#include "net/windows/winsock.h"

namespace vse::net::windows
{

ErrorCode ErrorFromWsa(int error)
{
    switch (error)
    {
    case 0: return ErrorCode::Ok;
    case WSAEWOULDBLOCK:
    case WSAEINPROGRESS:
    case WSAEALREADY: return ErrorCode::WouldBlock;
    case WSAECONNREFUSED: return ErrorCode::Refused;
    case WSAECONNRESET:
    case WSAECONNABORTED: return ErrorCode::Reset;
    case WSAETIMEDOUT: return ErrorCode::TimedOut;
    case WSAENETUNREACH:
    case WSAEHOSTUNREACH:
    case WSAENETDOWN:
    case WSAEHOSTDOWN:
    case WSAENETRESET: return ErrorCode::Unreachable;
    case WSAEADDRINUSE: return ErrorCode::AddressInUse;
    case WSAEADDRNOTAVAIL: return ErrorCode::AddressNotAvailable;
    case WSAENOTCONN: return ErrorCode::NotConnected;
    case WSAEISCONN: return ErrorCode::AlreadyConnected;
    case WSAEMSGSIZE: return ErrorCode::MessageTooLarge;
    case WSAESHUTDOWN:
    case WSAEDISCON: return ErrorCode::Closed;
    case WSAECANCELLED:
    case WSA_E_CANCELLED:
    case WSA_OPERATION_ABORTED: return ErrorCode::Cancelled;
    case WSAEBADF:
    case WSAENOTSOCK:
    case WSAEINVAL:
    case WSAEFAULT:
    case WSAEDESTADDRREQ:
    case WSANOTINITIALISED: return ErrorCode::Invalid;
    case WSAEAFNOSUPPORT:
    case WSAEPFNOSUPPORT:
    case WSAEPROTONOSUPPORT:
    case WSAESOCKTNOSUPPORT:
    case WSAEPROTOTYPE:
    case WSAEOPNOTSUPP:
    case WSAENOPROTOOPT:
    case WSAVERNOTSUPPORTED:
    case WSASYSNOTREADY: return ErrorCode::Unsupported;
    case WSAEMFILE:
    case WSAENOBUFS:
    case WSAEPROCLIM:
    case WSA_NOT_ENOUGH_MEMORY: return ErrorCode::LimitExceeded;
    default: return ErrorCode::Other;
    }
}

ErrorCode ErrorFromResolveError(int error)
{
    switch (error)
    {
    case 0: return ErrorCode::Ok;
    case WSAHOST_NOT_FOUND:  // EAI_NONAME
    case WSANO_DATA:         // EAI_NODATA: the name exists but has no address of the family
    case WSATYPE_NOT_FOUND:  // EAI_SERVICE
        return ErrorCode::NotFound;
    case WSATRY_AGAIN: return ErrorCode::TimedOut;                // EAI_AGAIN: a temporary failure of the name server
    case WSANO_RECOVERY: return ErrorCode::Unreachable;           // EAI_FAIL
    case WSA_NOT_ENOUGH_MEMORY: return ErrorCode::LimitExceeded;  // EAI_MEMORY
    case WSAEAFNOSUPPORT:                                         // EAI_FAMILY
    case WSAESOCKTNOSUPPORT:                                      // EAI_SOCKTYPE
        return ErrorCode::Unsupported;
    default: return ErrorFromWsa(error);  // EAI_BADFLAGS is WSAEINVAL: Invalid
    }
}

}  // namespace vse::net::windows
