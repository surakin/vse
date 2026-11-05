#include "jobs/frame_pool.h"

#include <new>

#include <cstdlib>

#include "log/assert.h"
#include "os/os.h"

namespace engine_placeholder::jobs {

namespace {

thread_local FramePool::ThreadCache t_cache;

// Guards "at most one pool uses thread caches" (a thread_local cache is
// keyed to a single pool).
std::atomic<bool> g_thread_cached_pool_exists{false};

void* BlockToUser(void* block) {
    return static_cast<unsigned char*>(block) + FramePool::kHeaderBytes;
}

} // namespace

u64 FramePool::Stats::TotalPooledAllocs() const {
    u64 n = 0;
    for (usize i = 0; i < kClassCount; ++i) n += pooled_allocs[i];
    return n;
}

u64 FramePool::Stats::TotalPooledFrees() const {
    u64 n = 0;
    for (usize i = 0; i < kClassCount; ++i) n += pooled_frees[i];
    return n;
}

u64 FramePool::Stats::Live() const {
    return (TotalPooledAllocs() + fallback_allocs) - (TotalPooledFrees() + fallback_frees);
}

FramePool::FramePool(const Config& config) : config_(config) {
    if (config_.thread_caches) {
        bool expected = false;
        ENGINE_ASSERT(g_thread_cached_pool_exists.compare_exchange_strong(expected, true),
                      "FramePool: only one pool per process may use thread caches");
    }
    // Reserve every slab now: it only claims address space, so there is
    // nothing to stall on later. A failed reservation leaves that class
    // with zero slots and every request falls back to malloc.
    for (usize c = 0; c < kClassCount; ++c) {
        usize slots = config_.slots_per_class[c];
        if (slots == 0) continue;
        void* mem = os::ReserveMemory(slots * kBlockSizes[c]);
        if (mem != nullptr) {
            classes_[c].base = static_cast<unsigned char*>(mem);
            classes_[c].slots = slots;
        }
    }
}

FramePool::~FramePool() {
    // Blocks parked in this thread's cache point into the slabs about to
    // be released.
    if (t_cache.owner == this) {
        FlushCache(t_cache);
        t_cache.owner = nullptr;
    }
    for (usize c = 0; c < kClassCount; ++c) {
        if (classes_[c].base != nullptr) {
            os::ReleaseMemory(classes_[c].base, classes_[c].slots * kBlockSizes[c]);
        }
    }
    if (config_.thread_caches) {
        g_thread_cached_pool_exists.store(false);
    }
}

FramePool& FramePool::Global() {
    // Immortal by design: built once in static storage and never destroyed (no heap), so thread-exit cache
    // flushes and late frees always find it alive.
    alignas(FramePool) static unsigned char storage[sizeof(FramePool)];
    static FramePool* instance = [] {
        Config config;
        config.thread_caches = true;
        return new (storage) FramePool(config);
    }();
    return *instance;
}

int FramePool::ClassForRequest(usize size) {
    for (usize c = 0; c < kClassCount; ++c) {
        if (size + kHeaderBytes <= kBlockSizes[c]) {
            return static_cast<int>(c);
        }
    }
    return -1;
}

void* FramePool::CentralPopLocked(usize class_index) {
    ClassState& cs = classes_[class_index];
    if (cs.free_list != nullptr) {
        void* block = cs.free_list;
        cs.free_list = *static_cast<void**>(block);
        return block;
    }
    if (cs.next_unused < cs.slots) {
        return cs.base + (cs.next_unused++) * kBlockSizes[class_index];
    }
    return nullptr;
}

void FramePool::CentralPushLocked(usize class_index, void* block) {
    ClassState& cs = classes_[class_index];
    *static_cast<void**>(block) = cs.free_list;
    cs.free_list = block;
}

usize FramePool::CentralAvailableForTesting(usize class_index) {
    std::lock_guard<std::mutex> lock(mutex_);
    ClassState& cs = classes_[class_index];
    usize n = cs.slots - cs.next_unused;
    for (void* b = cs.free_list; b != nullptr; b = *static_cast<void**>(b)) {
        ++n;
    }
    return n;
}

void* FramePool::AllocateFallback(usize size) {
    void* block = std::malloc(size + kHeaderBytes);
    ENGINE_ASSERT(block != nullptr, "FramePool: libc malloc fallback failed");
    auto* header = static_cast<Header*>(block);
    header->class_index = kFallbackClass;
    header->magic = kHeaderMagic;
    header->reserved = 0;
    fallback_allocs_.fetch_add(1, std::memory_order_relaxed);
    return BlockToUser(block);
}

void* FramePool::Allocate(usize size) {
    int cls = ClassForRequest(size);
    if (cls < 0) {
        return AllocateFallback(size);
    }
    usize c = static_cast<usize>(cls);

    void* block = nullptr;
    ThreadCache& cache = t_cache;
    if (config_.thread_caches && !cache.destroyed) {
        ENGINE_ASSERT(cache.owner == nullptr || cache.owner == this, "FramePool: thread cache belongs to another pool");
        cache.owner = this;
        if (cache.head[c] == nullptr) {
            // Refill a batch from the central pool under one lock.
            std::lock_guard<std::mutex> lock(mutex_);
            for (u32 i = 0; i < kRefillBatch; ++i) {
                void* b = CentralPopLocked(c);
                if (b == nullptr) break;
                *static_cast<void**>(b) = cache.head[c];
                cache.head[c] = b;
                ++cache.count[c];
            }
        }
        if (cache.head[c] != nullptr) {
            block = cache.head[c];
            cache.head[c] = *static_cast<void**>(block);
            --cache.count[c];
        }
    } else {
        // Uncached pool, or a late allocation after this thread's cache
        // was destroyed: straight to the central pool.
        std::lock_guard<std::mutex> lock(mutex_);
        block = CentralPopLocked(c);
    }

    if (block == nullptr) {
        return AllocateFallback(size); // class exhausted
    }
    auto* header = static_cast<Header*>(block);
    header->class_index = static_cast<u32>(c);
    header->magic = kHeaderMagic;
    header->reserved = 0;
    pooled_allocs_[c].fetch_add(1, std::memory_order_relaxed);
    return BlockToUser(block);
}

void FramePool::Deallocate(void* ptr) {
    ENGINE_ASSERT(ptr != nullptr, "FramePool::Deallocate(nullptr)");
    void* block = static_cast<unsigned char*>(ptr) - kHeaderBytes;
    auto* header = static_cast<Header*>(block);
    ENGINE_ASSERT(header->magic == kHeaderMagic, "FramePool::Deallocate: pointer not from a FramePool (or double free)");

    u32 cls = header->class_index;
    header->magic = 0; // catches double free of the same block
    if (cls == kFallbackClass) {
        fallback_frees_.fetch_add(1, std::memory_order_relaxed);
        std::free(block);
        return;
    }
    ENGINE_ASSERT(cls < kClassCount, "FramePool::Deallocate: corrupt header");
    pooled_frees_[cls].fetch_add(1, std::memory_order_relaxed);

    ThreadCache& cache = t_cache;
    if (!config_.thread_caches || cache.destroyed) {
        std::lock_guard<std::mutex> lock(mutex_);
        CentralPushLocked(cls, block);
        return;
    }
    ENGINE_ASSERT(cache.owner == nullptr || cache.owner == this, "FramePool: thread cache belongs to another pool");
    cache.owner = this;
    *static_cast<void**>(block) = cache.head[cls];
    cache.head[cls] = block;
    ++cache.count[cls];
    if (cache.count[cls] > kSpillThreshold) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (u32 i = 0; i < kSpillThreshold / 2; ++i) {
            void* b = cache.head[cls];
            cache.head[cls] = *static_cast<void**>(b);
            --cache.count[cls];
            CentralPushLocked(cls, b);
        }
    }
}

void FramePool::FlushCache(ThreadCache& cache) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (usize c = 0; c < kClassCount; ++c) {
        while (cache.head[c] != nullptr) {
            void* b = cache.head[c];
            cache.head[c] = *static_cast<void**>(b);
            --cache.count[c];
            CentralPushLocked(c, b);
        }
    }
}

FramePool::ThreadCache::~ThreadCache() {
    // Thread exit: return cached blocks to the central pool so they
    // aren't stranded. The Global() pool outlives every thread.
    if (owner != nullptr) {
        owner->FlushCache(*this);
        owner = nullptr;
    }
    destroyed = true;
}

FramePool::Stats FramePool::GetStats() const {
    Stats s;
    for (usize i = 0; i < kClassCount; ++i) {
        s.pooled_allocs[i] = pooled_allocs_[i].load(std::memory_order_relaxed);
        s.pooled_frees[i] = pooled_frees_[i].load(std::memory_order_relaxed);
    }
    s.fallback_allocs = fallback_allocs_.load(std::memory_order_relaxed);
    s.fallback_frees = fallback_frees_.load(std::memory_order_relaxed);
    return s;
}

} // namespace engine_placeholder::jobs
