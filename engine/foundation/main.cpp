#include "arena_allocator.h"
#include "array.h"
#include "hash_map.h"
#include "mem.h"
#include "os.h"
#include "pool_allocator.h"
#include "stack_allocator.h"
#include "string.h"
#include "types.h"

using engine_placeholder::Array;
using engine_placeholder::ArenaAllocator;
using engine_placeholder::HashMap;
using engine_placeholder::PoolAllocator;
using engine_placeholder::StackAllocator;
using engine_placeholder::String;
using engine_placeholder::StringBuilder;
using engine_placeholder::StringView;
using engine_placeholder::u8;
using engine_placeholder::u32;
using engine_placeholder::usize;
using engine_placeholder::os::Exit;
using engine_placeholder::os::Write;

namespace {

void Expect(bool cond, const char* label) {
    Write(1, label, strlen(label));
    if (cond) {
        const char pass[] = ": PASS\n";
        Write(1, pass, sizeof(pass) - 1);
    } else {
        const char fail[] = ": FAIL\n";
        Write(1, fail, sizeof(fail) - 1);
        Exit(1);
    }
}

bool BytesEqual(const u8* a, const u8* b, usize n) {
    for (usize i = 0; i < n; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

void TestMemcpy() {
    {
        u8 src[1] = {0xAA};
        u8 dst[1] = {0x00};
        memcpy(dst, src, 0);
        Expect(dst[0] == 0x00, "memcpy size 0 is a no-op");
    }
    {
        u8 src[1] = {0x42};
        u8 dst[1] = {0x00};
        memcpy(dst, src, 1);
        Expect(dst[0] == 0x42, "memcpy size 1");
    }
    {
        u8 src[7] = {1, 2, 3, 4, 5, 6, 7};
        u8 dst[7] = {0, 0, 0, 0, 0, 0, 0};
        memcpy(dst, src, 7);
        Expect(BytesEqual(dst, src, 7), "memcpy size 7 (unaligned length)");
    }
    {
        // 40-byte source buffer; copy 32 bytes starting at a misaligned
        // (odd) offset to exercise unaligned pointers.
        u8 buf[40];
        for (usize i = 0; i < 40; ++i) {
            buf[i] = static_cast<u8>(i);
        }
        u8 dst[32] = {};
        memcpy(dst, buf + 3, 32);
        Expect(BytesEqual(dst, buf + 3, 32), "memcpy size 32 (misaligned src)");
    }
}

void TestMemset() {
    {
        u8 buf[10] = {};
        memset(buf, 0xAB, 10);
        bool ok = true;
        for (usize i = 0; i < 10; ++i) {
            ok = ok && buf[i] == 0xAB;
        }
        Expect(ok, "memset size 10");
    }
    {
        u8 buf[1] = {0x11};
        memset(buf, 0, 0);
        Expect(buf[0] == 0x11, "memset size 0 is a no-op");
    }
    {
        // Odd byte count at an odd offset into a larger buffer.
        u8 buf[16];
        for (usize i = 0; i < 16; ++i) {
            buf[i] = 0;
        }
        memset(buf + 3, 0xFF, 9);
        bool ok = true;
        for (usize i = 0; i < 3; ++i) {
            ok = ok && buf[i] == 0;
        }
        for (usize i = 3; i < 12; ++i) {
            ok = ok && buf[i] == 0xFF;
        }
        for (usize i = 12; i < 16; ++i) {
            ok = ok && buf[i] == 0;
        }
        Expect(ok, "memset size 9 (misaligned offset)");
    }
}

void TestMemmove() {
    {
        // Forward overlap: dest > src.
        // buf = {0,1,2,3,4,5,6,7,8,9}; memmove(buf+2, buf, 6)
        // hand-computed: temp = buf[0..5] = {0,1,2,3,4,5}
        //   buf[2..7] = temp; buf[0],[1] and buf[8],[9] untouched
        //   => {0,1,0,1,2,3,4,5,8,9}
        u8 buf[10];
        for (usize i = 0; i < 10; ++i) {
            buf[i] = static_cast<u8>(i);
        }
        memmove(buf + 2, buf, 6);
        const u8 expected[10] = {0, 1, 0, 1, 2, 3, 4, 5, 8, 9};
        Expect(BytesEqual(buf, expected, 10), "memmove forward overlap (dest > src)");
    }
    {
        // Backward overlap: dest < src.
        // buf = {0,1,2,3,4,5,6,7,8,9}; memmove(buf, buf+2, 6)
        // hand-computed: temp = buf[2..7] = {2,3,4,5,6,7}
        //   buf[0..5] = temp; buf[6..9] unchanged (6,7,8,9; the source
        //   copy already captured old buf[6],buf[7] before they'd be
        //   read again, so they're untouched here since dest never
        //   reaches index 6)
        //   => {2,3,4,5,6,7,6,7,8,9}
        u8 buf[10];
        for (usize i = 0; i < 10; ++i) {
            buf[i] = static_cast<u8>(i);
        }
        memmove(buf, buf + 2, 6);
        const u8 expected[10] = {2, 3, 4, 5, 6, 7, 6, 7, 8, 9};
        Expect(BytesEqual(buf, expected, 10), "memmove backward overlap (dest < src)");
    }
    {
        // Non-overlapping regions behave like memcpy.
        u8 buf[12];
        for (usize i = 0; i < 12; ++i) {
            buf[i] = static_cast<u8>(i);
        }
        u8 expected[6] = {0, 1, 2, 3, 4, 5};
        memmove(buf + 6, buf, 6);
        Expect(BytesEqual(buf + 6, expected, 6), "memmove non-overlapping");
    }
}

void TestStrlen() {
    Expect(strlen("") == 0, "strlen empty string");
    Expect(strlen("hi") == 2, "strlen \"hi\"");
    Expect(strlen("Hello, world!") == 13, "strlen \"Hello, world!\"");
}

void TestArenaAllocator() {
    constexpr usize kCount = 1000;
    constexpr usize kAlign = 8;

    // 1 MiB is generous for 1000 allocations of at most 64 bytes plus
    // per-allocation alignment padding (worst case ~1000 * (64 + 7) ~
    // 71 KiB) — sized to comfortably avoid exercising the
    // out-of-capacity path in this test.
    ArenaAllocator arena(1024 * 1024);

    void* ptrs[kCount];
    usize sizes[kCount];
    bool all_allocs_ok = true;

    for (usize i = 0; i < kCount; ++i) {
        usize size = 1 + (i % 64);
        void* p = arena.Alloc(size, kAlign);
        if (p == nullptr) {
            all_allocs_ok = false;
            break;
        }
        ptrs[i] = p;
        sizes[i] = size;
        // Unique checksum pattern per allocation.
        memset(p, static_cast<int>(i % 256), size);
    }
    Expect(all_allocs_ok, "1000 arena allocations of varying size succeed");

    bool no_overlap = true;
    for (usize i = 0; i < kCount && no_overlap; ++i) {
        auto* a_begin = static_cast<u8*>(ptrs[i]);
        auto* a_end = a_begin + sizes[i];
        for (usize j = i + 1; j < kCount; ++j) {
            auto* b_begin = static_cast<u8*>(ptrs[j]);
            auto* b_end = b_begin + sizes[j];
            if (a_begin < b_end && b_begin < a_end) {
                no_overlap = false;
                break;
            }
        }
    }
    Expect(no_overlap, "arena allocations do not overlap");

    bool checksums_ok = true;
    for (usize i = 0; i < kCount && checksums_ok; ++i) {
        const auto* p = static_cast<const u8*>(ptrs[i]);
        const u8 expected = static_cast<u8>(i % 256);
        for (usize b = 0; b < sizes[i]; ++b) {
            if (p[b] != expected) {
                checksums_ok = false;
                break;
            }
        }
    }
    Expect(checksums_ok, "arena allocation checksums intact after 1000 allocs");
}

void TestPoolAllocator() {
    constexpr usize kSlotSize = 32;
    constexpr usize kSlotCount = 64;
    constexpr usize kIterations = 10000;

    PoolAllocator pool(kSlotSize, kSlotCount);

    void* live[kSlotCount] = {};
    bool live_flag[kSlotCount] = {};

    // Small deterministic xorshift32 PRNG — reproducible across
    // platforms without depending on libc's rand() implementation.
    u32 rng_state = 0x12345678u;
    auto NextRand = [&rng_state]() -> u32 {
        rng_state ^= rng_state << 13;
        rng_state ^= rng_state >> 17;
        rng_state ^= rng_state << 5;
        return rng_state;
    };

    bool no_alias = true;
    for (usize iter = 0; iter < kIterations && no_alias; ++iter) {
        usize idx = NextRand() % kSlotCount;
        if (!live_flag[idx]) {
            void* p = pool.Alloc();
            if (p == nullptr) {
                // Pool momentarily full (every logical slot live); skip
                // this iteration rather than treating it as failure.
                continue;
            }
            for (usize j = 0; j < kSlotCount; ++j) {
                if (live_flag[j] && live[j] == p) {
                    no_alias = false;
                    break;
                }
            }
            live[idx] = p;
            live_flag[idx] = true;
        } else {
            pool.Free(live[idx]);
            live_flag[idx] = false;
            live[idx] = nullptr;
        }
    }
    Expect(no_alias, "pool allocator churn: no two live allocations alias");
}

void TestStackAllocator() {
    StackAllocator stack(4096);

    constexpr usize kFirstBatch = 10;
    constexpr usize kSecondBatch = 5;
    constexpr usize kObjSize = 16;

    void* first[kFirstBatch];
    bool first_push_ok = true;
    for (usize i = 0; i < kFirstBatch; ++i) {
        void* p = stack.Push(kObjSize, 8);
        if (p == nullptr) {
            first_push_ok = false;
            break;
        }
        first[i] = p;
        memset(p, static_cast<int>(i + 1), kObjSize);
    }
    Expect(first_push_ok, "stack push (first batch) succeeds");

    StackAllocator::Marker marker = stack.CurrentMarker();

    void* second_first = nullptr;
    bool second_push_ok = true;
    for (usize i = 0; i < kSecondBatch; ++i) {
        void* p = stack.Push(kObjSize, 8);
        if (p == nullptr) {
            second_push_ok = false;
            break;
        }
        if (i == 0) {
            second_first = p;
        }
        memset(p, static_cast<int>(100 + i), kObjSize);
    }
    Expect(second_push_ok, "stack push (second batch) succeeds");

    stack.PopToMarker(marker);

    bool first_intact = true;
    for (usize i = 0; i < kFirstBatch && first_intact; ++i) {
        const auto* p = static_cast<const u8*>(first[i]);
        const u8 expected = static_cast<u8>(i + 1);
        for (usize b = 0; b < kObjSize; ++b) {
            if (p[b] != expected) {
                first_intact = false;
                break;
            }
        }
    }
    Expect(first_intact, "first batch remains valid after PopToMarker");

    void* reused = stack.Push(kObjSize, 8);
    Expect(reused == second_first,
           "PopToMarker rewinds the offset so the freed region is reused");
}

void TestArrayStress() {
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
    Expect(boundary_ok, "array contents correct at every resize boundary (100k pushes)");

    bool final_ok = arr.Size() == kCount;
    for (usize i = 0; i < kCount && final_ok; ++i) {
        if (arr[i] != i) {
            final_ok = false;
        }
    }
    Expect(final_ok, "array contents correct after all 100k pushes");
}

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

void TestArrayNonTrivialResize() {
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
        Expect(moves_from_resize == 5,
               "resize move-constructs existing elements, not just the new push");

        bool contents_ok = true;
        for (usize i = 0; i < 5; ++i) {
            if (arr[i].value != i) {
                contents_ok = false;
                break;
            }
        }
        Expect(contents_ok, "non-trivial array contents correct across resize");

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
        Expect(final_contents_ok, "non-trivial array contents correct after further growth");
    } // arr destructor runs here, destroying all 50 live elements.

    Expect(TrackedValue::constructions == TrackedValue::destructions,
           "no construction/destruction leak across non-trivial array resize");
}

void TestArrayInsertRemove() {
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
    Expect(insert_ok, "insert at middle index produces expected sequence");

    arr.Remove(0); // [1,99,2,3,4]
    const usize expected_after_remove[] = {1, 99, 2, 3, 4};
    bool remove_ok = arr.Size() == 5;
    for (usize i = 0; i < 5 && remove_ok; ++i) {
        remove_ok = arr[i] == expected_after_remove[i];
    }
    Expect(remove_ok, "remove at front index produces expected sequence");

    arr.Insert(arr.Size(), 42); // [1,99,2,3,4,42]
    Expect(arr.Size() == 6 && arr[5] == 42, "insert at end index appends correctly");

    arr.Remove(arr.Size() - 1); // [1,99,2,3,4]
    Expect(arr.Size() == 5 && arr[4] == 4, "remove at last index shrinks correctly");
}

void TestHashMap() {
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
    Expect(map.Size() == kCount, "hash map size matches insert count");

    bool all_present_ok = true;
    for (usize key = 0; key < kCount && all_present_ok; ++key) {
        const usize* value = map.Find(key);
        all_present_ok = value != nullptr && *value == key * 7 + 3;
    }
    Expect(all_present_ok, "every inserted key looks up to the right value");

    // Remove the even-valued keys.
    usize removed = 0;
    for (usize key = 0; key < kCount; key += 2) {
        if (map.Remove(key)) {
            ++removed;
        }
    }
    Expect(removed == kCount / 2, "removed exactly the even-keyed half");
    Expect(map.Size() == kCount / 2, "hash map size reflects the removals");

    bool removed_are_absent = true;
    for (usize key = 0; key < kCount && removed_are_absent; key += 2) {
        removed_are_absent = map.Find(key) == nullptr;
    }
    Expect(removed_are_absent, "removed keys report absent");

    bool survivors_ok = true;
    for (usize key = 1; key < kCount && survivors_ok; key += 2) {
        const usize* value = map.Find(key);
        survivors_ok = value != nullptr && *value == key * 7 + 3;
    }
    Expect(survivors_ok, "surviving (odd-keyed) half still resolves correctly");
}

void TestStringView() {
    StringView a("hello");
    StringView b("hello");
    StringView c("world");
    Expect(a == b, "StringView equality for identical content");
    Expect(a != c, "StringView inequality for different content");
    Expect(a.Compare(c) < 0, "StringView lexicographic comparison ('hello' < 'world')");
    Expect(c.Compare(a) > 0, "StringView lexicographic comparison ('world' > 'hello')");

    StringView sub = a.Substring(1, 3); // "ell"
    StringView expected_sub("ell");
    Expect(sub == expected_sub, "StringView substring extracts expected range");
}

void TestStringType() {
    ArenaAllocator arena(4096);
    String<ArenaAllocator> s(arena);
    Expect(s.Length() == 0, "default-constructed String is empty");

    s.Append(StringView("hello"));
    s.Append(' ');
    s.Append(StringView("world"));
    StringView expected("hello world");
    Expect(s.View() == expected, "String::Append builds expected content");
    Expect(s.CStr()[s.Length()] == '\0', "String maintains a null terminator");
}

void TestStringBuilder() {
    ArenaAllocator arena(1024 * 1024);
    StringBuilder<ArenaAllocator> sb(arena);
    sb.Append("answer=");
    sb.AppendInt(-42);
    sb.Append(" count=");
    sb.AppendUInt(7);
    sb.Append(" pi=");
    sb.AppendFloat(3.25, 2);
    sb.Append(" name=");
    sb.Append("engine");

    const char expected[] = "answer=-42 count=7 pi=3.25 name=engine";
    StringView actual = sb.View();
    bool matches = actual.Length() == sizeof(expected) - 1 &&
                   BytesEqual(reinterpret_cast<const u8*>(actual.Data()),
                              reinterpret_cast<const u8*>(expected), actual.Length());
    Expect(matches, "StringBuilder formats ints/floats/strings byte-for-byte correctly");

    // Write the formatted result via os::Write, demonstrating the same
    // buffer already verified above going out through the Day 2 wrapper.
    Write(1, actual.Data(), actual.Length());
    const char newline[] = "\n";
    Write(1, newline, 1);
}

} // namespace

int main() {
    TestMemcpy();
    TestMemset();
    TestMemmove();
    TestStrlen();
    TestArenaAllocator();
    TestPoolAllocator();
    TestStackAllocator();
    TestArrayStress();
    TestArrayNonTrivialResize();
    TestArrayInsertRemove();
    TestHashMap();
    TestStringView();
    TestStringType();
    TestStringBuilder();

    const char ok[] = "OK\n";
    Write(1, ok, sizeof(ok) - 1);
    return 0;
}
