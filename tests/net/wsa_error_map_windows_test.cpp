// The Winsock error -> ErrorCode table of the Windows socket backend (Phase 20 Day 278). Runs from a list of numbers
// (Winsock's codes are fixed: WSAEWOULDBLOCK is 10035), so it needs no socket and no Windows header.

#include <catch2/catch_test_macros.hpp>

#include "containers/array.h"
#include "net/windows/wsa_error.h"

namespace vse::net
{
namespace
{

struct WsaRow
{
    int error;
    ErrorCode code;
};

TEST_CASE("Winsock errors map to portable codes", "[net][errno]")
{
    const Array<WsaRow, 46> rows = {{
        {0, ErrorCode::Ok},
        {10035, ErrorCode::WouldBlock},  // WSAEWOULDBLOCK
        {10036, ErrorCode::WouldBlock},  // WSAEINPROGRESS
        {10037, ErrorCode::WouldBlock},  // WSAEALREADY
        {10004,
         ErrorCode::Other},  // WSAEINTR: never seen on a non-blocking socket; must not read as "wait for readiness"
        {10061, ErrorCode::Refused},              // WSAECONNREFUSED
        {10054, ErrorCode::Reset},                // WSAECONNRESET
        {10053, ErrorCode::Reset},                // WSAECONNABORTED
        {10060, ErrorCode::TimedOut},             // WSAETIMEDOUT
        {10051, ErrorCode::Unreachable},          // WSAENETUNREACH
        {10065, ErrorCode::Unreachable},          // WSAEHOSTUNREACH
        {10050, ErrorCode::Unreachable},          // WSAENETDOWN
        {10064, ErrorCode::Unreachable},          // WSAEHOSTDOWN
        {10052, ErrorCode::Unreachable},          // WSAENETRESET
        {10048, ErrorCode::AddressInUse},         // WSAEADDRINUSE
        {10049, ErrorCode::AddressNotAvailable},  // WSAEADDRNOTAVAIL
        {10057, ErrorCode::NotConnected},         // WSAENOTCONN
        {10056, ErrorCode::AlreadyConnected},     // WSAEISCONN
        {10040, ErrorCode::MessageTooLarge},      // WSAEMSGSIZE
        {10058, ErrorCode::Closed},               // WSAESHUTDOWN
        {10101, ErrorCode::Closed},               // WSAEDISCON
        {10103, ErrorCode::Cancelled},            // WSAECANCELLED
        {10111, ErrorCode::Cancelled},            // WSA_E_CANCELLED
        {995, ErrorCode::Cancelled},              // WSA_OPERATION_ABORTED
        {10009, ErrorCode::Invalid},              // WSAEBADF
        {10038, ErrorCode::Invalid},              // WSAENOTSOCK
        {10022, ErrorCode::Invalid},              // WSAEINVAL
        {10014, ErrorCode::Invalid},              // WSAEFAULT
        {10039, ErrorCode::Invalid},              // WSAEDESTADDRREQ
        {10093, ErrorCode::Invalid},              // WSANOTINITIALISED
        {10047, ErrorCode::Unsupported},          // WSAEAFNOSUPPORT
        {10046, ErrorCode::Unsupported},          // WSAEPFNOSUPPORT
        {10043, ErrorCode::Unsupported},          // WSAEPROTONOSUPPORT
        {10044, ErrorCode::Unsupported},          // WSAESOCKTNOSUPPORT
        {10041, ErrorCode::Unsupported},          // WSAEPROTOTYPE
        {10045, ErrorCode::Unsupported},          // WSAEOPNOTSUPP
        {10042, ErrorCode::Unsupported},          // WSAENOPROTOOPT
        {10092, ErrorCode::Unsupported},          // WSAVERNOTSUPPORTED
        {10091, ErrorCode::Unsupported},          // WSASYSNOTREADY
        {10024, ErrorCode::LimitExceeded},        // WSAEMFILE
        {10055, ErrorCode::LimitExceeded},        // WSAENOBUFS
        {10067, ErrorCode::LimitExceeded},        // WSAEPROCLIM
        {8, ErrorCode::LimitExceeded},            // WSA_NOT_ENOUGH_MEMORY
        {10013, ErrorCode::Other},                // WSAEACCES (Bind turns it into AddressInUse itself)
        {11001, ErrorCode::Other},                // WSAHOST_NOT_FOUND: a resolver code, not a socket one
        {99999, ErrorCode::Other},
    }};
    for (const WsaRow& row : rows)
    {
        CAPTURE(row.error);
        CHECK(windows::ErrorFromWsa(row.error) == row.code);
    }
    const Status s = windows::StatusFromWsa(10061);
    CHECK(s.Is(ErrorCode::Refused));
    CHECK(s.OsError() == 10061);
}

TEST_CASE("GetAddrInfoW results map to the same meanings as POSIX's EAI_ codes", "[net][errno]")
{
    const Array<WsaRow, 10> rows = {{
        {0, ErrorCode::Ok},
        {11001, ErrorCode::NotFound},     // WSAHOST_NOT_FOUND (EAI_NONAME)
        {11004, ErrorCode::NotFound},     // WSANO_DATA (EAI_NODATA)
        {10109, ErrorCode::NotFound},     // WSATYPE_NOT_FOUND (EAI_SERVICE)
        {11002, ErrorCode::TimedOut},     // WSATRY_AGAIN (EAI_AGAIN)
        {11003, ErrorCode::Unreachable},  // WSANO_RECOVERY (EAI_FAIL)
        {8, ErrorCode::LimitExceeded},    // WSA_NOT_ENOUGH_MEMORY (EAI_MEMORY)
        {10047, ErrorCode::Unsupported},  // WSAEAFNOSUPPORT (EAI_FAMILY)
        {10044, ErrorCode::Unsupported},  // WSAESOCKTNOSUPPORT (EAI_SOCKTYPE)
        {10022, ErrorCode::Invalid},      // WSAEINVAL (EAI_BADFLAGS)
    }};
    for (const WsaRow& row : rows)
    {
        CAPTURE(row.error);
        CHECK(windows::ErrorFromResolveError(row.error) == row.code);
    }
}

}  // namespace
}  // namespace vse::net
