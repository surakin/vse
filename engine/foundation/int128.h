#pragma once

#include "lang/ordering.h"
#include "lang/traits.h"

#include "string_view.h"
#include "types.h"

//! 128-bit integers: [[u128]] and [[i128]].
//!
//! Plain classes of two 64-bit halves with a defined layout and one defined behavior on every compiler and target (MSVC has no `__int128`). Where the compiler has `unsigned __int128` the operators use it
//! internally; elsewhere, or when `ENGINE_FORCE_PORTABLE_INT128` is defined, they use portable algorithms (carry arithmetic, 32-bit partial products, Knuth algorithm D) that are tested against the native
//! type. `ENGINE_HAS_NATIVE_INT128` says which one this translation unit uses.
//!
//! Where the builtin type's behavior is undefined this one is defined: `+ - *` and unary minus wrap modulo 2^128 (also for [[i128]]), a shift by 128 or more gives 0 (or the sign fill for `i128 >>`), and
//! division truncates toward zero.

#if !defined(ENGINE_FORCE_PORTABLE_INT128) && defined(__SIZEOF_INT128__)
#define ENGINE_HAS_NATIVE_INT128 1
#define ENGINE_INT128_NAMESPACE int128_native
#else
#define ENGINE_HAS_NATIVE_INT128 0
#define ENGINE_INT128_NAMESPACE int128_portable
#endif

namespace engine_placeholder {

template <typename K>
struct Hash;

// The two implementations live in different inline namespaces, so a program that links a translation unit built with ENGINE_FORCE_PORTABLE_INT128 against the library (the portable test does) never has
// two definitions of one inline function: the symbols differ.
inline namespace ENGINE_INT128_NAMESPACE {

struct u128;
struct i128;

namespace detail128 {
#if ENGINE_HAS_NATIVE_INT128
/// The compiler's unsigned 128-bit type (native mode only).
__extension__ typedef unsigned __int128 NativeU;
/// The compiler's signed 128-bit type (native mode only).
__extension__ typedef __int128 NativeI;
#endif
/// Portable algorithms on (low, high) halves; always compiled so tests can check them against the native type.
struct Parts {
    /// The low 64 bits.
    u64 lo;
    /// The high 64 bits.
    u64 hi;
};
/// Multiplies two values modulo 2^128 with 32-bit partial products.
Parts PortableMul(Parts a, Parts b);
/// Divides `n` by `d` (not zero), giving the quotient and remainder.
void PortableDivMod(Parts n, Parts d, Parts* quotient, Parts* remainder);
} // namespace detail128

/// An unsigned 128-bit integer.
///
/// - Thread-safety: thread-compatible plain data (immutable values may be shared freely).
/// - Ownership: a value; owns nothing.
struct alignas(16) u128 {
    /// The low 64 bits.
    u64 lo = 0;
    /// The high 64 bits.
    u64 hi = 0;

    /// The longest text `ToText` writes, including the terminating NUL (128 binary digits).
    static constexpr usize kMaxTextBytes = 129;

    /// Creates zero.
    constexpr u128() = default;
    /// Creates a value from an unsigned integer of up to 64 bits.
    template <typename T>
        requires(IsIntegral<T> && IsUnsigned<T> && sizeof(T) <= 8 && !IsSame<T, bool>)
    constexpr u128(T value) : lo(static_cast<u64>(value)), hi(0) {}
    /// Creates a value from its high and low halves.
    static constexpr u128 FromParts(u64 high, u64 low) {
        u128 r;
        r.lo = low;
        r.hi = high;
        return r;
    }
    /// Returns the largest value, 2^128 - 1.
    static constexpr u128 Max() { return FromParts(~u64{0}, ~u64{0}); }

    /// Converts to a built-in integer type, keeping the low bits (like a narrowing cast).
    template <typename T>
        requires(IsIntegral<T> && !IsSame<T, bool>)
    explicit constexpr operator T() const {
        return static_cast<T>(lo);
    }
    /// Returns true if the value is not zero.
    explicit constexpr operator bool() const { return (lo | hi) != 0; }
    /// Converts to a double (rounded; large values lose low bits).
    explicit constexpr operator f64() const {
        if (hi == 0) return static_cast<f64>(lo);
        u32 shift = 0; // the number of significant bits in `hi`; the 64 bits above `shift` are then the top of the value
        for (u64 h = hi; h != 0; h >>= 1) ++shift;
        u64 top = shift == 64 ? hi : (hi << (64 - shift)) | (lo >> shift);
        const u64 dropped = shift == 64 ? lo : lo & ((u64{1} << shift) - 1);
        if (dropped != 0) top |= 1; // a sticky bit, so the one conversion below rounds the whole value correctly
        const f64 scale = static_cast<f64>(u64{1} << (shift % 64)) * (shift == 64 ? 18446744073709551616.0 : 1.0);
        return static_cast<f64>(top) * scale;
    }

    /// Returns the number of leading zero bits (128 for zero).
    constexpr u32 CountLeadingZeros() const {
        if (hi != 0) return Clz64(hi);
        return 64 + Clz64(lo);
    }
    /// Returns the number of trailing zero bits (128 for zero).
    constexpr u32 CountTrailingZeros() const {
        if (lo != 0) return Ctz64(lo);
        return 64 + Ctz64(hi);
    }
    /// Returns the number of set bits.
    constexpr u32 PopCount() const { return Pop64(lo) + Pop64(hi); }
    /// Returns the value with its 16 bytes in reverse order.
    constexpr u128 ByteSwap() const { return FromParts(Swap64(lo), Swap64(hi)); }
    /// Returns a hash of the value.
    constexpr usize Hash() const {
        u64 h = lo * 0x9E3779B97F4A7C15ull;
        h ^= (h >> 32);
        h += hi * 0xC2B2AE3D27D4EB4Full;
        h ^= (h >> 29);
        return static_cast<usize>(h * 0x165667B19E3779F9ull);
    }

    /// Writes the value as text in `base` (2 to 16, digits `0-9a-f`) with a terminating NUL.
    ///
    /// - Param out: destination; at least `kMaxTextBytes` bytes always suffices.
    /// - Param capacity: size of `out`.
    /// - Param base: 2 to 16.
    /// - Returns: the number of characters written, excluding the NUL.
    /// - Fails: returns 0 and writes an empty string (when `capacity` is not 0) if the text does not fit or `base` is not 2 to 16.
    usize ToText(char* out, usize capacity, u32 base = 10) const;
    /// Reads unsigned digits in `base` (2 to 16, either letter case, no sign, no prefix, no separators).
    ///
    /// - Param text: the digits.
    /// - Param base: 2 to 16.
    /// - Param out: receives the value; unchanged on failure.
    /// - Returns: true if `text` is a non-empty run of digits whose value fits.
    /// - Fails: returns false for empty text, a character that is not a digit of `base`, or a value over 2^128 - 1.
    static bool FromText(StringView text, u32 base, u128* out);

    /// Adds `other`, wrapping modulo 2^128.
    constexpr u128& operator+=(u128 other) {
        const u64 low = lo + other.lo;
        hi = hi + other.hi + (low < lo ? 1 : 0);
        lo = low;
        return *this;
    }
    /// Subtracts `other`, wrapping modulo 2^128.
    constexpr u128& operator-=(u128 other) {
        const u64 borrow = lo < other.lo ? 1 : 0;
        lo = lo - other.lo;
        hi = hi - other.hi - borrow;
        return *this;
    }
    /// Multiplies by `other`, wrapping modulo 2^128.
    u128& operator*=(u128 other) {
#if ENGINE_HAS_NATIVE_INT128
        const detail128::NativeU r = ToNative() * other.ToNative();
        *this = FromNative(r);
#else
        const detail128::Parts r = detail128::PortableMul({lo, hi}, {other.lo, other.hi});
        lo = r.lo;
        hi = r.hi;
#endif
        return *this;
    }
    /// Divides by `other`, truncating.
    ///
    /// - Aborts: `other` is zero.
    u128& operator/=(u128 other);
    /// Replaces the value by its remainder when divided by `other`.
    ///
    /// - Aborts: `other` is zero.
    u128& operator%=(u128 other);
    /// Shifts left by `count` bits; 128 or more gives 0.
    constexpr u128& operator<<=(u32 count) {
        if (count == 0) return *this;
        if (count >= 128) {
            lo = hi = 0;
        } else if (count >= 64) {
            hi = lo << (count - 64);
            lo = 0;
        } else {
            hi = (hi << count) | (lo >> (64 - count));
            lo = lo << count;
        }
        return *this;
    }
    /// Shifts right by `count` bits; 128 or more gives 0.
    constexpr u128& operator>>=(u32 count) {
        if (count == 0) return *this;
        if (count >= 128) {
            lo = hi = 0;
        } else if (count >= 64) {
            lo = hi >> (count - 64);
            hi = 0;
        } else {
            lo = (lo >> count) | (hi << (64 - count));
            hi = hi >> count;
        }
        return *this;
    }
    /// ANDs with `other`.
    constexpr u128& operator&=(u128 other) {
        lo &= other.lo;
        hi &= other.hi;
        return *this;
    }
    /// ORs with `other`.
    constexpr u128& operator|=(u128 other) {
        lo |= other.lo;
        hi |= other.hi;
        return *this;
    }
    /// XORs with `other`.
    constexpr u128& operator^=(u128 other) {
        lo ^= other.lo;
        hi ^= other.hi;
        return *this;
    }
    /// Adds one (wrapping) and returns the new value.
    constexpr u128& operator++() { return *this += u128(1u); }
    /// Adds one (wrapping) and returns the old value.
    constexpr u128 operator++(int) {
        const u128 old = *this;
        ++*this;
        return old;
    }
    /// Subtracts one (wrapping) and returns the new value.
    constexpr u128& operator--() { return *this -= u128(1u); }
    /// Subtracts one (wrapping) and returns the old value.
    constexpr u128 operator--(int) {
        const u128 old = *this;
        --*this;
        return old;
    }
    /// Returns the value unchanged.
    constexpr u128 operator+() const { return *this; }
    /// Returns the two's complement negation (2^128 minus the value, wrapping).
    constexpr u128 operator-() const { return FromParts(0, 0) -= *this; }
    /// Returns the bitwise complement.
    constexpr u128 operator~() const { return FromParts(~hi, ~lo); }
    /// Returns true if the value is zero.
    constexpr bool operator!() const { return (lo | hi) == 0; }

    /// Returns the sum, wrapping.
    friend constexpr u128 operator+(u128 a, u128 b) { return a += b; }
    /// Returns the difference, wrapping.
    friend constexpr u128 operator-(u128 a, u128 b) { return a -= b; }
    /// Returns the product, wrapping.
    friend u128 operator*(u128 a, u128 b) { return a *= b; }
    /// Returns the quotient, truncated.
    ///
    /// - Aborts: `b` is zero.
    friend u128 operator/(u128 a, u128 b) { return a /= b; }
    /// Returns the remainder.
    ///
    /// - Aborts: `b` is zero.
    friend u128 operator%(u128 a, u128 b) { return a %= b; }
    /// Returns `a` shifted left.
    friend constexpr u128 operator<<(u128 a, u32 count) { return a <<= count; }
    /// Returns `a` shifted right.
    friend constexpr u128 operator>>(u128 a, u32 count) { return a >>= count; }
    /// Returns the bitwise AND.
    friend constexpr u128 operator&(u128 a, u128 b) { return a &= b; }
    /// Returns the bitwise OR.
    friend constexpr u128 operator|(u128 a, u128 b) { return a |= b; }
    /// Returns the bitwise XOR.
    friend constexpr u128 operator^(u128 a, u128 b) { return a ^= b; }
    /// Returns true if both are equal.
    friend constexpr bool operator==(u128 a, u128 b) { return a.lo == b.lo && a.hi == b.hi; }
    /// Returns true if they differ.
    friend constexpr bool operator!=(u128 a, u128 b) { return !(a == b); }
    /// Returns true if `a` is less than `b`.
    friend constexpr bool operator<(u128 a, u128 b) { return a.hi != b.hi ? a.hi < b.hi : a.lo < b.lo; }
    /// Returns true if `a` is greater than `b`.
    friend constexpr bool operator>(u128 a, u128 b) { return b < a; }
    /// Returns true if `a` is at most `b`.
    friend constexpr bool operator<=(u128 a, u128 b) { return !(b < a); }
    /// Returns true if `a` is at least `b`.
    friend constexpr bool operator>=(u128 a, u128 b) { return !(a < b); }
    /// Three-way comparison (the order of `<`).
    friend constexpr Ordering operator<=>(u128 a, u128 b) { return a < b ? Ordering::less : (a == b ? Ordering::equal : Ordering::greater); }

#if ENGINE_HAS_NATIVE_INT128
    /// Returns the value as the compiler's native type (only where `ENGINE_HAS_NATIVE_INT128`).
    constexpr detail128::NativeU ToNative() const { return (static_cast<detail128::NativeU>(hi) << 64) | lo; }
    /// Makes a value from the compiler's native type (only where `ENGINE_HAS_NATIVE_INT128`).
    static constexpr u128 FromNative(detail128::NativeU v) { return FromParts(static_cast<u64>(v >> 64), static_cast<u64>(v)); }
#endif

private:
    static constexpr u32 Clz64(u64 v) {
        if (v == 0) return 64;
        u32 n = 0;
        while ((v & (u64{1} << 63)) == 0) {
            v <<= 1;
            ++n;
        }
        return n;
    }
    static constexpr u32 Ctz64(u64 v) {
        if (v == 0) return 64;
        u32 n = 0;
        while ((v & 1) == 0) {
            v >>= 1;
            ++n;
        }
        return n;
    }
    static constexpr u32 Pop64(u64 v) {
        u32 n = 0;
        while (v != 0) {
            v &= v - 1;
            ++n;
        }
        return n;
    }
    static constexpr u64 Swap64(u64 v) {
        u64 r = 0;
        for (int i = 0; i < 8; ++i) {
            r = (r << 8) | (v & 0xff);
            v >>= 8;
        }
        return r;
    }
};

/// A signed 128-bit integer in two's complement; the same storage as [[u128]].
///
/// Arithmetic wraps (signed overflow is defined), division truncates toward zero, and `>>` is arithmetic.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: a value; owns nothing.
struct alignas(16) i128 {
    /// The low 64 bits of the two's complement representation.
    u64 lo = 0;
    /// The high 64 bits (the sign is the top bit).
    u64 hi = 0;

    /// The longest text `ToText` writes, including the sign and the terminating NUL.
    static constexpr usize kMaxTextBytes = 130;

    /// Creates zero.
    constexpr i128() = default;
    /// Creates a value from a signed integer of up to 64 bits (sign-extended).
    template <typename T>
        requires(IsIntegral<T> && IsSigned<T> && sizeof(T) <= 8)
    constexpr i128(T value) : lo(static_cast<u64>(static_cast<i64>(value))), hi(value < 0 ? ~u64{0} : 0) {}
    /// Creates a value from an unsigned integer of up to 64 bits.
    template <typename T>
        requires(IsIntegral<T> && IsUnsigned<T> && sizeof(T) <= 8 && !IsSame<T, bool>)
    constexpr i128(T value) : lo(static_cast<u64>(value)), hi(0) {}
    /// Reinterprets the bits of an unsigned value.
    explicit constexpr i128(u128 value) : lo(value.lo), hi(value.hi) {}
    /// Creates a value from its high and low halves.
    static constexpr i128 FromParts(u64 high, u64 low) {
        i128 r;
        r.lo = low;
        r.hi = high;
        return r;
    }
    /// Returns the largest value, 2^127 - 1.
    static constexpr i128 Max() { return FromParts(~u64{0} >> 1, ~u64{0}); }
    /// Returns the smallest value, -2^127.
    static constexpr i128 Min() { return FromParts(u64{1} << 63, 0); }

    /// Reinterprets the bits as an unsigned value.
    constexpr u128 ToBits() const { return u128::FromParts(hi, lo); }
    /// Returns true if the value is negative.
    constexpr bool IsNegative() const { return (hi >> 63) != 0; }
    /// Returns the magnitude (for the minimum value, 2^127).
    constexpr u128 Magnitude() const { return IsNegative() ? -ToBits() : ToBits(); }
    /// Converts to a built-in integer type, keeping the low bits.
    template <typename T>
        requires(IsIntegral<T> && !IsSame<T, bool>)
    explicit constexpr operator T() const {
        return static_cast<T>(lo);
    }
    /// Returns true if the value is not zero.
    explicit constexpr operator bool() const { return (lo | hi) != 0; }
    /// Converts to a double (rounded).
    explicit constexpr operator f64() const { return IsNegative() ? -static_cast<f64>(Magnitude()) : static_cast<f64>(ToBits()); }
    /// Returns a hash of the value.
    constexpr usize Hash() const { return ToBits().Hash(); }

    /// Writes the value in `base` (2 to 16) with a leading `-` when negative and a terminating NUL.
    ///
    /// - Param out: destination; at least `kMaxTextBytes` bytes always suffices.
    /// - Param capacity: size of `out`.
    /// - Param base: 2 to 16.
    /// - Returns: the number of characters written, excluding the NUL.
    /// - Fails: returns 0 and writes an empty string (when `capacity` is not 0) if the text does not fit or `base` is not 2 to 16.
    usize ToText(char* out, usize capacity, u32 base = 10) const;
    /// Reads digits in `base` with an optional leading `-` (or `+`).
    ///
    /// - Param text: the digits.
    /// - Param base: 2 to 16.
    /// - Param out: receives the value; unchanged on failure.
    /// - Returns: true if `text` is a non-empty run of digits (after the sign) whose value fits.
    /// - Fails: returns false for empty text, a bad character, or a value outside -2^127 .. 2^127 - 1.
    static bool FromText(StringView text, u32 base, i128* out);

    /// Adds `other`, wrapping.
    constexpr i128& operator+=(i128 other) { return *this = i128(ToBits() + other.ToBits()); }
    /// Subtracts `other`, wrapping.
    constexpr i128& operator-=(i128 other) { return *this = i128(ToBits() - other.ToBits()); }
    /// Multiplies by `other`, wrapping.
    i128& operator*=(i128 other) { return *this = i128(ToBits() * other.ToBits()); }
    /// Divides by `other`, truncating toward zero (the minimum value divided by -1 wraps to the minimum).
    ///
    /// - Aborts: `other` is zero.
    i128& operator/=(i128 other);
    /// Replaces the value by its remainder, which has the sign of the dividend.
    ///
    /// - Aborts: `other` is zero.
    i128& operator%=(i128 other);
    /// Shifts left; 128 or more gives 0.
    constexpr i128& operator<<=(u32 count) { return *this = i128(ToBits() << count); }
    /// Shifts right arithmetically; 128 or more gives 0 or -1 by sign.
    constexpr i128& operator>>=(u32 count) {
        if (!IsNegative()) return *this = i128(ToBits() >> count);
        return *this = i128(~((~ToBits()) >> count));
    }
    /// ANDs with `other`.
    constexpr i128& operator&=(i128 other) { return *this = i128(ToBits() & other.ToBits()); }
    /// ORs with `other`.
    constexpr i128& operator|=(i128 other) { return *this = i128(ToBits() | other.ToBits()); }
    /// XORs with `other`.
    constexpr i128& operator^=(i128 other) { return *this = i128(ToBits() ^ other.ToBits()); }
    /// Adds one (wrapping) and returns the new value.
    constexpr i128& operator++() { return *this += i128(1); }
    /// Adds one (wrapping) and returns the old value.
    constexpr i128 operator++(int) {
        const i128 old = *this;
        ++*this;
        return old;
    }
    /// Subtracts one (wrapping) and returns the new value.
    constexpr i128& operator--() { return *this -= i128(1); }
    /// Subtracts one (wrapping) and returns the old value.
    constexpr i128 operator--(int) {
        const i128 old = *this;
        --*this;
        return old;
    }
    /// Returns the value unchanged.
    constexpr i128 operator+() const { return *this; }
    /// Returns the negation (the minimum value negates to itself).
    constexpr i128 operator-() const { return i128(-ToBits()); }
    /// Returns the bitwise complement.
    constexpr i128 operator~() const { return i128(~ToBits()); }
    /// Returns true if the value is zero.
    constexpr bool operator!() const { return (lo | hi) == 0; }

    /// Returns the sum, wrapping.
    friend constexpr i128 operator+(i128 a, i128 b) { return a += b; }
    /// Returns the difference, wrapping.
    friend constexpr i128 operator-(i128 a, i128 b) { return a -= b; }
    /// Returns the product, wrapping.
    friend i128 operator*(i128 a, i128 b) { return a *= b; }
    /// Returns the quotient, truncated toward zero.
    ///
    /// - Aborts: `b` is zero.
    friend i128 operator/(i128 a, i128 b) { return a /= b; }
    /// Returns the remainder.
    ///
    /// - Aborts: `b` is zero.
    friend i128 operator%(i128 a, i128 b) { return a %= b; }
    /// Returns `a` shifted left.
    friend constexpr i128 operator<<(i128 a, u32 count) { return a <<= count; }
    /// Returns `a` shifted right arithmetically.
    friend constexpr i128 operator>>(i128 a, u32 count) { return a >>= count; }
    /// Returns the bitwise AND.
    friend constexpr i128 operator&(i128 a, i128 b) { return a &= b; }
    /// Returns the bitwise OR.
    friend constexpr i128 operator|(i128 a, i128 b) { return a |= b; }
    /// Returns the bitwise XOR.
    friend constexpr i128 operator^(i128 a, i128 b) { return a ^= b; }
    /// Returns true if both are equal.
    friend constexpr bool operator==(i128 a, i128 b) { return a.lo == b.lo && a.hi == b.hi; }
    /// Returns true if they differ.
    friend constexpr bool operator!=(i128 a, i128 b) { return !(a == b); }
    /// Returns true if `a` is less than `b` (signed).
    friend constexpr bool operator<(i128 a, i128 b) {
        if (a.IsNegative() != b.IsNegative()) return a.IsNegative();
        return a.ToBits() < b.ToBits();
    }
    /// Returns true if `a` is greater than `b`.
    friend constexpr bool operator>(i128 a, i128 b) { return b < a; }
    /// Returns true if `a` is at most `b`.
    friend constexpr bool operator<=(i128 a, i128 b) { return !(b < a); }
    /// Returns true if `a` is at least `b`.
    friend constexpr bool operator>=(i128 a, i128 b) { return !(a < b); }
    /// Three-way comparison (the order of `<`, signed).
    friend constexpr Ordering operator<=>(i128 a, i128 b) { return a < b ? Ordering::less : (a == b ? Ordering::equal : Ordering::greater); }
};

static_assert(sizeof(u128) == 16 && sizeof(i128) == 16, "the 128-bit types are 16 bytes");
static_assert(alignof(u128) == 16 && alignof(i128) == 16, "the 128-bit types are 16-byte aligned");
static_assert(IsTriviallyCopyable<u128> && IsTriviallyCopyable<i128>, "the 128-bit types are plain data");
static_assert(IsStandardLayout<u128> && IsStandardLayout<i128>, "the 128-bit types have a defined layout");

} // inline namespace (native or portable implementation)

/// Hash function for `u128` keys in a [[HashMap]].
template <>
struct Hash<u128> {
    /// Returns the hash of `key`.
    usize operator()(const u128& key) const { return key.Hash(); }
};
/// Hash function for `i128` keys in a [[HashMap]].
template <>
struct Hash<i128> {
    /// Returns the hash of `key`.
    usize operator()(const i128& key) const { return key.Hash(); }
};

} // namespace engine_placeholder
