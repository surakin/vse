#include <catch2/catch_test_macros.hpp>

#include "mem/stack_allocator.h"
#include "types.h"
#include "cstr/mem_ops.h"

using engine_placeholder::StackAllocator;
using engine_placeholder::u8;
using engine_placeholder::usize;

TEST_CASE("StackAllocator: Push/PopToMarker rewinds and preserves earlier contents", "[mem][stack]") {
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
        engine_placeholder::MemSet(p, static_cast<int>(i + 1), kObjSize);
    }
    REQUIRE(first_push_ok);

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
        engine_placeholder::MemSet(p, static_cast<int>(100 + i), kObjSize);
    }
    REQUIRE(second_push_ok);

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
    REQUIRE(first_intact);

    void* reused = stack.Push(kObjSize, 8);
    REQUIRE(reused == second_first);
}

TEST_CASE("StackAllocator::Push rejects a size that would overflow the bounds check, rather than wrapping",
          "[mem][stack]") {
    StackAllocator stack(64);
    // Near-SIZE_MAX: `aligned + size` would wrap past `capacity_` under
    // the old unguarded `aligned + size > capacity_` check, making an
    // absurdly oversized request look like it fits a 64-byte stack.
    constexpr usize kHuge = ~usize(0) - 8;
    void* p = stack.Push(kHuge, 8);
    REQUIRE(p == nullptr);

    // The stack must still be fully usable afterwards — a rejected huge
    // request shouldn't have corrupted `offset_`.
    void* ok = stack.Push(16, 8);
    REQUIRE(ok != nullptr);
}
