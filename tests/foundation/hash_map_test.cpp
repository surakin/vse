#include <catch2/catch_test_macros.hpp>

#include "containers/hash_map.h"
#include "mem/heap_allocator.h"
#include "mem/arena_allocator.h"
#include "types.h"

using engine_placeholder::ArenaAllocator;
using engine_placeholder::HashMap;
using engine_placeholder::usize;

TEST_CASE("HashMap: insert/find/remove stays correct across a 1000-entry churn", "[containers][hash_map]") {
    // 32 MiB is generous: across ~8 rehash-doubling generations (8 ->
    // ~2048 slots) both the slot array and the temporary rehash
    // snapshot abandon their old blocks in the arena (Day 6 behavior),
    // which adds up to a few hundred KiB at most for 1000 entries.
    ArenaAllocator arena(32 * 1024 * 1024);
    HashMap<usize, usize, ArenaAllocator> map(arena);

    constexpr usize kCount = 1000;
    for (usize key = 0; key < kCount; ++key) {
        map.Insert(key, key * 7 + 3);
    }
    REQUIRE(map.Size() == kCount);

    bool all_present_ok = true;
    for (usize key = 0; key < kCount && all_present_ok; ++key) {
        const usize* value = map.Find(key);
        all_present_ok = value != nullptr && *value == key * 7 + 3;
    }
    REQUIRE(all_present_ok);

    // Remove the even-valued keys.
    usize removed = 0;
    for (usize key = 0; key < kCount; key += 2) {
        if (map.Remove(key)) {
            ++removed;
        }
    }
    REQUIRE(removed == kCount / 2);
    REQUIRE(map.Size() == kCount / 2);

    bool removed_are_absent = true;
    for (usize key = 0; key < kCount && removed_are_absent; key += 2) {
        removed_are_absent = map.Find(key) == nullptr;
    }
    REQUIRE(removed_are_absent);

    bool survivors_ok = true;
    for (usize key = 1; key < kCount && survivors_ok; key += 2) {
        const usize* value = map.Find(key);
        survivors_ok = value != nullptr && *value == key * 7 + 3;
    }
    REQUIRE(survivors_ok);
}

TEST_CASE("HashMap: tombstone accumulation from insert/remove churn still triggers a rehash",
          "[containers][hash_map]") {
    // Insert-then-immediately-remove a distinct key each iteration:
    // Size() (== count_) stays at 0 throughout, so a NeedsGrow() that
    // only counted occupied slots would never see a reason to rehash,
    // even as every iteration leaves behind one more tombstone.
    ArenaAllocator arena(1024 * 1024);
    HashMap<usize, usize, ArenaAllocator> map(arena, 8);
    usize initial_capacity = map.SlotCapacity();

    for (usize i = 0; i < 100; ++i) {
        map.Insert(i, i);
        map.Remove(i);
    }

    REQUIRE(map.Size() == 0);
    // The tombstones were cleared by rehashing, at the same size: nothing live needed more room.
    REQUIRE(map.TombstoneCount() * 10 < map.SlotCapacity() * 7);
    REQUIRE(map.SlotCapacity() == initial_capacity);
}

TEST_CASE("HashMap: insert/remove churn with few live keys does not grow the table", "[containers][hash_map]") {
    engine_placeholder::HeapAllocator heap;
    HashMap<usize, usize, engine_placeholder::HeapAllocator> map(heap);
    for (usize k = 0; k < 10; ++k) {
        map.Insert(k, k);
    }
    // A first round of churn may grow the table once (ten live keys fill 16 slots past the threshold for doubling).
    for (usize k = 1000; k < 2000; ++k) {
        map.Insert(k, k);
        map.Remove(k);
    }
    const usize capacity_after_setup = map.SlotCapacity();
    CHECK(capacity_after_setup <= 32);
    // 100,000 more fresh keys inserted and removed again: the live count stays at 10, so the table must not grow.
    for (usize k = 2000; k < 102000; ++k) {
        map.Insert(k, k);
        map.Remove(k);
    }
    CHECK(map.Size() == 10);
    CHECK(map.SlotCapacity() == capacity_after_setup);
    for (usize k = 0; k < 10; ++k) {
        const usize* v = map.Find(k);
        REQUIRE(v != nullptr);
        CHECK(*v == k);
    }
}
