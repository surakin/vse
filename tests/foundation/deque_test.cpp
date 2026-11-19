// Container audit series Day 3: Deque, the growable ring.
#include <catch2/catch_test_macros.hpp>

#include "containers/deque.h"
#include "containers/unique_ptr.h"
#include "mem/heap_allocator.h"
#include "types.h"

using namespace engine_placeholder;

TEST_CASE("Deque: both ends, wrap-around and growth while wrapped", "[containers][deque]")
{
    Deque<u32> q;
    CHECK(q.Empty());
    CHECK(q.Capacity() == 0);
    for (u32 i = 0; i < 6; ++i)
    {
        q.PushBack(i);
    }
    for (u32 i = 0; i < 4; ++i)
    {
        CHECK(q.PopFront() == i);  // the head moves forward
    }
    for (u32 i = 6; i < 12; ++i)
    {
        q.PushBack(i);  // wraps past the end of the 8 slots, then grows while wrapped
    }
    q.PushFront(100u);
    REQUIRE(q.Size() == 9);
    CHECK(q.Front() == 100);
    CHECK(q.Back() == 11);
    CHECK(q[1] == 4);
    u32 expected = 4;
    usize visited = 0;
    for (u32 x : q)
    {
        if (visited++ == 0)
        {
            CHECK(x == 100);
            continue;
        }
        CHECK(x == expected++);
    }
    CHECK(q.PopBack() == 11);
    CHECK(q.Size() == 8);
    q.Clear();
    CHECK(q.Empty());
    const usize cap = q.Capacity();
    q.Reserve(100);
    CHECK(q.Capacity() >= 100);
    CHECK(q.Capacity() > cap);
}

TEST_CASE("Deque: move-only elements, a FIFO of 100000", "[containers][deque]")
{
    Deque<UniquePtr<u32>> q;
    u32 next_out = 0;  // first in, first out: the pops come in push order
    bool in_order = true;
    for (u32 i = 0; i < 100000; ++i)
    {
        q.PushBack(MakeUnique<u32>(i));
        if (i % 3 == 0)
        {
            in_order = in_order && *q.PopFront() == next_out++;
        }
    }
    while (!q.Empty())
    {
        in_order = in_order && *q.PopFront() == next_out++;
    }
    CHECK(in_order);
    CHECK(next_out == 100000);
    Deque<UniquePtr<u32>> a;
    a.PushBack(MakeUnique<u32>(7));
    Deque<UniquePtr<u32>> b(static_cast<Deque<UniquePtr<u32>>&&>(a));
    CHECK(a.Empty());
    CHECK(*b.Front() == 7);
}
