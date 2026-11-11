// The errno -> ErrorCode table of the POSIX socket backend (Phase 20 Day 268). Runs from a table, so it needs no
// socket.

#include <catch2/catch_test_macros.hpp>

#include "net/posix/posix_net.h"
#include "os/last_error.h"

namespace engine_placeholder::net
{
namespace
{

struct Row
{
    int error;
    ErrorCode code;
};

TEST_CASE("errno values map to portable codes", "[net][errno]")
{
    const Row rows[] = {
        {0, ErrorCode::Ok},
        {EAGAIN, ErrorCode::WouldBlock},
        {EWOULDBLOCK, ErrorCode::WouldBlock},
        {EINPROGRESS, ErrorCode::WouldBlock},
        {EALREADY, ErrorCode::WouldBlock},
        {EINTR,
         ErrorCode::Other},  // retried inside the backend; if one ever escapes it must not read as "wait for readiness"

        {ECONNREFUSED, ErrorCode::Refused},
        {ECONNRESET, ErrorCode::Reset},
        {ECONNABORTED, ErrorCode::Reset},
        {ETIMEDOUT, ErrorCode::TimedOut},
        {ENETUNREACH, ErrorCode::Unreachable},
        {EHOSTUNREACH, ErrorCode::Unreachable},
        {ENETDOWN, ErrorCode::Unreachable},
        {EHOSTDOWN, ErrorCode::Unreachable},
        {EADDRINUSE, ErrorCode::AddressInUse},
        {EADDRNOTAVAIL, ErrorCode::AddressNotAvailable},
        {ENOTCONN, ErrorCode::NotConnected},
        {EISCONN, ErrorCode::AlreadyConnected},
        {EMSGSIZE, ErrorCode::MessageTooLarge},
        {EPIPE, ErrorCode::Closed},
        {ESHUTDOWN, ErrorCode::Closed},
        {ECANCELED, ErrorCode::Cancelled},
        {EBADF, ErrorCode::Invalid},
        {ENOTSOCK, ErrorCode::Invalid},
        {EINVAL, ErrorCode::Invalid},
        {EDESTADDRREQ, ErrorCode::Invalid},
        {EAFNOSUPPORT, ErrorCode::Unsupported},
        {EPROTONOSUPPORT, ErrorCode::Unsupported},
        {EOPNOTSUPP, ErrorCode::Unsupported},
        {ENOPROTOOPT, ErrorCode::Unsupported},
        {EMFILE, ErrorCode::LimitExceeded},
        {ENFILE, ErrorCode::LimitExceeded},
        {ENOBUFS, ErrorCode::LimitExceeded},
        {ENOMEM, ErrorCode::LimitExceeded},
        {EACCES, ErrorCode::Other},
        {EPERM, ErrorCode::Other},
        {99999, ErrorCode::Other},
    };
    for (const Row& row : rows)
    {
        CAPTURE(row.error);
        CHECK(posix::ErrorFromErrno(row.error) == row.code);
    }
    const Status s = posix::StatusFromErrno(ECONNREFUSED);
    CHECK(s.Is(ErrorCode::Refused));
    CHECK(s.OsError() == ECONNREFUSED);
}

}  // namespace
}  // namespace engine_placeholder::net
