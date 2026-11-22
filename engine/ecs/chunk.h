#pragma once

//! The 32 KiB blocks that hold the rows of one archetype, and the pool they come from.

#include "containers/array.h"
#include "containers/vector.h"
#include "ecs/component.h"
#include "ecs/entity.h"
#include "mem/heap_allocator.h"
#include "types.h"

namespace vse::ecs
{

/// The size of a chunk in bytes (header and columns). Chosen by experiment; a compile-time constant.
inline constexpr usize kChunkBytes = 32 * 1024;

/// The most components (tags included) an archetype holds.
inline constexpr u32 kMaxArchetypeComponents = 32;

/// The header at the start of every chunk; the columns follow it.
///
/// `written` and `added` hold the world tick of the last write and of the last addition for each component column
/// (indexed by column number minus one), the change-detection data (see "Change detection" in the ECS guide). A chunk
/// is `kChunkBytes` long and 64-byte aligned.
///
/// - Thread-safety: thread-compatible; a chunk is touched by one job at a time.
/// - Ownership: the chunk is owned by an archetype and drawn from a [[ecs::ChunkPool]].
struct alignas(64) Chunk
{
    /// The index of the archetype in its world.
    u32 archetype = 0;
    /// The rows in use.
    u32 count = 0;
    /// The most rows the chunk holds.
    u32 capacity = 0;
    /// Unused.
    u32 reserved = 0;
    /// The tick of the last write, per component column.
    Array<u64, kMaxArchetypeComponents> written;
    /// The tick of the last addition, per component column.
    Array<u64, kMaxArchetypeComponents> added;

    /// The first byte after the header.
    /// - Fails: never.
    /// - Ownership: views the chunk's own memory.
    byte* Data() { return reinterpret_cast<byte*>(this) + sizeof(Chunk); }
    /// The first byte after the header.
    /// - Fails: never.
    /// - Ownership: views the chunk's own memory.
    const byte* Data() const { return reinterpret_cast<const byte*>(this) + sizeof(Chunk); }
};
static_assert(sizeof(Chunk) % 64 == 0, "the columns after the header start 64-byte aligned");

/// Counters of a [[ecs::ChunkPool]].
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct ChunkPoolStats
{
    /// Chunks handed out and not yet released.
    u32 chunks = 0;
    /// Bytes of those chunks.
    usize bytes = 0;
    /// Released chunks kept for reuse.
    u32 pooled = 0;
    /// Times a chunk was handed out from the kept ones instead of the heap.
    u64 reused = 0;
    /// Archetypes refused because one row does not fit in a chunk.
    u64 refused_layouts = 0;
    /// Requests the heap could not satisfy.
    u64 refused_allocations = 0;
};

/// The supplier of chunks: 64-byte aligned `kChunkBytes` blocks from the heap, with released chunks kept for reuse up
/// to a budget.
///
/// No chunk is ever allocated per entity: only when an archetype's last chunk is full.
///
/// - Thread-safety: thread-compatible; the world owns one and uses it at sync points.
/// - Ownership: owns the kept chunks; a chunk handed out belongs to the caller until `Release`. Every chunk must be
/// released before the pool is destroyed.
class ChunkPool
{
public:
    /// Creates a pool that keeps at most `budget` released chunks (the rest go back to the heap).
    explicit ChunkPool(u32 budget = 64) : budget_(budget) {}
    ChunkPool(const ChunkPool&) = delete;
    ChunkPool& operator=(const ChunkPool&) = delete;
    /// Frees the kept chunks.
    ///
    /// - Aborts: a chunk handed out was not released.
    ~ChunkPool();

    /// Hands out a chunk with a zeroed header (`archetype` set, `count` 0, `capacity` as given) and unspecified column
    /// bytes.
    ///
    /// - Returns: the chunk, or null when the heap is exhausted (counted in `refused_allocations`).
    /// - Fails: out of memory.
    /// - Ownership: the caller owns the chunk until it passes it to `Release`.
    Chunk* Acquire(u32 archetype, u32 capacity);

    /// Takes a chunk back: kept when fewer than the budget are kept, else freed.
    /// - Aborts: `chunk` was not handed out by this pool or was already released (detected only by the live count).
    /// - Ownership: the pool owns the chunk again; the caller must not use it.
    void Release(Chunk* chunk);

    /// Frees every kept chunk.
    void Trim();

    /// Records that an archetype layout was refused.
    void CountRefusedLayout() { ++stats_.refused_layouts; }

    /// The counters.
    ChunkPoolStats Stats() const { return stats_; }

    /// The most released chunks kept.
    u32 Budget() const { return budget_; }

private:
    Vector<Chunk*> kept_;
    u32 budget_;
    ChunkPoolStats stats_;
};

}  // namespace vse::ecs
