// The state hash of the physics step (Phase 25 Day 386): a 64-bit FNV-1a hash of an ordered sequence of words, the
// value the reference scenes compare across Linux, Windows and macOS
// (docs/superpowers/specs/2026-11-16-physics-design.md, section 2, "State hash").
#pragma once

#include "cstr/mem_ops.h"
#include "containers/hash.h"
#include "lang/scalar.h"
#include "types.h"

namespace vse::physics
{

/// A running FNV-1a 64-bit hash of what is fed to it, byte by byte.
///
/// Every word is fed in **little-endian byte order** whatever the machine's own order, so equal states give equal
/// hashes everywhere. A float is hashed by its bit pattern with **-0.0 written as +0.0** (a sign of zero that depends
/// on the order of independent operations is not a state difference); a NaN or an infinity is hashed as it is and makes
/// `Valid()` false, because a simulation that produced one has failed. Nothing but the words fed is hashed: no padding,
/// no pointers.
///
/// - Thread-safety: thread-compatible; one hasher belongs to one thread.
/// - Ownership: plain value; owns nothing.
class StateHasher
{
public:
    /// Makes a hasher at the FNV-1a offset basis; `Value()` of a fresh hasher is `kOffsetBasis`.
    StateHasher() : hash_(kOffsetBasis), valid_(true) {}

    /// The FNV-1a 64-bit offset basis.
    static constexpr u64 kOffsetBasis = kFnv1a64Basis;
    /// The FNV-1a 64-bit prime.
    static constexpr u64 kPrime = kFnv1a64Prime;

    /// Feeds one byte.
    ///
    /// - Param value: the byte.
    void AddU8(u8 value) { hash_ = (hash_ ^ static_cast<u64>(value)) * kPrime; }

    /// Feeds four bytes, least significant first.
    ///
    /// - Param value: the word.
    void AddU32(u32 value)
    {
        AddU8(static_cast<u8>(value));
        AddU8(static_cast<u8>(value >> 8));
        AddU8(static_cast<u8>(value >> 16));
        AddU8(static_cast<u8>(value >> 24));
    }

    /// Feeds eight bytes, least significant first.
    ///
    /// - Param value: the word.
    void AddU64(u64 value)
    {
        AddU32(static_cast<u32>(value));
        AddU32(static_cast<u32>(value >> 32));
    }

    /// Feeds a float by its bit pattern, with -0.0 as +0.0.
    ///
    /// - Param value: the float; a NaN or an infinity clears `Valid()`.
    void AddF32(f32 value)
    {
        if (!IsFinite(value))
        {
            valid_ = false;
        }
        if (value == 0.0f)
        {
            value = 0.0f;  // -0.0 compares equal to 0.0 and is replaced by the positive zero
        }
        AddU32(BitsOf(value));
    }

    /// Feeds a double by its bit pattern, with -0.0 as +0.0.
    ///
    /// - Param value: the double; a NaN or an infinity clears `Valid()`.
    void AddF64(f64 value)
    {
        if (!IsFinite(value))
        {
            valid_ = false;
        }
        if (value == 0.0)
        {
            value = 0.0;  // -0.0 compares equal to 0.0 and is replaced by the positive zero
        }
        u64 bits = 0;
        MemCopy(&bits, &value, sizeof(bits));
        AddU64(bits);
    }

    /// Returns the hash of everything fed so far.
    u64 Value() const { return hash_; }

    /// Returns whether every float fed was finite.
    ///
    /// - Fails: never.
    bool Valid() const { return valid_; }

    /// Returns the bit pattern of a float (the bytes of the value, no conversion).
    ///
    /// - Param value: the float.
    static u32 BitsOf(f32 value)
    {
        u32 bits = 0;
        MemCopy(&bits, &value, sizeof(bits));
        return bits;
    }

private:
    u64 hash_;
    bool valid_;
};

}  // namespace vse::physics
