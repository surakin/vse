// Container audit series Day 3: BinaryHeap, the priority queue.
#include <catch2/catch_test_macros.hpp>

#include "containers/binary_heap.h"
#include "containers/vector.h"
#include "mem/heap_allocator.h"
#include "types.h"

using namespace vse;

namespace
{

struct Job
{
    u32 priority = 0;
    u32 sequence = 0;
};

// Higher priority first; among equals, the lower sequence (the older one) first.
struct JobLess
{
    bool operator()(const Job& a, const Job& b) const
    {
        return a.priority < b.priority || (a.priority == b.priority && a.sequence > b.sequence);
    }
};

}  // namespace

TEST_CASE("BinaryHeap: pops in priority order, equal priorities oldest first", "[containers][binary_heap]")
{
    BinaryHeap<Job, JobLess> heap;
    u32 state = 12345;
    for (u32 i = 0; i < 2000; ++i)
    {
        state = state * 1103515245u + 12345u;
        heap.Push(Job{(state >> 16) % 10, i});
    }
    CHECK(heap.Size() == 2000);
    Job previous = heap.Pop();
    while (!heap.Empty())
    {
        const Job next = heap.Pop();
        const bool in_order = next.priority < previous.priority ||
                              (next.priority == previous.priority && next.sequence > previous.sequence);
        REQUIRE(in_order);
        previous = next;
    }
}

TEST_CASE("BinaryHeap: Remove takes an element out of the middle", "[containers][binary_heap]")
{
    BinaryHeap<u32, bool (*)(const u32&, const u32&)> heap([](const u32& a, const u32& b) { return a < b; });
    for (u32 i = 0; i < 100; ++i)
    {
        heap.Push(i);
    }
    // Remove every multiple of 7 by its storage position.
    for (usize i = heap.Size(); i > 0; --i)
    {
        if (i - 1 < heap.Size() && heap.At(i - 1) % 7 == 0)
        {
            heap.Remove(i - 1);
        }
    }
    u32 expected = 99;
    usize popped = 0;
    while (!heap.Empty())
    {
        while (expected % 7 == 0)
        {
            --expected;
        }
        REQUIRE(heap.Pop() == expected);
        --expected;
        ++popped;
    }
    CHECK(popped == 100 - 15);
}
