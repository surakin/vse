#pragma once

#include <cstdio>
#include <cwchar>

#include "cstr/varargs.h"
#include "debug/compiler.h"
#include "types.h"

//! Formatted text (Phase 16): the engine's spelling of `snprintf`, `vsnprintf`, `swprintf` and `sscanf`. Formatting to a console
//! is [[os::Print]]; this header formats to a buffer.
namespace engine_placeholder {

/// Formats into `buffer` like `snprintf`: writes at most `capacity - 1` characters and a terminator, and returns the length the full text has.
///
/// - Param capacity: the size of `buffer` in bytes; zero writes nothing (and `buffer` may then be null).
/// - Returns: the length of the full text; a result of `capacity` or more means the text was cut; negative on an encoding error.
/// - Thread-safety: thread-safe.
int FormatTo(char* buffer, usize capacity, const char* format, ...) ENGINE_PRINTF_FORMAT(3, 4);

/// As [[FormatTo]] with the arguments already in a list (`vsnprintf`).
///
/// - Thread-safety: thread-safe.
inline int VFormatTo(char* buffer, usize capacity, const char* format, VarArgs args) {
    return std::vsnprintf(buffer, capacity, format, args);
}

inline int FormatTo(char* buffer, usize capacity, const char* format, ...) {
    VarArgs args;
    ENGINE_VA_START(args, format);
    const int length = std::vsnprintf(buffer, capacity, format, args);
    ENGINE_VA_END(args);
    return length;
}

/// Formats wide text into `buffer` of `capacity` wide characters, with `swprintf`'s conversions (`%ls` for wide text).
///
/// - Returns: the number of wide characters written, or negative when the text did not fit.
/// - Thread-safety: thread-safe.
template <class... Args>
inline int WFormatTo(wchar_t* buffer, usize capacity, const wchar_t* format, Args... args) {
    return std::swprintf(buffer, capacity, format, args...);
}

/// Reads values from `text` according to `format` (`sscanf`); the arguments are pointers.
///
/// - Returns: the number of values assigned, or a negative value at the end of input before the first conversion.
/// - Thread-safety: thread-safe.
template <class... Args>
inline int ScanFrom(const char* text, const char* format, Args... args) {
    return std::sscanf(text, format, args...);
}

} // namespace engine_placeholder
