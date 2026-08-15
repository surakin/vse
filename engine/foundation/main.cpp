#include <csetjmp>
#include <fcntl.h>

#include "arena_allocator.h"
#include "array.h"
#include "hash_map.h"
#include "log.h"
#include "mem.h"
#include "os.h"
#include "pool_allocator.h"
#include "stack_allocator.h"
#include "grapheme.h"
#include "matrix.h"
#include "normalize.h"
#include "quaternion.h"
#include "string.h"
#include "transform.h"
#include "types.h"
#include "utf8.h"
#include "utf8_codec.h"
#include "vector.h"

using engine_placeholder::Array;
using engine_placeholder::ArenaAllocator;
using engine_placeholder::HashMap;
using engine_placeholder::Log;
using engine_placeholder::LogChannel;
using engine_placeholder::LogCore;
using engine_placeholder::LogVerbosity;
using engine_placeholder::PoolAllocator;
using engine_placeholder::SetExitSinkForTesting;
using engine_placeholder::SetWriteSinkForTesting;
using engine_placeholder::StackAllocator;
using engine_placeholder::String;
using engine_placeholder::StringBuilder;
using engine_placeholder::StringView;
using engine_placeholder::DecodedCodepoint;
using engine_placeholder::DecodeUtf8At;
using engine_placeholder::EncodeUtf8;
using engine_placeholder::CodepointCount;
using engine_placeholder::IsValidUtf8;
using engine_placeholder::Utf8Codepoints;
using engine_placeholder::GraphemeClusterCount;
using engine_placeholder::GraphemeClusters;
using engine_placeholder::NormalizeNFC;
using engine_placeholder::NormalizeNFD;
using engine_placeholder::Mat3;
using engine_placeholder::Mat4;
using engine_placeholder::Vec2;
using engine_placeholder::Vec3;
using engine_placeholder::Vec4;
using engine_placeholder::Compose;
using engine_placeholder::Quaternion;
using engine_placeholder::Slerp;
using engine_placeholder::Transform;
using engine_placeholder::kPi;
using engine_placeholder::f32;
using engine_placeholder::u8;
using engine_placeholder::u32;
using engine_placeholder::usize;
using engine_placeholder::LoadConfig;
using engine_placeholder::os::Close;
using engine_placeholder::os::Exit;
using engine_placeholder::os::Open;
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

void TestUtf8CodecRoundTrip() {
    // Boundary-value round trips at every UTF-8 length transition.
    const u32 kBoundaries[] = {
        0x007F, 0x0080, // 1 <-> 2 byte
        0x07FF, 0x0800, // 2 <-> 3 byte
        0xFFFF, 0x10000, // 3 <-> 4 byte
        0x10FFFF, // max valid codepoint
    };
    bool all_round_trip_ok = true;
    for (u32 cp : kBoundaries) {
        char buf[4] = {};
        usize encoded_len = EncodeUtf8(cp, buf);
        bool valid = false;
        DecodedCodepoint decoded = DecodeUtf8At(buf, encoded_len, 0, &valid);
        if (!valid || decoded.codepoint != cp || decoded.byte_length != encoded_len) {
            all_round_trip_ok = false;
            break;
        }
    }
    Expect(all_round_trip_ok, "EncodeUtf8/DecodeUtf8At round-trip at every length-transition boundary");
}

void TestUtf8MixedContent() {
    ArenaAllocator arena(4096);
    StringBuilder<ArenaAllocator> sb(arena);
    sb.Append("A"); // ASCII, 1 byte
    sb.AppendCodepoint(0x00E9); // 'é', 2 bytes
    sb.AppendCodepoint(0x4E2D); // '中', 3 bytes
    sb.AppendCodepoint(0x1F600); // '😀', 4 bytes
    sb.Append("Z"); // ASCII, 1 byte

    StringView sv = sb.View();
    Expect(CodepointCount(sv) == 5, "CodepointCount counts codepoints, not bytes, for mixed content");

    const u32 expected[] = {'A', 0x00E9, 0x4E2D, 0x1F600, 'Z'};
    usize i = 0;
    bool sequence_ok = true;
    for (u32 cp : Utf8Codepoints(sv)) {
        if (i >= 5 || cp != expected[i]) {
            sequence_ok = false;
            break;
        }
        ++i;
    }
    Expect(sequence_ok && i == 5, "Utf8Codepoints iterates mixed ASCII/2/3/4-byte content in order");
}

void TestUtf8IsValidRejectsMalformed() {
    // Truncated multi-byte lead byte at end-of-buffer (a 2-byte lead with
    // no continuation byte following).
    const char truncated[] = {static_cast<char>(0xC2)};
    Expect(!IsValidUtf8(StringView(truncated, 1)), "IsValidUtf8 rejects a truncated lead byte at end-of-buffer");

    // Lone continuation byte with no lead byte.
    const char lone_continuation[] = {static_cast<char>(0x80)};
    Expect(!IsValidUtf8(StringView(lone_continuation, 1)), "IsValidUtf8 rejects a lone continuation byte");

    // Overlong encoding of 'A' (0x41) as a 2-byte sequence.
    const char overlong[] = {static_cast<char>(0xC1), static_cast<char>(0x81)};
    Expect(!IsValidUtf8(StringView(overlong, 2)), "IsValidUtf8 rejects an overlong encoding of 'A'");

    // UTF-8-encoded surrogate (an attempt to encode U+D800).
    const char surrogate[] = {static_cast<char>(0xED), static_cast<char>(0xA0), static_cast<char>(0x80)};
    Expect(!IsValidUtf8(StringView(surrogate, 3)), "IsValidUtf8 rejects a UTF-8-encoded surrogate");
}

void TestGraphemePlainAscii() {
    StringView sv("Hello!");
    Expect(GraphemeClusterCount(sv) == CodepointCount(sv) && GraphemeClusterCount(sv) == sv.Length(),
           "plain ASCII: grapheme count == codepoint count == byte count");
}

void TestGraphemeCombiningCharacter() {
    ArenaAllocator arena(4096);
    StringBuilder<ArenaAllocator> sb(arena);
    sb.AppendCodepoint(0x0065); // 'e'
    sb.AppendCodepoint(0x0301); // combining acute accent
    StringView sv = sb.View();

    Expect(GraphemeClusterCount(sv) == 1, "'e' + combining acute is one grapheme cluster");
    for (StringView cluster : GraphemeClusters(sv)) {
        Expect(cluster.Length() == sv.Length(), "combining-character cluster spans both codepoints' bytes");
    }
}

void TestGraphemeDecomposedHangul() {
    ArenaAllocator arena(4096);
    StringBuilder<ArenaAllocator> sb(arena);
    sb.AppendCodepoint(0x1100); // Hangul choseong (L) jamo
    sb.AppendCodepoint(0x1161); // Hangul jungseong (V) jamo
    StringView sv = sb.View();

    Expect(GraphemeClusterCount(sv) == 1, "decomposed Hangul L+V jamo is one grapheme cluster");
    for (StringView cluster : GraphemeClusters(sv)) {
        Expect(cluster.Length() == sv.Length(), "decomposed-Hangul cluster spans both codepoints' bytes");
    }
}

void TestGraphemeCrLf() {
    StringView sv("\r\n");
    Expect(GraphemeClusterCount(sv) == 1, "\"\\r\\n\" is one grapheme cluster, not two");
}

void TestGraphemeEmojiZwjSequence() {
    ArenaAllocator arena(4096);
    StringBuilder<ArenaAllocator> sb(arena);
    sb.AppendCodepoint(0x1F468); // MAN
    sb.AppendCodepoint(0x200D); // ZERO WIDTH JOINER
    sb.AppendCodepoint(0x1F469); // WOMAN
    StringView sv = sb.View();

    Expect(GraphemeClusterCount(sv) == 1, "emoji ZWJ sequence (man-ZWJ-woman) is one grapheme cluster");
}

void TestGraphemeRegionalIndicatorPair() {
    ArenaAllocator arena(4096);
    StringBuilder<ArenaAllocator> sb(arena);
    sb.AppendCodepoint(0x1F1FA); // REGIONAL INDICATOR SYMBOL LETTER U
    sb.AppendCodepoint(0x1F1F8); // REGIONAL INDICATOR SYMBOL LETTER S
    StringView sv = sb.View();

    Expect(GraphemeClusterCount(sv) == 1, "regional-indicator pair (US flag) is one grapheme cluster");
}

void TestGraphemeRegionalIndicatorTriple() {
    ArenaAllocator arena(4096);
    StringBuilder<ArenaAllocator> sb(arena);
    sb.AppendCodepoint(0x1F1FA); // U
    sb.AppendCodepoint(0x1F1F8); // S
    sb.AppendCodepoint(0x1F1EB); // F
    StringView sv = sb.View();

    Expect(GraphemeClusterCount(sv) == 2,
           "three regional indicators split into a pair + a singleton, not one cluster of three");
}

u32 NthCodepoint(StringView sv, usize n) {
    usize i = 0;
    for (u32 cp : Utf8Codepoints(sv)) {
        if (i == n) {
            return cp;
        }
        ++i;
    }
    return 0;
}

void TestNormalizeNfdDecomposesPrecomposed() {
    ArenaAllocator src_arena(4096);
    StringBuilder<ArenaAllocator> sb(src_arena);
    sb.AppendCodepoint(0x00E9); // precomposed 'é'
    StringView source = sb.View();

    ArenaAllocator out_arena(4096);
    String<ArenaAllocator> nfd = NormalizeNFD(out_arena, source);

    Expect(CodepointCount(nfd.View()) == 2, "NFD decomposes precomposed 'é' into 2 codepoints");
    Expect(NthCodepoint(nfd.View(), 0) == 0x0065 && NthCodepoint(nfd.View(), 1) == 0x0301,
           "NFD('é') == 'e' + combining acute, in that order");
}

void TestNormalizeNfcComposesDecomposed() {
    ArenaAllocator src_arena(4096);
    StringBuilder<ArenaAllocator> sb(src_arena);
    sb.AppendCodepoint(0x0065); // 'e'
    sb.AppendCodepoint(0x0301); // combining acute
    StringView source = sb.View();

    ArenaAllocator out_arena(4096);
    String<ArenaAllocator> nfc = NormalizeNFC(out_arena, source);

    Expect(CodepointCount(nfc.View()) == 1 && NthCodepoint(nfc.View(), 0) == 0x00E9,
           "NFC('e' + combining acute) == precomposed 'é'");
}

void TestNormalizeRoundTripAndIdempotency() {
    ArenaAllocator src_arena(4096);
    StringBuilder<ArenaAllocator> sb(src_arena);
    sb.Append("caf");
    sb.AppendCodepoint(0x00E9); // precomposed 'é'
    sb.Append(" ");
    sb.AppendCodepoint(0x0065); // already-decomposed 'e' + combining acute
    sb.AppendCodepoint(0x0301);
    StringView source = sb.View();

    ArenaAllocator out_arena(1024 * 1024);
    String<ArenaAllocator> nfc1 = NormalizeNFC(out_arena, source);
    String<ArenaAllocator> nfd1 = NormalizeNFD(out_arena, source);
    String<ArenaAllocator> nfd_of_nfc1 = NormalizeNFD(out_arena, nfc1.View());
    String<ArenaAllocator> nfc_of_nfc1 = NormalizeNFC(out_arena, nfc1.View());
    String<ArenaAllocator> nfd_of_nfd1 = NormalizeNFD(out_arena, nfd1.View());

    Expect(nfd_of_nfc1.View() == nfd1.View(), "NFD(NFC(s)) == NFD(s)");
    Expect(nfc_of_nfc1.View() == nfc1.View(), "NFC(NFC(s)) == NFC(s)");
    Expect(nfd_of_nfd1.View() == nfd1.View(), "NFD(NFD(s)) == NFD(s)");
}

void TestNormalizeHangulRoundTrip() {
    ArenaAllocator src_arena(4096);
    StringBuilder<ArenaAllocator> sb(src_arena);
    sb.AppendCodepoint(0xAC00); // precomposed Hangul syllable GA
    StringView source = sb.View();

    ArenaAllocator out_arena(4096);
    String<ArenaAllocator> nfd = NormalizeNFD(out_arena, source);
    Expect(CodepointCount(nfd.View()) == 2 && NthCodepoint(nfd.View(), 0) == 0x1100 &&
               NthCodepoint(nfd.View(), 1) == 0x1161,
           "NFD(U+AC00) decomposes into L+V jamo via the algorithmic Hangul path");

    String<ArenaAllocator> nfc = NormalizeNFC(out_arena, nfd.View());
    Expect(nfc.View() == source, "NFC of the decomposed jamo recomposes back to U+AC00");
}

void TestNormalizeCompositionExclusion() {
    // U+0958 (DEVANAGARI LETTER QA) canonically decomposes to U+0915 +
    // U+093C but is on the composition-exclusions list, so NFC must not
    // recompose it back.
    ArenaAllocator src_arena(4096);
    StringBuilder<ArenaAllocator> sb(src_arena);
    sb.AppendCodepoint(0x0958);
    StringView source = sb.View();

    ArenaAllocator out_arena(4096);
    String<ArenaAllocator> nfd = NormalizeNFD(out_arena, source);
    Expect(CodepointCount(nfd.View()) == 2 && NthCodepoint(nfd.View(), 0) == 0x0915 &&
               NthCodepoint(nfd.View(), 1) == 0x093C,
           "NFD(U+0958) decomposes into U+0915 + U+093C");

    String<ArenaAllocator> nfc = NormalizeNFC(out_arena, nfd.View());
    Expect(nfc.View() == nfd.View() && nfc.View() != source,
           "composition-excluded U+0958 does not recompose from its decomposition");
}

bool NearlyEqual(f32 a, f32 b, f32 epsilon = 1e-4f) {
    f32 diff = a - b;
    return (diff < 0 ? -diff : diff) <= epsilon;
}

bool Vec2NearlyEqual(const Vec2& a, const Vec2& b) { return NearlyEqual(a.x, b.x) && NearlyEqual(a.y, b.y); }
bool Vec3NearlyEqual(const Vec3& a, const Vec3& b) {
    return NearlyEqual(a.x, b.x) && NearlyEqual(a.y, b.y) && NearlyEqual(a.z, b.z);
}

void TestVecDot() {
    Expect(NearlyEqual(Vec3(1, 2, 3).Dot(Vec3(4, 5, 6)), 32.0f), "Vec3(1,2,3).Dot(Vec3(4,5,6)) == 32");
    Expect(NearlyEqual(Vec3(1, 0, 0).Dot(Vec3(0, 1, 0)), 0.0f), "orthogonal Vec3s dot to 0");
    Expect(NearlyEqual(Vec2(3, 4).Dot(Vec2(3, 4)), 25.0f), "Vec2(3,4).Dot(Vec2(3,4)) == 25 (== |v|^2)");
}

void TestVecCross() {
    Expect(Vec3NearlyEqual(Vec3(1, 0, 0).Cross(Vec3(0, 1, 0)), Vec3(0, 0, 1)), "x-axis cross y-axis == z-axis");
    Expect(Vec3NearlyEqual(Vec3(0, 1, 0).Cross(Vec3(1, 0, 0)), Vec3(0, 0, -1)),
           "y-axis cross x-axis == -z-axis (anti-commutative, right-handed)");
    Expect(Vec3NearlyEqual(Vec3(2, 3, 4).Cross(Vec3(5, 6, 7)), Vec3(-3, 6, -3)),
           "Vec3(2,3,4).Cross(Vec3(5,6,7)) == (-3,6,-3)");
}

void TestVecNormalize() {
    Expect(Vec3NearlyEqual(Vec3(3, 4, 0).Normalized(), Vec3(0.6f, 0.8f, 0.0f)), "Vec3(3,4,0) normalizes to (0.6,0.8,0)");
    Expect(NearlyEqual(Vec3(3, 4, 0).Normalized().Length(), 1.0f), "normalized Vec3 has unit length");
    Expect(Vec2NearlyEqual(Vec2(3, 4).Normalized(), Vec2(0.6f, 0.8f)), "Vec2(3,4) normalizes to (0.6,0.8)");
}

void TestMatMultiply() {
    // A: columns (1,4,7),(2,5,8),(3,6,10) == rows (1,2,3),(4,5,6),(7,8,10)
    Mat3 a = Mat3::FromBasis(Vec3(1, 4, 7), Vec3(2, 5, 8), Vec3(3, 6, 10));
    // B: columns (1,0,1),(0,1,0),(2,1,1) == rows (1,0,2),(0,1,1),(1,0,1)
    Mat3 b = Mat3::FromBasis(Vec3(1, 0, 1), Vec3(0, 1, 0), Vec3(2, 1, 1));

    // Hand-computed A*B (standard row-times-column):
    //   row0=(1,2,3): (1*1+2*0+3*1, 1*0+2*1+3*0, 1*2+2*1+3*1) = (4, 2, 7)
    //   row1=(4,5,6): (4*1+5*0+6*1, 4*0+5*1+6*0, 4*2+5*1+6*1) = (10, 5, 19)
    //   row2=(7,8,10): (7*1+8*0+10*1, 7*0+8*1+10*0, 7*2+8*1+10*1) = (17, 8, 32)
    // i.e. columns (4,10,17), (2,5,8), (7,19,32).
    Mat3 product = a * b;
    Expect(Vec3NearlyEqual(product.col[0], Vec3(4, 10, 17)) && Vec3NearlyEqual(product.col[1], Vec3(2, 5, 8)) &&
               Vec3NearlyEqual(product.col[2], Vec3(7, 19, 32)),
           "Mat3 multiply matches a hand-calculated reference product");

    Expect(Vec3NearlyEqual(Mat3::Identity() * a.col[0], a.col[0]) &&
               Vec3NearlyEqual((Mat3::Identity() * a).col[1], a.col[1]),
           "Mat3::Identity() is a multiplicative identity");

    // A * v with v=(1,2,3): (1*1+2*2+3*3, 4*1+5*2+6*3, 7*1+8*2+10*3) = (14,32,53)
    Expect(Vec3NearlyEqual(a * Vec3(1, 2, 3), Vec3(14, 32, 53)), "Mat3-vector multiply matches hand calculation");
}

void TestMatTranspose() {
    Mat3 a = Mat3::FromBasis(Vec3(1, 4, 7), Vec3(2, 5, 8), Vec3(3, 6, 10));
    Mat3 at = a.Transpose();
    // Transpose's columns are A's rows: (1,2,3), (4,5,6), (7,8,10).
    Expect(Vec3NearlyEqual(at.col[0], Vec3(1, 2, 3)) && Vec3NearlyEqual(at.col[1], Vec3(4, 5, 6)) &&
               Vec3NearlyEqual(at.col[2], Vec3(7, 8, 10)),
           "Mat3::Transpose swaps rows and columns as hand-calculated");
}

void TestMat4FromBasisAndMultiply() {
    Mat4 translate_a = Mat4::FromBasis(Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1), Vec3(1, 2, 3));
    Mat4 translate_b = Mat4::FromBasis(Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1), Vec3(10, 20, 30));

    // Composing two pure translations should add their offsets.
    Mat4 combined = translate_a * translate_b;
    Vec4 origin_translated = combined * Vec4(0, 0, 0, 1);
    Expect(NearlyEqual(origin_translated.x, 11) && NearlyEqual(origin_translated.y, 22) &&
               NearlyEqual(origin_translated.z, 33),
           "composing two Mat4 translations adds their offsets");
}

bool QuaternionNearlyEqual(const Quaternion& a, const Quaternion& b) {
    return NearlyEqual(a.x, b.x) && NearlyEqual(a.y, b.y) && NearlyEqual(a.z, b.z) && NearlyEqual(a.w, b.w);
}

void TestQuaternionMultiplyMatchesHamiltonProduct() {
    // Unit quaternions i=(1,0,0,0), j=(0,1,0,0), k=(0,0,1,0): i*j == k,
    // j*i == -k (anti-commutative), matching quaternion-algebra identities.
    Quaternion i(1, 0, 0, 0);
    Quaternion j(0, 1, 0, 0);
    Quaternion k(0, 0, 1, 0);
    Expect(QuaternionNearlyEqual(i * j, k), "i*j == k");
    Expect(QuaternionNearlyEqual(j * i, Quaternion(-k.x, -k.y, -k.z, -k.w)), "j*i == -k (anti-commutative)");
}

void TestQuaternionRotatesKnownVector() {
    // +90 degrees about +Z rotates +X to +Y (right-handed).
    Quaternion rot = Quaternion::FromAxisAngle(Vec3(0, 0, 1), kPi * 0.5f);
    Vec3 rotated = rot.RotateVector(Vec3(1, 0, 0));
    Expect(Vec3NearlyEqual(rotated, Vec3(0, 1, 0)), "90-degree rotation about +Z sends +X to +Y");
}

void TestQuaternionToMat3MatchesRotateVector() {
    Quaternion rot = Quaternion::FromAxisAngle(Vec3(0, 0, 1), kPi * 0.5f);
    Vec3 v(1, 0, 0);
    Expect(Vec3NearlyEqual(rot.ToMat3() * v, rot.RotateVector(v)),
           "Quaternion::ToMat3() applied to a vector matches RotateVector() on the same vector");
}

void TestQuaternionMatrixRoundTrip() {
    // Axis (1,2,2)/3 (unit length), angle 1.0 rad keeps trace(ToMat3()) >
    // 0 so FromMat3 takes the same branch FromAxisAngle's w-sign implies,
    // giving an exact (not just up-to-sign) round trip.
    Quaternion original = Quaternion::FromAxisAngle(Vec3(1.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f), 1.0f);
    Quaternion recovered = Quaternion::FromMat3(original.ToMat3());
    Expect(QuaternionNearlyEqual(original, recovered), "Quaternion -> Mat3 -> Quaternion recovers the original");
}

void TestQuaternionSlerp() {
    Quaternion identity = Quaternion::Identity();
    Quaternion ninety = Quaternion::FromAxisAngle(Vec3(0, 0, 1), kPi * 0.5f);
    Quaternion forty_five = Quaternion::FromAxisAngle(Vec3(0, 0, 1), kPi * 0.25f);

    Expect(QuaternionNearlyEqual(Slerp(identity, ninety, 0.0f), identity), "Slerp(a,b,0) == a");
    Expect(QuaternionNearlyEqual(Slerp(identity, ninety, 1.0f), ninety), "Slerp(a,b,1) == b");
    Expect(QuaternionNearlyEqual(Slerp(identity, ninety, 0.5f), forty_five),
           "Slerp halfway between 0 and 90 degrees about the same axis == 45 degrees about that axis");
}

void TestTransformCompose() {
    Transform parent;
    parent.position = Vec3(1, 0, 0);
    parent.rotation = Quaternion::FromAxisAngle(Vec3(0, 0, 1), kPi * 0.5f);

    Transform child;
    child.position = Vec3(1, 0, 0);

    Transform composed = Compose(parent, child);
    // child's local (1,0,0) offset gets rotated 90 degrees about Z by the
    // parent (-> (0,1,0)) then added to the parent's own position (1,0,0).
    Expect(Vec3NearlyEqual(composed.position, Vec3(1, 1, 0)),
           "Compose(parent, child) rotates the child's local offset into parent space before adding it");
    Expect(QuaternionNearlyEqual(composed.rotation, parent.rotation),
           "Compose(parent, child) with identity child rotation keeps the parent's rotation");
}

void TestTransformToMatrix() {
    Transform t;
    t.position = Vec3(2, 3, 4);
    t.scale = Vec3(2, 3, 4);

    Vec4 transformed = t.ToMatrix() * Vec4(1, 1, 1, 1);
    Expect(NearlyEqual(transformed.x, 4) && NearlyEqual(transformed.y, 6) && NearlyEqual(transformed.z, 8),
           "Transform::ToMatrix() applies scale before translation");
    Vec4 origin_transformed = t.ToMatrix() * Vec4(0, 0, 0, 1);
    Expect(NearlyEqual(origin_transformed.x, 2) && NearlyEqual(origin_transformed.y, 3) &&
               NearlyEqual(origin_transformed.z, 4),
           "Transform::ToMatrix() translates the origin to the transform's position");
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

char g_capture_buffer[1024];
usize g_capture_len = 0;

long CapturingWriteSink(int fd, const void* buf, usize len) {
    (void)fd;
    usize copy_len = len;
    if (g_capture_len + copy_len > sizeof(g_capture_buffer)) {
        copy_len = sizeof(g_capture_buffer) - g_capture_len;
    }
    memcpy(g_capture_buffer + g_capture_len, buf, copy_len);
    g_capture_len += copy_len;
    return static_cast<long>(len);
}

std::jmp_buf g_test_exit_jmp;
int g_test_exit_code = 0;

[[noreturn]] void TestExitSink(int code) {
    g_test_exit_code = code;
    std::longjmp(g_test_exit_jmp, 1);
}

void TestDefaultChannelVerbosity() {
    g_capture_len = 0;
    SetWriteSinkForTesting(&CapturingWriteSink);

    Log(LogCore, LogVerbosity::Error, "error visible by default");
    Log(LogCore, LogVerbosity::Info, "info hidden by default");

    SetWriteSinkForTesting(&Write);

    StringView captured(g_capture_buffer, g_capture_len);
    Expect(captured.Contains(StringView("error visible by default")),
           "Error-level messages emit by default");
    Expect(!captured.Contains(StringView("info hidden by default")),
           "Info-level messages are suppressed by default (Warning+ default)");
}

void TestAssertFailure() {
    g_capture_len = 0;
    SetWriteSinkForTesting(&CapturingWriteSink);
    SetExitSinkForTesting(&TestExitSink);

    bool reached_after_assert = false;
    if (setjmp(g_test_exit_jmp) == 0) {
        ENGINE_ASSERT(false, "intentional test failure");
        reached_after_assert = true; // should never execute
    }

    SetWriteSinkForTesting(&Write);
    SetExitSinkForTesting(&Exit);

    Expect(!reached_after_assert, "code after a failing ENGINE_ASSERT is not reached");
    Expect(g_test_exit_code == 1, "intentionally-failing assert exits with code 1");

    StringView captured(g_capture_buffer, g_capture_len);
    Expect(captured.Contains(StringView("intentional test failure")),
           "assert failure output contains the given message");
    Expect(captured.Contains(StringView("ASSERT FAILED")),
           "assert failure output is clearly labeled");
}

void TestConfigChannelToggle() {
    static LogChannel test_channel("TestChannel", LogVerbosity::Warn);
    test_channel.SetVerbosity(LogVerbosity::Warn);
    test_channel.SetEnabled(true);

    const char* config_path = "/tmp/engine_test_log_config.txt";
    int fd = Open(config_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    const char content[] = "TestChannel.verbosity=Info\nTestChannel.enabled=true\n";
    Write(fd, content, sizeof(content) - 1);
    Close(fd);

    bool loaded = LoadConfig(config_path);
    Expect(loaded, "config file loads successfully");
    Expect(test_channel.Verbosity() == LogVerbosity::Info, "config overrides channel verbosity");
    Expect(test_channel.Enabled(), "config confirms channel enabled");

    g_capture_len = 0;
    SetWriteSinkForTesting(&CapturingWriteSink);
    Log(test_channel, LogVerbosity::Info, "emitted message");
    Log(test_channel, LogVerbosity::Debug, "suppressed message");
    SetWriteSinkForTesting(&Write);

    StringView captured(g_capture_buffer, g_capture_len);
    Expect(captured.Contains(StringView("emitted message")),
           "emitted (Info<=Info) message reaches the write sink");
    Expect(!captured.Contains(StringView("suppressed message")),
           "suppressed (Debug>Info) message does not reach the write sink");

    // Disable the channel entirely; even Error-level must be suppressed.
    test_channel.SetEnabled(false);
    g_capture_len = 0;
    SetWriteSinkForTesting(&CapturingWriteSink);
    Log(test_channel, LogVerbosity::Error, "should not appear when disabled");
    SetWriteSinkForTesting(&Write);

    StringView captured2(g_capture_buffer, g_capture_len);
    Expect(captured2.Length() == 0, "disabled channel suppresses even Error-level messages");
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
    TestUtf8CodecRoundTrip();
    TestUtf8MixedContent();
    TestUtf8IsValidRejectsMalformed();
    TestGraphemePlainAscii();
    TestGraphemeCombiningCharacter();
    TestGraphemeDecomposedHangul();
    TestGraphemeCrLf();
    TestGraphemeEmojiZwjSequence();
    TestGraphemeRegionalIndicatorPair();
    TestGraphemeRegionalIndicatorTriple();
    TestNormalizeNfdDecomposesPrecomposed();
    TestNormalizeNfcComposesDecomposed();
    TestNormalizeRoundTripAndIdempotency();
    TestNormalizeHangulRoundTrip();
    TestNormalizeCompositionExclusion();
    TestVecDot();
    TestVecCross();
    TestVecNormalize();
    TestMatMultiply();
    TestMatTranspose();
    TestMat4FromBasisAndMultiply();
    TestQuaternionMultiplyMatchesHamiltonProduct();
    TestQuaternionRotatesKnownVector();
    TestQuaternionToMat3MatchesRotateVector();
    TestQuaternionMatrixRoundTrip();
    TestQuaternionSlerp();
    TestTransformCompose();
    TestTransformToMatrix();
    TestStringView();
    TestStringType();
    TestStringBuilder();
    TestDefaultChannelVerbosity();
    TestAssertFailure();
    TestConfigChannelToggle();

    const char ok[] = "OK\n";
    Write(1, ok, sizeof(ok) - 1);
    return 0;
}
