#include "sync/atomic.h"
#include "sync/thread.h"
#include "containers/vector.h"

#include <catch2/catch_test_macros.hpp>

#include "jobs/deque.h"

using namespace engine_placeholder;
using namespace engine_placeholder::jobs;

namespace {

// Sentinel handles: non-null addresses that encode an id. They are never
// resumed, only compared.
CoroutineHandle<> MakeHandle(u64 id) {
    return CoroutineHandle<>::from_address(reinterpret_cast<void*>((id + 1) * 16));
}
u64 IdOf(CoroutineHandle<> h) { return (reinterpret_cast<u64>(h.address()) / 16) - 1; }

// Thief threads must not use Catch macros (not thread-safe); they record
// into the tally and the main thread asserts afterwards.
Atomic<u64> g_bad_ids{0}; // out-of-range / garbage handles seen

void RecordRaw(Vector<Atomic<u32>>& counts, CoroutineHandle<> h) {
    u64 id = IdOf(h);
    if (id < counts.Size()) counts[id].FetchAdd(1, MemoryOrder::Relaxed);
    else g_bad_ids.FetchAdd(1, MemoryOrder::Relaxed);
}

// Runs `total` pushes with `thieves` stealing concurrently; the owner
// interleaves pops. Returns the number of ids consumed zero times and
// more than once.
struct Outcome {
    usize missing = 0;
    usize duplicated = 0;
    u64 stolen = 0;
};

Outcome RunSession(usize total, usize thieves, u32 pop_every) {
    WorkStealingDeque dq;
    Vector<Atomic<u32>> counts; // sized once while empty, so the atomics never move
    counts.Resize(total);
    for (usize i = 0; i < counts.Size(); ++i) counts[i].Store(0, MemoryOrder::Relaxed);
    Atomic<bool> producer_done{false};
    Atomic<u64> stolen{0};

    Vector<Thread> ts;
    for (usize i = 0; i < thieves; ++i) {
        ts.Push(Thread([&] {
            for (;;) {
                CoroutineHandle<> h;
                StealStatus st = dq.StealTop(h);
                if (st == StealStatus::Success) {
                    RecordRaw(counts, h);
                    stolen.FetchAdd(1, MemoryOrder::Relaxed);
                } else if (st == StealStatus::Empty) {
                    if (producer_done.Load(MemoryOrder::Acquire)) {
                        // The owner finished and drained: nothing more arrives.
                        return;
                    }
                    ThisThread::Yield();
                }
                // Lost: retry immediately.
            }
        }));
    }

    u64 rng = 0x9E3779B97F4A7C15ull;
    for (usize id = 0; id < total; ++id) {
        while (!dq.PushBottom(MakeHandle(id))) {
            auto h = dq.PopBottom(); // full: make room
            if (h) RecordRaw(counts, h);
        }
        rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
        if (pop_every != 0 && (rng % pop_every) == 0) {
            auto h = dq.PopBottom();
            if (h) RecordRaw(counts, h);
        }
    }
    // Drain as owner.
    for (;;) {
        auto h = dq.PopBottom();
        if (!h) break;
        RecordRaw(counts, h);
    }
    producer_done.Store(true, MemoryOrder::Release);
    for (usize i = 0; i < ts.Size(); ++i) ts[i].Join();

    Outcome out;
    out.stolen = stolen.Load();
    for (usize i = 0; i < counts.Size(); ++i) {
        u32 v = counts[i].Load();
        if (v == 0) ++out.missing;
        if (v > 1) ++out.duplicated;
    }
    return out;
}

} // namespace

TEST_CASE("Deque single-threaded LIFO from the owner, FIFO for thieves", "[jobs][deque]") {
    WorkStealingDeque dq;
    REQUIRE_FALSE(dq.PopBottom());
    CoroutineHandle<> out;
    REQUIRE(dq.StealTop(out) == StealStatus::Empty);

    for (u64 i = 0; i < 4; ++i) REQUIRE(dq.PushBottom(MakeHandle(i)));
    REQUIRE(dq.StealTop(out) == StealStatus::Success);
    REQUIRE(IdOf(out) == 0); // oldest
    REQUIRE(IdOf(dq.PopBottom()) == 3); // newest
    REQUIRE(IdOf(dq.PopBottom()) == 2);
    REQUIRE(IdOf(dq.PopBottom()) == 1);
    REQUIRE_FALSE(dq.PopBottom());
    REQUIRE(dq.StealTop(out) == StealStatus::Empty);
}

TEST_CASE("Deque reports full at capacity without losing or overwriting anything", "[jobs][deque]") {
    WorkStealingDeque dq;
    const usize cap = WorkStealingDeque::Capacity();
    for (usize i = 0; i < cap; ++i) REQUIRE(dq.PushBottom(MakeHandle(i)));
    REQUIRE_FALSE(dq.PushBottom(MakeHandle(cap)));
    REQUIRE_FALSE(dq.PushBottom(MakeHandle(cap + 1)));

    // Steal one: frees a slot, push succeeds again (wraps the ring).
    CoroutineHandle<> out;
    REQUIRE(dq.StealTop(out) == StealStatus::Success);
    REQUIRE(IdOf(out) == 0);
    REQUIRE(dq.PushBottom(MakeHandle(cap)));
    REQUIRE_FALSE(dq.PushBottom(MakeHandle(cap + 1)));

    // Drain: ids cap down to 1, every one exactly once, in LIFO order.
    for (usize expect = cap; expect >= 1; --expect) {
        auto h = dq.PopBottom();
        REQUIRE(h);
        REQUIRE(IdOf(h) == expect);
    }
    REQUIRE_FALSE(dq.PopBottom());
}

TEST_CASE("Deque: owner plus one thief, 1M ops, each handle consumed exactly once", "[jobs][deque]") {
    Outcome o = RunSession(1'000'000, 1, 3);
    REQUIRE(o.missing == 0);
    REQUIRE(o.duplicated == 0);
    REQUIRE(g_bad_ids.Load() == 0);
    REQUIRE(o.stolen > 0); // the thief paths really ran
}

TEST_CASE("Deque: owner plus four thieves, 1M ops, each handle consumed exactly once", "[jobs][deque]") {
    Outcome o = RunSession(1'000'000, 4, 3);
    REQUIRE(o.missing == 0);
    REQUIRE(o.duplicated == 0);
    REQUIRE(g_bad_ids.Load() == 0);
    REQUIRE(o.stolen > 0); // the thief paths really ran
}

TEST_CASE("Deque: single-element contention between PopBottom and four thieves", "[jobs][deque]") {
    // The queue never holds more than one element: every push is raced by
    // the owner's pop and four thieves (the last-element race).
    constexpr usize kRounds = 300'000;
    WorkStealingDeque dq;
    Vector<Atomic<u32>> counts;
    counts.Resize(kRounds);
    for (usize i = 0; i < counts.Size(); ++i) counts[i].Store(0, MemoryOrder::Relaxed);
    Atomic<bool> done{false};
    Atomic<u64> steals{0};
    Vector<Thread> ts;
    for (int i = 0; i < 4; ++i) {
        ts.Push(Thread([&] {
            while (!done.Load(MemoryOrder::Acquire)) {
                CoroutineHandle<> h;
                if (dq.StealTop(h) == StealStatus::Success) {
                    RecordRaw(counts, h);
                    steals.FetchAdd(1, MemoryOrder::Relaxed);
                }
            }
        }));
    }
    for (usize id = 0; id < kRounds; ++id) {
        REQUIRE(dq.PushBottom(MakeHandle(id)));
        auto h = dq.PopBottom();
        if (h) RecordRaw(counts, h);
    }
    // Anything still sitting in the deque after the loop is taken here.
    for (auto h = dq.PopBottom(); h; h = dq.PopBottom()) RecordRaw(counts, h);
    done.Store(true, MemoryOrder::Release);
    for (usize i = 0; i < ts.Size(); ++i) ts[i].Join();
    // A thief may have stolen the final element between our last pop and
    // the stop flag; either way every id must be consumed exactly once.
    usize bad = 0;
    for (usize i = 0; i < counts.Size(); ++i) bad += counts[i].Load() != 1;

    REQUIRE(bad == 0);
    REQUIRE(g_bad_ids.Load() == 0);
    REQUIRE(steals.Load() > 0);
}
