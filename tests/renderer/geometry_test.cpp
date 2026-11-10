// Phase 5 Day 73: cameras, bounds, frustum culling and the procedural meshes. Pure CPU maths, no device.
#include "lang/scalar.h"
#include "containers/hash_map.h"
#include "containers/vector.h"
#include "mem/heap_allocator.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "renderer/bounds.h"
#include "renderer/camera.h"
#include "renderer/frustum.h"
#include "renderer/mesh.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;
using Catch::Approx;

namespace {

Vec4 Project(const Mat4& m, const Vec3& p) {
    Vec4 c = m * Vec4(p.x, p.y, p.z, 1.0f);
    return Vec4(c.x / c.w, c.y / c.w, c.z / c.w, c.w);
}

bool Near(const Vec3& a, const Vec3& b, f32 eps = 1e-4f) {
    return Abs(a.x - b.x) < eps && Abs(a.y - b.y) < eps && Abs(a.z - b.z) < eps;
}

} // namespace

TEST_CASE("A camera looks down -Z by default and LookAt turns it", "[renderer][camera]") {
    Camera cam = MakeLookAtCamera(Vec3(0, 0, 5), Vec3(0, 0, 0), Vec3(0, 1, 0));
    CHECK(Near(cam.Forward(), Vec3(0, 0, -1)));
    CHECK(Near(cam.Right(), Vec3(1, 0, 0)));
    CHECK(Near(cam.Up(), Vec3(0, 1, 0)));

    Vec4 origin = cam.View() * Vec4(0, 0, 0, 1);
    CHECK(origin.z == Approx(-5.0f)); // in front of the camera means negative view-space Z
}

TEST_CASE("Projection maps near to depth 0, far to 1 and flips Y", "[renderer][camera]") {
    Camera cam;
    cam.near_plane = 0.5f;
    cam.far_plane = 50.0f;
    Mat4 p = cam.Projection();
    CHECK(Project(p, Vec3(0, 0, -0.5f)).z == Approx(0.0f).margin(1e-5));
    CHECK(Project(p, Vec3(0, 0, -50.0f)).z == Approx(1.0f).margin(1e-5));
    CHECK(Project(p, Vec3(0, 1, -2.0f)).y < 0.0f); // up in view space is up on screen, i.e. negative clip Y in Vulkan
    CHECK(Project(p, Vec3(1, 0, -2.0f)).x > 0.0f);
}

TEST_CASE("Inverse matrices undo the forward ones", "[renderer][camera]") {
    Camera cam = MakeLookAtCamera(Vec3(3, 2, 7), Vec3(-1, 0.5f, 0), Vec3(0, 1, 0));
    Mat4 round = cam.InverseView() * cam.View();
    Mat4 proj = cam.InverseProjection() * cam.Projection();
    Mat4 vp = cam.InverseViewProjection() * cam.ViewProjection();
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            f32 want = c == r ? 1.0f : 0.0f;
            CHECK(round.col[c][r] == Approx(want).margin(1e-4));
            CHECK(proj.col[c][r] == Approx(want).margin(1e-4));
            CHECK(vp.col[c][r] == Approx(want).margin(2e-3));
        }
    }
}

TEST_CASE("Bounds of a point set", "[renderer][bounds]") {
    const f32 pts[] = {-1, -2, -3, 3, 2, 1, 0, 0, 0};
    Aabb box = ComputeAabb(pts, 3, 3 * sizeof(f32));
    CHECK(Near(box.min, Vec3(-1, -2, -3)));
    CHECK(Near(box.max, Vec3(3, 2, 1)));
    Bounds b = ComputeBounds(pts, 3, 3 * sizeof(f32));
    CHECK(Near(b.sphere.center, Vec3(1, 0, -1)));
    // Every point is inside the sphere.
    for (int i = 0; i < 3; ++i) {
        Vec3 p(pts[i * 3], pts[i * 3 + 1], pts[i * 3 + 2]);
        CHECK((p - b.sphere.center).Length() <= b.sphere.radius + 1e-4f);
    }
}

TEST_CASE("Transformed bounds still enclose the transformed points", "[renderer][bounds]") {
    MeshData cube;
    REQUIRE(MakeCubeMesh(2.0f, &cube) == nullptr);
    Mat4 m = ModelMatrix(Vec3(10, 0, 0), 3.0f, 0.7f);
    Bounds world = TransformBounds(cube.GetBounds(), m);
    for (u32 i = 0; i < cube.VertexCount(); ++i) {
        const f32* p = cube.PositionAt(i);
        Vec4 w = m * Vec4(p[0], p[1], p[2], 1.0f);
        Vec3 wp(w.x, w.y, w.z);
        CHECK(wp.x >= world.box.min.x - 1e-3f);
        CHECK(wp.x <= world.box.max.x + 1e-3f);
        CHECK(wp.y >= world.box.min.y - 1e-3f);
        CHECK(wp.y <= world.box.max.y + 1e-3f);
        CHECK(wp.z >= world.box.min.z - 1e-3f);
        CHECK(wp.z <= world.box.max.z + 1e-3f);
        CHECK((wp - world.sphere.center).Length() <= world.sphere.radius + 1e-3f);
    }
}

TEST_CASE("Frustum classifies spheres and boxes", "[renderer][frustum]") {
    Camera cam = MakeLookAtCamera(Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 1, 0));
    cam.near_plane = 1.0f;
    cam.far_plane = 100.0f;
    Frustum f = Frustum::FromViewProjection(cam.ViewProjection());

    CHECK(f.TestSphere({Vec3(0, 0, -10), 1.0f}) == Containment::Inside);
    CHECK(f.TestSphere({Vec3(0, 0, 10), 1.0f}) == Containment::Outside);    // behind
    CHECK(f.TestSphere({Vec3(0, 0, -200), 1.0f}) == Containment::Outside);  // beyond far
    CHECK(f.TestSphere({Vec3(0, 0, -0.5f), 1.0f}) == Containment::Intersects); // straddles near
    CHECK(f.TestSphere({Vec3(500, 0, -10), 1.0f}) == Containment::Outside);
    CHECK(f.TestSphere({Vec3(0, 500, -10), 1.0f}) == Containment::Outside);

    Aabb in{Vec3(-0.5f, -0.5f, -11), Vec3(0.5f, 0.5f, -10)};
    Aabb out{Vec3(100, 0, -11), Vec3(101, 1, -10)};
    Aabb straddle{Vec3(-1, -1, -101), Vec3(1, 1, -99)};
    CHECK(f.TestAabb(in) == Containment::Inside);
    CHECK(f.TestAabb(out) == Containment::Outside);
    CHECK(f.TestAabb(straddle) == Containment::Intersects);
}

TEST_CASE("Frustum edges follow the field of view", "[renderer][frustum]") {
    Camera cam;
    cam.fov_y = 1.5707964f; // 90 degrees: at distance d the half-height is d
    cam.aspect = 1.0f;
    cam.near_plane = 0.1f;
    cam.far_plane = 100.0f;
    Frustum f = Frustum::FromViewProjection(cam.ViewProjection());
    CHECK(f.TestSphere({Vec3(0, 9.0f, -10), 0.1f}) == Containment::Inside);
    CHECK(f.TestSphere({Vec3(0, 11.0f, -10), 0.1f}) == Containment::Outside);
    CHECK(f.TestSphere({Vec3(9.0f, 0, -10), 0.1f}) == Containment::Inside);
    CHECK(f.TestSphere({Vec3(-11.0f, 0, -10), 0.1f}) == Containment::Outside);
}

TEST_CASE("Procedural meshes are closed, in range and wound outward", "[renderer][mesh]") {
    MeshData cube, sphere, plane;
    REQUIRE(MakeCubeMesh(2.0f, &cube) == nullptr);
    REQUIRE(MakeSphereMesh(1.5f, 16, 8, &sphere) == nullptr);
    REQUIRE(MakePlaneMesh(4.0f, 6.0f, 2, 3, &plane) == nullptr);

    CHECK(cube.VertexCount() == 24);
    CHECK(cube.IndexCount() == 36);
    CHECK(plane.VertexCount() == 3 * 4);
    CHECK(plane.IndexCount() == 2 * 3 * 6);
    CHECK(Near(cube.GetBounds().box.min, Vec3(-1, -1, -1)));
    CHECK(Near(cube.GetBounds().box.max, Vec3(1, 1, 1)));
    CHECK(Near(plane.GetBounds().box.max, Vec3(2, 0, 3)));

    for (const MeshData* m : {&cube, &sphere, &plane}) {
        for (u32 i = 0; i < m->IndexCount(); ++i) CHECK(m->IndexAt(i) < m->VertexCount());
    }
    // Triangle winding agrees with the stored normal (counter-clockwise seen from outside).
    for (const MeshData* m : {&cube, &sphere, &plane}) {
        const StandardVertex* v = static_cast<const StandardVertex*>(m->Vertices());
        for (u32 t = 0; t + 2 < m->IndexCount(); t += 3) {
            const f32* a = m->PositionAt(m->IndexAt(t));
            const f32* b = m->PositionAt(m->IndexAt(t + 1));
            const f32* c = m->PositionAt(m->IndexAt(t + 2));
            Vec3 e1(b[0] - a[0], b[1] - a[1], b[2] - a[2]);
            Vec3 e2(c[0] - a[0], c[1] - a[1], c[2] - a[2]);
            Vec3 n = e1.Cross(e2);
            if (n.Length() < 1e-6f) continue; // degenerate pole triangles
            const StandardVertex& sv = v[m->IndexAt(t)];
            CHECK(n.Dot(Vec3(sv.normal[0], sv.normal[1], sv.normal[2])) > 0.0f);
        }
    }
    // Sphere vertices sit on the sphere.
    for (u32 i = 0; i < sphere.VertexCount(); ++i) {
        const f32* p = sphere.PositionAt(i);
        CHECK(Vec3(p[0], p[1], p[2]).Length() == Approx(1.5f).margin(1e-4));
    }
}

TEST_CASE("Mesh builders reject bad parameters", "[renderer][mesh]") {
    MeshData m;
    CHECK(MakeCubeMesh(0.0f, &m) != nullptr);
    CHECK(MakeSphereMesh(1.0f, 2, 8, &m) != nullptr);
    CHECK(MakePlaneMesh(1.0f, 1.0f, 0, 1, &m) != nullptr);
    CHECK_FALSE(m.Valid());
}

TEST_CASE("LookAt does not depend on the length of up", "[renderer][camera]") {
    Camera a = MakeLookAtCamera(Vec3(0, 0, 5), Vec3(0, 0, 0), Vec3(0, 1, 0));
    Camera tiny = MakeLookAtCamera(Vec3(0, 0, 5), Vec3(0, 0, 0), Vec3(0, 1e-7f, 0));
    Camera huge = MakeLookAtCamera(Vec3(0, 0, 5), Vec3(0, 0, 0), Vec3(0, 1e6f, 0));
    CHECK(Near(tiny.Right(), a.Right()));
    CHECK(Near(tiny.Up(), a.Up()));
    CHECK(Near(huge.Up(), a.Up()));
}

TEST_CASE("MeshData::Set rejects a position that is not 4-byte aligned", "[renderer][mesh]") {
    u8 vertices[16 * 3] = {};
    const u16 indices[3] = {0, 1, 2};
    VertexLayoutData layout;
    layout.stride = 15;
    layout.attribute_count = 0;
    MeshData m;
    CHECK(m.Set(layout, 0, vertices, 3, rhi::IndexType::UInt16, indices, 3) != nullptr);
    layout.stride = 16;
    CHECK(m.Set(layout, 1, vertices, 3, rhi::IndexType::UInt16, indices, 3) != nullptr);
    CHECK(m.Set(layout, 4, vertices, 3, rhi::IndexType::UInt16, indices, 3) == nullptr);
}

TEST_CASE("Cube and sphere are watertight and every normal and tangent has unit length", "[renderer][mesh]") {
    MeshData cube, sphere;
    REQUIRE(MakeCubeMesh(2.0f, &cube) == nullptr);
    REQUIRE(MakeSphereMesh(1.0f, 16, 8, &sphere) == nullptr);
    for (const MeshData* m : {&cube, &sphere}) {
        const StandardVertex* v = static_cast<const StandardVertex*>(m->Vertices());
        for (u32 i = 0; i < m->VertexCount(); ++i) {
            CHECK(Vec3(v[i].normal[0], v[i].normal[1], v[i].normal[2]).Length() == Approx(1.0f).margin(1e-4));
            CHECK(Vec3(v[i].tangent[0], v[i].tangent[1], v[i].tangent[2]).Length() == Approx(1.0f).margin(1e-4));
            CHECK(Abs(v[i].tangent[3]) == Approx(1.0f));
        }
        // Weld vertices by position; in a closed surface every undirected edge is used by exactly two triangles, once in
        // each direction.
        HashMap<u64, u32, HeapAllocator> weld(HeapAllocator::Default());
        u32 welded = 0;
        auto pack = [](f32 coordinate) { return static_cast<u64>(static_cast<i64>(LRound(coordinate * 1000)) + (1 << 20)) & 0x1FFFFFu; };
        auto id = [&](u32 index) {
            const f32* p = m->PositionAt(index);
            const u64 key = (pack(p[0]) << 42) | (pack(p[1]) << 21) | pack(p[2]);
            if (const u32* found = weld.Find(key)) return *found;
            weld.Insert(key, welded);
            return welded++;
        };
        HashMap<u64, int, HeapAllocator> directed(HeapAllocator::Default()); // (from << 32 | to) -> how many triangles use that directed edge
        Vector<u64> edges;
        for (u32 t = 0; t < m->IndexCount(); t += 3) {
            u32 w[3] = {id(m->IndexAt(t)), id(m->IndexAt(t + 1)), id(m->IndexAt(t + 2))};
            if (w[0] == w[1] || w[1] == w[2] || w[0] == w[2]) continue; // degenerate pole triangle
            for (int e = 0; e < 3; ++e) {
                const u64 edge = (static_cast<u64>(w[e]) << 32) | w[(e + 1) % 3];
                if (int* count = directed.Find(edge)) {
                    ++*count;
                } else {
                    directed.Insert(edge, 1);
                    edges.Push(edge);
                }
            }
        }
        for (usize i = 0; i < edges.Size(); ++i) {
            const u64 edge = edges[i];
            CHECK(*directed.Find(edge) == 1);
            const int* opposite = directed.Find((edge << 32) | (edge >> 32));
            REQUIRE(opposite != nullptr);
            CHECK(*opposite == 1);
        }
    }
}
