#pragma once

//! Errors of the storage layer, reported as values: [[storage::ErrorCode]] and [[storage::Status]]. No storage failure
//! aborts, and no input, however damaged, is a reason to abort.

#include "lang/status_try.h"
#include "types.h"

namespace engine_placeholder::storage
{

/// What went wrong.
enum class ErrorCode : u8
{
    Ok,        ///< Success.
    NotFound,  ///< A named thing does not exist: a component id or name that is not registered.
    Exists,    ///< An id, a name or a type is registered already.
    Corrupt,   ///< Stored bytes are damaged: truncated, a value outside its declared range, text that is not UTF-8, a
               ///< count that does not match.
    TooNew,    ///< A record was written by a newer version of a component than is registered.
    TooOld,    ///< A record was written by a version older than the oldest the registry can upgrade from.
    LimitExceeded,  ///< A length or count is above a configured limit (on write or on read).
    Mismatch,  ///< A registration disagrees with the manifest recorded in a store: an id now has another name, or a
               ///< name another id.
    Invalid,  ///< The caller broke a rule: an empty name, id 0, a component without fields that is not flagged, an enum
              ///< field without a range.
    Busy,     ///< The store is in use: a batch is open already, or another writer holds it.
    Io,       ///< The medium failed: a read, write or sync error.
    Cancelled,  ///< A [[storage::CancelToken]] was set; the operation stopped between batches and changed nothing that
                ///< was committed.
    MissingReference,  ///< A reference names a key that is in no stored set and no world.
    Duplicate,         ///< Two entities carry one key.
    Unsupported,  ///< The store or the component does not offer this (a query on a store without the column layout, or
                  ///< on a component that is not queryable).
    Other,        ///< Anything else.
};

/// Returns a stable lower-case name for `code` (`"corrupt"`); never null.
///
/// - Ownership: the string is static.
/// - Fails: never.
const char* ErrorCodeName(ErrorCode code);

/// The result of an operation that returns nothing else: a portable [[storage::ErrorCode]] and a short message.
///
/// - Thread-safety: immutable value.
/// - Ownership: the message is a string with static lifetime (a literal); the type allocates nothing.
class [[nodiscard]] Status
{
public:
    /// Creates a success status.
    constexpr Status() = default;

    /// Returns a success status.
    static constexpr Status Success() { return Status(); }
    /// Returns a failure with `code` and `message`.
    ///
    /// - Param message: a string literal (or other static string); never null.
    static constexpr Status Make(ErrorCode code, const char* message)
    {
        Status s;
        s.code_ = code;
        s.message_ = message;
        return s;
    }

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
    /// The message; empty for success.
    ///
    /// - Ownership: the string has static lifetime.
    /// - Fails: never.
    constexpr const char* Message() const { return message_; }

private:
    ErrorCode code_ = ErrorCode::Ok;
    const char* message_ = "";
};

}  // namespace engine_placeholder::storage
