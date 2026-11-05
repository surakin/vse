#include <catch2/catch_test_macros.hpp>

#include "containers/array.h"
#include "types.h"

using engine_placeholder::Array;
using engine_placeholder::u32;
using engine_placeholder::usize;

namespace {

struct Tracked {
    static inline usize alive = 0;
    Tracked() { ++alive; }
    Tracked(const Tracked&) = delete;
    Tracked& operator=(const Tracked&) = delete;
    ~Tracked() { --alive; }
};

struct Holder {
    u32 a;
    u32 b;
    Array<u32, 6> values;
};

} // namespace

TEST_CASE("Array: storage is inline, so an object is its members plus the elements", "[containers][array]") {
    static_assert(sizeof(Array<u32, 6>) == 6 * sizeof(u32));
    static_assert(sizeof(Holder) == 2 * sizeof(u32) + 6 * sizeof(u32));
    static_assert(Array<u32, 6>::Size() == 6);
    Holder h{};
    REQUIRE(h.values.Size() == 6);
    // The elements sit inside the holder's own bytes.
    const unsigned char* lo = reinterpret_cast<const unsigned char*>(&h);
    const unsigned char* p = reinterpret_cast<const unsigned char*>(h.values.Data());
    REQUIRE(p >= lo);
    REQUIRE(p + 6 * sizeof(u32) <= lo + sizeof(Holder));
}

TEST_CASE("Array: aggregate initialization, value-initialization, indexing and iteration", "[containers][array]") {
    Array<u32, 4> a = {{1, 2, 3, 4}};
    u32 sum = 0;
    for (u32 v : a) sum += v;
    REQUIRE(sum == 10);
    a[2] = 30;
    const Array<u32, 4>& c = a;
    REQUIRE(c[2] == 30u);
    REQUIRE(c.Data() == a.Data());

    Array<u32, 5> zero{};
    for (usize i = 0; i < zero.Size(); ++i) REQUIRE(zero[i] == 0u);
    Array<u32, 5> partial = {{7}};
    REQUIRE(partial[0] == 7u);
    REQUIRE(partial[4] == 0u);
}

TEST_CASE("Array: elements are constructed and destroyed with the object", "[containers][array]") {
    REQUIRE(Tracked::alive == 0);
    {
        Array<Tracked, 3> a;
        REQUIRE(Tracked::alive == 3);
        (void)a;
    }
    REQUIRE(Tracked::alive == 0);
}
