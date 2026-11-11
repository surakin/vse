#include "algorithm.h"
#include "containers/vector.h"

#include <catch2/catch_test_macros.hpp>

#include "rhi/handle_pool.h"
#include "lang/utility.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;

namespace
{

struct TestTag
{
};
using TestHandle = Handle<TestTag>;

int g_live_values = 0;

struct Tracked
{
    int id = 0;
    explicit Tracked(int i = 0) : id(i) { ++g_live_values; }
    Tracked(Tracked&& o) noexcept : id(o.id) { ++g_live_values; }
    Tracked(const Tracked&) = delete;
    Tracked& operator=(const Tracked&) = delete;
    ~Tracked() { --g_live_values; }
};

using Pool = HandlePool<Tracked, TestTag>;

}  // namespace

TEST_CASE("Allocate returns a valid handle that Get resolves", "[rhi][pool]")
{
    Pool pool;
    TestHandle h = pool.Allocate(Tracked(7));
    REQUIRE(h.IsValid());
    REQUIRE(pool.Get(h) != nullptr);
    CHECK(pool.Get(h)->id == 7);
    CHECK(pool.LiveCount() == 1);
}

TEST_CASE("Invalid and out-of-range handles never resolve", "[rhi][pool]")
{
    Pool pool;
    CHECK(pool.Get(TestHandle{}) == nullptr);
    CHECK(pool.Get(TestHandle{0, 1}) == nullptr);  // nothing allocated yet
    TestHandle h = pool.Allocate(Tracked(1));
    CHECK(pool.Get(TestHandle{h.index, 0}) == nullptr);      // generation 0
    CHECK(pool.Get(TestHandle{h.index + 1, 1}) == nullptr);  // a free slot
    CHECK(pool.Get(TestHandle{0xFFFFFFFFu, 1}) == nullptr);  // far out of range
    CHECK_FALSE(pool.Free(TestHandle{}));
    CHECK_FALSE(pool.Retire(TestHandle{h.index, 99}, 1));
}

TEST_CASE("A freed handle goes stale and a reused slot gets a new generation", "[rhi][pool]")
{
    Pool pool;
    TestHandle first = pool.Allocate(Tracked(1));
    REQUIRE(pool.Free(first));
    CHECK(pool.Get(first) == nullptr);
    CHECK_FALSE(pool.Free(first));  // double free is reported, not repeated

    TestHandle second = pool.Allocate(Tracked(2));
    CHECK(second.index == first.index);  // the slot is reused
    CHECK(second.generation != first.generation);
    CHECK(second != first);
    CHECK(pool.Get(first) == nullptr);  // the old copy does not resolve to the new occupant
    REQUIRE(pool.Get(second) != nullptr);
    CHECK(pool.Get(second)->id == 2);
}

TEST_CASE("Generations are never zero and increase on every reuse", "[rhi][pool]")
{
    Pool pool;
    u32 last = 0;
    for (int i = 0; i < 1000; ++i)
    {
        TestHandle h = pool.Allocate(Tracked(i));
        CHECK(h.generation != 0);
        if (i > 0)
        {
            CHECK(h.generation > last);
        }
        last = h.generation;
        pool.Free(h);
    }
}

TEST_CASE("Retire invalidates at once but keeps the value until its frame completes", "[rhi][pool]")
{
    g_live_values = 0;
    {
        Pool pool;
        TestHandle h = pool.Allocate(Tracked(5));
        REQUIRE(pool.Retire(h, 10));
        CHECK(pool.Get(h) == nullptr);  // invalid immediately
        CHECK(pool.LiveCount() == 0);
        CHECK(pool.RetiredCount() == 1);
        CHECK(g_live_values == 1);  // value still alive for the GPU

        int released = 0;
        CHECK(pool.CollectRetired(9, [&](Tracked&) { ++released; }) == 0);  // frame 10 not done
        CHECK(released == 0);
        CHECK(pool.RetiredCount() == 1);

        // The slot is not recycled while retired: a new allocation gets a different slot.
        TestHandle other = pool.Allocate(Tracked(6));
        CHECK(other.index != h.index);

        int seen_id = 0;
        CHECK(pool.CollectRetired(10,
                                  [&](Tracked& t)
                                  {
                                      seen_id = t.id;
                                      ++released;
                                  }) == 1);
        CHECK(released == 1);
        CHECK(seen_id == 5);  // the callback saw the live value
        CHECK(pool.RetiredCount() == 0);
        CHECK(pool.Get(h) == nullptr);

        // Now the slot is reusable, and with a generation newer than the retired handle's.
        TestHandle reused = pool.Allocate(Tracked(8));
        CHECK(reused.index == h.index);
        CHECK(reused.generation != h.generation);
        CHECK(pool.Get(h) == nullptr);
    }
    CHECK(g_live_values == 0);
}

TEST_CASE("CollectRetired releases oldest first and stops at the first unfinished frame", "[rhi][pool]")
{
    Pool pool;
    TestHandle a = pool.Allocate(Tracked(1));
    TestHandle b = pool.Allocate(Tracked(2));
    TestHandle c = pool.Allocate(Tracked(3));
    pool.Retire(a, 1);
    pool.Retire(b, 2);
    pool.Retire(c, 2);

    Vector<int> order;
    CHECK(pool.CollectRetired(1, [&](Tracked& t) { order.Push(t.id); }) == 1);
    CHECK(pool.CollectRetired(5, [&](Tracked& t) { order.Push(t.id); }) == 2);
    REQUIRE(order.Size() == 3);
    CHECK(order[0] == 1);
    CHECK(order[1] == 2);
    CHECK(order[2] == 3);
}

TEST_CASE("CollectAllRetired drains everything", "[rhi][pool]")
{
    Pool pool;
    for (int i = 0; i < 10; ++i)
    {
        pool.Retire(pool.Allocate(Tracked(i)), 1000 + static_cast<u64>(i));
    }
    CHECK(pool.CollectAllRetired([](Tracked&) {}) == 10);
    CHECK(pool.RetiredCount() == 0);
}

TEST_CASE("The pool grows past one chunk and keeps pointers stable", "[rhi][pool]")
{
    Pool pool;
    Vector<TestHandle> handles;
    Vector<Tracked*> pointers;
    for (int i = 0; i < 1000; ++i)
    {
        handles.Push(pool.Allocate(Tracked(i)));
        REQUIRE(handles[handles.Size() - 1].IsValid());
        pointers.Push(pool.Get(handles[handles.Size() - 1]));
    }
    CHECK(pool.Capacity() >= 1000);
    for (int i = 0; i < 1000; ++i)
    {
        CHECK(pool.Get(handles[static_cast<usize>(i)]) == pointers[static_cast<usize>(i)]);  // never moved
        CHECK(pointers[static_cast<usize>(i)]->id == i);
    }
    // All handles are distinct.
    Vector<u64> keys;
    for (usize i = 0; i < handles.Size(); ++i)
    {
        keys.Push((static_cast<u64>(handles[i].index) << 32) | handles[i].generation);
    }
    Sort(engine_placeholder::Span<u64>(keys.Data(), keys.Size()));
    usize repeated = 0;
    for (usize i = 1; i < keys.Size(); ++i)
    {
        repeated += keys[i] == keys[i - 1] ? 1 : 0;
    }
    CHECK(repeated == 0);
}

TEST_CASE("Allocation fails cleanly at the capacity limit", "[rhi][pool]")
{
    HandlePool<u32, TestTag> pool;
    for (u32 i = 0; i < (HandlePool<u32, TestTag>::kMaxCapacity); ++i)
    {
        u32 v = i;
        REQUIRE(pool.Allocate(Move(v)).IsValid());
    }
    u32 extra = 1;
    CHECK_FALSE(pool.Allocate(Move(extra)).IsValid());
    CHECK(pool.LiveCount() == HandlePool<u32, TestTag>::kMaxCapacity);
}

TEST_CASE("Destroying the pool destroys live and retired values", "[rhi][pool]")
{
    g_live_values = 0;
    {
        Pool pool;
        pool.Allocate(Tracked(1));
        pool.Retire(pool.Allocate(Tracked(2)), 3);
        CHECK(g_live_values == 2);
    }
    CHECK(g_live_values == 0);
}

TEST_CASE("ForEachLive visits exactly the live values", "[rhi][pool]")
{
    Pool pool;
    TestHandle a = pool.Allocate(Tracked(1));
    TestHandle b = pool.Allocate(Tracked(2));
    TestHandle c = pool.Allocate(Tracked(3));
    pool.Free(a);
    pool.Retire(b, 1);
    int sum = 0;
    int count = 0;
    pool.ForEachLive(
        [&](TestHandle h, Tracked& t)
        {
            CHECK(pool.Get(h) == &t);
            sum += t.id;
            ++count;
        });
    CHECK(count == 1);
    CHECK(sum == 3);
    (void)c;
}

TEST_CASE("Random allocate/free/retire/collect matches a reference model", "[rhi][pool][stress]")
{
    g_live_values = 0;
    {
        Pool pool;
        struct Entry
        {
            TestHandle h;
            int id;
        };
        Vector<Entry> live;
        Vector<TestHandle> dead;
        u64 frame = 1;
        u64 completed = 0;
        u64 rng = 0x2545F4914F6CDD1Dull;
        auto next = [&]()
        {
            rng ^= rng << 13;
            rng ^= rng >> 7;
            rng ^= rng << 17;
            return rng;
        };
        int next_id = 1;
        for (int step = 0; step < 20000; ++step)
        {
            switch (next() % 6)
            {
            case 0:
            case 1:
            {
                int id = next_id++;
                TestHandle h = pool.Allocate(Tracked(id));
                REQUIRE(h.IsValid());
                live.Push(Entry{h, id});
                break;
            }
            case 2:
                if (!live.Empty())
                {
                    usize i = next() % live.Size();
                    REQUIRE(pool.Free(live[i].h));
                    dead.Push(live[i].h);
                    live[i] = live[live.Size() - 1];
                    live.Pop();
                }
                break;
            case 3:
                if (!live.Empty())
                {
                    usize i = next() % live.Size();
                    REQUIRE(pool.Retire(live[i].h, frame));
                    dead.Push(live[i].h);
                    live[i] = live[live.Size() - 1];
                    live.Pop();
                }
                break;
            case 4: ++frame; break;
            case 5:
                if (completed < frame)
                {
                    ++completed;
                    pool.CollectRetired(completed, [](Tracked&) {});
                }
                break;
            default: break;  // next() % 6 is 0 to 5, all handled above
            }
            // Spot-check the model.
            if (!live.Empty())
            {
                const Entry& e = live[next() % live.Size()];
                REQUIRE(pool.Get(e.h) != nullptr);
                CHECK(pool.Get(e.h)->id == e.id);
            }
            if (!dead.Empty())
            {
                CHECK(pool.Get(dead[next() % dead.Size()]) == nullptr);
            }
        }
        CHECK(pool.LiveCount() == live.Size());
    }
    CHECK(g_live_values == 0);
}
