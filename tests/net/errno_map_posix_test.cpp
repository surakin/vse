// The errno -> ErrorCode table of the POSIX socket backend (Phase 20 Day 268). Runs from a table, so it needs no
// socket.

#include <catch2/catch_test_macros.hpp>

#include "build_defs.h"
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
        {ENOTSUP, ErrorCode::Unsupported},  // a number of its own on macOS (Phase 26 Day 326)
        {EPROTOTYPE, ErrorCode::Unsupported},
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

TEST_CASE("A failed send's errno is mapped with the system's quirks", "[net][errors]")
{
    // macOS fails a send on a connection being torn down with EPROTOTYPE (Phase 26 Day 326): Closed there; elsewhere
    // the table's Unsupported. Every other number is the table's.
#if ENGINE_PLATFORM_MACOS
    CHECK(posix::SendErrorCode(EPROTOTYPE) == ErrorCode::Closed);
#else
    CHECK(posix::SendErrorCode(EPROTOTYPE) == ErrorCode::Unsupported);
#endif
    CHECK(posix::SendErrorCode(EPIPE) == ErrorCode::Closed);
    CHECK(posix::SendErrorCode(ECONNRESET) == ErrorCode::Reset);
    CHECK(posix::SendErrorCode(EAGAIN) == ErrorCode::WouldBlock);
}

}  // namespace
}  // namespace engine_placeholder::net
