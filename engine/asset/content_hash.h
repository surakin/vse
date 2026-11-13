#pragma once

//! [[asset::ContentHash]]: the 128-bit content hash of a blob (SHA-256 truncated to its first 16 bytes, the choice of
//! Phase 22 Day 313, Spike 2). It names blobs in the network cache, lets `verify` and the packer detect corruption and
//! identifies a base pak. It is not on the load path of a local file, and it is not a security feature (the engine's
//! networking is for trusted networks).

#include "containers/span.h"
#include "types.h"

namespace engine_placeholder::asset
{

/// A 128-bit content hash.
///
/// - Thread-safety: immutable value.
struct ContentHash
{
    u8 bytes[16] = {};  ///< The first 16 bytes of the SHA-256 digest.

    /// Returns true when every byte is zero (no hash recorded).
    /// - Fails: never.
    bool IsZero() const;
    /// Compares all 16 bytes.
    /// - Fails: never.
    friend bool operator==(const ContentHash& a, const ContentHash& b);
    /// Compares all 16 bytes.
    /// - Fails: never.
    friend bool operator!=(const ContentHash& a, const ContentHash& b) { return !(a == b); }
};

/// Writes `hash` as 32 lower-case hex digits and a terminating zero.
///
/// - Fails: never.
void FormatContentHash(const ContentHash& hash, char (&out)[33]);

/// Hashes a stream of chunks; the result equals hashing the concatenation in one call.
///
/// - Thread-safety: not-thread-safe; one hasher per stream.
/// - Ownership: holds its own state; nothing is borrowed.
class ContentHasher
{
public:
    /// Starts a new hash.
    ContentHasher();

    /// Adds `bytes` to the hash.
    ///
    /// - Aborts: `Update` is called after `Finish`.
    void Update(ByteSpan bytes);

    /// Finishes the hash; the hasher is spent afterwards (`Reset` starts again).
    ///
    /// - Aborts: `Finish` is called twice.
    ContentHash Finish();

    /// Starts a new hash, discarding the state.
    /// - Fails: never.
    void Reset();

private:
    void Block(const u8* block);

    u32 state_[8];
    u8 buffer_[64];
    u64 total_ = 0;
    u32 buffered_ = 0;
    bool finished_ = false;
};

/// Hashes `bytes` in one call.
///
/// - Fails: never.
ContentHash HashBytes(ByteSpan bytes);

}  // namespace engine_placeholder::asset
