#pragma once

#include <atomic>
#include <mutex>

#include "types.h"

// Phase 2 Day 27 (design decision D3): the allocator behind coroutine
// frames. Foundation's PoolAllocator is single-threaded, has one fixed
// slot size and returns nullptr when full; job frames are allocated on
// one thread and freed on another, vary in size, and `operator new` must
// not return null. So:
//
//   * a few size classes, each a slab reserved up front (address space
//     only — pages are committed as they are first touched) and handed
//     out by a bump pointer plus an intrusive free list of returned
//     blocks, behind one central mutex. (Foundation's PoolAllocator is
//     not used: its constructor writes a free-list link into every slot,
//     committing the whole slab at first use, on the hot path, while
//     holding the central lock.)
//   * an optional per-thread cache (intrusive free list per class, no
//     synchronization) that refills from / spills to the central pool in
//     batches — a block freed on a different thread than it was
//     allocated on just joins the freeing thread's cache (blocks of one
//     class are interchangeable);
//   * a counted libc malloc fallback for oversize requests or an
//     exhausted pool, so Allocate() never returns nullptr.
//
// Every block carries a 16-byte header (class index + magic) so
// Deallocate() routes pooled vs. fallback blocks without address-range
// checks and keeps the returned pointer 16-byte aligned.
namespace engine_placeholder::jobs {

class FramePool {
public:
    static constexpr usize kClassCount = 5;
    // Whole-block sizes, header included. Starting guess — Day 35 logs
    // the real frame-size histogram and retunes.
    static constexpr usize kBlockSizes[kClassCount] = {128, 256, 512, 1024, 2048};
    static constexpr usize kHeaderBytes = 16;
    static constexpr usize kMaxPooledRequest = kBlockSizes[kClassCount - 1] - kHeaderBytes;

    struct Config {
        usize slots_per_class[kClassCount] = {16384, 8192, 4096, 2048, 1024};
        // Per-thread caches are valid for exactly one pool per process
        // (the Global() one); local pools used by tests leave this off.
        bool thread_caches = false;
    };

    // Snapshot of the counters. "pooled" blocks came from a size class;
    // "fallback" blocks came from libc malloc.
    struct Stats {
        u64 pooled_allocs[kClassCount] = {};
        u64 pooled_frees[kClassCount] = {};
        u64 fallback_allocs = 0;
        u64 fallback_frees = 0;

        u64 TotalPooledAllocs() const;
        u64 TotalPooledFrees() const;
        // Live = allocated and not yet freed, pooled + fallback.
        u64 Live() const;
    };

    // Per-thread free-list heads/counts. Public only so the .cpp's
    // thread_local can name the type; not part of the API.
    struct ThreadCache {
        FramePool* owner = nullptr;
        void* head[kClassCount] = {};
        u32 count[kClassCount] = {};
        // Set by the destructor at thread exit. Thread-local destructors
        // run in unspecified order, so another one (or a static
        // destructor on the main thread) may still free a frame after
        // this cache is gone; such late allocations/frees must bypass
        // the cache and go straight to the central pool, or the block
        // would sit in a dead cache forever.
        bool destroyed = false;
        ~ThreadCache();
    };

    explicit FramePool(const Config& config);
    ~FramePool();

    FramePool(const FramePool&) = delete;
    FramePool& operator=(const FramePool&) = delete;

    // Never returns nullptr (asserts if even malloc fails). The result is
    // 16-byte aligned.
    void* Allocate(usize size);

    // `ptr` must come from Allocate() on this pool. Safe from any thread.
    void Deallocate(void* ptr);

    Stats GetStats() const;

    // Blocks the central pool could still hand out for `class_index`
    // (returned blocks plus never-used slab slots). Testing aid: after all
    // frames are freed and all caches flushed this equals the class's slot
    // count, so a shortfall is a leaked block.
    usize CentralAvailableForTesting(usize class_index);

    // The process-wide pool used by Job frames: default Config with
    // thread caches on. Created on first use and intentionally never
    // destroyed (thread-exit cache flushes and late frees must always
    // find it alive).
    static FramePool& Global();

private:
    friend struct ThreadCache;

    static constexpr u32 kFallbackClass = 0xFFFFFFFFu;
    static constexpr u32 kHeaderMagic = 0xF4A3E901u;
    static constexpr u32 kRefillBatch = 16;
    static constexpr u32 kSpillThreshold = 64; // spill half when exceeded

    struct Header {
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

    Config config_;
    std::mutex mutex_;

    // Per-class central state, guarded by mutex_.
    struct ClassState {
        unsigned char* base = nullptr; // slab start (nullptr = reservation failed)
        usize slots = 0;
        usize next_unused = 0; // bump index into the slab
        void* free_list = nullptr; // returned blocks
    };
    ClassState classes_[kClassCount];

    std::atomic<u64> pooled_allocs_[kClassCount] = {};
    std::atomic<u64> pooled_frees_[kClassCount] = {};
    std::atomic<u64> fallback_allocs_{0};
    std::atomic<u64> fallback_frees_{0};
};

} // namespace engine_placeholder::jobs
