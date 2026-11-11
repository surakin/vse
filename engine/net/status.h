#pragma once

//! Errors of the network layer, reported as values: [[net::ErrorCode]] and [[net::Status]]. No network failure aborts,
//! and none throws.

#include "types.h"

namespace engine_placeholder::net
{

/// What went wrong, in terms every operating system can express. The OS error number stays in [[net::Status]] for logs;
/// callers decide on this enum.
enum class ErrorCode : u8
{
    Ok,                   ///< Success.
    WouldBlock,           ///< A non-blocking operation cannot finish now; wait for readiness and retry.
    Refused,              ///< The peer refused the connection (nothing listens, or a firewall answered).
    Reset,                ///< The peer reset an established connection, or it was aborted.
    TimedOut,             ///< The operation or the connection timed out.
    Unreachable,          ///< No route to the host or network, or the network is down.
    AddressInUse,         ///< The local endpoint is already bound.
    AddressNotAvailable,  ///< The local address does not belong to this machine, or no ephemeral port is free.
    NotConnected,         ///< The socket has no peer.
    AlreadyConnected,     ///< The socket already has a peer or a connection attempt is under way.
    MessageTooLarge,      ///< A datagram or a message exceeds the size the socket, the transport or a configured limit
                          ///< allows.
    Closed,     ///< The socket, connection or reactor was closed (also: an orderly end of stream when reading).
    Cancelled,  ///< The operation was cancelled before it finished.
    Invalid,    ///< The caller passed something the interface does not accept: a malformed address, a bad port, a wrong
                ///< socket state.
    LimitExceeded,  ///< A configured or fixed limit was reached: connections, descriptors, queue capacity, a set's
                    ///< capacity.
    Unsupported,    ///< The operating system or the backend lacks the feature.
    Other,          ///< Anything else.
};

/// Returns a stable lower-case name for `code` (`"would_block"`), for logs and tests; never null.
///
/// - Ownership: the string is static.
/// - Fails: never.
const char* ErrorCodeName(ErrorCode code);

/// The result of an operation that returns nothing else: a portable [[net::ErrorCode]] and the operating system's own
/// error number.
///
/// The type allocates nothing.
///
/// - Thread-safety: immutable value.
class [[nodiscard]] Status
{
public:
    /// Creates a success status.
    constexpr Status() = default;

    /// Returns a success status.
    static constexpr Status Success() { return Status(); }
    /// Returns a failure with `code` and no OS error number.
    static constexpr Status Of(ErrorCode code) { return Status(code, 0); }
    /// Returns a failure with `code` and the operating system's error number (`errno`, `WSAGetLastError()`), kept for
    /// logs.
    static constexpr Status Make(ErrorCode code, i32 os_error) { return Status(code, os_error); }

    /// Returns true on success.
    /// - Fails: never.
    constexpr bool Ok() const { return code_ == ErrorCode::Ok; }
    /// Returns true on failure.
    /// - Fails: never.
    constexpr bool Failed() const { return code_ != ErrorCode::Ok; }
    /// Returns true when the status is exactly `code`.
    /// - Fails: never.
    constexpr bool Is(ErrorCode code) const { return code_ == code; }
    /// The portable code.
    constexpr ErrorCode Code() const { return code_; }
    /// The operating system's error number, for logs; never branch on it. Zero when there is none.
    constexpr i32 OsError() const { return os_error_; }

private:
    constexpr Status(ErrorCode code, i32 os_error) : code_(code), os_error_(os_error) {}

    ErrorCode code_ = ErrorCode::Ok;
    i32 os_error_ = 0;
};

}  // namespace engine_placeholder::net
