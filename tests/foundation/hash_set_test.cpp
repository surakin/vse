// Container audit series Day 2: HashSet, the HashMap table with no value.
#include <catch2/catch_test_macros.hpp>

#include "containers/hash_set.h"
#include "containers/vector.h"
#include "lang/utility.h"
#include "mem/heap_allocator.h"
#include "string/string.h"
#include "types.h"

using namespace vse;

namespace
{

using Name = String<HeapAllocator>;

}  // namespace

TEST_CASE("HashSet: insert, contains and remove through growth and churn", "[containers][hash_set]")
{
    HashSet<u64, HeapAllocator> set(HeapAllocator::Default());
    CHECK(set.SlotCapacity() == 0);
    for (u64 i = 0; i < 5000; ++i)
    {
        CHECK(set.Insert(i * 7));
    }
    CHECK_FALSE(set.Insert(u64{14}));  // already there
    CHECK(set.Size() == 5000);
    for (u64 i = 0; i < 5000; ++i)
    {
        CHECK(set.Contains(i * 7));
        CHECK_FALSE(set.Contains(i * 7 + 1));
    }
    for (u64 i = 0; i < 5000; i += 2)
    {
        CHECK(set.Remove(i * 7));
    }
    CHECK_FALSE(set.Remove(u64{0}));
    CHECK(set.Size() == 2500);
    // Insert/remove churn with few live keys does not grow the table.
    const usize cap = set.SlotCapacity();
    for (u64 i = 0; i < 100000; ++i)
    {
        set.Insert(i + 1000000);
        set.Remove(i + 1000000);
    }
    CHECK(set.SlotCapacity() == cap);
    CHECK(set.Size() == 2500);
}

TEST_CASE("HashSet: String keys found by a view, range-for, Clear", "[containers][hash_set]")
{
    HashSet<Name, HeapAllocator> set(HeapAllocator::Default());
    set.Reserve(64);
    const usize reserved = set.SlotCapacity();
    CHECK(set.Insert(Name(HeapAllocator::Default(), StringView("albedo"))));
    CHECK(set.Insert("normal"));  // a C string becomes a Name before it is hashed
    CHECK_FALSE(set.Insert(StringView("albedo")));
    CHECK(set.Contains(StringView("normal")));
    CHECK_FALSE(set.Contains(StringView("rough")));
    usize length = 0;
    for (const Name& key : set)
    {
        length += key.Length();
    }
    CHECK(length == 12);
    usize visited = 0;
    set.ForEach([&](const Name&) { ++visited; });
    CHECK(visited == 2);
    HashSet<Name, HeapAllocator> moved(Move(set));
    CHECK(moved.Size() == 2);
    CHECK(set.Size() == 0);
    moved.Clear();
    CHECK(moved.Empty());
    CHECK(moved.SlotCapacity() == reserved);
}
