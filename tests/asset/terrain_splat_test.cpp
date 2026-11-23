// Splat map normalisation (Phase 28 Day 489).
#include <catch2/catch_test_macros.hpp>

#include "asset/terrain_splat.h"

namespace vse::asset
{

TEST_CASE("splat image: every pixel sums to 255, the result is deterministic and sizes are checked", "[asset][terrain]")
{
    Vector<byte> in;
    in.Resize(4 * 4 * 4);
    u32 state = 99;
    for (usize i = 0; i < in.Size(); ++i)
    {
        state = state * 1664525u + 1013904223u;
        in[i] = static_cast<byte>(state >> 20);
    }
    Vector<byte> a;
    Vector<byte> b;
    REQUIRE(NormalizeSplatImage(4, 4, ByteSpan(in.Data(), in.Size()), &a).Ok());
    REQUIRE(NormalizeSplatImage(4, 4, ByteSpan(in.Data(), in.Size()), &b).Ok());
    REQUIRE(a.Size() == in.Size());
    CHECK(MemCompare(a.Data(), b.Data(), a.Size()) == 0);
    for (usize i = 0; i < a.Size(); i += 4)
    {
        CHECK(static_cast<u32>(a[i]) + a[i + 1] + a[i + 2] + a[i + 3] == 255);
    }
    CHECK_FALSE(NormalizeSplatImage(4, 4, ByteSpan(in.Data(), in.Size() - 1), &a).Ok());
    CHECK_FALSE(NormalizeSplatImage(0, 4, ByteSpan(in.Data(), in.Size()), &a).Ok());
}

}  // namespace vse::asset
