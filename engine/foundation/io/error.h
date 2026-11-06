#pragma once

#include "types.h"

//! Errors of the file layer, reported as values: [[io::Error]] and [[io::Status]].
namespace engine_placeholder::io {

/// What went wrong, in terms every platform and every `File` backend can express.
///
/// The operating system's own code is kept in [[Status::os_code]] for logs; callers decide on this enum.
enum class Error : u8 {
    None,             ///< Success.
    NotFound,         ///< The file, directory or a parent does not exist (`ENOENT`).
    PermissionDenied, ///< The process may not do this (`EACCES`, `EPERM`).
    AlreadyExists,    ///< The target exists and the operation needs it not to (`EEXIST`).
    NotADirectory,    ///< A directory was needed (`ENOTDIR`).
    IsADirectory,     ///< A file was needed (`EISDIR`).
    NotEmpty,         ///< A directory still has entries (`ENOTEMPTY`).
    NoSpace,          ///< The device or quota is full (`ENOSPC`, `EDQUOT`, `EFBIG`).
    TooLong,          ///< A name or path is longer than the system allows (`ENAMETOOLONG`, `ERANGE`), or longer than [[io::kMaxPathBytes]].
    Interrupted,      ///< The operation was cancelled or interrupted before it finished (`EINTR`; a `File` closed with requests in flight).
    Unsupported,      ///< This backend or platform cannot do it (`ENOSYS`, `ENOTSUP`, `EOPNOTSUPP`, an unknown URL scheme).
    Invalid,          ///< An argument is not valid for the operation (`EINVAL`, `EBADF`), or a refused argument such as `RemoveAll("/")`.
    CrossDevice,      ///< The two paths are on different file systems (`EXDEV`).
    SymlinkLoop,      ///< Too many symbolic links (`ELOOP`), or a symbolic link where one is refused (`O_NOFOLLOW`).
    ReadOnly,         ///< The file system is read-only (`EROFS`).
    Busy,             ///< The resource is in use (`EBUSY`, `ETXTBSY`).
    TooManyOpenFiles, ///< The process or system is out of descriptors (`EMFILE`, `ENFILE`).
    OutOfMemory,      ///< Memory ran out (`ENOMEM`).
    EndOfFile,        ///< A read ended before the requested bytes (only from the exact-read helpers; a plain `Read` returns fewer bytes instead).
    Io,               ///< Anything else: a device error (`EIO`) or a code with no portable meaning.
};

/// Returns a stable lower-case name for `error` (`"not_found"`), for logs and tests; never null.
const char* ErrorName(Error error);

/// Maps an operating system error code (`errno` on POSIX, `GetLastError()` on Windows) to an [[io::Error]]; the table is one
/// function per platform. Codes not listed in [[io::Error]] collapse into [[io::Error::Io]]. Zero maps to [[io::Error::None]].
/// Backend: Windows: GetLastError() codes (ERROR_FILE_NOT_FOUND, ERROR_ACCESS_DENIED, ERROR_ALREADY_EXISTS, ERROR_DISK_FULL, ...); macOS: errno as on Linux.
Error ErrorFromOsCode(i32 os_code);

/// The result of an operation that returns nothing else: an [[io::Error]] plus the OS code behind it, for logs only.
///
/// Thread-safety: immutable value.
struct Status {
    Error error = Error::None; ///< What went wrong; [[io::Error::None]] means success.
    i32 os_code = 0;           ///< The platform's own code, for logs; never branch on it.

    /// Returns true on success.
    bool Ok() const { return error == Error::None; }
    /// Returns true on failure.
    bool Failed() const { return error != Error::None; }
    /// Returns true when the status is exactly `e`.
    bool Is(Error e) const { return error == e; }
    /// Returns a success status.
    static Status Success() { return Status{}; }
    /// Returns a failure with `e` and no OS code.
    static Status Of(Error e) { return Status{e, 0}; }
    /// Returns the failure for a platform code.
    static Status FromOs(i32 code) { return Status{ErrorFromOsCode(code), code}; }
};

} // namespace engine_placeholder::io
