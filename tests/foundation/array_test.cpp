#include <catch2/catch_test_macros.hpp>

#include "containers/array.h"
#include "log/log.h"
#include "mem/arena_allocator.h"
#include "os/os.h"
#include "types.h"

using engine_placeholder::ArenaAllocator;
using engine_placeholder::Array;
using engine_placeholder::SetExitSinkForTesting;
using engine_placeholder::SetWriteSinkForTesting;
using engine_placeholder::usize;
using engine_placeholder::os::Write;

TEST_CASE("Array: 100k pushes stay correct at every resize boundary and at the end", "[containers][array]") {
    // 8 MiB is generous: growth 4,8,16,...,131072 abandons each prior
    // block in the arena, totalling well under 1 MiB for usize elements.
    ArenaAllocator arena(8 * 1024 * 1024);
    Array<usize, ArenaAllocator> arr(arena);

    constexpr usize kCount = 100000;
    bool boundary_ok = true;
    usize prev_capacity = arr.Capacity();
    for (usize i = 0; i < kCount && boundary_ok; ++i) {
        arr.Push(i);
        if (arr.Capacity() != prev_capacity) {
            for (usize j = 0; j <= i; ++j) {
                if (arr[j] != j) {
                    boundary_ok = false;
                    break;
                }
            }
            prev_capacity = arr.Capacity();
        }
    }
    REQUIRE(boundary_ok);

    bool final_ok = arr.Size() == kCount;
    for (usize i = 0; i < kCount && final_ok; ++i) {
        if (arr[i] != i) {
            final_ok = false;
        }
    }
    REQUIRE(final_ok);
}

namespace {

struct TrackedValue {
    static usize constructions;
    static usize destructions;
    static usize moves;

    usize value;

    explicit TrackedValue(usize v) : value(v) {
        ++constructions;
    }
    TrackedValue(const TrackedValue& other) : value(other.value) {
        ++constructions;
    }
    TrackedValue(TrackedValue&& other) noexcept : value(other.value) {
        other.value = 0;
        ++constructions;
        ++moves;
    }
    TrackedValue& operator=(const TrackedValue& other) {
        value = other.value;
        return *this;
    }
    TrackedValue& operator=(TrackedValue&& other) noexcept {
        value = other.value;
        other.value = 0;
        ++moves;
        return *this;
    }
    ~TrackedValue() {
        ++destructions;
    }
};

usize TrackedValue::constructions = 0;
usize TrackedValue::destructions = 0;
usize TrackedValue::moves = 0;

} // namespace

TEST_CASE("Array: non-trivial element type resizes via move construction, no leaks", "[containers][array]") {
    TrackedValue::constructions = 0;
    TrackedValue::destructions = 0;
    TrackedValue::moves = 0;

    {
        ArenaAllocator arena(1024 * 1024);
        Array<TrackedValue, ArenaAllocator> arr(arena);

        // Fill the initial capacity (4) exactly.
        for (usize i = 0; i < 4; ++i) {
            arr.Push(TrackedValue(i));
        }
        usize moves_before_resize = TrackedValue::moves;

        // Fifth push forces a resize (4 -> 8): Reserve() move-constructs
        // the 4 existing elements, plus the usual one move for the push
        // itself.
        arr.Push(TrackedValue(4));
        usize moves_from_resize = TrackedValue::moves - moves_before_resize;
        REQUIRE(moves_from_resize == 5);

        bool contents_ok = true;
        for (usize i = 0; i < 5; ++i) {
            if (arr[i].value != i) {
                contents_ok = false;
                break;
            }
        }
        REQUIRE(contents_ok);

        for (usize i = 5; i < 50; ++i) {
            arr.Push(TrackedValue(i));
        }
        bool final_contents_ok = true;
        for (usize i = 0; i < 50; ++i) {
            if (arr[i].value != i) {
                final_contents_ok = false;
                break;
            }
        }
        REQUIRE(final_contents_ok);
    } // arr destructor runs here, destroying all 50 live elements.

    REQUIRE(TrackedValue::constructions == TrackedValue::destructions);
}

TEST_CASE("Array: Insert/Remove at front/middle/end produce expected sequences", "[containers][array]") {
    ArenaAllocator arena(64 * 1024);
    Array<usize, ArenaAllocator> arr(arena);
    for (usize i = 0; i < 5; ++i) {
        arr.Push(i); // [0,1,2,3,4]
    }

    arr.Insert(2, 99); // [0,1,99,2,3,4]
    const usize expected_after_insert[] = {0, 1, 99, 2, 3, 4};
    bool insert_ok = arr.Size() == 6;
    for (usize i = 0; i < 6 && insert_ok; ++i) {
        insert_ok = arr[i] == expected_after_insert[i];
    }
    REQUIRE(insert_ok);

    arr.Remove(0); // [1,99,2,3,4]
    const usize expected_after_remove[] = {1, 99, 2, 3, 4};
    bool remove_ok = arr.Size() == 5;
    for (usize i = 0; i < 5 && remove_ok; ++i) {
        remove_ok = arr[i] == expected_after_remove[i];
    }
    REQUIRE(remove_ok);

    arr.Insert(arr.Size(), 42); // [1,99,2,3,4,42]
    REQUIRE((arr.Size() == 6 && arr[5] == 42));

    arr.Remove(arr.Size() - 1); // [1,99,2,3,4]
    REQUIRE((arr.Size() == 5 && arr[4] == 4));
}

namespace {

// Exceptions are enabled for this test target (unlike the `foundation`
// production library — see /tests/foundation/CMakeLists.txt), so the
// exit sink can unwind the stack by throwing when an ENGINE_ASSERT
// fires, instead of aborting the whole test process.
struct AssertTriggered {
    int code;
};

[[noreturn]] void ThrowOnExit(int code) {
    throw AssertTriggered{code};
}

long DiscardWrites(int fd, const void* buf, usize len) {
    (void)fd;
    (void)buf;
    return static_cast<long>(len);
}

// Runs `fn` with the assert-failure exit path redirected to a throw;
// returns true if `fn` triggered an ENGINE_ASSERT failure.
template <typename Fn>
bool TriggersAssert(Fn&& fn) {
    SetWriteSinkForTesting(&DiscardWrites);
    SetExitSinkForTesting(&ThrowOnExit);
    bool triggered = false;
    try {
        fn();
    } catch (const AssertTriggered&) {
        triggered = true;
    }
    SetWriteSinkForTesting(&Write);
    SetExitSinkForTesting(&engine_placeholder::os::Exit);
    return triggered;
}

} // namespace

TEST_CASE("Array::Push asserts instead of placement-new'ing into a null pointer on allocator OOM",
          "[containers][array][assert]") {
    // 32-byte arena fits exactly the first Reserve() (capacity 4 *
    // sizeof(usize) == 32 bytes) but not the next doubling (capacity 8,
    // 64 bytes) — the 5th push's growth must hit the OOM path.
    ArenaAllocator arena(32);
    Array<usize, ArenaAllocator> arr(arena);

    bool asserted = TriggersAssert([&] {
        for (usize i = 0; i < 5; ++i) {
            arr.Push(i);
        }
    });
    REQUIRE(asserted);
}

TEST_CASE("Array::Pop asserts on an empty array instead of underflowing size_", "[containers][array][assert]") {
    ArenaAllocator arena(4096);
    Array<usize, ArenaAllocator> arr(arena);

    bool asserted = TriggersAssert([&] { arr.Pop(); });
    REQUIRE(asserted);
}
