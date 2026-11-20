#pragma once

#include "cstr/cstr.h"

#include "types.h"

//! [[StringView]], the non-owning text view. It lives in the base layer (it needs nothing but `types.h`) so the OS
//! primitives, the log and the integer types can take text without depending on the string module; `string/string.h`
//! includes it, so existing code is unaffected.
namespace vse
{

/// Non-owning view of a contiguous range of chars: a pointer and a length.
///
/// The view does not require NUL termination and never copies; it is valid only
/// while the viewed memory is. Comparison and searching are bytewise.
///
/// - Thread-safety: thread-compatible. Reading is safe from many threads while
///   the viewed memory is unchanged.
/// - Ownership: does not own the characters; the caller keeps them alive.
class StringView
{
public:
    /// Creates an empty view with a null data pointer.
    constexpr StringView() = default;
    /// Creates a view of `length` chars starting at `data`.
    ///
    /// - Param data: first character; not copied.
    /// - Param length: number of chars in the view.
    constexpr StringView(const char* data, usize length) : data_(data), length_(length) {}
    /// Creates a view of a NUL-terminated string, excluding the terminator.
    ///
    /// - Param c_str: NUL-terminated string; must not be null.
    StringView(const char* c_str) : data_(c_str), length_(CStrLength(c_str)) {}

    /// Returns the pointer to the first char (not necessarily NUL-terminated).
    const char* Data() const { return data_; }
    /// Returns the length in bytes.
    usize Length() const { return length_; }
    /// Returns true if the view has zero length.
    bool Empty() const { return length_ == 0; }

    /// Returns the byte at `index`.
    ///
    /// - Param index: byte position; must be less than `Length()` (not checked).
    char operator[](usize index) const { return data_[index]; }

    /// The first byte, for a range-for over the bytes (`for (char c : view)`); bytes, not code points.
    ///
    /// - Internal: the range-for protocol.
    const char* begin() const { return data_; }
    /// The end of a range-for over the bytes.
    ///
    /// - Internal: the range-for protocol.
    const char* end() const { return data_ + length_; }

    /// Returns true if both views have the same length and bytes.
    bool operator==(const StringView& other) const
    {
        if (length_ != other.length_)
        {
            return false;
        }
        for (usize i = 0; i < length_; ++i)
        {
            if (data_[i] != other.data_[i])
            {
                return false;
            }
        }
        return true;
    }
    /// Returns true if the views differ in length or any byte.
    bool operator!=(const StringView& other) const { return !(*this == other); }

    /// Compares bytewise (as unsigned chars), shorter-prefix first.
    ///
    /// - Param other: view to compare against.
    /// - Returns: a negative value if this view sorts before `other`, zero if
    ///   equal, positive if after.
    int Compare(const StringView& other) const
    {
        usize min_len = length_ < other.length_ ? length_ : other.length_;
        for (usize i = 0; i < min_len; ++i)
        {
            if (data_[i] != other.data_[i])
            {
                return static_cast<int>(static_cast<byte>(data_[i])) -
                       static_cast<int>(static_cast<byte>(other.data_[i]));
            }
        }
        if (length_ == other.length_)
        {
            return 0;
        }
        return length_ < other.length_ ? -1 : 1;
    }

    /// Returns true if this view sorts before `other` in `Compare` order.
    bool operator<(const StringView& other) const { return Compare(other) < 0; }

    /// Returns the sub-range of `count` bytes starting at `start`, sharing the same memory.
    ///
    /// - Param start: first byte of the sub-range.
    /// - Param count: number of bytes; `start + count` must not exceed
    ///   `Length()` (not checked).
    StringView Substring(usize start, usize count) const { return StringView(data_ + start, count); }

    /// Finds the first occurrence of the byte `c`.
    ///
    /// - Param c: byte to look for.
    /// - Returns: its index, or `Length()` if it does not occur.
    /// - Fails: reports absence by returning `Length()`.
    usize IndexOf(char c) const
    {
        for (usize i = 0; i < length_; ++i)
        {
            if (data_[i] == c)
            {
                return i;
            }
        }
        return length_;
    }

    /// Finds the first occurrence of `pattern` as a substring.
    ///
    /// - Param pattern: bytes to search for.
    /// - Returns: the start index of the first match, or `Length()` if there is
    ///   none or `pattern` is empty.
    /// - Fails: reports absence by returning `Length()`.
    usize Find(StringView pattern) const
    {
        if (pattern.length_ == 0 || pattern.length_ > length_)
        {
            return length_;
        }
        usize last_start = length_ - pattern.length_;
        for (usize start = 0; start <= last_start; ++start)
        {
            bool match = true;
            for (usize i = 0; i < pattern.length_; ++i)
            {
                if (data_[start + i] != pattern.data_[i])
                {
                    match = false;
                    break;
                }
            }
            if (match)
            {
                return start;
            }
        }
        return length_;
    }

    /// Returns the sub-range from `start` to the end, sharing the same memory.
    ///
    /// - Param start: first byte of the sub-range; must not exceed `Length()` (not checked).
    StringView Substring(usize start) const { return StringView(data_ + start, length_ - start); }

    /// Finds the last occurrence of the byte `c`.
    ///
    /// - Param c: byte to look for.
    /// - Returns: its index, or `Length()` if it does not occur.
    /// - Fails: reports absence by returning `Length()`.
    usize LastIndexOf(char c) const
    {
        for (usize i = length_; i > 0; --i)
        {
            if (data_[i - 1] == c)
            {
                return i - 1;
            }
        }
        return length_;
    }

    /// Returns true if the view begins with `prefix`; an empty prefix always matches.
    bool StartsWith(StringView prefix) const
    {
        return prefix.length_ <= length_ && Substring(0, prefix.length_) == prefix;
    }

    /// Returns true if the view ends with `suffix`; an empty suffix always matches.
    bool EndsWith(StringView suffix) const
    {
        return suffix.length_ <= length_ && Substring(length_ - suffix.length_) == suffix;
    }

    /// Returns true if `pattern` occurs in this view; an empty pattern never does.
    bool Contains(StringView pattern) const { return Find(pattern) != length_; }

private:
    const char* data_ = nullptr;
    usize length_ = 0;
};

}  // namespace vse
