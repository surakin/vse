#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "jobs/frame_pool.h"

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
        REQUIRE(reinterpret_cast<std::uintptr_t>(p) % 16 == 0);
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
    std::vector<void*> blocks;
    std::vector<usize> sizes;
    for (usize i = 0; i < 100; ++i) {
        usize size = 8 + (i * 37) % 1900;
        void* p = pool.Allocate(size);
        Fill(p, size, static_cast<u8>(i));
        blocks.push_back(p);
        sizes.push_back(size);
    }
    for (usize i = 0; i < blocks.size(); ++i) {
        REQUIRE(Check(blocks[i], sizes[i], static_cast<u8>(i)));
    }
    for (void* p : blocks) pool.Deallocate(p);
    REQUIRE(pool.GetStats().Live() == 0);
}

TEST_CASE("FramePool oversize requests take the counted malloc fallback", "[jobs][framepool]") {
    FramePool pool(LocalConfig());
    void* p = pool.Allocate(FramePool::kMaxPooledRequest + 1);
    REQUIRE(p != nullptr);
    REQUIRE(reinterpret_cast<std::uintptr_t>(p) % 16 == 0);
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
    std::vector<void*> blocks;
    for (int i = 0; i < 7; ++i) {
        void* p = pool.Allocate(16);
        REQUIRE(p != nullptr);
        blocks.push_back(p);
    }
    FramePool::Stats s = pool.GetStats();
    REQUIRE(s.pooled_allocs[0] == 4);
    REQUIRE(s.fallback_allocs == 3);

    for (void* p : blocks) pool.Deallocate(p);
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

    std::vector<void*> blocks;
    for (int i = 0; i < 1000; ++i) blocks.push_back(pool.Allocate(100));
    std::thread freer([&] {
        for (void* p : blocks) pool.Deallocate(p);
    });
    freer.join();

    // The freeing thread exited and flushed its cache; everything is
    // allocatable again with no fallback.
    std::vector<void*> again;
    for (int i = 0; i < 1000; ++i) again.push_back(pool.Allocate(100));
    for (void* p : again) pool.Deallocate(p);

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
    std::vector<std::atomic<void*>> mailbox(kSlots);
    for (auto& m : mailbox) m.store(nullptr);
    std::atomic<int> corruption{0};

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
            void* got = mailbox[slot].exchange(nullptr, std::memory_order_acq_rel);
            if (got != nullptr) {
                if (!verify(got)) corruption.fetch_add(1);
                pool.Deallocate(got);
            } else {
                usize size = kSizes[next() % (sizeof(kSizes) / sizeof(kSizes[0]))];
                void* p = pool.Allocate(size);
                stamp(p, size, (static_cast<u64>(id) << 40) | ++counter);
                void* expected = nullptr;
                if (!mailbox[slot].compare_exchange_strong(expected, p, std::memory_order_acq_rel)) {
                    if (!verify(p)) corruption.fetch_add(1);
                    pool.Deallocate(p);
                }
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) threads.emplace_back(worker, i);
    for (auto& t : threads) t.join();

    for (auto& m : mailbox) {
        void* p = m.exchange(nullptr);
        if (p != nullptr) {
            if (!verify(p)) corruption.fetch_add(1);
            pool.Deallocate(p);
        }
    }

    REQUIRE(corruption.load() == 0);
    const FramePool::Stats after = pool.GetStats();
    REQUIRE(after.Live() == before.Live());
    REQUIRE(after.TotalPooledAllocs() > before.TotalPooledAllocs());
    REQUIRE(after.fallback_allocs > before.fallback_allocs); // 3000-byte requests exercised the fallback
}
