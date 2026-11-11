#pragma once

#include <sys/stat.h>

#include "io/error.h"
#include "io/path.h"
#include "string_view.h"
#include "types.h"

// The hooks the shared POSIX file layer (io/posix/, Phase 26 Day 303) asks of each system: io/linux/posix_hooks.cpp and
// io/macos/posix_hooks.cpp define them. Internal to the file layer's backends: no other file includes this header
// (module_deps_check: io/posix/ is per-platform code owned by io/linux/ and io/macos/).
namespace engine_placeholder::io::posix {

/// The modification time of `st` in nanoseconds since the Unix epoch (`st_mtim` on Linux, `st_mtimespec` on macOS).
i64 ModifiedUnixNs(const struct stat& st);

/// Makes what was written to `fd` durable: `fsync` on Linux; on macOS `fcntl(F_FULLFSYNC)`, which reaches the medium where `fsync` only reaches
/// the drive's cache, falling back to `fsync` when the file system refuses it. Returns 0, or -1 with the error in `errno`; retries `EINTR`.
int SyncDescriptor(int fd);

/// Copies the rest of `in` to `out`, both open descriptors (`Copy` has opened them, refused the same file twice and truncated `out`): `CopyLoop` on
/// Linux; on macOS `fcopyfile(COPYFILE_DATA)` for a regular-file source (the data only: `Copy` sets the permission bits itself and keeps no times or
/// attributes) and `CopyLoop` for anything else, or when `fcopyfile` is interrupted or refuses (both descriptors rewound, `out` emptied first).
/// Returns 0, or -1 with the error in `errno`.
int CopyData(int in, int out);

/// Copies the rest of `in` to `out` with `read` and `write`, retrying `EINTR` and short writes (defined in io/posix/file_system.cpp).
/// Returns 0, or -1 with the error in `errno`.
int CopyLoop(int in, int out);

// Shared by the systems' special-folder files (defined in io/posix/file_system.cpp).

/// True for an application name that is one path component of at most 200 bytes (not `.`, `..`, no `/` or NUL).
bool ValidAppName(StringView name);
/// The environment variable `name` when it holds an absolute path, else empty.
StringView AbsoluteEnv(const char* name);
/// `$HOME` when absolute, else the password database's home of the effective user.
/// Fails: `NotFound` when neither gives an absolute path.
Status HomeDirectory(Path* out);
/// Parses `text` as a POSIX path. Fails: `TooLong`.
Status FromPath(StringView text, Path* out);
/// The application name the crash dump directory is named after: `ENGINE_APP_NAME` when set, else the executable's file name; NUL-terminated.
/// Fails: `Invalid` (`ENGINE_APP_NAME` over 200 bytes), `NotFound` (no usable executable name), or `ExecutablePath`'s error.
Status CrashAppName(char (&app)[256]);

} // namespace engine_placeholder::io::posix
