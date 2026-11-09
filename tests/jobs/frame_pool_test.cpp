#include "sync/atomic.h"
#include "types.h"
#include <cstdio>
#include <cstring>
#include "sync/thread.h"

#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"

#include "jobs/frame_pool.h"
#include "os/os.h"

using namespace engine_placeholder;
using engine_placeholder::jobs::FramePool;

namespace {

FramePool::Config LocalConfig(usize slots_class0 = 64) {
    FramePool::Config c;
    for (usize i = 0; i < FramePool::kClassCount; ++i) {
        c.slots_per_class[i] = 64;
    }
    c.slots_per_class[0] = slots_class0;
    c.thread_caches = false;
    return c;
}

// Fills [p, p+size) with a pattern derived from `seed`, then checks it.
void Fill(void* p, usize size, u8 seed) {
    auto* b = static_cast<u8*>(p);
    for (usize i = 0; i < size; ++i) b[i] = static_cast<u8>(seed + i);
}
bool Check(const void* p, usize size, u8 seed) {
    const auto* b = static_cast<const u8*>(p);
    for (usize i = 0; i < size; ++i) {
        if (b[i] != static_cast<u8>(seed + i)) return false;
    }
    return true;
}

} // namespace

TEST_CASE("FramePool routes requests to the smallest class that fits and aligns to 16", "[jobs][framepool]") {
    FramePool pool(LocalConfig());

    struct Case { usize size; usize expected_class; };
    const Case cases[] = {
        {0, 0}, {1, 0}, {112, 0},           // 128 - 16 header
        {113, 1}, {240, 1},                 // 256 - 16
        {241, 2}, {496, 2},
        {497, 3}, {1008, 3},
        {1009, 4}, {FramePool::kMaxPooledRequest, 4},
    };
    for (const Case& c : cases) {
        INFO("size " << c.size);
        FramePool::Stats before = pool.GetStats();
        void* p = pool.Allocate(c.size);
        REQUIRE(p != nullptr);
        REQUIRE(reinterpret_cast<usize>(p) % 16 == 0);
        FramePool::Stats during = pool.GetStats();
        REQUIRE(during.pooled_allocs[c.expected_class] == before.pooled_allocs[c.expected_class] + 1);
        REQUIRE(during.fallback_allocs == before.fallback_allocs);
        Fill(p, c.size, 7);
        REQUIRE(Check(p, c.size, 7));
        pool.Deallocate(p);
        REQUIRE(pool.GetStats().pooled_frees[c.expected_class] == before.pooled_frees[c.expected_class] + 1);
    }
    REQUIRE(pool.GetStats().Live() == 0);
}

TEST_CASE("FramePool live blocks never overlap", "[jobs][framepool]") {
    FramePool pool(LocalConfig(32));
    Vector<void*> blocks;
    Vector<usize> sizes;
    for (usize i = 0; i < 100; ++i) {
        usize size = 8 + (i * 37) % 1900;
        void* p = pool.Allocate(size);
        Fill(p, size, static_cast<u8>(i));
        blocks.Push(p);
        sizes.Push(size);
    }
    for (usize i = 0; i < blocks.Size(); ++i) {
        REQUIRE(Check(blocks[i], sizes[i], static_cast<u8>(i)));
    }
    for (usize i = 0; i < blocks.Size(); ++i) pool.Deallocate(blocks[i]);
    REQUIRE(pool.GetStats().Live() == 0);
}

TEST_CASE("FramePool oversize requests take the counted malloc fallback", "[jobs][framepool]") {
    FramePool pool(LocalConfig());
    void* p = pool.Allocate(FramePool::kMaxPooledRequest + 1);
    REQUIRE(p != nullptr);
    REQUIRE(reinterpret_cast<usize>(p) % 16 == 0);
    Fill(p, FramePool::kMaxPooledRequest + 1, 3);
    REQUIRE(Check(p, FramePool::kMaxPooledRequest + 1, 3));

    FramePool::Stats s = pool.GetStats();
    REQUIRE(s.fallback_allocs == 1);
    REQUIRE(s.TotalPooledAllocs() == 0);
    REQUIRE(s.Live() == 1);

    void* huge = pool.Allocate(1 << 20);
    REQUIRE(huge != nullptr);
    pool.Deallocate(p);
    pool.Deallocate(huge);
    s = pool.GetStats();
    REQUIRE(s.fallback_allocs == 2);
    REQUIRE(s.fallback_frees == 2);
    REQUIRE(s.Live() == 0);
}

TEST_CASE("FramePool falls back (never nullptr) when a class is exhausted, then recovers", "[jobs][framepool]") {
    FramePool pool(LocalConfig(4)); // only 4 class-0 slots
    Vector<void*> blocks;
    for (int i = 0; i < 7; ++i) {
        void* p = pool.Allocate(16);
        REQUIRE(p != nullptr);
        blocks.Push(p);
    }
    FramePool::Stats s = pool.GetStats();
    REQUIRE(s.pooled_allocs[0] == 4);
    REQUIRE(s.fallback_allocs == 3);

    for (usize i = 0; i < blocks.Size(); ++i) pool.Deallocate(blocks[i]);
    s = pool.GetStats();
    REQUIRE(s.pooled_frees[0] == 4);
    REQUIRE(s.fallback_frees == 3);
    REQUIRE(s.Live() == 0);

    // Freed pooled slots are reusable.
    void* again = pool.Allocate(16);
    REQUIRE(pool.GetStats().pooled_allocs[0] == 5);
    pool.Deallocate(again);
}

TEST_CASE("FramePool global instance: blocks freed on another thread are reusable", "[jobs][framepool]") {
    FramePool& pool = FramePool::Global();
    FramePool::Stats before = pool.GetStats();

    Vector<void*> blocks;
    for (int i = 0; i < 1000; ++i) blocks.Push(pool.Allocate(100));
    Thread freer([&] {
        for (usize i = 0; i < blocks.Size(); ++i) pool.Deallocate(blocks[i]);
    });
    freer.Join();

    // The freeing thread exited and flushed its cache; everything is
    // allocatable again with no fallback.
    Vector<void*> again;
    for (int i = 0; i < 1000; ++i) again.Push(pool.Allocate(100));
    for (usize i = 0; i < again.Size(); ++i) pool.Deallocate(again[i]);

    FramePool::Stats after = pool.GetStats();
    REQUIRE(after.Live() == before.Live());
    REQUIRE(after.fallback_allocs == before.fallback_allocs);
}

TEST_CASE("FramePool survives 8-thread cross-thread alloc/free churn with no overlapping live blocks",
          "[jobs][framepool]") {
    FramePool& pool = FramePool::Global();
    const FramePool::Stats before = pool.GetStats();

    constexpr int kThreads = 8;
    constexpr int kOpsPerThread = 125000; // 1M operations total
    constexpr usize kSlots = 4096;
    static const usize kSizes[] = {24, 100, 200, 400, 900, 1900, 3000 /* fallback */};

    // Mailbox: a block parked here by one thread is freed by whichever
    // thread takes it, so allocation and free routinely cross threads.
    Vector<Atomic<void*>> mailbox; // sized once while empty, so the atomics never move
    mailbox.Resize(kSlots);
    for (usize i = 0; i < mailbox.Size(); ++i) mailbox[i].Store(nullptr);
    Atomic<int> corruption{0};

    // Each block: [0..8) = tag, [8..16) = size, [size-8..size) = tag.
    auto stamp = [](void* p, usize size, u64 tag) {
        auto* b = static_cast<u8*>(p);
        std::memcpy(b, &tag, 8);
        std::memcpy(b + 8, &size, 8);
        std::memcpy(b + size - 8, &tag, 8);
        for (usize i = 16; i + 8 < size; ++i) b[i] = static_cast<u8>(tag + i);
    };
    auto verify = [](const void* p) {
        const auto* b = static_cast<const u8*>(p);
        u64 tag, tail;
        usize size;
        std::memcpy(&tag, b, 8);
        std::memcpy(&size, b + 8, 8);
        if (size < 24 || size > 4096) return false;
        std::memcpy(&tail, b + size - 8, 8);
        if (tag != tail) return false;
        for (usize i = 16; i + 8 < size; ++i) {
            if (b[i] != static_cast<u8>(tag + i)) return false;
        }
        return true;
    };

    auto worker = [&](int id) {
        u32 rng = 0x9E3779B9u * static_cast<u32>(id + 1);
        auto next = [&] { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; };
        u64 counter = 0;
        for (int op = 0; op < kOpsPerThread; ++op) {
            usize slot = next() % kSlots;
            void* got = mailbox[slot].Exchange(nullptr, MemoryOrder::AcqRel);
            if (got != nullptr) {
                if (!verify(got)) corruption.FetchAdd(1);
                pool.Deallocate(got);
            } else {
                usize size = kSizes[next() % (sizeof(kSizes) / sizeof(kSizes[0]))];
                void* p = pool.Allocate(size);
                stamp(p, size, (static_cast<u64>(id) << 40) | ++counter);
                void* expected = nullptr;
                if (!mailbox[slot].CompareExchangeStrong(expected, p, MemoryOrder::AcqRel)) {
                    if (!verify(p)) corruption.FetchAdd(1);
                    pool.Deallocate(p);
                }
            }
        }
    };

    Vector<Thread> threads;
    for (int i = 0; i < kThreads; ++i) threads.Push(Thread(worker, i));
    for (usize i = 0; i < threads.Size(); ++i) threads[i].Join();

    for (usize i = 0; i < mailbox.Size(); ++i) {
        void* p = mailbox[i].Exchange(nullptr);
        if (p != nullptr) {
            if (!verify(p)) corruption.FetchAdd(1);
            pool.Deallocate(p);
        }
    }

    REQUIRE(corruption.Load() == 0);
    const FramePool::Stats after = pool.GetStats();
    REQUIRE(after.Live() == before.Live());
    REQUIRE(after.TotalPooledAllocs() > before.TotalPooledAllocs());
    REQUIRE(after.fallback_allocs > before.fallback_allocs); // 3000-byte requests exercised the fallback
}

TEST_CASE("FramePool does not commit slab memory until blocks are used", "[jobs][framepool]") {
    // Reserve a pool with a large slab and allocate one block: resident
    // set size must stay far below the slab size. (The old PoolAllocator-
    // backed design wrote a link into every slot on first use, committing
    // the whole slab under the central lock.)
    auto resident_kb = [] { return os::ResidentMemoryKiB(); };
    FramePool::Config c;
    for (usize i = 0; i < FramePool::kClassCount; ++i) c.slots_per_class[i] = 0;
    c.slots_per_class[4] = 65536; // 128 MiB of 2 KiB blocks
    const i64 before = resident_kb();
    FramePool pool(c);
    void* p = pool.Allocate(1500);
    const i64 after = resident_kb();
    REQUIRE(p != nullptr);
    REQUIRE(pool.GetStats().pooled_allocs[4] == 1);
    REQUIRE(after - before < 8 * 1024); // < 8 MiB of the 128 MiB slab
    pool.Deallocate(p);
}

TEST_CASE("FramePool: a free after the thread's cache was destroyed still reaches the central pool",
          "[jobs][framepool]") {
    FramePool& pool = FramePool::Global();
    const usize cls = 1; // 256-byte blocks
    // Quiesce: other tests' threads have exited and flushed their caches.
    const usize available_before = pool.CentralAvailableForTesting(cls);
    const FramePool::Stats before = pool.GetStats();

    Thread t([&] {
        // Constructed first => destroyed LAST. Its destructor runs after
        // the FramePool's per-thread cache has already been destroyed.
        struct LateFree {
            FramePool* pool;
            void* block = nullptr;
            ~LateFree() {
                if (block != nullptr) pool->Deallocate(block); // late free
                void* again = pool->Allocate(200);              // late alloc
                pool->Deallocate(again);
            }
        };
        thread_local LateFree late{&pool};
        late.block = pool.Allocate(200); // first pool use => cache constructed after `late`
    });
    t.Join();

    const FramePool::Stats after = pool.GetStats();
    REQUIRE(after.Live() == before.Live());
    // Nothing stranded: every block that left the central pool came back.
    REQUIRE(pool.CentralAvailableForTesting(cls) == available_before);
}
