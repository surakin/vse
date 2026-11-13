#pragma once

//! Errors of the asset pipeline, reported as values: [[asset::ErrorCode]] and [[asset::Status]]. No asset failure
//! aborts, and no input, however damaged, is a reason to abort.

#include "lang/status_try.h"
#include "types.h"

namespace engine_placeholder::asset
{

/// What went wrong.
enum class ErrorCode : u8
{
    Ok,                      ///< Success.
    MissingDependency,       ///< A resource names a dependency that no mount, table or pak holds.
    DependencyTypeMismatch,  ///< A dependency exists but is not of the type (or the version) the reference declared.
    DependencyCycle,         ///< Resources depend on each other in a cycle.
    DuplicateId,             ///< Two resources (or two paths, or two sources) claim one id, or one path claims two ids.
    NotFound,                ///< A named thing does not exist: an id, a path, a section.
    Corrupt,                 ///< Bytes are damaged: truncated, out of range, overlapping, not aligned, or a hash that
                             ///< does not match.
    UnsupportedVersion,      ///< A blob, pak or container is not the current version; the message names `asset_mint
                             ///< remint`.
    TypeMismatch,            ///< A resource is not of the type the caller asked for, or the type is not registered.
    TooLarge,                ///< A size or count is above a bound.
    Io,                      ///< The medium failed: a read or write error.
    Cancelled,               ///< The operation was cancelled and changed nothing that was committed.
    Offline,                 ///< The network backend is offline and the cache does not hold the resource.
    LimitExceeded,           ///< A configured limit (a budget, a count) is exceeded.
    WrongTarget,             ///< A blob or pak was made for another target platform; the message names `asset_mint
                             ///< pack --target`.
    Invalid,                 ///< The caller broke a rule: a malformed path, a null id where one is required.
    Other,                   ///< Anything else.
};

/// Returns a stable lower-case name for `code` (`"corrupt"`); never null.
///
/// - Ownership: the string is static.
/// - Fails: never.
const char* ErrorCodeName(ErrorCode code);

/// The result of an operation that returns nothing else: a portable [[asset::ErrorCode]] and a short message.
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
    /// - Fails: never.
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

}  // namespace engine_placeholder::asset
