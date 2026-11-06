// Phase 5 Day 78: per-vertex colors and primitive topology in meshes: the color vertex layout, topology and restart
// validation in MeshData::Set, strip/list conversions (the triangle set and every winding survive), the strip grid, and
// that a strip mesh carries its topology to the GPU copy and is culled by its vertices like any other.
#include <algorithm>
#include <cstring>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "renderer/frame_data.h"
#include "renderer/render_scene.h"
#include "rhi/null/null_device.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

namespace {

// The triangles of a list mesh as sorted, rotation-normalized (smallest index first) triples: equal sets mean the same
// triangles with the same winding.
struct Triple {
    u32 v[3];
    bool operator<(const Triple& o) const { return std::lexicographical_compare(v, v + 3, o.v, o.v + 3); }
    bool operator==(const Triple& o) const { return v[0] == o.v[0] && v[1] == o.v[1] && v[2] == o.v[2]; }
};

std::vector<Triple> Triangles(const MeshData& list) {
    REQUIRE(list.Topology() == rhi::PrimitiveTopology::TriangleList);
    std::vector<Triple> out;
    for (u32 i = 0; i + 2 < list.IndexCount(); i += 3) {
        u32 a = list.IndexAt(i), b = list.IndexAt(i + 1), c = list.IndexAt(i + 2);
        // rotate so the smallest index is first, keeping the cyclic order (the winding)
        while (a > b || a > c) {
            const u32 t = a;
            a = b;
            b = c;
            c = t;
        }
        out.push_back({{a, b, c}});
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool SameTriangles(const MeshData& a, const MeshData& b) {
    const std::vector<Triple> ta = Triangles(a);
    const std::vector<Triple> tb = Triangles(b);
    return ta.size() == tb.size() && std::equal(ta.begin(), ta.end(), tb.begin());
}

MeshData PlaneList(u32 x, u32 z) {
    MeshData m;
    REQUIRE(MakePlaneMesh(2.0f, 2.0f, x, z, &m) == nullptr);
    return m;
}

const VertexLayoutData kLayout = StandardVertexLayout();

} // namespace

TEST_CASE("The color vertex layout extends the standard one with a UNorm8x4 color at location 4", "[renderer][mesh][topology]") {
    const VertexLayoutData standard = StandardVertexLayout();
    const VertexLayoutData colored = ColorVertexLayout();
    REQUIRE(colored.attribute_count == 5);
    for (u32 i = 0; i < 4; ++i) {
        CHECK(colored.attributes[i].location == standard.attributes[i].location);
        CHECK(colored.attributes[i].format == standard.attributes[i].format);
        CHECK(colored.attributes[i].offset == standard.attributes[i].offset);
    }
    CHECK(colored.attributes[4].location == 4);
    CHECK(colored.attributes[4].format == rhi::VertexFormat::UNorm8x4);
    CHECK(colored.attributes[4].offset == 48);
    CHECK(colored.stride == 52);
    CHECK(sizeof(ColorVertex) == 52);
    CHECK(offsetof(ColorVertex, color) == 48);
}

TEST_CASE("A colored mesh keeps the geometry and gives each vertex its color", "[renderer][mesh][topology]") {
    MeshData plane = PlaneList(2, 2);
    std::vector<u8> colors(plane.VertexCount() * 4);
    for (u32 i = 0; i < plane.VertexCount(); ++i) {
        colors[i * 4 + 0] = static_cast<u8>(i);
        colors[i * 4 + 1] = static_cast<u8>(255 - i);
        colors[i * 4 + 2] = 7;
        colors[i * 4 + 3] = 255;
    }
    MeshData colored;
    REQUIRE(MakeColoredMesh(plane, colors.data(), plane.VertexCount(), &colored) == nullptr);
    CHECK(colored.VertexCount() == plane.VertexCount());
    CHECK(colored.IndexCount() == plane.IndexCount());
    CHECK(colored.Layout().stride == 52);
    CHECK(colored.GetBounds().box.min.x == plane.GetBounds().box.min.x);
    CHECK(colored.GetBounds().box.max.z == plane.GetBounds().box.max.z);
    const ColorVertex* cv = static_cast<const ColorVertex*>(colored.Vertices());
    const StandardVertex* sv = static_cast<const StandardVertex*>(plane.Vertices());
    for (u32 i = 0; i < plane.VertexCount(); ++i) {
        CHECK(std::memcmp(&cv[i].base, &sv[i], sizeof(StandardVertex)) == 0);
        CHECK(cv[i].color[0] == static_cast<u8>(i));
        CHECK(cv[i].color[1] == static_cast<u8>(255 - i));
        CHECK(cv[i].color[2] == 7);
        CHECK(cv[i].color[3] == 255);
    }
    for (u32 i = 0; i < plane.IndexCount(); ++i) CHECK(colored.IndexAt(i) == plane.IndexAt(i));

    const u8 red[4] = {255, 0, 0, 255};
    MeshData uniform;
    REQUIRE(MakeUniformColoredMesh(plane, red, &uniform) == nullptr);
    const ColorVertex* uv = static_cast<const ColorVertex*>(uniform.Vertices());
    for (u32 i = 0; i < uniform.VertexCount(); ++i) CHECK(uv[i].color[0] == 255);
}

TEST_CASE("Coloring rejects a wrong color count, an empty source and a mesh that is not standard", "[renderer][mesh][topology]") {
    MeshData plane = PlaneList(1, 1);
    const u8 colors[4 * 3] = {};
    MeshData out;
    CHECK(std::string(MakeColoredMesh(plane, colors, 3, &out)) == "colored mesh: the number of colors must equal the number of vertices");
    CHECK(MakeColoredMesh(plane, nullptr, plane.VertexCount(), &out) != nullptr);
    MeshData empty;
    CHECK(std::string(MakeColoredMesh(empty, colors, 0, &out)) == "colored mesh: the source mesh is empty");
    // Already colored: not the standard layout any more.
    const u8 red[4] = {255, 0, 0, 255};
    MeshData colored;
    REQUIRE(MakeUniformColoredMesh(plane, red, &colored) == nullptr);
    MeshData again;
    CHECK(std::string(MakeUniformColoredMesh(colored, red, &again)) == "colored mesh: the source mesh is not in the standard vertex layout");
}

TEST_CASE("Topology decides what an index count and an index value mean", "[renderer][mesh][topology]") {
    StandardVertex v[4] = {};
    for (int i = 0; i < 4; ++i) v[i].position[0] = static_cast<f32>(i);
    auto set = [&](const u16* idx, u32 n, rhi::PrimitiveTopology t) {
        MeshData m;
        const char* e = m.Set(kLayout, 0, v, 4, rhi::IndexType::UInt16, idx, n, t);
        return std::string(e == nullptr ? "" : e);
    };
    const u16 five[5] = {0, 1, 2, 3, 0};
    const u16 four[4] = {0, 1, 2, 3};
    const u16 two[2] = {0, 1};
    const u16 restarted[9] = {0, 1, 2, 0xFFFF, 1, 2, 3, 0, 1};
    CHECK(set(five, 5, rhi::PrimitiveTopology::TriangleList) == "mesh: the index count does not make whole primitives of the topology");
    CHECK(set(five, 5, rhi::PrimitiveTopology::TriangleStrip).empty());
    CHECK(set(two, 2, rhi::PrimitiveTopology::TriangleStrip) == "mesh: the index count does not make whole primitives of the topology");
    CHECK(set(four, 4, rhi::PrimitiveTopology::LineList).empty());
    CHECK(set(five, 5, rhi::PrimitiveTopology::LineList) == "mesh: the index count does not make whole primitives of the topology");
    CHECK(set(two, 2, rhi::PrimitiveTopology::LineStrip).empty());
    CHECK(set(two, 1, rhi::PrimitiveTopology::PointList).empty());
    CHECK(set(four, 4, static_cast<rhi::PrimitiveTopology>(9)) == "mesh: unknown primitive topology");
    // The all-ones index is a restart marker in a strip, and an out-of-range vertex anywhere else.
    CHECK(set(restarted, 9, rhi::PrimitiveTopology::TriangleStrip).empty());
    CHECK(set(restarted, 9, rhi::PrimitiveTopology::TriangleList) == "mesh: an index points past the last vertex");
    MeshData strip;
    REQUIRE(strip.Set(kLayout, 0, v, 4, rhi::IndexType::UInt16, restarted, 9, rhi::PrimitiveTopology::TriangleStrip) == nullptr);
    CHECK(strip.Topology() == rhi::PrimitiveTopology::TriangleStrip);
    CHECK(strip.HasPrimitiveRestart());
    MeshData plain;
    REQUIRE(plain.Set(kLayout, 0, v, 4, rhi::IndexType::UInt16, four, 4, rhi::PrimitiveTopology::TriangleStrip) == nullptr);
    CHECK_FALSE(plain.HasPrimitiveRestart());
    MeshData list = PlaneList(1, 1);
    CHECK(list.Topology() == rhi::PrimitiveTopology::TriangleList);
    CHECK_FALSE(list.HasPrimitiveRestart());

    // A strip with 16-bit indices cannot address 65536 vertices (0xFFFF is the marker).
    std::vector<StandardVertex> many(65536);
    const u16 idx[3] = {0, 1, 2};
    MeshData big;
    CHECK(std::string(big.Set(kLayout, 0, many.data(), 65536, rhi::IndexType::UInt16, idx, 3, rhi::PrimitiveTopology::TriangleStrip)) ==
          "mesh: a strip with 16-bit indices addresses at most 65535 vertices (0xFFFF restarts the strip)");
    CHECK(big.Set(kLayout, 0, many.data(), 65536, rhi::IndexType::UInt16, idx, 3, rhi::PrimitiveTopology::TriangleList) == nullptr);
}

TEST_CASE("The strip grid has the list grid's triangles with fewer indices", "[renderer][mesh][topology]") {
    for (u32 cells : {1u, 2u, 7u}) {
        MeshData list = PlaneList(cells, cells + 1);
        MeshData strip;
        REQUIRE(MakePlaneStripMesh(2.0f, 2.0f, cells, cells + 1, &strip) == nullptr);
        CHECK(strip.Topology() == rhi::PrimitiveTopology::TriangleStrip);
        CHECK(strip.HasPrimitiveRestart() == (cells + 1 > 1));
        CHECK(strip.VertexCount() == list.VertexCount());
        CHECK(std::memcmp(strip.Vertices(), list.Vertices(), list.VertexBytes()) == 0);
        MeshData back;
        REQUIRE(StripToList(strip, &back) == nullptr);
        CHECK(SameTriangles(list, back));
        CHECK(strip.IndexCount() < list.IndexCount() + 3 * (cells + 1)); // never worse than a list plus its row markers
        if (cells >= 7) CHECK(strip.IndexCount() * 3 < list.IndexCount() * 2 + 100); // about two thirds
    }
}

TEST_CASE("Strip to list drops degenerate joins and keeps every winding", "[renderer][mesh][topology]") {
    StandardVertex v[8] = {};
    for (int i = 0; i < 8; ++i) v[i].position[0] = static_cast<f32>(i);
    // two quads joined by degenerate triangles: 0 1 2 3 | 3 4 | 4 5 6 7
    const u16 idx[10] = {0, 1, 2, 3, 3, 4, 4, 5, 6, 7};
    MeshData strip;
    REQUIRE(strip.Set(kLayout, 0, v, 8, rhi::IndexType::UInt16, idx, 10, rhi::PrimitiveTopology::TriangleStrip) == nullptr);
    MeshData list;
    REQUIRE(StripToList(strip, &list) == nullptr);
    // quad 1: (0,1,2) and (2,1,3); quad 2: (4,5,6) and (6,5,7); the join triangles are gone
    std::vector<Triple> expected = {{{0, 1, 2}}, {{1, 3, 2}}, {{4, 5, 6}}, {{5, 7, 6}}};
    std::sort(expected.begin(), expected.end());
    CHECK(Triangles(list) == expected);

    MeshData not_strip = PlaneList(1, 1);
    MeshData out;
    CHECK(std::string(StripToList(not_strip, &out)) == "strip to list: the mesh is not a triangle strip");
    const u16 degenerate[3] = {1, 1, 2};
    MeshData none;
    REQUIRE(none.Set(kLayout, 0, v, 8, rhi::IndexType::UInt16, degenerate, 3, rhi::PrimitiveTopology::TriangleStrip) == nullptr);
    CHECK(std::string(StripToList(none, &out)) == "strip to list: the strip holds no triangle");
}

TEST_CASE("List to strip preserves the triangle set and winding, with either join", "[renderer][mesh][topology]") {
    MeshData cube, sphere, plane;
    REQUIRE(MakeCubeMesh(1.0f, &cube) == nullptr);
    REQUIRE(MakeSphereMesh(1.0f, 12, 8, &sphere) == nullptr);
    plane = PlaneList(6, 5);
    const MeshData* inputs[] = {&cube, &sphere, &plane};
    for (const MeshData* in : inputs) {
        for (StripJoin join : {StripJoin::Restart, StripJoin::Degenerate}) {
            MeshData strip;
            REQUIRE(ListToStrip(*in, join, &strip) == nullptr);
            CHECK(strip.Topology() == rhi::PrimitiveTopology::TriangleStrip);
            CHECK(strip.HasPrimitiveRestart() == (join == StripJoin::Restart && strip.IndexCount() > 0 && strip.HasPrimitiveRestart()));
            if (join == StripJoin::Degenerate) CHECK_FALSE(strip.HasPrimitiveRestart());
            MeshData back;
            REQUIRE(StripToList(strip, &back) == nullptr);
            CHECK(SameTriangles(*in, back));
            if (in == &sphere || in == &plane) CHECK(strip.IndexCount() < in->IndexCount()); // strips save indices on connected grids
        }
    }
}

TEST_CASE("List to strip handles a shuffled mesh and a lone triangle", "[renderer][mesh][topology]") {
    MeshData plane = PlaneList(5, 5);
    // Reverse the triangle order and rotate each triangle's corners: the same triangle set, in an order that gives the
    // greedy walk no help.
    std::vector<u16> shuffled;
    for (u32 t = plane.IndexCount() / 3; t > 0; --t) {
        const u32 i = (t - 1) * 3;
        shuffled.push_back(static_cast<u16>(plane.IndexAt(i + 1)));
        shuffled.push_back(static_cast<u16>(plane.IndexAt(i + 2)));
        shuffled.push_back(static_cast<u16>(plane.IndexAt(i)));
    }
    MeshData mixed;
    REQUIRE(mixed.Set(plane.Layout(), 0, plane.Vertices(), plane.VertexCount(), rhi::IndexType::UInt16, shuffled.data(), static_cast<u32>(shuffled.size())) == nullptr);
    for (StripJoin join : {StripJoin::Restart, StripJoin::Degenerate}) {
        MeshData strip, back;
        REQUIRE(ListToStrip(mixed, join, &strip) == nullptr);
        REQUIRE(StripToList(strip, &back) == nullptr);
        CHECK(SameTriangles(plane, back));
    }
    StandardVertex v[3] = {};
    const u16 one[3] = {0, 1, 2};
    MeshData lone;
    REQUIRE(lone.Set(kLayout, 0, v, 3, rhi::IndexType::UInt16, one, 3) == nullptr);
    MeshData strip;
    REQUIRE(ListToStrip(lone, StripJoin::Restart, &strip) == nullptr);
    CHECK(strip.IndexCount() == 3);
    MeshData wrong;
    CHECK(std::string(ListToStrip(strip, StripJoin::Restart, &wrong)) == "list to strip: the mesh is not a triangle list");
}

TEST_CASE("A GPU strip mesh carries its topology and is culled by its vertices", "[renderer][mesh][topology][scene]") {
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    {
        MeshData list = PlaneList(2, 2);
        MeshData strip;
        REQUIRE(MakePlaneStripMesh(2.0f, 2.0f, 2, 2, &strip) == nullptr);
        CHECK(strip.GetBounds().box.min.x == list.GetBounds().box.min.x);
        CHECK(strip.GetBounds().box.max.x == list.GetBounds().box.max.x);
        GpuMesh gpu;
        REQUIRE(gpu.Create(device.Get(), strip, "strip"));
        CHECK(gpu.Topology() == rhi::PrimitiveTopology::TriangleStrip);
        CHECK(gpu.HasPrimitiveRestart());
        GpuMesh moved = static_cast<GpuMesh&&>(gpu);
        CHECK(moved.Topology() == rhi::PrimitiveTopology::TriangleStrip);
        CHECK(moved.HasPrimitiveRestart());

        RenderScene scene(RenderSceneDesc{});
        RenderObjectDesc d;
        d.mesh = &moved;
        d.transform = ModelMatrix(Vec3(0, 0, -10), 1.0f, 0.0f);
        scene.AddObject(d);
        d.transform = ModelMatrix(Vec3(0, 0, 10), 1.0f, 0.0f); // behind the camera
        scene.AddObject(d);
        Camera cam = MakeLookAtCamera(Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 1, 0));
        cam.near_plane = 0.5f;
        cam.far_plane = 100.0f;
        std::vector<u32> out(scene.SlotCount());
        CHECK(scene.Cull(Frustum::FromViewProjection(cam.ViewProjection()), out.data(), static_cast<u32>(out.size())) == 1);
    }
    device->WaitIdle();
    device.Reset();
}

TEST_CASE("Conversions keep the position offset, and a widened strip uses the 32-bit marker", "[renderer][mesh][topology]") {
    // Position after a 4-float attribute: offset 16, stride 28.
    struct V {
        f32 other[4];
        f32 position[3];
    };
    VertexLayoutData layout;
    layout.stride = sizeof(V);
    V v[4] = {};
    for (int i = 0; i < 4; ++i) {
        v[i].other[0] = 1000.0f;
        v[i].position[0] = static_cast<f32>(i);
    }
    const u16 idx[6] = {0, 1, 2, 2, 1, 3};
    MeshData list;
    REQUIRE(list.Set(layout, 16, v, 4, rhi::IndexType::UInt16, idx, 6) == nullptr);
    CHECK(list.PositionOffset() == 16);
    MeshData strip, back;
    REQUIRE(ListToStrip(list, StripJoin::Restart, &strip) == nullptr);
    CHECK(strip.PositionOffset() == 16);
    CHECK(strip.GetBounds().box.max.x == 3.0f);
    REQUIRE(StripToList(strip, &back) == nullptr);
    CHECK(back.GetBounds().box.max.x == 3.0f);

    // 65536 vertices: the list uses index 0xFFFF, so the strip is widened and must restart with 0xFFFFFFFF.
    std::vector<StandardVertex> many(65536);
    const u16 two[6] = {0, 1, 2, 65533, 65534, 65535}; // two disjoint triangles: two strips
    MeshData big, wide;
    REQUIRE(big.Set(kLayout, 0, many.data(), 65536, rhi::IndexType::UInt16, two, 6) == nullptr);
    REQUIRE(ListToStrip(big, StripJoin::Restart, &wide) == nullptr);
    CHECK(wide.IndexFormat() == rhi::IndexType::UInt32);
    CHECK(wide.HasPrimitiveRestart());
    CHECK(wide.IndexCount() == 7);
    CHECK(wide.IndexAt(3) == 0xFFFFFFFFu);
    CHECK(wide.IndexAt(6) == 65535);
}

TEST_CASE("A uniform color needs a color", "[renderer][mesh][topology]") {
    MeshData plane = PlaneList(1, 1);
    MeshData out;
    CHECK(std::string(MakeUniformColoredMesh(plane, nullptr, &out)) == "colored mesh: the color is null");
}
