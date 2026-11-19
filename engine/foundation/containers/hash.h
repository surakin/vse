#pragma once

#include "containers/span.h"
#include "lang/traits.h"
#include "string_view.h"
#include "types.h"

//! The engine's hash functions: `Hash<K>`, the function object [[HashMap]] and [[HashSet]] use for their keys, and
//! `Fnv1a64`, the one byte hash (container audit series, Day 2). Every module that hashes bytes calls `Fnv1a64` instead
//! of writing FNV-1a out again; its values are the published FNV-1a 64-bit values, so a hash stored in a minted file or
//! compared across machines does not change.
namespace engine_placeholder
{

namespace detail
{

// 64-bit finalizer mix (Murmur3-style fmix64) — decent avalanche for
// small integer/pointer keys without needing a full hash algorithm.
inline usize MixHash64(u64 x)
{
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return static_cast<usize>(x);
}

}  // namespace detail

/// The FNV-1a 64-bit offset basis: the value of `Fnv1a64` over no bytes.
inline constexpr u64 kFnv1a64Basis = 0xcbf29ce484222325ULL;
/// The FNV-1a 64-bit prime.
inline constexpr u64 kFnv1a64Prime = 0x100000001b3ULL;
/// The starting value many modules wrote for FNV-1a before `Fnv1a64` existed: the offset basis with its last digit
/// missing (`1469598103934665603`). It is kept as a seed (`Fnv1a64Builder(kFnv1a64LegacyBasis)`) where a hash made
/// from it may be stored or compared (widget ids, settings keys, import names), so that those values do not change;
/// new code starts at `kFnv1a64Basis`.
inline constexpr u64 kFnv1a64LegacyBasis = 1469598103934665603ULL;

/// FNV-1a 64-bit built up a piece at a time: `AddBytes` of a buffer gives the same value as `Fnv1a64` of it, and
/// adding pieces gives the value of their concatenation.
///
/// `AddWord` is not byte-wise: it folds a whole 64-bit word in one step (`h = (h ^ word) * prime`), which is what the
/// engine's state hashes do (`physics::StateHash`, the script runtime's); it is here so they share the constants.
///
/// - Thread-safety: thread-compatible value.
class Fnv1a64Builder
{
public:
    /// Starts at the offset basis, or at `seed` to continue an earlier hash.
    ///
    /// - Param seed: the starting state.
    constexpr explicit Fnv1a64Builder(u64 seed = kFnv1a64Basis) : h_(seed) {}

    /// Adds one byte.
    ///
    /// - Param b: the byte.
    /// - Returns: this builder.
    constexpr Fnv1a64Builder& AddByte(u8 b)
    {
        h_ = (h_ ^ b) * kFnv1a64Prime;
        return *this;
    }

    /// Adds every byte of `bytes` in order.
    ///
    /// - Param bytes: the bytes; read during the call only.
    /// - Returns: this builder.
    Fnv1a64Builder& AddBytes(ByteSpan bytes)
    {
        for (usize i = 0; i < bytes.Size(); ++i)
        {
            h_ = (h_ ^ static_cast<u8>(bytes[i])) * kFnv1a64Prime;
        }
        return *this;
    }

    /// Adds the bytes of `text` in order (no terminator).
    ///
    /// - Param text: the text.
    /// - Returns: this builder.
    Fnv1a64Builder& AddText(StringView text)
    {
        for (usize i = 0; i < text.Length(); ++i)
        {
            h_ = (h_ ^ static_cast<u8>(text[i])) * kFnv1a64Prime;
        }
        return *this;
    }

    /// Adds the bytes of a NUL-terminated string, stopping at the terminator.
    ///
    /// - Param text: the string; null adds nothing.
    /// - Returns: this builder.
    Fnv1a64Builder& AddCString(const char* text)
    {
        for (usize i = 0; text != nullptr && text[i] != '\0'; ++i)
        {
            h_ = (h_ ^ static_cast<u8>(text[i])) * kFnv1a64Prime;
        }
        return *this;
    }

    /// Adds the eight bytes of `value`, least significant first.
    ///
    /// - Param value: the value.
    /// - Returns: this builder.
    constexpr Fnv1a64Builder& AddU64Bytes(u64 value)
    {
        for (u32 i = 0; i < 8; ++i)
        {
            h_ = (h_ ^ ((value >> (8 * i)) & 0xffu)) * kFnv1a64Prime;
        }
        return *this;
    }

    /// Folds a whole 64-bit word in one step: `h = (h ^ word) * prime` (not the word's eight bytes).
    ///
    /// - Param word: the word.
    /// - Returns: this builder.
    constexpr Fnv1a64Builder& AddWord(u64 word)
    {
        h_ = (h_ ^ word) * kFnv1a64Prime;
        return *this;
    }

    /// Returns the hash so far.
    constexpr u64 Value() const { return h_; }

private:
    u64 h_;
};

/// Returns the FNV-1a 64-bit hash of `bytes`.
///
/// - Param bytes: the bytes; read during the call only.
inline u64 Fnv1a64(ByteSpan bytes)
{
    return Fnv1a64Builder().AddBytes(bytes).Value();
}

/// Returns the FNV-1a 64-bit hash of the bytes of `text` (no terminator).
///
/// - Param text: the text.
inline u64 Fnv1a64(StringView text)
{
    return Fnv1a64Builder().AddText(text).Value();
}

/// Mixes the hash `b` into the hash `a`, for a key made of several fields (`HashCombine(Hash<A>{}(x), Hash<B>{}(y))`).
/// The result depends on the order of the arguments.
///
/// - Param a: the hash so far.
/// - Param b: the next field's hash.
inline usize HashCombine(usize a, usize b)
{
    return detail::MixHash64(static_cast<u64>(a) ^ (static_cast<u64>(b) + 0x9e3779b97f4a7c15ULL +
                                                    (static_cast<u64>(a) << 6) + (static_cast<u64>(a) >> 2)));
}

/// Hash function object used by [[HashMap]] and [[HashSet]]: `Hash<K>{}(key)` returns a `usize` hash.
///
/// The primary template is intentionally undefined; specialize it for each key type used. Specializations exist for
/// the fixed-width integer types, for pointers, for [[StringView]] and (in `string/string.h`) for [[String]].
template <typename K> struct Hash;

/// Hash function for `i8` keys.
template <> struct Hash<i8>
{
    /// Returns a well-mixed hash of the integer value.
    usize operator()(i8 k) const { return detail::MixHash64(static_cast<u64>(static_cast<u8>(k))); }
};
/// Hash function for `i16` keys.
template <> struct Hash<i16>
{
    /// Returns a well-mixed hash of the integer value.
    usize operator()(i16 k) const { return detail::MixHash64(static_cast<u64>(static_cast<u16>(k))); }
};
/// Hash function for `i32` keys.
template <> struct Hash<i32>
{
    /// Returns a well-mixed hash of the integer value.
    usize operator()(i32 k) const { return detail::MixHash64(static_cast<u64>(static_cast<u32>(k))); }
};
/// Hash function for `i64` keys.
template <> struct Hash<i64>
{
    /// Returns a well-mixed hash of the integer value.
    usize operator()(i64 k) const { return detail::MixHash64(static_cast<u64>(k)); }
};
/// Hash function for `u8` keys.
template <> struct Hash<u8>
{
    /// Returns a well-mixed hash of the integer value.
    usize operator()(u8 k) const { return detail::MixHash64(static_cast<u64>(k)); }
};
/// Hash function for `u16` keys.
template <> struct Hash<u16>
{
    /// Returns a well-mixed hash of the integer value.
    usize operator()(u16 k) const { return detail::MixHash64(static_cast<u64>(k)); }
};
/// Hash function for `u32` keys.
template <> struct Hash<u32>
{
    /// Returns a well-mixed hash of the integer value.
    usize operator()(u32 k) const { return detail::MixHash64(static_cast<u64>(k)); }
};
/// Hash function for `u64` keys.
template <> struct Hash<u64>
{
    /// Returns a well-mixed hash of the integer value.
    usize operator()(u64 k) const { return detail::MixHash64(k); }
};

/// Hash function for `usize` and `isize` keys where they are types of their own: on macOS `usize` is `unsigned long`
/// and `u64` is `unsigned long long` (Linux and Windows make them the same type, so there the `u64` and `i64` hashes
/// serve). Phase 21 Day 285.
template <typename K>
    requires((IsSame<K, usize> && !IsSame<usize, u64>) || (IsSame<K, isize> && !IsSame<isize, i64>))
struct Hash<K>
{
    // Returns a well-mixed hash of the integer value (a plain comment: docgen does not attach a doc comment inside a
    // constrained specialization).
    usize operator()(K k) const { return detail::MixHash64(static_cast<u64>(k)); }
};

/// Hash function for pointer keys; hashes the address, not the pointee.
template <typename T> struct Hash<T*>
{
    /// Returns a well-mixed hash of the pointer's address.
    usize operator()(T* k) const { return detail::MixHash64(reinterpret_cast<u64>(k)); }
};

/// Hash function for [[StringView]] keys (Phase 27 Day 474): `Fnv1a64` over the bytes, then mixed. A string type that
/// hashes as a key gives the same value as the view of its contents, so a map keyed by strings can be searched with a
/// view (`String<A>`'s hash is in `string/string.h`).
template <> struct Hash<StringView>
{
    /// Returns a well-mixed hash of the bytes.
    usize operator()(StringView k) const { return detail::MixHash64(Fnv1a64(k)); }
};

}  // namespace engine_placeholder
