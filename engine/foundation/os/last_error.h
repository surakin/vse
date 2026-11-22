#pragma once

#include <cerrno>

//! The C library's error number: the engine's spelling of `errno`, which is a macro and cannot be named from
//! other files. The `E*` constants (`EINTR`, `ENOENT`, ...) arrive through this header.
namespace vse::os
{

/// The calling thread's last system error (`errno`).
///
/// - Thread-safety: thread-safe (the value is per thread). Signal-safe.
inline int LastError() noexcept
{
    return errno;
}

/// Sets the calling thread's last system error, usually to 0 before a call whose failure is told by it alone (`errno =
/// value`).
///
/// - Thread-safety: thread-safe (the value is per thread). Signal-safe.
inline void SetLastError(int value) noexcept
{
    errno = value;
}

}  // namespace vse::os
