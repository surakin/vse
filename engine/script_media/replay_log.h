// The replay log of a scripted session (Phase 27 Day 469): the input snapshot of every tick and the state hash after
// it. A session recorded at any thread count replays at any other to the same hashes, because the scripts read only the
// snapshot, the world and their seeded random streams (docs/guide/scripting.md, "Saving scripted state").
#pragma once

#include "bytes/byte_reader.h"
#include "bytes/byte_writer.h"
#include "containers/vector.h"
#include "script_media/input_snapshot.h"
#include "types.h"

namespace engine_placeholder::script_media
{

/// The version of the record [[script_media::ReplayLog::Serialize]] writes.
inline constexpr u32 kReplayLogVersion = 1;
/// The most ticks a log read from bytes may hold.
inline constexpr u64 kMaxReplayTicks = u64{1} << 24;

/// One tick of a recorded session.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns its bytes.
struct ReplayEntry
{
    /// The tick the snapshot was taken for (the runtime's tick count before the tick ran).
    u64 tick = 0;
    /// The snapshot, as [[script_media::InputSnapshot::Serialize]] wrote it.
    Vector<u8> input;
    /// The hash of the world after the tick (what the host computed; the log only keeps it).
    u64 hash = 0;
};

/// A recorded session: what it started from and one entry per tick.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns its entries.
class ReplayLog
{
public:
    /// Makes an empty log.
    ReplayLog() = default;

    /// Starts a log: forgets the entries and keeps what the session started from.
    ///
    /// - Param random_seed: the runtime's `random_seed`.
    /// - Param start_tick: the tick the first entry is for.
    /// - Param start_hash: the hash of the world the session started from (a snapshot's hash, or 0 for a new world).
    /// - Fails: never.
    void Begin(u32 random_seed, u64 start_tick, u64 start_hash);

    /// Adds a tick.
    ///
    /// - Param tick: the tick number.
    /// - Param snapshot: the input the scripts read this tick.
    /// - Param hash: the world's hash after the tick.
    /// - Fails: never.
    void Record(u64 tick, const InputSnapshot& snapshot, u64 hash);

    /// The runtime seed the session used.
    ///
    /// - Fails: never.
    u32 RandomSeed() const { return random_seed_; }
    /// The first tick.
    ///
    /// - Fails: never.
    u64 StartTick() const { return start_tick_; }
    /// The hash of the world the session started from.
    ///
    /// - Fails: never.
    u64 StartHash() const { return start_hash_; }
    /// The number of ticks recorded.
    ///
    /// - Fails: never.
    usize Size() const { return entries_.Size(); }
    /// One entry.
    ///
    /// - Param index: below `Size()`.
    /// - Aborts: `index` is out of range.
    const ReplayEntry& At(usize index) const;

    /// Reads an entry's snapshot back.
    ///
    /// - Param index: below `Size()`.
    /// - Param out: receives the snapshot.
    /// - Returns: false when the stored bytes do not decode (a log read from bytes is checked, so this is a log built
    /// by hand).
    /// - Aborts: `index` is out of range.
    /// - Ownership: `out` is borrowed for the call.
    /// - Fails: returns false, as described.
    bool Snapshot(usize index, InputSnapshot* out) const;

    /// Writes the log: version, seed, start, then each entry.
    ///
    /// - Param out: receives the bytes (appended).
    /// - Ownership: `out` is borrowed for the call.
    /// - Fails: never.
    void Serialize(bytes::ByteWriter* out) const;

    /// Reads a log written by `Serialize`, replacing this one; every snapshot is decoded once to check it, ticks must
    /// rise by one, and counts and lengths are held to limits.
    ///
    /// - Param in: the bytes; the reader moves past the log on success.
    /// - Returns: false (and this log is unchanged) for a version this build does not know, a truncated or oversized
    /// record, a snapshot that does not decode or ticks that do not follow each other.
    /// - Ownership: `in` is borrowed for the call.
    /// - Fails: returns false, as described.
    bool Deserialize(bytes::ByteReader* in);

    /// Whether `other` holds the same seed, start and entries.
    ///
    /// - Fails: never.
    bool Equals(const ReplayLog& other) const;

private:
    u32 random_seed_ = 0;
    u64 start_tick_ = 0;
    u64 start_hash_ = 0;
    Vector<ReplayEntry> entries_;
};

}  // namespace engine_placeholder::script_media
