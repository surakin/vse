// Container audit series Day 3: the searching and selecting algorithms, and the sorts on containers, against
// brute-force oracles.
#include <catch2/catch_test_macros.hpp>

#include "algorithm.h"
#include "containers/array.h"
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

TEST_CASE("Sort orders every input pattern and keeps the multiset, for small and large elements", "[algorithm]")
{
    // Container audit follow-up: the pdqsort-style Sort, against a brute-force check (sorted, and the same elements).
    struct Big
    {
        u64 key = 0;
        u32 tag = 0;
        Array<u64, 5> payload{};
    };
    for (u32 kind = 0; kind < 5; ++kind)
    {
        for (usize n : {usize{0}, usize{1}, usize{2}, usize{23}, usize{24}, usize{25}, usize{127}, usize{129},
                        usize{1000}, usize{20000}})
        {
            Vector<u32> v = Made(n, kind, 11);
            u64 sum = 0;
            for (usize i = 0; i < n; ++i)
            {
                sum += v[i];
            }
            Sort(v, Less);
            u64 after = 0;
            for (usize i = 0; i < n; ++i)
            {
                after += v[i];
                if (i > 0)
                {
                    REQUIRE_FALSE(v[i] < v[i - 1]);
                }
            }
            CHECK(after == sum);
            const Vector<u32> keys = Made(n, kind, 11);
            Vector<Big> big;
            for (usize i = 0; i < n; ++i)
            {
                Big b;
                b.key = keys[i];
                b.tag = static_cast<u32>(i);
                b.payload[0] = b.key * 3 + i;
                big.Push(b);
            }
            Sort(big, [](const Big& a, const Big& b) { return a.key < b.key; });
            u64 tags = 0;
            for (usize i = 0; i < n; ++i)
            {
                REQUIRE(big[i].payload[0] == big[i].key * 3 + big[i].tag);  // each element moved whole
                tags += big[i].tag;
                if (i > 0)
                {
                    REQUIRE_FALSE(big[i].key < big[i - 1].key);
                }
            }
            CHECK(tags == (n == 0 ? 0 : n * (n - 1) / 2));
        }
    }
}

TEST_CASE("Sort stays fast on an adversarial pattern (organ pipe, sawtooth)", "[algorithm]")
{
    Vector<u32> v;
    const u32 n = 200000;
    for (u32 i = 0; i < n; ++i)
    {
        v.Push(i < n / 2 ? i : n - i);  // organ pipe
    }
    Sort(v, Less);
    for (u32 i = 1; i < n; ++i)
    {
        REQUIRE(v[i - 1] <= v[i]);
    }
    v.Clear();
    for (u32 i = 0; i < n; ++i)
    {
        v.Push(i % 1000);  // sawtooth
    }
    Sort(v, Less);
    for (u32 i = 1; i < n; ++i)
    {
        REQUIRE(v[i - 1] <= v[i]);
    }
}
