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
