// The hash of rendered audio (Phase 26 Day 415): a 64-bit FNV-1a hash of blocks of f32 samples, the value the
// same-build regression checks compare (docs/superpowers/specs/2026-11-17-audio-design.md, section 4).
#pragma once

#include "containers/span.h"
#include "containers/hash.h"
#include "types.h"

namespace engine_placeholder::audio
{

/// A running FNV-1a 64-bit hash of f32 samples.
///
/// A sample is hashed by its bit pattern in **little-endian byte order** whatever the machine's own order, with **-0.0
/// hashed as +0.0** (the sign of a zero that depends on the order of independent operations is not a difference a
/// listener can hear). A NaN or an infinity is hashed as it is and makes `Valid()` false, because a mix that produced
/// one has failed. Nothing but the samples is hashed: no block lengths, no padding. The hash equals the physics
/// `StateHasher`'s over the same words (the same FNV-1a constants), but the audio module does not depend on physics, so
/// it has its own.
///
/// - Thread-safety: thread-compatible; one hasher belongs to one thread.
/// - Ownership: plain value; owns nothing.
class BlockHasher
{
public:
    /// Makes a hasher at the FNV-1a offset basis; `Value()` of a fresh hasher is `kOffsetBasis`.
    BlockHasher() : hash_(kOffsetBasis), valid_(true) {}

    /// The FNV-1a 64-bit offset basis.
    static constexpr u64 kOffsetBasis = kFnv1a64Basis;
    /// The FNV-1a 64-bit prime.
    static constexpr u64 kPrime = kFnv1a64Prime;

    /// Feeds one sample.
    ///
    /// - Param value: the sample; a NaN or an infinity clears `Valid()`.
    void AddSample(f32 value);

    /// Feeds a block of samples in order.
    ///
    /// - Param samples: the samples (interleaved frames are fed as they lie).
    void AddBlock(Span<const f32> samples);

    /// Returns the hash of everything fed so far.
    u64 Value() const { return hash_; }

    /// Returns whether every sample fed was finite.
    ///
    /// - Fails: never.
    bool Valid() const { return valid_; }

private:
    u64 hash_;
    bool valid_;
};

/// The hash of a whole render: the blocks folded in order, plus how many blocks and samples went in.
///
/// Two renders have equal hashes only if they have the same samples in the same order; the block boundaries do not
/// matter (a render pulled in callbacks of 64 or 1,024 frames hashes like one pulled in 256).
///
/// - Thread-safety: thread-compatible.
/// - Ownership: plain value; owns nothing.
class RenderHash
{
public:
    /// Folds in the next block of the render.
    ///
    /// - Param samples: the block's interleaved samples.
    void AddBlock(Span<const f32> samples)
    {
        hasher_.AddBlock(samples);
        blocks_ += 1;
        samples_ += samples.Size();
    }

    /// The hash of the samples so far.
    u64 Value() const { return hasher_.Value(); }

    /// Returns whether every sample so far was finite.
    ///
    /// - Fails: never.
    bool Valid() const { return hasher_.Valid(); }

    /// The number of blocks folded in.
    u64 Blocks() const { return blocks_; }

    /// The number of samples folded in.
    u64 Samples() const { return samples_; }

private:
    BlockHasher hasher_;
    u64 blocks_ = 0;
    u64 samples_ = 0;
};

/// Hashes one block of samples from the start.
///
/// - Param samples: the samples.
/// - Param valid: set to whether every sample was finite; may be null.
/// - Returns: the hash.
/// - Thread-safety: thread-safe; pure.
/// - Ownership: borrows `samples` and `valid` for the call only.
u64 HashBlock(Span<const f32> samples, bool* valid);

}  // namespace engine_placeholder::audio
