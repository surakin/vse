#pragma once

//! Errors of the database layer, reported as values: [[database::ErrorCode]] and [[database::Status]]. No database
//! failure aborts.

#include "containers/array.h"
#include "string_view.h"
#include "lang/status_try.h"
#include "types.h"

namespace engine_placeholder::database
{

/// What went wrong, in terms every backend can express. The backend's own code and message stay in [[database::Status]]
/// for logs; callers decide on this enum.
enum class ErrorCode : u8
{
    Ok,          ///< Success.
    Busy,        ///< Another connection holds a lock and the busy timeout ran out; the operation can be retried.
    Locked,      ///< The connection itself holds a conflicting statement (a write while one of its own reads is still
                 ///< open).
    Constraint,  ///< A primary key, unique, not-null or foreign-key rule refused the statement; the statement changed
                 ///< nothing.
    NotFound,    ///< A named thing does not exist: a database to open without create, or a parameter name.
    Corrupt,     ///< The stored database is damaged or is not a database.
    Full,        ///< The device is full, or the database reached its size limit.
    ReadOnly,    ///< A write was attempted on a read-only connection or file.
    Misuse,  ///< The caller broke a rule of the interface: a bind index out of range, a nested `Begin`, a `Commit` with
             ///< no transaction.
    Syntax,  ///< The statement text is not valid SQL for this backend.
    Io,      ///< The file layer failed (a read, write, sync or open error).
    TooBig,  ///< A value or statement is larger than the backend allows.
    Unsupported,  ///< The backend lacks the feature or the statement form (see [[database::Feature]]).
    Interrupted,  ///< The operation was cancelled before it finished.
    Other,        ///< Anything else.
};

/// Returns a stable lower-case name for `code` (`"busy"`), for logs and tests; never null.
///
/// - Ownership: the string is static.
/// - Fails: never.
const char* ErrorCodeName(ErrorCode code);

/// The result of an operation that returns nothing else: a portable [[database::ErrorCode]], the backend's own code and
/// a short message copied into the value.
///
/// A message longer than [[database::Status::kMaxMessageBytes]] is cut. The type allocates nothing.
///
/// - Thread-safety: immutable value.
class [[nodiscard]] Status
{
public:
    /// The most message bytes a status keeps.
    static constexpr usize kMaxMessageBytes = 119;

    /// Creates a success status.
    constexpr Status() = default;

    /// Returns a success status.
    static Status Success() { return Status(); }
    /// Returns a failure with `code`, no backend code and no message.
    static Status Of(ErrorCode code)
    {
        Status s;
        s.code_ = code;
        return s;
    }
    /// Returns a failure with `code`, the backend's own code and (a copy of, cut to
    /// [[database::Status::kMaxMessageBytes]]) its message.
    static Status Make(ErrorCode code, i32 backend_code, StringView message);

    /// Returns true on success.
    /// - Fails: never.
    bool Ok() const { return code_ == ErrorCode::Ok; }
    /// Returns true on failure.
    /// - Fails: never.
    bool Failed() const { return code_ != ErrorCode::Ok; }
    /// Returns true when the status is exactly `code`.
    /// - Fails: never.
    bool Is(ErrorCode code) const { return code_ == code; }
    /// The portable code.
    ErrorCode Code() const { return code_; }
    /// The backend's own code (for SQLite the extended result code), for logs; never branch on it. Zero when there is
    /// none.
    i32 BackendCode() const { return backend_code_; }
    /// The message, valid while this status lives; empty when there is none.
    StringView Message() const { return StringView(message_.Data(), length_); }

private:
    ErrorCode code_ = ErrorCode::Ok;
    u8 length_ = 0;
    i32 backend_code_ = 0;
    Array<char, kMaxMessageBytes + 1> message_{};
};

}  // namespace engine_placeholder::database
