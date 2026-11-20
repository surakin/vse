#pragma once

#include "os/os.h"

// The half of os.h that Linux and macOS share (Phase 21 Day 285): os/posix/os_posix.cpp holds the calls that are the
// same POSIX calls on both, and each platform's os.cpp (os/linux/, os/macos/) supplies the rest and the two hooks
// below. Internal to those files: no other file includes this header (module_deps_check: os/posix/ is per-platform code
// owned by os/linux/ and os/macos/).
namespace vse::os::posix
{

/// Copies `path` into `buffer` with a terminator, for a POSIX call that takes a C string; nullptr when it does not fit
/// (never truncated).
/// - Thread-safety: thread-safe. Async-signal-safe (the crash report's CreateFileForWriting uses it).
const char* ToCPath(StringView path, char (&buffer)[kMaxPathBytes]);

/// The process environment array (`environ` on Linux, `*_NSGetEnviron()` on macOS, where a library cannot name
/// `environ`); may be null. Read again at every call: `setenv` can move the array.
char** ProcessEnvironment();

}  // namespace vse::os::posix
