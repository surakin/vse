// Container audit series Day 7: DenseMap, the map whose entries are dense in slots, against a HashMap oracle.
#include <catch2/catch_test_macros.hpp>

#include "containers/dense_map.h"
#include "containers/hash_map.h"
#include "containers/unique_ptr.h"
#include "mem/heap_allocator.h"
#include "string/string.h"
#include "types.h"

using namespace vse;

TEST_CASE("DenseMap: churn agrees with a HashMap, and slots stay dense", "[containers][dense_map]")
{
    DenseMap<u64, u64> map;
    HashMap<u64, u64, HeapAllocator> oracle(HeapAllocator::Default());
    u64 state = 99;
    for (u32 step = 0; step < 20000; ++step)
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        const u64 key = (state >> 33) % 500;
        if ((state >> 20) % 3 == 0)
        {
            u32 moved = 0;
            const bool had = oracle.Remove(key);
            REQUIRE(map.Remove(key, &moved) == had);
            if (had && moved != DenseMap<u64, u64>::kNone)
            {
                REQUIRE(moved == map.Size());  // the last slot moved
            }
        }
        else
        {
            const auto made = map.Emplace(key, key * 3);
            oracle.Emplace(key, key * 3);
            REQUIRE(map.KeyAt(made.slot) == key);
            REQUIRE(map.At(made.slot) == key * 3);
        }
        REQUIRE(map.Size() == oracle.Size());
    }
    for (u64 key = 0; key < 500; ++key)
    {
        const u64* got = map.Find(key);
        const u64* want = oracle.Find(key);
        REQUIRE((got == nullptr) == (want == nullptr));
        if (got != nullptr)
        {
            CHECK(*got == *want);
            CHECK(map.KeyAt(map.SlotOf(key)) == key);
        }
    }
    usize visited = 0;
    u32 slot = 0;
    for (auto& [key, value] : map)  // slot order
    {
        CHECK(map.SlotOf(key) == slot++);
        CHECK(value == key * 3);
        ++visited;
    }
    CHECK(visited == map.Size());
}

TEST_CASE("DenseMap: slots are stable until a removal; RemoveIf visits each entry once", "[containers][dense_map]")
{
    DenseMap<u32, u32> map;
    map.Reserve(100);
    for (u32 i = 0; i < 100; ++i)
    {
        CHECK(map.Emplace(i, i).slot == i);
    }
    CHECK_FALSE(map.Emplace(5u, 999u).inserted);
    CHECK(*map.Find(5u) == 5);
    CHECK(map.RemoveAt(10) == 99);  // the last entry (key 99) moved into slot 10
    CHECK(map.KeyAt(10) == 99);
    CHECK(map.RemoveAt(static_cast<u32>(map.Size() - 1)) == DenseMap<u32, u32>::kNone);
    usize calls = 0;
    const usize removed = map.RemoveIf(
        [&](const u32& key, u32&)
        {
            ++calls;
            return key % 2 == 0;
        });
    CHECK(calls == 98);
    CHECK(removed == 48);  // the even keys 0..96, less 10 (removed above)
    for (u32 i = 0; i < 100; ++i)
    {
        CHECK(map.Contains(i) == (i % 2 == 1 && i != 98));
    }
    map.Clear();
    CHECK(map.Empty());
    CHECK_FALSE(map.Contains(1u));
    map.Emplace(7u, 1u);
    CHECK(map.SlotOf(7u) == 0);
}

TEST_CASE("DenseMap: String keys found by a view, move-only values", "[containers][dense_map]")
{
    DenseMap<String<HeapAllocator>, UniquePtr<u32>> map;
    for (u32 i = 0; i < 50; ++i)
    {
        char text[16];
        text[0] = 'k';
        text[1] = static_cast<char>('a' + i % 26);
        text[2] = static_cast<char>('a' + i / 26);
        CHECK(map.Emplace(StringView(text, 3), MakeUnique<u32>(i)).inserted);
    }
    CHECK(map.Contains(StringView("kaa")));
    CHECK(**map.Find(StringView("kba")) == 1);
    CHECK(map.Remove(StringView("kaa")));
    CHECK_FALSE(map.Contains(StringView("kaa")));
    CHECK(map.Size() == 49);
    DenseMap<String<HeapAllocator>, UniquePtr<u32>> moved(
        static_cast<DenseMap<String<HeapAllocator>, UniquePtr<u32>>&&>(map));
    CHECK(moved.Size() == 49);
    CHECK(map.Size() == 0);
    CHECK(moved.Emplace("new", MakeUnique<u32>(7)).inserted);  // a literal: made into a String before hashing
    CHECK(**moved.Find(StringView("new")) == 7);
}
