#pragma once

#include "sync/atomic.h"
#include "sync/mutex.h"

#include "types.h"

//! The allocator behind coroutine frames.
//!
//! Foundation's `PoolAllocator` is single-threaded, has one fixed slot size
//! and returns null when full; job frames are allocated on one thread and
//! freed on another, vary in size, and `operator new` must not return null.
//! This pool therefore has:
//! - a few size classes, each a slab reserved up front (address space only;
//!   pages are committed as they are first touched), handed out by a bump
//!   pointer plus an intrusive free list of returned blocks, behind one
//!   central mutex;
//! - an optional per-thread cache (an intrusive free list per class, no
//!   synchronization) that refills from and spills to the central pool in
//!   batches. A block freed on a different thread than it was allocated on
//!   simply joins the freeing thread's cache, since blocks of one class are
//!   interchangeable;
//! - a counted libc `malloc` fallback for oversize requests or an exhausted
//!   class, so [[jobs::FramePool::Allocate]] never returns null.
//!
//! Every block carries a 16-byte header (class index and magic) so
//! deallocation routes pooled and fallback blocks without address-range
//! checks and the returned pointer stays 16-byte aligned.
namespace vse::jobs
{

/// Thread-safe, size-classed allocator for coroutine frames, with an optional per-thread cache.
///
/// Requests up to [[jobs::FramePool::kMaxPooledRequest]] bytes come from a
/// size class; larger ones, or ones that find their class exhausted, fall
/// back to `malloc` and are counted separately. Blocks may be freed on any
/// thread. A pool cannot be copied. Destroying a pool releases its slabs, so
/// every pooled block must be freed before then; the global pool is never
/// destroyed.
///
/// - Thread-safety: thread-safe. Allocation and deallocation may run on any
///   thread concurrently; the central pool is guarded by one mutex.
/// - Ownership: owns the reserved slabs of every size class and releases them
///   in the destructor. Blocks returned by allocation belong to the caller
///   until passed back to [[jobs::FramePool::Deallocate]].
/// - See: [[jobs::Job]]
class FramePool
{
public:
    /// Number of size classes.
    static constexpr usize kClassCount = 5;
    /// Whole-block size of each class in bytes, header included.
    static constexpr usize kBlockSizes[kClassCount] = {128, 256, 512, 1024, 2048};
    /// Bytes of per-block header; also the alignment guarantee of returned pointers.
    static constexpr usize kHeaderBytes = 16;
    /// Largest request in bytes that a size class can serve; bigger ones use the fallback.
    static constexpr usize kMaxPooledRequest = kBlockSizes[kClassCount - 1] - kHeaderBytes;

    /// Pool construction parameters.
    ///
    /// - Thread-safety: not-thread-safe. A plain value type.
    struct Config
    {
        /// Number of blocks reserved for each size class.
        usize slots_per_class[kClassCount] = {16384, 8192, 4096, 2048, 1024};
        /// Enables per-thread caches.
        ///
        /// Valid for exactly one pool per process (the global one); local
        /// pools such as those in tests leave it off. Constructing a second
        /// pool with caches aborts.
        bool thread_caches = false;
    };

    /// Snapshot of the allocation counters.
    ///
    /// Pooled blocks came from a size class; fallback blocks came from libc `malloc`.
    ///
    /// - Thread-safety: not-thread-safe. A plain value copy; each counter was read atomically but the set is not a
    /// consistent cut.
    struct Stats
    {
        /// Pooled allocations per size class.
        u64 pooled_allocs[kClassCount] = {};
        /// Pooled deallocations per size class.
        u64 pooled_frees[kClassCount] = {};
        /// Allocations served by `malloc`.
        u64 fallback_allocs = 0;
        /// Deallocations of `malloc` blocks.
        u64 fallback_frees = 0;

        /// Sums pooled allocations over all classes.
        ///
        /// - Returns: the total pooled allocation count.
        u64 TotalPooledAllocs() const;

        /// Sums pooled deallocations over all classes.
        ///
        /// - Returns: the total pooled deallocation count.
        u64 TotalPooledFrees() const;

        /// Counts blocks allocated and not yet freed, pooled and fallback together.
        ///
        /// - Returns: allocations minus deallocations.
        u64 Live() const;
    };

    /// Per-thread free-list heads and counts.
    ///
    /// - Thread-safety: owner-thread-only. Each instance belongs to one thread.
    /// - Internal: public only so the implementation's `thread_local` can name the type.
    struct ThreadCache
    {
        FramePool* owner = nullptr;
        void* head[kClassCount] = {};
        u32 count[kClassCount] = {};
        // Trivially destructible on purpose (Phase 23 Day 345): a `thread_local` with a destructor registers it on the
        // host's thread, and then the shared engine library cannot be unloaded while that thread lives. The cache is
        // handed back by `ReleaseThreadCache`, which every engine thread calls before it ends.
    };

    /// Creates a pool and reserves one slab of address space per size class.
    ///
    /// A class whose reservation fails gets zero slots, and its requests use the fallback.
    ///
    /// - Param config: slots per class and the thread-cache switch.
    /// - Aborts: `config.thread_caches` is set while another pool already uses thread caches.
    explicit FramePool(const Config& config);

    /// Releases every slab, after flushing the calling thread's cache.
    ///
    /// - Ownership: frees the reserved slabs; pooled blocks still outstanding become dangling.
    ~FramePool();

    FramePool(const FramePool&) = delete;
    FramePool& operator=(const FramePool&) = delete;

    /// Allocates a block for a coroutine frame.
    ///
    /// - Param size: requested size in bytes.
    /// - Returns: a 16-byte-aligned block of at least `size` bytes; never null.
    /// - Aborts: even the `malloc` fallback fails.
    /// - Ownership: the caller owns the block and must give it back with [[jobs::FramePool::Deallocate]] on this pool.
    /// - Thread-safety: thread-safe.
    void* Allocate(usize size);

    /// Returns a block to the pool.
    ///
    /// The block may be freed on a different thread than the one that
    /// allocated it.
    ///
    /// - Param ptr: a pointer obtained from [[jobs::FramePool::Allocate]] on this pool and not freed since.
    /// - Aborts: `ptr` is null, or it does not carry a valid block header (not from a pool, or a double free).
    /// - Ownership: takes back ownership of the block; it must not be used afterwards.
    /// - Thread-safety: thread-safe.
    void Deallocate(void* ptr);

    /// Reads the allocation counters.
    ///
    /// - Returns: a snapshot of the counters, exact once the pool is quiescent.
    /// - Thread-safety: thread-safe.
    Stats GetStats() const;

    /// Counts the blocks the central pool could still hand out for a class.
    ///
    /// That is returned blocks plus never-used slab slots. Testing aid: after
    /// all frames are freed and all caches flushed this equals the class's
    /// slot count, so a shortfall is a leaked block.
    ///
    /// - Param class_index: size class in `[0, kClassCount)`.
    /// - Returns: the number of available blocks in the central pool.
    /// - Thread-safety: thread-safe.
    usize CentralAvailableForTesting(usize class_index);

    /// Gives the process-wide pool used by job frames.
    ///
    /// It uses the default [[jobs::FramePool::Config]] with thread caches on.
    /// Created on first use and intentionally never destroyed, so thread-exit
    /// cache flushes and late frees always find it alive.
    ///
    /// - Returns: a reference valid for the rest of the process, or until [[jobs::FramePool::DestroyGlobalAtUnload]].
    /// - Thread-safety: thread-safe.
    static FramePool& Global();

    /// Releases the memory of the process-wide pool when the shared engine library is unloaded (Phase 23 Day 348): the
    /// pool is otherwise immortal, and its slabs would outlive the library's own image. Does nothing if the pool was
    /// never used.
    ///
    /// - Thread-safety: not-thread-safe. Call only when no thread can use the pool any more: every engine thread has
    /// ended or called `NotifyThreadEnd`, and nothing calls [[jobs::FramePool::Global]] afterwards.
    /// - Aborts: never.
    static void DestroyGlobalAtUnload();

    /// Hands the calling thread's cached blocks back to the pool that owns them. [[NotifyThreadEnd]] calls it (the
    /// first use of a cache registers it), so a thread that calls `NotifyThreadEnd` before it ends needs nothing more;
    /// the job system's workers, the I/O thread and a scheduler's creating thread do. A thread that ends without either
    /// strands at most 64 blocks per size class, which the pool never sees again.
    ///
    /// - Thread-safety: thread-safe; acts on the calling thread's own cache.
    static void ReleaseThreadCache();

private:
    static constexpr u32 kFallbackClass = 0xFFFFFFFFu;
    static constexpr u32 kHeaderMagic = 0xF4A3E901u;
    static constexpr u32 kRefillBatch = 16;
    static constexpr u32 kSpillThreshold = 64;  // spill half when exceeded

    struct Header
    {
        u32 class_index;
        u32 magic;
        u64 reserved;
    };
    static_assert(sizeof(Header) == kHeaderBytes);

    static int ClassForRequest(usize size);

    // Central pool operations; the first two require mutex_ held.
    void* CentralPopLocked(usize class_index);
    void CentralPushLocked(usize class_index, void* block);
    void* AllocateFallback(usize size);
    void FlushCache(ThreadCache& cache);
    // Makes this pool the owner of the calling thread's cache and has NotifyThreadEnd hand the cache back when the
    // thread ends.
    void NoteCacheUse(ThreadCache& cache);

    Config config_;
    Mutex mutex_;

    // Per-class central state, guarded by mutex_.
    struct ClassState
    {
        byte* base = nullptr;  // slab start (nullptr = reservation failed)
        usize slots = 0;
        usize next_unused = 0;      // bump index into the slab
        void* free_list = nullptr;  // returned blocks
    };
    ClassState classes_[kClassCount];

    Atomic<u64> pooled_allocs_[kClassCount] = {};
    Atomic<u64> pooled_frees_[kClassCount] = {};
    Atomic<u64> fallback_allocs_{0};
    Atomic<u64> fallback_frees_{0};
};

}  // namespace vse::jobs
