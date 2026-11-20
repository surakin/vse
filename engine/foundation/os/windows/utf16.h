#pragma once

#include "string_view.h"
#include "types.h"

// UTF-8 and UTF-16 at the Win32 boundary (Phase 13 Day 149). Internal to the Windows backends: the engine is UTF-8
// everywhere, and these convert only where a Win32 function takes or returns `wchar_t`. Hand-written rather than
// MultiByteToWideChar, so invalid input is counted instead of hidden, and the functions take no lock and allocate
// nothing (the crash path uses them).
//
// Both directions write into a caller buffer and return the full length the result needs (in code units, without the
// terminator), so a caller can retry with a larger buffer. They write whole code points only, and terminate the output
// with 0 whenever `capacity` > 0. Invalid input (a malformed UTF-8 sequence, an overlong or surrogate encoding, an
// unpaired UTF-16 surrogate) becomes U+FFFD and is counted in `*replaced` (if not null); it is never dropped.

namespace vse::os::windows
{

/// Converts UTF-8 to UTF-16.
/// - Returns: the number of `wchar_t` the whole result needs, not counting the terminator.
usize Utf8ToUtf16(StringView utf8, wchar_t* out, usize capacity, usize* replaced = nullptr);

/// Converts `length` UTF-16 code units to UTF-8.
/// - Returns: the number of bytes the whole result needs, not counting the terminator.
usize Utf16ToUtf8(const wchar_t* utf16, usize length, char* out, usize capacity, usize* replaced = nullptr);

/// Converts a UTF-8 path to the UTF-16 form a Win32 file function takes: '/' becomes '\', and an absolute path
/// (`C:\...` or `\\server\share`) that does not fit in MAX_PATH - 1 gets the `\\?\` (or `\\?\UNC\`) prefix, which lifts
/// the limit whatever `LongPathsEnabled` says. A path that already starts with `\\?\` or `\\.\` is kept as it is.
/// Relative paths are never prefixed (the prefix turns off the relative-path rules).
/// - Returns: the number of `wchar_t` the whole result needs, not counting the terminator; if it is >= `capacity`,
/// nothing usable was written.
usize PathToWin32(StringView utf8_path, wchar_t* out, usize capacity, usize* replaced = nullptr);

}  // namespace vse::os::windows
