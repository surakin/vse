#pragma once

#include <cstring>

#include "containers/vector.h"
#include "types.h"
#include "string/utf8_codec.h"

//! Text types: non-owning StringView, allocator-backed String, and
//! StringBuilder for formatting numbers and text.
//!
//! Strings are byte sequences, treated as UTF-8 by the helpers in utf8.h.
namespace engine_placeholder {

/// Non-owning view of a contiguous range of chars: a pointer and a length.
///
/// The view does not require NUL termination and never copies; it is valid only
/// while the viewed memory is. Comparison and searching are bytewise.
///
/// Thread-safety: thread-compatible. Reading is safe from many threads while
///   the viewed memory is unchanged.
/// Ownership: does not own the characters; the caller keeps them alive.
class StringView {
public:
    /// Creates an empty view with a null data pointer.
    constexpr StringView() = default;
    /// Creates a view of `length` chars starting at `data`.
    ///
    /// Param data: first character; not copied.
    /// Param length: number of chars in the view.
    constexpr StringView(const char* data, usize length) : data_(data), length_(length) {}
    /// Creates a view of a NUL-terminated string, excluding the terminator.
    ///
    /// Param c_str: NUL-terminated string; must not be null.
    StringView(const char* c_str) : data_(c_str), length_(strlen(c_str)) {}

    /// Returns the pointer to the first char (not necessarily NUL-terminated).
    const char* Data() const { return data_; }
    /// Returns the length in bytes.
    usize Length() const { return length_; }
    /// Returns true if the view has zero length.
    bool Empty() const { return length_ == 0; }

    /// Returns the byte at `index`.
    ///
    /// Param index: byte position; must be less than `Length()` (not checked).
    char operator[](usize index) const { return data_[index]; }

    /// Returns true if both views have the same length and bytes.
    bool operator==(const StringView& other) const {
        if (length_ != other.length_) {
            return false;
        }
        for (usize i = 0; i < length_; ++i) {
            if (data_[i] != other.data_[i]) {
                return false;
            }
        }
        return true;
    }
    /// Returns true if the views differ in length or any byte.
    bool operator!=(const StringView& other) const { return !(*this == other); }

    /// Compares bytewise (as unsigned chars), shorter-prefix first.
    ///
    /// Param other: view to compare against.
    /// Returns: a negative value if this view sorts before `other`, zero if
    ///   equal, positive if after.
    int Compare(const StringView& other) const {
        usize min_len = length_ < other.length_ ? length_ : other.length_;
        for (usize i = 0; i < min_len; ++i) {
            if (data_[i] != other.data_[i]) {
                return static_cast<int>(static_cast<byte>(data_[i])) -
                       static_cast<int>(static_cast<byte>(other.data_[i]));
            }
        }
        if (length_ == other.length_) {
            return 0;
        }
        return length_ < other.length_ ? -1 : 1;
    }

    /// Returns true if this view sorts before `other` in `Compare` order.
    bool operator<(const StringView& other) const { return Compare(other) < 0; }

    /// Returns the sub-range of `count` bytes starting at `start`, sharing the same memory.
    ///
    /// Param start: first byte of the sub-range.
    /// Param count: number of bytes; `start + count` must not exceed
    ///   `Length()` (not checked).
    StringView Substring(usize start, usize count) const {
        return StringView(data_ + start, count);
    }

    /// Finds the first occurrence of the byte `c`.
    ///
    /// Param c: byte to look for.
    /// Returns: its index, or `Length()` if it does not occur.
    /// Fails: reports absence by returning `Length()`.
    usize IndexOf(char c) const {
        for (usize i = 0; i < length_; ++i) {
            if (data_[i] == c) {
                return i;
            }
        }
        return length_;
    }

    /// Finds the first occurrence of `pattern` as a substring.
    ///
    /// Param pattern: bytes to search for.
    /// Returns: the start index of the first match, or `Length()` if there is
    ///   none or `pattern` is empty.
    /// Fails: reports absence by returning `Length()`.
    usize Find(StringView pattern) const {
        if (pattern.length_ == 0 || pattern.length_ > length_) {
            return length_;
        }
        usize last_start = length_ - pattern.length_;
        for (usize start = 0; start <= last_start; ++start) {
            bool match = true;
            for (usize i = 0; i < pattern.length_; ++i) {
                if (data_[start + i] != pattern.data_[i]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return start;
            }
        }
        return length_;
    }

    /// Returns the sub-range from `start` to the end, sharing the same memory.
    ///
    /// Param start: first byte of the sub-range; must not exceed `Length()` (not checked).
    StringView Substring(usize start) const { return StringView(data_ + start, length_ - start); }

    /// Finds the last occurrence of the byte `c`.
    ///
    /// Param c: byte to look for.
    /// Returns: its index, or `Length()` if it does not occur.
    /// Fails: reports absence by returning `Length()`.
    usize LastIndexOf(char c) const {
        for (usize i = length_; i > 0; --i) {
            if (data_[i - 1] == c) {
                return i - 1;
            }
        }
        return length_;
    }

    /// Returns true if the view begins with `prefix`; an empty prefix always matches.
    bool StartsWith(StringView prefix) const {
        return prefix.length_ <= length_ && Substring(0, prefix.length_) == prefix;
    }

    /// Returns true if the view ends with `suffix`; an empty suffix always matches.
    bool EndsWith(StringView suffix) const {
        return suffix.length_ <= length_ && Substring(length_ - suffix.length_) == suffix;
    }

    /// Returns true if `pattern` occurs in this view; an empty pattern never does.
    bool Contains(StringView pattern) const { return Find(pattern) != length_; }

private:
    const char* data_ = nullptr;
    usize length_ = 0;
};

/// Owning, growable, always NUL-terminated string whose bytes come from an allocator.
///
/// Backed by a [[Vector]] of `char`, so it inherits that class's allocation
/// behavior: growth abandons old blocks inside the allocator rather than freeing
/// them. Contents are expected to be well-formed UTF-8. A moved-from string is
/// left with no storage and must not be used except for assignment or
/// destruction.
///
/// Thread-safety: not-thread-safe. Synchronize externally if shared.
/// Ownership: the allocator owns the memory and must outlive the string; the
///   string never frees it. Views and C-string pointers are invalidated by any
///   append.
template <typename Allocator>
class String {
public:
    /// Creates an empty string, allocating space for the terminator.
    ///
    /// Param allocator: allocator for the characters; must outlive the string.
    /// Aborts: the allocator is out of memory.
    explicit String(Allocator& allocator) : chars_(allocator) {
        chars_.Push('\0');
    }

    /// Creates a string holding a copy of `sv`.
    ///
    /// Param allocator: allocator for the characters; must outlive the string.
    /// Param sv: text to copy; need not be NUL-terminated.
    /// Aborts: the allocator is out of memory.
    String(Allocator& allocator, StringView sv) : chars_(allocator) {
        chars_.Reserve(sv.Length() + 1);
        for (usize i = 0; i < sv.Length(); ++i) {
            chars_.Push(sv[i]);
        }
        chars_.Push('\0');
    }

    String(const String&) = delete;
    String& operator=(const String&) = delete;

    String(String&&) noexcept = default;
    String& operator=(String&&) noexcept = default;

    /// Appends a copy of `sv`.
    ///
    /// Param sv: text to append; must not view this string's own storage
    ///   because growth may invalidate it.
    /// Aborts: growth is needed and the allocator is out of memory.
    void Append(StringView sv) {
        chars_.Pop(); // drop the trailing '\0'
        for (usize i = 0; i < sv.Length(); ++i) {
            chars_.Push(sv[i]);
        }
        chars_.Push('\0');
    }

    /// Appends one ASCII character.
    ///
    /// Param c: character to append; must be ASCII (below 0x80) because a lone
    ///   non-ASCII byte would make the string malformed UTF-8 (not checked).
    ///   Use [[StringBuilder::AppendCodepoint]] for other codepoints.
    /// Aborts: growth is needed and the allocator is out of memory.
    void Append(char c) {
        chars_.Pop();
        chars_.Push(c);
        chars_.Push('\0');
    }

    /// Appends already-encoded bytes verbatim, without validating them.
    ///
    /// Unlike `Append(char)`, the bytes may be part of a multi-byte UTF-8
    /// sequence; the caller is responsible for keeping the string well formed.
    ///
    /// Param bytes: first byte to append.
    /// Param count: number of bytes.
    /// Aborts: growth is needed and the allocator is out of memory.
    void AppendRawBytes(const char* bytes, usize count) {
        chars_.Pop();
        for (usize i = 0; i < count; ++i) {
            chars_.Push(bytes[i]);
        }
        chars_.Push('\0');
    }

    /// Returns the NUL-terminated contents.
    ///
    /// The pointer is valid until the string is appended to, moved from or
    /// destroyed.
    const char* CStr() const { return &chars_[0]; }
    /// Returns the length in bytes, excluding the terminator.
    usize Length() const { return chars_.Size() - 1; }

    /// Returns a non-owning view of the contents, valid until the string is modified or destroyed.
    StringView View() const { return StringView(CStr(), Length()); }
    /// Converts to a view of the contents (the same lifetime rules as `View`).
    operator StringView() const& { return View(); }
    /// A temporary string does not convert: the view would dangle; call `View()` while the string is alive instead.
    operator StringView() const&& = delete;

    /// Returns true if the contents equal `other`, byte for byte.
    bool operator==(StringView other) const { return View() == other; }
    /// Returns true if the contents differ from `other`.
    bool operator!=(StringView other) const { return View() != other; }

private:
    Vector<char, Allocator> chars_;
};

/// Accumulates text and formatted numbers into a [[String]]; each `Append` returns
/// the builder so calls can be chained.
///
/// No printf-style formatting is used; the number formatters are hand-written.
/// Memory behavior is that of the underlying [[String]].
///
/// Thread-safety: not-thread-safe. Synchronize externally if shared.
/// Ownership: the allocator owns the memory and must outlive the builder; the
///   views and C-string pointers it returns are invalidated by any append.
template <typename Allocator>
class StringBuilder {
public:
    /// Creates an empty builder.
    ///
    /// Param allocator: allocator for the characters; must outlive the builder.
    /// Aborts: the allocator is out of memory.
    explicit StringBuilder(Allocator& allocator) : str_(allocator) {}

    /// Appends the text `sv`.
    ///
    /// Param sv: text to append.
    /// Returns: this builder.
    /// Aborts: growth is needed and the allocator is out of memory.
    StringBuilder& Append(StringView sv) {
        str_.Append(sv);
        return *this;
    }

    /// Appends one ASCII character.
    ///
    /// Param c: character to append; must be below 0x80 (not checked).
    /// Returns: this builder.
    /// Aborts: growth is needed and the allocator is out of memory.
    StringBuilder& Append(char c) {
        str_.Append(c);
        return *this;
    }

    /// Appends a Unicode codepoint encoded as UTF-8.
    ///
    /// Param codepoint: scalar value to encode; surrogates and values above
    ///   0x10FFFF are replaced by U+FFFD.
    /// Returns: this builder.
    /// Aborts: growth is needed and the allocator is out of memory.
    StringBuilder& AppendCodepoint(u32 codepoint) {
        char buf[4];
        usize len = EncodeUtf8(codepoint, buf);
        str_.AppendRawBytes(buf, len);
        return *this;
    }

    /// Appends a signed integer in decimal, with a leading `-` if negative.
    ///
    /// Handles the full `i64` range.
    ///
    /// Param value: number to format.
    /// Returns: this builder.
    /// Aborts: growth is needed and the allocator is out of memory.
    StringBuilder& AppendInt(i64 value) {
        if (value == 0) {
            return Append('0');
        }
        bool negative = value < 0;
        // Avoid UB negating INT64_MIN directly.
        u64 magnitude = negative ? (static_cast<u64>(-(value + 1)) + 1) : static_cast<u64>(value);
        if (negative) {
            Append('-');
        }
        AppendDigits(magnitude);
        return *this;
    }

    /// Appends an unsigned integer in decimal.
    ///
    /// Param value: number to format.
    /// Returns: this builder.
    /// Aborts: growth is needed and the allocator is out of memory.
    StringBuilder& AppendUInt(u64 value) {
        if (value == 0) {
            return Append('0');
        }
        AppendDigits(value);
        return *this;
    }

    /// Appends an unsigned integer as lowercase hexadecimal with no `0x` prefix.
    ///
    /// Always emits at least one digit, so 0 becomes `0`.
    ///
    /// Param value: number to format.
    /// Param min_digits: minimum digit count; the output is zero-padded on the
    ///   left, and values above 16 are treated as 16.
    /// Returns: this builder.
    /// Aborts: growth is needed and the allocator is out of memory.
    StringBuilder& AppendHex(u64 value, int min_digits = 1) {
        char digits[16];
        int n = 0;
        while (value > 0) {
            u64 nibble = value & 0xF;
            digits[n++] = static_cast<char>(nibble < 10 ? '0' + nibble : 'a' + (nibble - 10));
            value >>= 4;
        }
        while (n < min_digits && n < 16) {
            digits[n++] = '0';
        }
        if (n == 0) {
            digits[n++] = '0';
        }
        while (n > 0) {
            Append(digits[--n]);
        }
        return *this;
    }

    /// Appends a number in fixed-point notation, rounding half up.
    ///
    /// Not a general shortest-round-trip formatter: use it for values that are
    /// exactly or nearly representable at the requested precision. The scaled
    /// value is held in a `u64`, so very large magnitudes, NaN, infinities and
    /// a `precision` above 18 give unspecified output.
    ///
    /// Param value: number to format.
    /// Param precision: digits after the decimal point; 0 omits the point.
    /// Returns: this builder.
    /// Aborts: growth is needed and the allocator is out of memory.
    StringBuilder& AppendFloat(f64 value, int precision = 6) {
        if (value < 0) {
            Append('-');
            value = -value;
        }
        f64 scale = 1.0;
        for (int i = 0; i < precision; ++i) {
            scale *= 10.0;
        }
        u64 scaled = static_cast<u64>(value * scale + 0.5);
        u64 divisor = static_cast<u64>(scale);
        u64 int_part = divisor > 0 ? scaled / divisor : scaled;
        u64 frac_part = divisor > 0 ? scaled % divisor : 0;

        AppendUInt(int_part);
        if (precision > 0) {
            Append('.');
            AppendFracDigits(frac_part, precision);
        }
        return *this;
    }

    /// Returns a view of the text built so far, valid until the next append.
    StringView View() const { return str_.View(); }
    /// Returns the NUL-terminated text built so far, valid until the next append.
    const char* CStr() const { return str_.CStr(); }

private:
    void AppendDigits(u64 value) {
        char digits[20];
        int n = 0;
        while (value > 0) {
            digits[n++] = static_cast<char>('0' + (value % 10));
            value /= 10;
        }
        while (n > 0) {
            Append(digits[--n]);
        }
    }

    // Appends exactly `precision` digits, zero-padded on the left.
    void AppendFracDigits(u64 value, int precision) {
        char digits[20];
        int n = 0;
        if (value == 0) {
            for (int i = 0; i < precision; ++i) {
                digits[n++] = '0';
            }
        } else {
            while (value > 0) {
                digits[n++] = static_cast<char>('0' + (value % 10));
                value /= 10;
            }
            while (n < precision) {
                digits[n++] = '0';
            }
        }
        while (n > 0) {
            Append(digits[--n]);
        }
    }

    String<Allocator> str_;
};

} // namespace engine_placeholder
