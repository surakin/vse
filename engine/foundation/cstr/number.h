#pragma once

#include <charconv>
#include <cstdlib>

#include "types.h"

//! Number conversion (Phase 16): the engine's spelling of `atoi`, `atol`, `strtoul`, `strtod` and `to_chars`. Each forwards to the C
//! function of the same meaning, including its quirks (leading space skipped, no error report except through `end`).
namespace engine_placeholder {

/// Parses a decimal int; 0 when `text` has no number (`atoi`).
inline int CStrToInt(const char* text) noexcept { return std::atoi(text); }
/// Parses a decimal long; 0 when `text` has no number (`atol`).
inline long CStrToLong(const char* text) noexcept { return std::atol(text); }
/// Parses a long in `base` and stores where parsing stopped in `*end` when not null (`strtol`).
inline long CStrToLong(const char* text, char** end, int base) noexcept { return std::strtol(text, end, base); }
/// Parses a long long in `base` (`strtoll`).
inline long long CStrToLongLong(const char* text, char** end, int base) noexcept { return std::strtoll(text, end, base); }
/// Parses an unsigned long in `base` (`strtoul`).
inline unsigned long CStrToULong(const char* text, char** end, int base) noexcept { return std::strtoul(text, end, base); }
/// Parses an unsigned long long in `base` (`strtoull`).
inline unsigned long long CStrToULongLong(const char* text, char** end, int base) noexcept { return std::strtoull(text, end, base); }
/// Parses a double; 0 when `text` has no number (`atof`).
inline f64 CStrToDouble(const char* text) noexcept { return std::atof(text); }
/// Parses a double and stores where parsing stopped in `*end` when not null (`strtod`).
inline f64 CStrToDouble(const char* text, char** end) noexcept { return std::strtod(text, end); }
/// Parses a float and stores where parsing stopped in `*end` when not null (`strtof`).
inline f32 CStrToFloat(const char* text, char** end) noexcept { return std::strtof(text, end); }

/// The result of [[FormatShortest]]: where the text ends, and whether it fitted.
struct FormatShortestResult {
    /// One past the last character written.
    char* end;
    /// True when the buffer was big enough.
    bool ok;
};

/// Writes the shortest decimal text that parses back to exactly `value` into `[first, last)`, without a terminator (`std::to_chars`).
///
/// Thread-safety: thread-safe.
inline FormatShortestResult FormatShortest(char* first, char* last, f64 value) noexcept {
    const std::to_chars_result r = std::to_chars(first, last, value);
    return {r.ptr, r.ec == std::errc()};
}

} // namespace engine_placeholder
