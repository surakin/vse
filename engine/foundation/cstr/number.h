#pragma once

#include <charconv>
#include <cstdlib>

#include "build_defs.h"
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

/// The result of [[ParseDoubleExact]]: the value, where the number ended, and whether a number was read.
struct ParseDoubleResult {
    /// The parsed number; 0 when `ok` is false.
    f64 value;
    /// One past the last character consumed.
    const char* end;
    /// True when a number was read at `first`.
    bool ok;
};

#if !ENGINE_PLATFORM_WINDOWS
namespace detail {

/// [[ParseDoubleExact]] without `std::from_chars`, for a standard library that lacks it for `double` (Apple's libc++, where it is a
/// deleted function): reads the same grammar itself (an optional `-`; decimal digits with an optional `.` and at least one digit, then
/// an exponent only if one is complete; `inf`, `infinity`, `nan`, `nan(` letters, digits and `_` `)`, in any case), so `end` is the
/// same, and converts a normalized copy of at most 780 significant digits plus a sticky digit with `strtod_l` in the C locale, which
/// keeps the rounding exact. A result out of the range of `f64` (it would be infinite, or zero from nonzero digits) is not `ok`, with
/// `end` after the number, as `std::from_chars` reports it. Built on POSIX systems (on Linux it is the oracle's twin:
/// `tests/cstr/parse_double_test.cpp` compares the two); not on Windows.
///
/// Fails: `ok` is false, with `end` at `first`, when no number starts there (or the C locale cannot be made), and with `end` after the
/// number when it is out of range; `value` is 0 then.
/// Thread-safety: thread-safe (the C locale is made once, on first use).
ParseDoubleResult ParseDoubleWithoutFromChars(const char* first, const char* last) noexcept;

} // namespace detail
#endif

/// Parses a decimal number at the start of `[first, last)`, independent of the process locale, with no leading `+` or space and
/// hex form (`std::from_chars`). `nan` and `inf` are read as such; a caller that needs a finite number checks the value.
///
/// On macOS and iOS this is `detail::ParseDoubleWithoutFromChars` (Apple's libc++ has no `from_chars` for `double`), with the same
/// results.
/// Thread-safety: thread-safe.
inline ParseDoubleResult ParseDoubleExact(const char* first, const char* last) noexcept {
#if ENGINE_PLATFORM_MACOS || ENGINE_PLATFORM_IOS
    return detail::ParseDoubleWithoutFromChars(first, last);
#else
    f64 value = 0;
    const std::from_chars_result r = std::from_chars(first, last, value);
    const bool ok = r.ec == std::errc();
    return {ok ? value : 0.0, r.ptr, ok};
#endif
}

} // namespace engine_placeholder
