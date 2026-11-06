#pragma once

#include "types.h"

// Linux details of core dumps that tests reach directly.
namespace engine_placeholder::debug {

/// Describes where a core goes, given the text of `/proc/sys/kernel/core_pattern`.
///
/// For a piped pattern it names the program (and gives the `coredumpctl` hint for systemd-coredump), otherwise the file name
/// pattern. Linux only.
///
/// Param pattern: the pattern text without its newline; null or empty gives "unknown". Borrowed for the call.
/// Param out: receives a null-terminated sentence, cut to fit.
/// Param capacity: size of `out` in bytes.
/// Fails: never; an unreadable pattern is described as unknown.
/// Ownership: the caller owns `out`.
/// Thread-safety: thread-safe.
void DescribeCorePattern(const char* pattern, char* out, usize capacity);

/// Reports whether the pattern hands the core to a program (it starts with `|`).
/// Param pattern: the pattern text; borrowed for the call; may be null.
/// Returns: true for a piped pattern.
/// Fails: never.
/// Ownership: nothing is retained.
/// Thread-safety: thread-safe.
bool CorePatternIsPipe(const char* pattern);

} // namespace engine_placeholder::debug
