#include <catch2/catch_test_macros.hpp>

#include "containers/hash_map.h"
#include "mem/heap_allocator.h"
#include "mem/arena_allocator.h"
#include "containers/array.h"
#include "lang/utility.h"
#include "string/string.h"
#include "types.h"

using engine_placeholder::ArenaAllocator;
using engine_placeholder::HashMap;
using engine_placeholder::usize;

TEST_CASE("HashMap: insert/find/remove stays correct across a 1000-entry churn", "[containers][hash_map]")
{
    // 32 MiB is generous: across ~8 rehash-doubling generations (8 ->
    // ~2048 slots) both the slot array and the temporary rehash
    // snapshot abandon their old blocks in the arena (Day 6 behavior),
    // which adds up to a few hundred KiB at most for 1000 entries.
    ArenaAllocator arena(32 * 1024 * 1024);
    HashMap<usize, usize, ArenaAllocator> map(arena);

    constexpr usize kCount = 1000;
    for (usize key = 0; key < kCount; ++key)
    {
        map.Insert(key, key * 7 + 3);
    }
    REQUIRE(map.Size() == kCount);

    bool all_present_ok = true;
    for (usize key = 0; key < kCount && all_present_ok; ++key)
    {
        const usize* value = map.Find(key);
        all_present_ok = value != nullptr && *value == key * 7 + 3;
    }
    REQUIRE(all_present_ok);

    // Remove the even-valued keys.
    usize removed = 0;
    for (usize key = 0; key < kCount; key += 2)
    {
        if (map.Remove(key))
        {
            ++removed;
        }
    }
    REQUIRE(removed == kCount / 2);
    REQUIRE(map.Size() == kCount / 2);

    bool removed_are_absent = true;
    for (usize key = 0; key < kCount && removed_are_absent; key += 2)
    {
        removed_are_absent = map.Find(key) == nullptr;
    }
    REQUIRE(removed_are_absent);

    bool survivors_ok = true;
    for (usize key = 1; key < kCount && survivors_ok; key += 2)
    {
        const usize* value = map.Find(key);
        survivors_ok = value != nullptr && *value == key * 7 + 3;
    }
    REQUIRE(survivors_ok);
}

TEST_CASE("HashMap: tombstone accumulation from insert/remove churn still triggers a rehash", "[containers][hash_map]")
{
    // Insert-then-immediately-remove a distinct key each iteration:
    // Size() (== count_) stays at 0 throughout, so a NeedsGrow() that
    // only counted occupied slots would never see a reason to rehash,
    // even as every iteration leaves behind one more tombstone.
    ArenaAllocator arena(1024 * 1024);
    HashMap<usize, usize, ArenaAllocator> map(arena, 8);
    map.Insert(usize{1000}, usize{0});  // the first table is made by the first insert (container audit Day 2)
    map.Remove(usize{1000});
    usize initial_capacity = map.SlotCapacity();

    for (usize i = 0; i < 100; ++i)
    {
        map.Insert(i, i);
        map.Remove(i);
    }

    REQUIRE(map.Size() == 0);
    // The tombstones were cleared by rehashing, at the same size: nothing live needed more room.
    REQUIRE(map.TombstoneCount() * 10 < map.SlotCapacity() * 7);
    REQUIRE(map.SlotCapacity() == initial_capacity);
}

TEST_CASE("HashMap: insert/remove churn with few live keys does not grow the table", "[containers][hash_map]")
{
    engine_placeholder::HeapAllocator heap;
    HashMap<usize, usize, engine_placeholder::HeapAllocator> map(heap);
    for (usize k = 0; k < 10; ++k)
    {
        map.Insert(k, k);
    }
    // A first round of churn may grow the table once (ten live keys fill 16 slots past the threshold for doubling).
    for (usize k = 1000; k < 2000; ++k)
    {
        map.Insert(k, k);
        map.Remove(k);
    }
    const usize capacity_after_setup = map.SlotCapacity();
    CHECK(capacity_after_setup <= 32);
    // 100,000 more fresh keys inserted and removed again: the live count stays at 10, so the table must not grow.
    for (usize k = 2000; k < 102000; ++k)
    {
        map.Insert(k, k);
        map.Remove(k);
    }
    CHECK(map.Size() == 10);
    CHECK(map.SlotCapacity() == capacity_after_setup);
    for (usize k = 0; k < 10; ++k)
    {
        const usize* v = map.Find(k);
        REQUIRE(v != nullptr);
        CHECK(*v == k);
    }
}

// Phase 27 Day 474: move-only keys and values without a default constructor, lookup by a borrowed key, ForEach, and
// each key and value destroyed exactly once.

namespace
{

using engine_placeholder::HeapAllocator;
using engine_placeholder::Move;
using engine_placeholder::StringView;
using engine_placeholder::u32;
using Name = engine_placeholder::String<HeapAllocator>;

Name MakeName(u32 i)
{
    engine_placeholder::Array<char, 16> text{};
    usize n = 0;
    text[n++] = 'n';
    do
    {
        text[n++] = static_cast<char>('0' + i % 10);
        i /= 10;
    } while (i != 0);
    return Name(HeapAllocator::Default(), StringView(text.Data(), n));
}

// Counts live objects; move-only, no default constructor.
struct CountedValue
{
    static inline int live = 0;
    explicit CountedValue(int v) : value(v) { ++live; }
    CountedValue(CountedValue&& other) noexcept : value(other.value) { ++live; }
    CountedValue& operator=(CountedValue&& other) noexcept
    {
        value = other.value;
        return *this;
    }
    CountedValue(const CountedValue&) = delete;
    CountedValue& operator=(const CountedValue&) = delete;
    ~CountedValue() { --live; }
    int value;
};

}  // namespace

TEST_CASE("HashMap: String keys, found by a StringView, through growth and removal", "[containers][hash_map]")
{
    HashMap<Name, CountedValue, HeapAllocator> map(HeapAllocator::Default());
    CountedValue::live = 0;
    for (u32 i = 0; i < 500; ++i)
    {
        map.Insert(MakeName(i), CountedValue(static_cast<int>(i)));
    }
    REQUIRE(map.Size() == 500);
    CHECK(CountedValue::live ==
          500);  // the moved-from temporaries are gone; rehashes destroyed the old table's entries
    CHECK(map.SlotCapacity() > 500);
    for (u32 i = 0; i < 500; ++i)
    {
        const Name name = MakeName(i);
        const CountedValue* by_view = map.Find(name.View());
        const CountedValue* by_key = map.Find(name);
        REQUIRE(by_view != nullptr);
        CHECK(by_view == by_key);
        CHECK(by_view->value == static_cast<int>(i));
    }
    CHECK(map.Find(StringView("absent")) == nullptr);

    map.Insert(MakeName(7), CountedValue(70));  // replaces the value, keeps one entry
    CHECK(map.Size() == 500);
    CHECK(map.Find(StringView("n7"))->value == 70);
    CHECK(CountedValue::live == 500);

    for (u32 i = 0; i < 500; i += 2)
    {
        CHECK(map.Remove(MakeName(i).View()));
    }
    CHECK_FALSE(map.Remove(StringView("n0")));
    CHECK(map.Size() == 250);
    CHECK(CountedValue::live == 250);  // removal destroys the value at once

    int sum = 0;
    usize visited = 0;
    map.ForEach(
        [&](const Name& key, CountedValue& value)
        {
            CHECK(key.View().Length() > 1);
            sum += value.value;
            ++visited;
        });
    CHECK(visited == 250);
    int expected = 0;
    for (int i = 1; i < 500; i += 2)
    {
        expected += i == 7 ? 70 : i;
    }
    CHECK(sum == expected);
}

TEST_CASE("HashMap: a moved map keeps its entries and the moved-from map can be used again", "[containers][hash_map]")
{
    CountedValue::live = 0;
    {
        HashMap<Name, CountedValue, HeapAllocator> a(HeapAllocator::Default());
        a.Insert(MakeName(1), CountedValue(1));
        a.Insert(MakeName(2), CountedValue(2));
        HashMap<Name, CountedValue, HeapAllocator> b(Move(a));
        CHECK(b.Size() == 2);
        CHECK(b.Find(StringView("n2"))->value == 2);
        CHECK(a.Size() == 0);
        CHECK(a.Find(StringView("n1")) == nullptr);
        a.Insert(MakeName(3), CountedValue(3));  // a moved-from map makes a table on its next insert
        CHECK(a.Find(StringView("n3"))->value == 3);
        a = Move(b);  // destroys n3, takes n1 and n2
        CHECK(a.Size() == 2);
        CHECK(CountedValue::live == 2);
        const HashMap<Name, CountedValue, HeapAllocator>& view = a;
        int sum = 0;
        view.ForEach([&](const Name&, const CountedValue& value) { sum += value.value; });
        CHECK(sum == 3);
    }
    CHECK(CountedValue::live == 0);  // the map's destructor destroys what it holds
}

TEST_CASE("HashMap: the StringView hash agrees with the String hash", "[containers][hash_map]")
{
    using engine_placeholder::Hash;
    const Name name(HeapAllocator::Default(), StringView("sk\xC3\xA5l.lua"));
    CHECK(Hash<Name>{}(name) == Hash<StringView>{}(StringView("sk\xC3\xA5l.lua")));
    CHECK(Hash<StringView>{}(StringView("a")) != Hash<StringView>{}(StringView("b")));
    CHECK(Hash<StringView>{}(StringView("")) == Hash<StringView>{}(StringView()));
}

// Container audit series Day 2: lazy first table, power-of-two capacity, Clear, Reserve, Contains, Emplace, range-for,
// and keys of another type converted before they are hashed.

TEST_CASE("HashMap: no table until the first insert, then a power of two", "[containers][hash_map]")
{
    HashMap<u32, u32, HeapAllocator> map(HeapAllocator::Default(), 100);
    CHECK(map.SlotCapacity() == 0);
    CHECK(map.Find(5u) == nullptr);
    CHECK_FALSE(map.Remove(5u));
    CHECK_FALSE(map.Contains(5u));
    usize visited = 0;
    for (auto& entry : map)
    {
        visited += entry.value;
    }
    CHECK(visited == 0);
    map.Insert(5u, 50u);
    CHECK(map.SlotCapacity() == 128);  // 100 rounded up
    for (u32 i = 0; i < 1000; ++i)
    {
        map.Insert(i, i);
    }
    const usize cap = map.SlotCapacity();
    CHECK((cap & (cap - 1)) == 0);
    CHECK(map.Size() == 1000);
}

TEST_CASE("HashMap: Clear keeps the table, Reserve grows it once", "[containers][hash_map]")
{
    CountedValue::live = 0;
    HashMap<Name, CountedValue, HeapAllocator> map(HeapAllocator::Default());
    map.Reserve(1000);
    const usize reserved = map.SlotCapacity();
    CHECK(reserved >= 1000 * 10 / 7);
    for (u32 i = 0; i < 1000; ++i)
    {
        map.Insert(MakeName(i), CountedValue(static_cast<int>(i)));
    }
    CHECK(map.SlotCapacity() == reserved);  // no rehash on the way
    for (u32 i = 0; i < 1000; i += 3)
    {
        map.Remove(MakeName(i).View());
    }
    CHECK(map.TombstoneCount() > 0);
    map.Clear();
    CHECK(map.Size() == 0);
    CHECK(map.Empty());
    CHECK(map.TombstoneCount() == 0);
    CHECK(map.SlotCapacity() == reserved);
    CHECK(CountedValue::live == 0);
    CHECK(map.Find(StringView("n1")) == nullptr);
    map.Insert(MakeName(1), CountedValue(1));
    CHECK(map.Find(StringView("n1"))->value == 1);
}

TEST_CASE("HashMap: Emplace makes the value only when the key is absent", "[containers][hash_map]")
{
    CountedValue::live = 0;
    HashMap<Name, CountedValue, HeapAllocator> map(HeapAllocator::Default());
    auto first = map.Emplace(StringView("n1"), 10);  // a view: converted to a Name only when absent
    CHECK(first.inserted);
    CHECK(first.value->value == 10);
    auto again = map.Emplace(StringView("n1"), 99);
    CHECK_FALSE(again.inserted);
    CHECK(again.value->value == 10);  // left as it was
    CHECK(CountedValue::live == 1);
    for (u32 i = 2; i < 300; ++i)
    {
        CHECK(map.Emplace(MakeName(i), static_cast<int>(i)).inserted);
    }
    CHECK(map.Size() == 299);
    CHECK(map.Contains(MakeName(299)));  // MakeName writes the digits in reverse ("n992")
    CHECK(map.Contains(MakeName(2)));
    CHECK_FALSE(map.Contains(MakeName(300)));
}

TEST_CASE("HashMap: a key of another type is converted before it is hashed", "[containers][hash_map]")
{
    // A C string hashed as a pointer, or an int hashed as an i32 in a u64 map, would land in the wrong slot and could
    // not be found by the key type.
    HashMap<Name, u32, HeapAllocator> names(HeapAllocator::Default());
    names.Insert("n5", 5u);
    CHECK(names.Find(StringView("n5")) != nullptr);
    CHECK(names.Emplace("n6", 6u).inserted);
    CHECK(names.Find(MakeName(6)) != nullptr);
    HashMap<engine_placeholder::u64, u32, HeapAllocator> wide(HeapAllocator::Default());
    for (int i = 0; i < 100; ++i)
    {
        wide.Insert(i, static_cast<u32>(i));
    }
    for (engine_placeholder::u64 i = 0; i < 100; ++i)
    {
        REQUIRE(wide.Find(i) != nullptr);
        CHECK(*wide.Find(i) == i);
    }
    // A negative int hashed as an i32 differs from the same value hashed as an i64.
    HashMap<engine_placeholder::i64, u32, HeapAllocator> signed_keys(HeapAllocator::Default());
    for (int i = -50; i < 0; ++i)
    {
        signed_keys.Insert(i, static_cast<u32>(-i));
    }
    for (engine_placeholder::i64 i = -50; i < 0; ++i)
    {
        REQUIRE(signed_keys.Find(i) != nullptr);
        CHECK(*signed_keys.Find(i) == static_cast<u32>(-i));
    }
}

TEST_CASE("HashMap: a range-for visits every entry once, through churn", "[containers][hash_map]")
{
    HashMap<u32, u32, HeapAllocator> map(HeapAllocator::Default());
    for (u32 i = 0; i < 2000; ++i)
    {
        map.Insert(i, i * 2);
    }
    for (u32 i = 0; i < 2000; i += 2)
    {
        map.Remove(i);
    }
    engine_placeholder::Vector<u32> seen;
    seen.Resize(2000);
    for (auto& [key, value] : map)
    {
        CHECK(value == key * 2);
        ++seen[key];
        value += 1;  // values may be written through the loop
    }
    for (u32 i = 0; i < 2000; ++i)
    {
        CHECK(seen[i] == (i % 2 == 0 ? 0u : 1u));
    }
    const HashMap<u32, u32, HeapAllocator>& view = map;
    usize count = 0;
    for (const auto& entry : view)
    {
        CHECK(entry.value == entry.key * 2 + 1);
        ++count;
    }
    CHECK(count == 1000);

    HashMap<u32, u32, HeapAllocator> moved(Move(map));
    count = 0;
    for (auto& entry : map)  // moved-from: empty
    {
        count += entry.value;
    }
    CHECK(count == 0);
    for (auto& entry : moved)
    {
        count += entry.key > 0 ? 1 : 0;
    }
    CHECK(count == 1000);
}

TEST_CASE("HashMap: replacing a value at the load threshold does not grow the table", "[containers][hash_map]")
{
    HashMap<u32, u32, HeapAllocator> map(HeapAllocator::Default(), 16);
    u32 n = 0;
    while ((map.Size() + 2) * 10 < 16 * 7)  // fill to one below the threshold of 16 slots
    {
        map.Insert(n, n);
        ++n;
    }
    map.Insert(n, n);  // now at the threshold: the next new key would grow
    ++n;
    const usize cap = map.SlotCapacity();
    REQUIRE(cap == 16);
    for (u32 i = 0; i < n; ++i)
    {
        map.Insert(i, i + 100);  // replacements
        CHECK_FALSE(map.Emplace(i, 0u).inserted);
    }
    CHECK(map.SlotCapacity() == cap);
    CHECK(*map.Find(0u) == 100);
    map.Insert(n, n);  // a new key grows it
    CHECK(map.SlotCapacity() == 32);
}
