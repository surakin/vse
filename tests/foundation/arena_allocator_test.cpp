#include <catch2/catch_test_macros.hpp>

#include "mem/arena_allocator.h"
#include <cstring>
#include "types.h"

using engine_placeholder::ArenaAllocator;
using engine_placeholder::u8;
using engine_placeholder::usize;

TEST_CASE("ArenaAllocator: 1000 varying-size allocations are non-overlapping and intact", "[mem][arena]") {
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
    REQUIRE(all_allocs_ok);

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
    REQUIRE(no_overlap);

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
    REQUIRE(checksums_ok);
}

TEST_CASE("ArenaAllocator::Alloc rejects a size that would overflow the bounds check, rather than wrapping",
          "[mem][arena]") {
    ArenaAllocator arena(64);
    // Near-SIZE_MAX: `aligned + size` would wrap past `capacity_` under
    // the old unguarded `aligned + size > capacity_` check, making an
    // absurdly oversized request look like it fits a 64-byte arena.
    constexpr usize kHuge = ~usize(0) - 8;
    void* p = arena.Alloc(kHuge, 8);
    REQUIRE(p == nullptr);

    // The arena must still be fully usable afterwards — a rejected huge
    // request shouldn't have corrupted `offset_`.
    void* ok = arena.Alloc(16, 8);
    REQUIRE(ok != nullptr);
}

TEST_CASE("ArenaAllocator: each allocation uses exactly its size and the smallest alignment padding", "[mem][arena]") {
    ArenaAllocator arena(4096);
    u8* a = static_cast<u8*>(arena.Alloc(100, 1));
    REQUIRE(a != nullptr);
    u8* b = static_cast<u8*>(arena.Alloc(100, 1));
    REQUIRE(b != nullptr);
    CHECK(b == a + 100); // no byte skipped between byte-aligned allocations

    arena.Reset();
    u8* c = static_cast<u8*>(arena.Alloc(1, 1));
    u8* d = static_cast<u8*>(arena.Alloc(8, 8));
    REQUIRE((c != nullptr && d != nullptr));
    CHECK(d == c + 8); // the offset 1 is padded to 8, not to 16
    u8* e = static_cast<u8*>(arena.Alloc(8, 8));
    CHECK(e == d + 8); // an offset that is already aligned gets no padding
    u8* f = static_cast<u8*>(arena.Alloc(16, 16));
    REQUIRE(f != nullptr);
    CHECK(static_cast<usize>(f - c) == 32); // offset 24 is padded to 32, the next multiple of 16
}

TEST_CASE("ArenaAllocator: the whole capacity can be allocated, not a byte more, and Reset gives it back", "[mem][arena]") {
    ArenaAllocator arena(4096);
    REQUIRE(arena.Alloc(4096, 1) != nullptr);
    CHECK(arena.Alloc(1, 1) == nullptr);
    arena.Reset();
    REQUIRE(arena.Alloc(4095, 1) != nullptr);
    CHECK(arena.Alloc(1, 1) != nullptr);
    CHECK(arena.Alloc(1, 1) == nullptr);
}
