#include <catch2/catch_test_macros.hpp>

#include "algorithm.h"
#include "containers/array.h"
#include "containers/span.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"

using namespace vse;

namespace
{
struct Item
{
    int key;
    int order;
};
bool ByKey(const Item& a, const Item& b)
{
    return a.key < b.key;
}
}  // namespace

TEST_CASE("Min, Max and Clamp", "[types][algorithm]")
{
    CHECK(Min(3, 5) == 3);
    CHECK(Max(3, 5) == 5);
    CHECK(Min(2.5, -1.0) == -1.0);
    CHECK(Clamp(7.0f, 0.0f, 5.0f) == 5.0f);
    CHECK(Clamp(-7.0, 0.0, 5.0) == 0.0);
    CHECK(Clamp(3.0f, 0.0f, 5.0f) == 3.0f);
}

TEST_CASE("IndexOf finds the first match or the size", "[types][algorithm]")
{
    const int values[] = {4, 8, 15, 8};
    CHECK(IndexOf(Span<const int>(values), 8) == 1);
    CHECK(IndexOf(Span<const int>(values), 99) == 4);
    CHECK(IndexOf(Span<const int>(), 1) == 0);
}

TEST_CASE("Sort orders spans of every size", "[types][algorithm]")
{
    for (usize n : {0u, 1u, 2u, 15u, 16u, 17u, 100u, 1000u})
    {
        Vector<int> v;
        u64 state = 12345 + n;
        for (usize i = 0; i < n; ++i)
        {
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            v.Push(static_cast<int>(state >> 40) % 50);
        }
        Sort(Span<int>(v));
        bool sorted = true;
        for (usize i = 1; i < v.Size(); ++i)
        {
            sorted = sorted && !(v[i] < v[i - 1]);
        }
        CHECK(sorted);
    }
    Array<int, 4> a{{3, 1, 2, 0}};
    Sort(Span<int>(a), [](int x, int y) { return x > y; });
    CHECK(a.items[0] == 3);
    CHECK(a.items[3] == 0);
}

TEST_CASE("StableSort keeps equal keys in order", "[types][algorithm]")
{
    for (usize n : {5u, 8u, 9u, 64u, 500u})
    {
        Vector<Item> v;
        u64 state = 777 + n;
        for (usize i = 0; i < n; ++i)
        {
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            v.Push(Item{static_cast<int>(state >> 40) % 7, static_cast<int>(i)});
        }
        StableSort(Span<Item>(v), ByKey);
        bool ok = true;
        for (usize i = 1; i < v.Size(); ++i)
        {
            ok = ok && v[i - 1].key <= v[i].key;
            if (v[i - 1].key == v[i].key)
            {
                ok = ok && v[i - 1].order < v[i].order;
            }
        }
        CHECK(ok);
    }
}

TEST_CASE("Append and Assign copy a span into a vector", "[types][algorithm]")
{
    const int more[] = {7, 8, 9};
    Vector<int> v;
    v.Push(1);
    Append(v, Span<const int>(more));
    REQUIRE(v.Size() == 4);
    CHECK(v[3] == 9);
    Assign(v, Span<const int>(more, 2));
    REQUIRE(v.Size() == 2);
    CHECK(v[0] == 7);
}

TEST_CASE("IndexOf, Append and Assign accept vectors directly; Append grows geometrically", "[types][algorithm]")
{
    Vector<int> v;
    const int more[] = {1, 2, 3};
    Append(v, more);
    Append(v, more);
    CHECK(IndexOf(v, 3) == 2);
    CHECK(IndexOf(v, 9) == v.Size());
    Vector<int> w;
    Append(w, v);
    CHECK(w.Size() == 6);
    usize reallocations = 0, last_capacity = 0;
    Vector<int> grown;
    for (int i = 0; i < 1000; ++i)
    {
        Append(grown, more);
        if (grown.Capacity() != last_capacity)
        {
            ++reallocations;
            last_capacity = grown.Capacity();
        }
    }
    CHECK(reallocations < 20);
    CHECK(Clamp(15, 0, 10) == 10);
    CHECK(Clamp(7u, 0u, 10u) == 7u);
}

TEST_CASE("Vector::Empty reports whether there are elements", "[types][algorithm]")
{
    Vector<int> v;
    CHECK(v.Empty());
    v.Push(1);
    CHECK_FALSE(v.Empty());
    v.Clear();
    CHECK(v.Empty());
}

TEST_CASE("Vector::Insert moves a move-only value in", "[types][algorithm]")
{
    Vector<UniquePtr<int>> v;
    v.Push(MakeUnique<int>(1));
    v.Push(MakeUnique<int>(3));
    v.Insert(1, MakeUnique<int>(2));
    REQUIRE(v.Size() == 3);
    CHECK(*v[0] == 1);
    CHECK(*v[1] == 2);
    CHECK(*v[2] == 3);
}

TEST_CASE("Sort handles sorted, reversed, all-equal and organ-pipe input", "[types][algorithm]")
{
    for (int shape = 0; shape < 4; ++shape)
    {
        Vector<int> v;
        for (int i = 0; i < 5000; ++i)
        {
            v.Push(shape == 0 ? i : shape == 1 ? 5000 - i : shape == 2 ? 7 : (i < 2500 ? i : 5000 - i));
        }
        Sort(Span<int>(v));
        bool sorted = true;
        for (usize i = 1; i < v.Size(); ++i)
        {
            sorted = sorted && !(v[i] < v[i - 1]);
        }
        CHECK(sorted);
    }
}

TEST_CASE("Append of trivially copyable elements copies them all", "[types][algorithm]")
{
    Vector<byte> bytes;
    const byte data[300] = {1, 2, 3};
    Append(bytes, Span<const byte>(data, 300));
    Append(bytes, Span<const byte>(data, 3));
    REQUIRE(bytes.Size() == 303);
    CHECK(bytes[1] == 2);
    CHECK(bytes[301] == 2);
}
