#pragma once

#include <clocale>

#include "types.h"

//! The C library's locale (`setlocale`): the engine's own code never changes it, and formats and parses stored numbers
//! with [[FormatGeneral]], [[FormatShortest]] and [[ParseDoubleExact]], which ignore it. A host program may change it
//! (Qt and GTK call `setlocale(LC_ALL, "")` at startup), so tests use this wrapper to run the engine under a locale
//! whose decimal point is `,`.
namespace vse::os
{

/// Which part of the locale [[os::SetLocale]] changes or reads.
enum class LocaleCategory : u8
{
    All,     ///< Every category (`LC_ALL`).
    Numeric  ///< Number formatting and parsing: the decimal point (`LC_NUMERIC`).
};

/// Sets or reads the process locale for `category` (`setlocale`). The locale is process-wide: a caller that changes it
/// restores it before other threads format or parse with the C library's locale-dependent functions.
///
/// - Param category: the part of the locale.
/// - Param name: the locale's name (`"C"`, `""` for the user's environment, `"de_DE.UTF-8"`, `"de-DE"` on Windows), or
///   null to read the current one without changing it.
/// - Returns: the name of the locale now in effect, owned by the C library and valid until the next call; null when
///   the locale named is not installed (nothing changes then).
/// - Fails: returns null for a locale that is not installed.
/// - Thread-safety: not-thread-safe: it changes process-wide state that the C library's formatting reads.
inline const char* SetLocale(LocaleCategory category, const char* name) noexcept
{
    return std::setlocale(category == LocaleCategory::All ? LC_ALL : LC_NUMERIC, name);
}

}  // namespace vse::os
