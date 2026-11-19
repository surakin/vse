#include <catch2/catch_test_macros.hpp>

#include "containers/array.h"
#include "containers/vector.h"
#include "log/log.h"
#include "mem/arena_allocator.h"
#include "mem/heap_allocator.h"
#include "os/os.h"
#include "types.h"
#include "lang/utility.h"

using engine_placeholder::ArenaAllocator;
using engine_placeholder::AssertAction;
using engine_placeholder::AssertInfo;
using engine_placeholder::SetAssertHandler;
using engine_placeholder::usize;
using engine_placeholder::Vector;

TEST_CASE("Vector: 100k pushes stay correct at every resize boundary and at the end", "[containers][vector_container]")
{
    // 8 MiB is generous: growth 4,8,16,...,131072 abandons each prior
    // block in the arena, totalling well under 1 MiB for usize elements.
    ArenaAllocator arena(8 * 1024 * 1024);
    Vector<usize, ArenaAllocator> arr(arena);

    constexpr usize kCount = 100000;
    bool boundary_ok = true;
    usize prev_capacity = arr.Capacity();
    for (usize i = 0; i < kCount && boundary_ok; ++i)
    {
        arr.Push(i);
        if (arr.Capacity() != prev_capacity)
        {
            for (usize j = 0; j <= i; ++j)
            {
                if (arr[j] != j)
                {
                    boundary_ok = false;
                    break;
                }
            }
            prev_capacity = arr.Capacity();
        }
    }
    REQUIRE(boundary_ok);

    bool final_ok = arr.Size() == kCount;
    for (usize i = 0; i < kCount && final_ok; ++i)
    {
        if (arr[i] != i)
        {
            final_ok = false;
        }
    }
    REQUIRE(final_ok);
}

namespace
{

struct TrackedValue
{
    static usize constructions;
    static usize destructions;
    static usize moves;

    usize value;

    explicit TrackedValue(usize v) : value(v) { ++constructions; }
    TrackedValue(const TrackedValue& other) : value(other.value) { ++constructions; }
    TrackedValue(TrackedValue&& other) noexcept : value(other.value)
    {
        other.value = 0;
        ++constructions;
        ++moves;
    }
    TrackedValue& operator=(const TrackedValue& other) = default;
    TrackedValue& operator=(TrackedValue&& other) noexcept
    {
        value = other.value;
        other.value = 0;
        ++moves;
        return *this;
    }
    ~TrackedValue() { ++destructions; }
};

usize TrackedValue::constructions = 0;
usize TrackedValue::destructions = 0;
usize TrackedValue::moves = 0;

}  // namespace

TEST_CASE("Vector: non-trivial element type resizes via move construction, no leaks", "[containers][vector_container]")
{
    TrackedValue::constructions = 0;
    TrackedValue::destructions = 0;
    TrackedValue::moves = 0;

    {
        ArenaAllocator arena(1024 * 1024);
        Vector<TrackedValue, ArenaAllocator> arr(arena);

        // Fill the initial capacity (4) exactly.
        for (usize i = 0; i < 4; ++i)
        {
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
        for (usize i = 0; i < 5; ++i)
        {
            if (arr[i].value != i)
            {
                contents_ok = false;
                break;
            }
        }
        REQUIRE(contents_ok);

        for (usize i = 5; i < 50; ++i)
        {
            arr.Push(TrackedValue(i));
        }
        bool final_contents_ok = true;
        for (usize i = 0; i < 50; ++i)
        {
            if (arr[i].value != i)
            {
                final_contents_ok = false;
                break;
            }
        }
        REQUIRE(final_contents_ok);
    }  // arr destructor runs here, destroying all 50 live elements.

    REQUIRE(TrackedValue::constructions == TrackedValue::destructions);
}

TEST_CASE("Vector: Insert/Remove at front/middle/end produce expected sequences", "[containers][vector_container]")
{
    ArenaAllocator arena(64 * 1024);
    Vector<usize, ArenaAllocator> arr(arena);
    for (usize i = 0; i < 5; ++i)
    {
        arr.Push(i);  // [0,1,2,3,4]
    }

    arr.Insert(2, 99);  // [0,1,99,2,3,4]
    const usize expected_after_insert[] = {0, 1, 99, 2, 3, 4};
    bool insert_ok = arr.Size() == 6;
    for (usize i = 0; i < 6 && insert_ok; ++i)
    {
        insert_ok = arr[i] == expected_after_insert[i];
    }
    REQUIRE(insert_ok);

    arr.Remove(0);  // [1,99,2,3,4]
    const usize expected_after_remove[] = {1, 99, 2, 3, 4};
    bool remove_ok = arr.Size() == 5;
    for (usize i = 0; i < 5 && remove_ok; ++i)
    {
        remove_ok = arr[i] == expected_after_remove[i];
    }
    REQUIRE(remove_ok);

    arr.Insert(arr.Size(), 42);  // [1,99,2,3,4,42]
    REQUIRE((arr.Size() == 6 && arr[5] == 42));

    arr.Remove(arr.Size() - 1);  // [1,99,2,3,4]
    REQUIRE((arr.Size() == 5 && arr[4] == 4));
}

namespace
{

// Exceptions are enabled for this test target (unlike the `foundation`
// production library — see /tests/foundation/CMakeLists.txt), so the
// assert handler can unwind the stack by throwing when an ENGINE_ASSERT
// fires, instead of aborting the whole test process.
struct AssertTriggered
{
    const char* message;
};

AssertAction ThrowFromHandler(const AssertInfo& info)
{
    throw AssertTriggered{info.message};
}

// Runs `fn` with assert failures redirected to a throw;
// returns true if `fn` triggered an ENGINE_ASSERT failure.
template <typename Fn> bool TriggersAssert(Fn&& fn)
{
    const engine_placeholder::AssertHandler previous = SetAssertHandler(&ThrowFromHandler);
    bool triggered = false;
    try
    {
        fn();
    }
    catch (const AssertTriggered&)
    {
        triggered = true;
    }
    SetAssertHandler(previous);
    return triggered;
}

}  // namespace

TEST_CASE("Vector::Push asserts instead of placement-new'ing into a null pointer on allocator OOM",
          "[containers][vector_container][assert]")
{
    // 32-byte arena fits exactly the first Reserve() (capacity 4 *
    // sizeof(usize) == 32 bytes) but not the next doubling (capacity 8,
    // 64 bytes) — the 5th push's growth must hit the OOM path.
    ArenaAllocator arena(32);
    Vector<usize, ArenaAllocator> arr(arena);

    bool asserted = TriggersAssert(
        [&]
        {
            for (usize i = 0; i < 5; ++i)
            {
                arr.Push(i);
            }
        });
    REQUIRE(asserted);
}

TEST_CASE("Vector::Pop asserts on an empty array instead of underflowing size_",
          "[containers][vector_container][assert]")
{
    ArenaAllocator arena(4096);
    Vector<usize, ArenaAllocator> arr(arena);

    bool asserted = TriggersAssert([&] { arr.Pop(); });
    REQUIRE(asserted);
}

TEST_CASE("Vector::Resize asserts when the heap cannot supply the size, TryResize does not",
          "[containers][vector_container][assert]")
{
    Vector<usize> v;
    REQUIRE(TriggersAssert([&] { v.Resize(static_cast<usize>(1) << 56); }));
    REQUIRE_FALSE(v.TryResize(static_cast<usize>(1) << 56));
}

namespace
{

// Counts blocks handed out and returned, to prove the vector frees what it allocates.
struct CountingHeap
{
    usize allocs = 0;
    usize frees = 0;
    usize live_bytes = 0;
    void* Alloc(usize size, usize align)
    {
        ++allocs;
        live_bytes += size;
        return engine_placeholder::HeapAllocator::Default().Alloc(size, align);
    }
    void Free(void* ptr, usize size, usize align)
    {
        ++frees;
        live_bytes -= size;
        engine_placeholder::HeapAllocator::Default().Free(ptr, size, align);
    }
};

struct NonMovable
{
    NonMovable() = default;
    NonMovable(const NonMovable&) = delete;
    NonMovable& operator=(const NonMovable&) = delete;
    int value = 7;
};

}  // namespace

TEST_CASE("Vector: a Free-capable allocator gets every block back on growth, move-assign and destruction",
          "[containers][vector_container]")
{
    CountingHeap heap;
    {
        Vector<usize, CountingHeap> v(heap);
        for (usize i = 0; i < 1000; ++i)
        {
            v.Push(i);
        }
        REQUIRE(heap.allocs == heap.frees + 1);  // only the live block is outstanding
        REQUIRE(heap.live_bytes == v.Capacity() * sizeof(usize));

        Vector<usize, CountingHeap> other(heap);
        other.Push(1);
        v = engine_placeholder::Move(other);  // the old 1000-element block must be released here
        REQUIRE(heap.allocs == heap.frees + 1);
        REQUIRE(v.Size() == 1);
    }
    REQUIRE(heap.allocs == heap.frees);
    REQUIRE(heap.live_bytes == 0);
}

TEST_CASE("Vector: the default vector owns heap storage; Resize zero-initializes, shrinks and Clear keeps storage",
          "[containers][vector_container]")
{
    Vector<engine_placeholder::u32> v;
    REQUIRE(v.Data() == nullptr);
    REQUIRE(v.TryResize(10));
    REQUIRE(v.Size() == 10);
    for (usize i = 0; i < 10; ++i)
    {
        REQUIRE(v[i] == 0u);
        v[i] = static_cast<engine_placeholder::u32>(i + 1);
    }
    v.Resize(12);  // keeps old values, zeroes new ones
    REQUIRE(v[9] == 10u);
    REQUIRE(v[11] == 0u);
    v.Resize(3);
    v.Resize(5);  // regrowing after a shrink re-zeroes the revived slots
    REQUIRE(v[3] == 0u);
    const usize capacity = v.Capacity();
    v.Clear();
    REQUIRE(v.Size() == 0);
    REQUIRE(v.Capacity() == capacity);
    v.Reset();
    REQUIRE(v.Capacity() == 0);
    REQUIRE(v.Data() == nullptr);
}

TEST_CASE("Vector: TryResize and TryReserve report an impossible size without changing the vector",
          "[containers][vector_container]")
{
    Vector<engine_placeholder::u64> v;
    v.Resize(4);
    v[0] = 99;
    REQUIRE_FALSE(v.TryResize(static_cast<usize>(-1) / 2));
    REQUIRE_FALSE(v.TryReserve(static_cast<usize>(-1) / sizeof(engine_placeholder::u64) + 1));
    REQUIRE_FALSE(v.TryResize(static_cast<usize>(1) << 56));  // the heap refuses
    REQUIRE(v.Size() == 4);
    REQUIRE(v[0] == 99u);
}

TEST_CASE("Vector: element types that cannot move are supported by a single Resize", "[containers][vector_container]")
{
    Vector<NonMovable> v;
    v.Resize(5);
    REQUIRE(v.Size() == 5);
    REQUIRE(v[4].value == 7);
    v.Resize(2);
    REQUIRE(v.Size() == 2);
    REQUIRE(v.TryResize(5));        // within the capacity: fine
    REQUIRE_FALSE(v.TryResize(6));  // would need a new block, and the elements cannot move: reported, not aborted
    REQUIRE(v.Size() == 5);
}

TEST_CASE("Vector: Resize on a vector holding elements grows geometrically", "[containers][vector_container]")
{
    Vector<usize> v;
    v.Resize(10);
    usize reallocations = 0;
    usize capacity = v.Capacity();
    for (usize n = 11; n <= 1000; ++n)
    {
        v.Resize(n);
        if (v.Capacity() != capacity)
        {
            ++reallocations;
            capacity = v.Capacity();
        }
    }
    REQUIRE(reallocations <= 8);  // doubling from 10 reaches 1000 in 7 steps; exact growth would reallocate 990 times
}

TEST_CASE("Vector: TryResizeUninitialized sizes a byte buffer without touching it", "[containers][vector_container]")
{
    Vector<engine_placeholder::byte> v;
    REQUIRE(v.TryResizeUninitialized(64));
    REQUIRE(v.Size() == 64);
    v[63] = 9;
    REQUIRE(v.TryResizeUninitialized(8));
    REQUIRE(v.Size() == 8);
    REQUIRE_FALSE(v.TryResizeUninitialized(static_cast<usize>(1) << 56));
}

namespace
{
// Records its value; its destructor overwrites it, so reading a destroyed element shows up as -1.
struct VectorAliasProbe
{
    int value = 0;
    explicit VectorAliasProbe(int v) : value(v) {}
    VectorAliasProbe(const VectorAliasProbe& other) = default;
    VectorAliasProbe(VectorAliasProbe&& other) noexcept : value(other.value) {}
    VectorAliasProbe& operator=(const VectorAliasProbe& other) = default;
    VectorAliasProbe& operator=(VectorAliasProbe&& other) noexcept
    {
        value = other.value;
        return *this;
    }
    ~VectorAliasProbe() { value = -1; }
};
}  // namespace

TEST_CASE("Vector: pushing or inserting one of its own elements works when the vector must grow",
          "[containers][vector_container]")
{
    Vector<VectorAliasProbe> v;
    v.Push(VectorAliasProbe(7));
    while (v.Size() < v.Capacity())
    {
        v.Push(VectorAliasProbe(static_cast<int>(v.Size()) + 100));
    }
    // Full: the next push grows, which destroys and frees the element being copied.
    v.Push(v[0]);
    CHECK(v[v.Size() - 1].value == 7);
    while (v.Size() < v.Capacity())
    {
        v.Push(VectorAliasProbe(1));
    }
    const usize size = v.Size();
    const int second = v[1].value;
    v.Push(static_cast<VectorAliasProbe&&>(v[1]));  // moved from its own element
    CHECK(v[v.Size() - 1].value == second);
    while (v.Size() < v.Capacity())
    {
        v.Push(VectorAliasProbe(2));
    }
    // Insert at the front a copy of the last element, which the shift also moves.
    const int last = v[v.Size() - 1].value;
    v.Insert(0, v[v.Size() - 1]);
    CHECK(v[0].value == last);
    CHECK(v.Size() > size);
    // Without growth: insert a copy of an element that the shift moves.
    Vector<VectorAliasProbe> w;
    w.Reserve(8);
    w.Push(VectorAliasProbe(1));
    w.Push(VectorAliasProbe(2));
    w.Push(VectorAliasProbe(3));
    w.Insert(0, w[2]);
    CHECK(w[0].value == 3);
    CHECK(w[3].value == 3);
}

TEST_CASE("Vector: RemoveRange removes a run in one pass and keeps the order", "[containers][vector_container]")
{
    Vector<int> v;
    for (int i = 0; i < 8; ++i)
    {
        v.Push(i);
    }
    v.RemoveRange(0, 3);  // [3,4,5,6,7]
    REQUIRE(v.Size() == 5);
    CHECK(v[0] == 3);
    CHECK(v[4] == 7);
    v.RemoveRange(1, 2);  // [3,6,7]
    REQUIRE(v.Size() == 3);
    CHECK(v[1] == 6);
    v.RemoveRange(1, 0);
    CHECK(v.Size() == 3);
    v.RemoveRange(0, 3);
    CHECK(v.Empty());
}

// Container audit series Day 3: SwapRemove, Append, Emplace, Clone, Swap and range-for.
TEST_CASE("Vector: SwapRemove, Append, Emplace, Clone, Swap and range-for", "[containers][vector]")
{
    using engine_placeholder::u32;
    using engine_placeholder::Vector;
    Vector<u32> v;
    for (u32 i = 0; i < 6; ++i)
    {
        v.Push(i);
    }
    v.SwapRemove(1);  // the last element takes its place
    REQUIRE(v.Size() == 5);
    CHECK(v[1] == 5);
    v.SwapRemove(4);  // the last one itself
    CHECK(v.Size() == 4);
    CHECK(v[3] == 3);

    v.Append(v);  // a vector appended to itself: the source is copied from its offset after growing
    REQUIRE(v.Size() == 8);
    CHECK(v[4] == 0);
    CHECK(v[5] == 5);
    const engine_placeholder::Array<u32, 3> more = {7, 8, 9};
    v.Append(more);
    CHECK(v.Size() == 11);
    CHECK(v[10] == 9);

    u32 sum = 0;
    for (u32 x : v)
    {
        sum += x;
    }
    CHECK(sum == 2 * (0 + 5 + 2 + 3) + 7 + 8 + 9);

    Vector<u32> copy = v.Clone();
    CHECK(copy.Size() == v.Size());
    CHECK(copy[10] == 9);
    copy.Push(100);
    Vector<u32> other;
    other.Push(42);
    other.Swap(copy);
    CHECK(other.Size() == 12);
    CHECK(copy.Size() == 1);
    CHECK(copy[0] == 42);

    struct Pair
    {
        Pair(u32 a_, u32 b_) : a(a_), b(b_) {}
        u32 a;
        u32 b;
    };
    Vector<Pair> pairs;
    Pair& made = pairs.Emplace(3u, 4u);
    CHECK(made.a == 3);
    CHECK(pairs[0].b == 4);
}

namespace
{

struct CopiedName
{
    CopiedName(const char* t) : text(t) {}
    CopiedName(const CopiedName& o) : text(o.text) { ++copies; }
    CopiedName& operator=(const CopiedName&) = default;
    const char* text;
    static inline int copies = 0;
};

}  // namespace

TEST_CASE("Vector: Append of a non-trivial type copies each element", "[containers][vector]")
{
    using engine_placeholder::Vector;
    Vector<CopiedName> names;
    names.Emplace("a");
    names.Emplace("b");
    CopiedName::copies = 0;
    names.Append(names);
    CHECK(names.Size() == 4);
    CHECK(CopiedName::copies == 2);
    CHECK(names[3].text[0] == 'b');
}
