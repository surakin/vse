#pragma once

#include "array.h"
#include "mem.h"
#include "types.h"

// StringView (non-owning), String<Allocator> (owning, allocator-backed
// via Day 6's Array<char, Allocator>), and StringBuilder<Allocator>
// (integer/float/string formatting — there's no printf/sprintf here,
// per PLAN.md assumption #2's "still hand-rolls... string handling").
namespace engine_placeholder {

class StringView {
public:
    constexpr StringView() = default;
    constexpr StringView(const char* data, usize length) : data_(data), length_(length) {}
    StringView(const char* c_str) : data_(c_str), length_(strlen(c_str)) {}

    const char* Data() const { return data_; }
    usize Length() const { return length_; }
    bool Empty() const { return length_ == 0; }

    char operator[](usize index) const { return data_[index]; }

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
    bool operator!=(const StringView& other) const { return !(*this == other); }

    // Lexicographic comparison: <0 if this < other, 0 if equal, >0 if this > other.
    int Compare(const StringView& other) const {
        usize min_len = length_ < other.length_ ? length_ : other.length_;
        for (usize i = 0; i < min_len; ++i) {
            if (data_[i] != other.data_[i]) {
                return static_cast<int>(static_cast<unsigned char>(data_[i])) -
                       static_cast<int>(static_cast<unsigned char>(other.data_[i]));
            }
        }
        if (length_ == other.length_) {
            return 0;
        }
        return length_ < other.length_ ? -1 : 1;
    }

    bool operator<(const StringView& other) const { return Compare(other) < 0; }

    // Precondition: start + count <= Length().
    StringView Substring(usize start, usize count) const {
        return StringView(data_ + start, count);
    }

private:
    const char* data_ = nullptr;
    usize length_ = 0;
};

template <typename Allocator>
class String {
public:
    explicit String(Allocator& allocator) : chars_(allocator) {
        chars_.Push('\0');
    }

    String(Allocator& allocator, StringView sv) : chars_(allocator) {
        chars_.Reserve(sv.Length() + 1);
        for (usize i = 0; i < sv.Length(); ++i) {
            chars_.Push(sv[i]);
        }
        chars_.Push('\0');
    }

    String(const String&) = delete;
    String& operator=(const String&) = delete;

    void Append(StringView sv) {
        chars_.Pop(); // drop the trailing '\0'
        for (usize i = 0; i < sv.Length(); ++i) {
            chars_.Push(sv[i]);
        }
        chars_.Push('\0');
    }

    void Append(char c) {
        chars_.Pop();
        chars_.Push(c);
        chars_.Push('\0');
    }

    const char* CStr() const { return &chars_[0]; }
    usize Length() const { return chars_.Size() - 1; }

    StringView View() const { return StringView(CStr(), Length()); }

private:
    Array<char, Allocator> chars_;
};

template <typename Allocator>
class StringBuilder {
public:
    explicit StringBuilder(Allocator& allocator) : str_(allocator) {}

    StringBuilder& Append(StringView sv) {
        str_.Append(sv);
        return *this;
    }

    StringBuilder& Append(char c) {
        str_.Append(c);
        return *this;
    }

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

    StringBuilder& AppendUInt(u64 value) {
        if (value == 0) {
            return Append('0');
        }
        AppendDigits(value);
        return *this;
    }

    // Fixed-point formatting with `precision` digits after the decimal
    // point, round-half-up. Intended for values that are exactly (or
    // near-exactly) representable at that precision — not a general
    // shortest-round-trip float formatter.
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

    StringView View() const { return str_.View(); }
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
