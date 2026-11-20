#pragma once

#include "io/error.h"
#include "io/path.h"
#include "types.h"

// The Windows file backend's path boundary (Phase 13 Day 151): turns an [[io::Path]] into the UTF-16 text a Win32 file
// function takes. Internal to engine/foundation/io/windows/.
//
// The rules, in this order (engine/foundation/io/BACKEND.md, "NativePathStyle"):
//   1. A path written with the `\\?\` or `\\.\` prefix is passed through as it is (only its separators become `\`): the
//   caller asked for
//      the literal name or a device, and the name checks below are skipped.
//   2. Every other path is refused with `Invalid` when a component is a reserved device name (CON, PRN, AUX, NUL,
//   COM0-COM9, LPT0-LPT9,
//      the superscript COM/LPT forms, CONIN$, CONOUT$; with or without an extension, in any case, with spaces before
//      the dot), when a component ends in a dot or a space (Win32 strips them, so `a.` and `a` would be one file), when
//      a component holds a colon (an alternate data stream), or when the text is not valid UTF-8; a component longer
//      than 255 UTF-16 units is `TooLong`. Win32 itself refuses `<>"|?*` and control characters
//      (`ERROR_INVALID_NAME`, mapped to `Invalid`).
//   3. A path that fits in MAX_PATH - 1 characters is used as it is, relative or not, so Win32 resolves it as any
//   program would.
//   4. A longer path is made absolute and resolved with GetFullPathNameW (the current directory, the drive's current
//   directory for
//      `C:foo`, `.` and `..`, lexically as Win32 always does) and gets the `\\?\` (or `\\?\UNC\`) prefix, which lifts
//      the limit whatever `LongPathsEnabled` says.
namespace vse::io::windows
{

/// The share mode of every open the backend makes (BACKEND.md, "Sharing"): read, write and delete, so a file can be
/// opened any number of times and renamed, replaced or removed while open, as on Linux. FILE_SHARE_READ |
/// FILE_SHARE_WRITE | FILE_SHARE_DELETE, spelled out so this header needs no Windows header.
constexpr u32 kShareAll = 0x1 | 0x2 | 0x4;

/// Room for the longest [[io::Path]] (kMaxPathBytes UTF-8 bytes give at most as many UTF-16 units) plus a long current
/// directory and the prefix; about 9 KiB on the stack.
constexpr usize kWidePathCapacity = kMaxPathBytes + 512;

/// A converted path, NUL-terminated.
struct WidePath
{
    wchar_t text[kWidePathCapacity];
    usize length = 0;
};

/// Closes a Win32 handle (`void*`, INVALID_HANDLE_VALUE or null meaning none) at scope exit.
class HandleGuard
{
public:
    explicit HandleGuard(void* h) : h_(h) {}
    ~HandleGuard();
    HandleGuard(const HandleGuard&) = delete;
    HandleGuard& operator=(const HandleGuard&) = delete;

private:
    void* h_;
};

/// Converts `path` for a Win32 file function by the rules above. With `extended`, rule 4 applies whatever the length:
/// the result is absolute and prefixed, so names appended to it stay valid past MAX_PATH (a walk down a tree).
/// - Fails: `Invalid` (rule 2), `TooLong` (the result does not fit), or the error GetFullPathNameW reports.
Status ToWin32(const Path& path, WidePath* out, bool extended = false);

/// Returns true when `name` (one component, UTF-8) is a reserved device name by rule 2: the part before the first dot,
/// with trailing spaces removed, compared without case.
bool IsReservedDeviceName(StringView name);

/// Refines a "not found" from Win32: ERROR_PATH_NOT_FOUND also means "a parent is a file", which POSIX reports as
/// `NotADirectory`. Looks at the parents of `wide` (no further than the first that exists) and returns `NotADirectory`
/// when that one is not a directory, otherwise the status for `code`.
Status RefineNotFound(const WidePath& wide, u32 code);

/// Returns the status for the calling thread's GetLastError(), refined by [[RefineNotFound]] for the not-found codes.
Status LastError(const WidePath& wide);

}  // namespace vse::io::windows
