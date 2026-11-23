// The terrain's picture and its collider are one surface (Phase 28 Day 491): the height of the physics height field,
// built over the samples of a height map blob without copying them, equals `HeightMapView::HeightAt` at every point.

#include <catch2/catch_test_macros.hpp>

#include "asset/blob.h"
#include "asset/height_map.h"
#include "lang/scalar.h"
#include "physics/height_field.h"
#include "support.h"

namespace vse::asset
{

namespace
{

using test::AsBytes;
using test::Rid;

u32 TcRough(u32 x, u32 z)
{
    u32 v = x * 73856093u ^ z * 19349663u;
    v ^= v >> 13;
    v *= 0x5bd1e995u;
    v ^= v >> 15;
    return v & 0xFFFF;
}

}  // namespace

TEST_CASE("terrain collider: the field over a height map blob has the render surface", "[asset][height_map][physics]")
{
    constexpr u32 kWidth = 37;
    constexpr u32 kDepth = 29;
    Vector<byte> samples;
    samples.Resize(usize{kWidth} * kDepth * 2);
    for (u32 z = 0; z < kDepth; ++z)
    {
        for (u32 x = 0; x < kWidth; ++x)
        {
            const u32 s = TcRough(x, z);
            samples[(usize{z} * kWidth + x) * 2] = static_cast<byte>(s & 0xFF);
            samples[(usize{z} * kWidth + x) * 2 + 1] = static_cast<byte>(s >> 8);
        }
    }
    HeightMapHeader header;
    header.width = kWidth;
    header.height = kDepth;
    header.range_min = -20.0f;
    header.range_max = 60.0f;
    header.cell_size = 1.25f;
    TypeRegistry types;
    Vector<byte> blob;
    REQUIRE(BuildHeightMapBlob(types, Rid(9), header, AsBytes(samples), &blob).Ok());
    HeightMapView view;
    REQUIRE(view.Open(CopyShared(ByteSpan(blob.Data(), blob.Size())), types).Ok());

    physics::HeightFieldSpec spec;
    spec.bytes = view.Bytes();
    spec.offset = view.MipOffset(0);
    spec.width = view.Header().width;
    spec.height = view.Header().height;
    spec.range_min = view.Header().range_min;
    spec.range_max = view.Header().range_max;
    spec.cell_size = view.Header().cell_size;
    physics::Validation status = physics::Validation::Ok;
    const physics::HeightFieldConstPtr field = physics::HeightField::Create(spec, &status);
    REQUIRE(field);
    CHECK(status == physics::Validation::Ok);

    // The same samples, read in place: the field holds a share of the blob, not a copy of it.
    for (u32 z = 0; z < kDepth; ++z)
    {
        for (u32 x = 0; x < kWidth; ++x)
        {
            REQUIRE(field->Sample(x, z) == view.Sample(x, z));
        }
    }

    // 10,000 fixed points, a few outside the map (both clamp to its edge): the same bits.
    const f32 span_x = static_cast<f32>(kWidth - 1) * header.cell_size;
    const f32 span_z = static_cast<f32>(kDepth - 1) * header.cell_size;
    u32 state = 12345u;
    u32 compared = 0;
    for (u32 i = 0; i < 10000; ++i)
    {
        state = state * 1664525u + 1013904223u;
        const f32 x = (static_cast<f32>(state >> 8) / 16777216.0f) * (span_x + 4.0f) - 2.0f;
        state = state * 1664525u + 1013904223u;
        const f32 z = (static_cast<f32>(state >> 8) / 16777216.0f) * (span_z + 4.0f) - 2.0f;
        const f32 render = view.HeightAt(x, z);
        const f32 physics_height = field->HeightAtMap(x, z);
        REQUIRE(render == physics_height);
        ++compared;
    }
    CHECK(compared == 10000);

    // A ray cast down from above lands on the drawn surface (to the ray's own rounding).
    for (u32 i = 0; i < 200; ++i)
    {
        state = state * 1664525u + 1013904223u;
        const f32 x = (static_cast<f32>(state >> 8) / 16777216.0f) * span_x;
        state = state * 1664525u + 1013904223u;
        const f32 z = (static_cast<f32>(state >> 8) / 16777216.0f) * span_z;
        f32 t = 0.0f;
        REQUIRE(
            field->RayCast(Vec3(x - span_x * 0.5f, 100.0f, z - span_z * 0.5f), Vec3(0, -1, 0), 200.0f, &t, nullptr));
        CHECK(Abs((100.0f - t) - view.HeightAt(x, z)) < 2e-3f);
    }
}

}  // namespace vse::asset
