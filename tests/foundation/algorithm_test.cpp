// Container audit series Day 3: the searching and selecting algorithms, and the sorts on containers, against
// brute-force oracles.
#include <catch2/catch_test_macros.hpp>

#include "algorithm.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "types.h"

using namespace engine_placeholder;

namespace
{

Vector<u32> Made(usize n, u32 kind, u32 seed)
{
    Vector<u32> v;
    u32 state = seed;
    for (usize i = 0; i < n; ++i)
    {
        state = state * 1664525u + 1013904223u;
        switch (kind)
        {
        case 0: v.Push(state >> 8); break;               // random
        case 1: v.Push(static_cast<u32>(i)); break;      // sorted
        case 2: v.Push(static_cast<u32>(n - i)); break;  // reversed
        case 3: v.Push(7u); break;                       // all equal
        default: v.Push((state >> 8) % 5); break;        // few distinct
        }
    }
    return v;
}

bool Less(const u32& a, const u32& b)
{
    return a < b;
}

}  // namespace

TEST_CASE("LowerBound, UpperBound and BinarySearch agree with a scan", "[algorithm]")
{
    Vector<u32> v = Made(500, 4, 9);
    Sort(v, Less);
    const Span<const u32> s(v.Data(), v.Size());
    for (u32 value = 0; value < 7; ++value)
    {
        usize lower = 0;
        while (lower < v.Size() && v[lower] < value)
        {
            ++lower;
        }
        usize upper = lower;
        while (upper < v.Size() && v[upper] == value)
        {
            ++upper;
        }
        CHECK(LowerBound(s, value, Less) == lower);
        CHECK(UpperBound(s, value, Less) == upper);
        const usize found = BinarySearch(s, value, Less);
        if (lower == upper)
        {
            CHECK(found == v.Size());
        }
        else
        {
            CHECK(v[found] == value);
        }
    }
    CHECK(LowerBound(Span<const u32>(), 3u, Less) == 0);
    // A mutable span and the vector itself are accepted too.
    CHECK(LowerBound(Span<u32>(v.Data(), v.Size()), 3u, Less) == LowerBound(s, 3u, Less));
    CHECK(UpperBound(v, 3u, Less) == UpperBound(s, 3u, Less));
    CHECK(BinarySearch(v, 99u, Less) == v.Size());
}

TEST_CASE("NthElement and PartialSort settle what a full sort would", "[algorithm]")
{
    for (u32 kind = 0; kind < 5; ++kind)
    {
        for (usize n : {usize{0}, usize{1}, usize{17}, usize{300}, usize{5000}})
        {
            Vector<u32> sorted = Made(n, kind, 3);
            Sort(sorted, Less);
            for (usize nth : {usize{0}, n / 3, n > 0 ? n - 1 : 0})
            {
                Vector<u32> v = Made(n, kind, 3);
                NthElement(Span<u32>(v.Data(), v.Size()), nth, Less);
                if (n == 0)
                {
                    continue;
                }
                REQUIRE(v[nth] == sorted[nth]);
                for (usize i = 0; i < nth; ++i)
                {
                    REQUIRE_FALSE(v[nth] < v[i]);
                }
                for (usize i = nth + 1; i < n; ++i)
                {
                    REQUIRE_FALSE(v[i] < v[nth]);
                }
                Vector<u32> w = Made(n, kind, 3);
                const usize k = nth + 1;
                PartialSort(Span<u32>(w.Data(), w.Size()), k, Less);
                for (usize i = 0; i < k; ++i)
                {
                    REQUIRE(w[i] == sorted[i]);
                }
            }
        }
    }
}

TEST_CASE("StableSort keeps equal elements in order for a non-trivial type", "[algorithm]")
{
    struct Item
    {
        u32 key = 0;
        u32 order = 0;
        Vector<u32> payload;  // not trivially copyable: only movable
    };
    Vector<Item> items;
    u32 state = 77;
    for (u32 i = 0; i < 1000; ++i)
    {
        state = state * 1664525u + 1013904223u;
        Item it;
        it.key = (state >> 8) % 10;
        it.order = i;
        it.payload.Push(i);
        items.Push(static_cast<Item&&>(it));
    }
    StableSort(items, [](const Item& a, const Item& b) { return a.key < b.key; });
    for (usize i = 1; i < items.Size(); ++i)
    {
        REQUIRE(items[i - 1].key <= items[i].key);
        if (items[i - 1].key == items[i].key)
        {
            REQUIRE(items[i - 1].order < items[i].order);
        }
        REQUIRE(items[i].payload[0] == items[i].order);
    }
}
