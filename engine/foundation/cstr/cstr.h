#pragma once

#include <cstring>
#include <cwchar>

#include "types.h"

//! NUL-terminated strings (Phase 16): the engine's spelling of the C library's string functions of `<cstring>` and
//! `<cwchar>`. These work on `const char*` for the places that meet the C world (command lines, environment, system
//! calls, literals); the owning and viewing string types are `String` and `StringView`.
namespace engine_placeholder
{

/// The number of characters before the terminating NUL (`strlen`).
///
/// - Param text: must not be null.
/// - Thread-safety: thread-compatible. The text must not be written by another thread meanwhile.
inline usize CStrLength(const char* text) noexcept
{
    return std::strlen(text);
}

/// Compares two strings as unsigned characters (`strcmp`).
///
/// - Returns: negative, zero or positive as `a` sorts before, equal to or after `b`.
/// - Thread-safety: thread-compatible.
inline int CStrCompare(const char* a, const char* b) noexcept
{
    return std::strcmp(a, b);
}

/// Compares at most `count` characters (`strncmp`).
///
/// - Thread-safety: thread-compatible.
inline int CStrCompareN(const char* a, const char* b, usize count) noexcept
{
    return std::strncmp(a, b, count);
}

/// Finds the first occurrence of `needle` in `haystack` (`strstr`).
///
/// - Returns: a pointer into `haystack`, or null.
/// - Thread-safety: thread-compatible.
inline const char* CStrFind(const char* haystack, const char* needle) noexcept
{
    return std::strstr(haystack, needle);
}
/// Finds the first occurrence of `needle` in a writable `haystack` (`strstr`).
inline char* CStrFind(char* haystack, const char* needle) noexcept
{
    return std::strstr(haystack, needle);
}

/// Finds the first occurrence of the character `c` (`strchr`); `c == '\0'` finds the terminator.
///
/// - Returns: a pointer into `text`, or null.
/// - Thread-safety: thread-compatible.
inline const char* CStrFindChar(const char* text, int c) noexcept
{
    return std::strchr(text, c);
}
/// Finds the first occurrence of the character `c` in writable text (`strchr`).
inline char* CStrFindChar(char* text, int c) noexcept
{
    return std::strchr(text, c);
}

/// Finds the last occurrence of the character `c` (`strrchr`).
///
/// - Returns: a pointer into `text`, or null.
/// - Thread-safety: thread-compatible.
inline const char* CStrFindLastChar(const char* text, int c) noexcept
{
    return std::strrchr(text, c);
}
/// Finds the last occurrence of the character `c` in writable text (`strrchr`).
inline char* CStrFindLastChar(char* text, int c) noexcept
{
    return std::strrchr(text, c);
}

/// Finds the first character of `text` that is in `set` (`strpbrk`).
///
/// - Thread-safety: thread-compatible.
inline const char* CStrFindAny(const char* text, const char* set) noexcept
{
    return std::strpbrk(text, set);
}

/// Copies the terminated string `source` including the terminator (`strcpy`); `destination` must be big enough.
///
/// Prefer [[CStrCopyTo]], which cannot overflow.
/// - Thread-safety: thread-compatible.
inline char* CStrCopy(char* destination, const char* source) noexcept
{
    return std::strcpy(destination, source);
}

/// Copies at most `count` characters and pads with NULs up to `count` (`strncpy`); the result is not terminated when
/// `source` has `count` or more characters.
///
/// - Thread-safety: thread-compatible.
inline char* CStrCopyN(char* destination, const char* source, usize count) noexcept
{
    return std::strncpy(destination, source, count);
}

/// Appends `source` to the terminated string `destination` (`strcat`); the buffer must be big enough.
///
/// - Thread-safety: thread-compatible.
inline char* CStrAppend(char* destination, const char* source) noexcept
{
    return std::strcat(destination, source);
}

/// Copies `source` into a buffer of `capacity` bytes and always terminates it (truncating when needed).
///
/// - Param capacity: the size of `destination`; zero writes nothing.
/// - Returns: the length of `source`, so a result of `capacity` or more means the text was cut.
/// - Thread-safety: thread-compatible.
inline usize CStrCopyTo(char* destination, usize capacity, const char* source) noexcept
{
    const usize length = std::strlen(source);
    if (capacity != 0)
    {
        const usize copied = length < capacity - 1 ? length : capacity - 1;
        if (copied != 0)
        {
            std::memcpy(destination, source, copied);
        }
        destination[copied] = '\0';
    }
    return length;
}

/// The number of wide characters before the terminating NUL (`wcslen`).
inline usize WStrLength(const wchar_t* text) noexcept
{
    return std::wcslen(text);
}
/// Compares two wide strings (`wcscmp`).
inline int WStrCompare(const wchar_t* a, const wchar_t* b) noexcept
{
    return std::wcscmp(a, b);
}
/// Compares at most `count` wide characters (`wcsncmp`).
inline int WStrCompareN(const wchar_t* a, const wchar_t* b, usize count) noexcept
{
    return std::wcsncmp(a, b, count);
}
/// Finds the first occurrence of `needle` in `haystack` (`wcsstr`).
inline const wchar_t* WStrFind(const wchar_t* haystack, const wchar_t* needle) noexcept
{
    return std::wcsstr(haystack, needle);
}
/// Finds the first occurrence of `needle` in a writable `haystack` (`wcsstr`).
inline wchar_t* WStrFind(wchar_t* haystack, const wchar_t* needle) noexcept
{
    return std::wcsstr(haystack, needle);
}
/// Finds the last occurrence of the wide character `c` (`wcsrchr`).
inline const wchar_t* WStrFindLastChar(const wchar_t* text, wchar_t c) noexcept
{
    return std::wcsrchr(text, c);
}
/// Finds the last occurrence of the wide character `c` in writable text (`wcsrchr`).
inline wchar_t* WStrFindLastChar(wchar_t* text, wchar_t c) noexcept
{
    return std::wcsrchr(text, c);
}
/// Appends `source` to the terminated wide string `destination` (`wcscat`).
inline wchar_t* WStrAppend(wchar_t* destination, const wchar_t* source) noexcept
{
    return std::wcscat(destination, source);
}
/// Copies `count` wide characters; the ranges must not overlap (`wmemcpy`).
inline wchar_t* WMemCopy(wchar_t* destination, const wchar_t* source, usize count) noexcept
{
    return std::wmemcpy(destination, source, count);
}
/// Copies `count` wide characters; the ranges may overlap (`wmemmove`).
inline wchar_t* WMemMove(wchar_t* destination, const wchar_t* source, usize count) noexcept
{
    return std::wmemmove(destination, source, count);
}

}  // namespace engine_placeholder
