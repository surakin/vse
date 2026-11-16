// The convex hull format, the hull builder and the minter (Phase 25 Day 381).

#include <catch2/catch_test_macros.hpp>

#include "asset/hull.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "hull_source.h"
#include "lang/limits.h"
#include "lang/scalar.h"
#include "support.h"

namespace engine_placeholder::asset
{

namespace
{

Array<f32, 3> P(f32 x, f32 y, f32 z)
{
    Array<f32, 3> p = {{x, y, z}};
    return p;
}

// The eight corners of a box with the given half extents.
Vector<Array<f32, 3>> BoxPoints(f32 hx, f32 hy, f32 hz)
{
    Vector<Array<f32, 3>> pts;
    for (u32 i = 0; i < 8; ++i)
    {
        pts.Push(P((i & 1) ? hx : -hx, (i & 2) ? hy : -hy, (i & 4) ? hz : -hz));
    }
    return pts;
}

Status Build(const Vector<Array<f32, 3>>& pts, HullGeometry* out)
{
    return BuildHullGeometry(Span<const Array<f32, 3>>(pts.Data(), pts.Size()), out);
}

bool Near(f32 a, f32 b, f32 tolerance)
{
    return Abs(a - b) <= tolerance;
}

Vector<byte> MintBlob(const TypeRegistry& types, const Vector<Array<f32, 3>>& pts)
{
    HullGeometry hull;
    REQUIRE(Build(pts, &hull).Ok());
    Vector<byte> blob;
    REQUIRE(BuildHullBlob(types, ResourceId::FromValue(7), hull, &blob).Ok());
    return blob;
}

// A copy of a blob with one section replaced.
Vector<byte> WithSection(const TypeRegistry& types, const Vector<byte>& good, u32 section, const Vector<byte>& bytes)
{
    BlobReader reader;
    REQUIRE(reader.Open(ByteSpan(good.Data(), good.Size()), types).Ok());
    BlobWriter writer(types, types::kHull, reader.Id(), 0);
    for (u32 s = kHullSectionHeader; s <= kHullSectionIndices; ++s)
    {
        ByteSpan span;
        REQUIRE(reader.FindSection(s, &span));
        if (s == section)
        {
            REQUIRE(writer.AddSection(s, ByteSpan(bytes.Data(), bytes.Size())).Ok());
        }
        else
        {
            REQUIRE(writer.AddSection(s, span).Ok());
        }
    }
    Vector<byte> out;
    REQUIRE(writer.Finish(&out).Ok());
    return out;
}

Vector<byte> SectionCopy(const TypeRegistry& types, const Vector<byte>& good, u32 section)
{
    BlobReader reader;
    REQUIRE(reader.Open(ByteSpan(good.Data(), good.Size()), types).Ok());
    ByteSpan span;
    REQUIRE(reader.FindSection(section, &span));
    Vector<byte> out;
    for (usize i = 0; i < span.Size(); ++i)
    {
        out.Push(span.Data()[i]);
    }
    return out;
}

Status OpenView(const TypeRegistry& types, const Vector<byte>& blob)
{
    HullView view;
    return view.Open(CopyShared(ByteSpan(blob.Data(), blob.Size())), types);
}

}  // namespace

TEST_CASE("hull: the type is registered at version 1", "[asset][hull]")
{
    TypeRegistry types;
    TypeInfo info;
    REQUIRE(types.Find(types::kHull, &info));
    CHECK(info.current_version == 1);
}

TEST_CASE("hull: a cube has eight vertices, six quad faces and the analytic mass", "[asset][hull]")
{
    HullGeometry hull;
    REQUIRE(Build(BoxPoints(1, 1, 1), &hull).Ok());
    CHECK(hull.vertices.Size() == 8);
    CHECK(hull.planes.Size() == 6);
    for (usize f = 0; f < 6; ++f)
    {
        CHECK(hull.face_count[f] == 4);
        // The outward unit normal at distance 1 from the centre.
        const Array<f32, 4>& pl = hull.planes[f];
        CHECK(Near(pl[3], 1.0f, 1e-6f));
        CHECK(Near(pl[0] * pl[0] + pl[1] * pl[1] + pl[2] * pl[2], 1.0f, 1e-6f));
        // Every corner of the face is on its plane.
        for (u32 k = 0; k < 4; ++k)
        {
            const Array<f32, 3>& v = hull.vertices[hull.indices[hull.face_first[f] + k]];
            CHECK(Near(pl[0] * v[0] + pl[1] * v[1] + pl[2] * v[2], pl[3], 1e-6f));
        }
    }
    CHECK(Near(hull.volume, 8.0f, 1e-5f));
    CHECK(Near(hull.centroid[0], 0.0f, 1e-6f));
    // A cube of side 2 and density 1: I = m (a^2 + b^2) / 12 = 8 * 8 / 12 about each axis, no products.
    CHECK(Near(hull.inertia[0], 64.0f / 12.0f, 1e-4f));
    CHECK(Near(hull.inertia[1], 64.0f / 12.0f, 1e-4f));
    CHECK(Near(hull.inertia[2], 64.0f / 12.0f, 1e-4f));
    CHECK(Near(hull.inertia[3], 0.0f, 1e-5f));
    CHECK(Near(hull.extent[0], 1.0f, 0.0f));
}

TEST_CASE("hull: an off-centre box has its centroid and inertia about the centroid", "[asset][hull]")
{
    Vector<Array<f32, 3>> pts = BoxPoints(0.5f, 1.0f, 1.5f);
    for (usize i = 0; i < pts.Size(); ++i)
    {
        pts[i][0] += 3.0f;
        pts[i][1] -= 1.0f;
    }
    HullGeometry hull;
    REQUIRE(Build(pts, &hull).Ok());
    CHECK(Near(hull.volume, 6.0f, 1e-4f));  // 1 x 2 x 3
    CHECK(Near(hull.centroid[0], 3.0f, 1e-5f));
    CHECK(Near(hull.centroid[1], -1.0f, 1e-5f));
    CHECK(Near(hull.inertia[0], 6.0f * (4.0f + 9.0f) / 12.0f, 1e-3f));
    CHECK(Near(hull.inertia[1], 6.0f * (1.0f + 9.0f) / 12.0f, 1e-3f));
    CHECK(Near(hull.inertia[2], 6.0f * (1.0f + 4.0f) / 12.0f, 1e-3f));
    CHECK(Near(hull.inertia[3], 0.0f, 1e-3f));
}

TEST_CASE("hull: a tetrahedron has four triangles; interior and repeated points are dropped", "[asset][hull]")
{
    Vector<Array<f32, 3>> pts;
    pts.Push(P(0, 0, 0));
    pts.Push(P(1, 0, 0));
    pts.Push(P(0, 1, 0));
    pts.Push(P(0, 0, 1));
    pts.Push(P(0.1f, 0.1f, 0.1f));  // inside
    pts.Push(P(1, 0, 0));           // a repeat
    HullGeometry hull;
    REQUIRE(Build(pts, &hull).Ok());
    CHECK(hull.vertices.Size() == 4);
    CHECK(hull.planes.Size() == 4);
    CHECK(Near(hull.volume, 1.0f / 6.0f, 1e-6f));
    CHECK(Near(hull.centroid[0], 0.25f, 1e-6f));
}

TEST_CASE("hull: a prism with a face of many corners keeps it as one polygon", "[asset][hull]")
{
    Vector<Array<f32, 3>> pts;
    const u32 sides = 24;
    for (u32 i = 0; i < sides; ++i)
    {
        const f32 a = 6.2831853f * static_cast<f32>(i) / static_cast<f32>(sides);
        pts.Push(P(Cos(a), -1.0f, Sin(a)));
        pts.Push(P(Cos(a), 1.0f, Sin(a)));
    }
    HullGeometry hull;
    REQUIRE(Build(pts, &hull).Ok());
    CHECK(hull.vertices.Size() == 48);
    CHECK(hull.planes.Size() == 26);  // 24 sides and two caps
    u32 caps = 0;
    for (usize f = 0; f < hull.planes.Size(); ++f)
    {
        caps += hull.face_count[f] == 24 ? 1u : 0u;
    }
    CHECK(caps == 2);
}

TEST_CASE("hull: degenerate input is refused with a reason", "[asset][hull]")
{
    HullGeometry hull;
    Vector<Array<f32, 3>> few;
    few.Push(P(0, 0, 0));
    few.Push(P(1, 0, 0));
    few.Push(P(0, 1, 0));
    CHECK(Build(few, &hull).Code() == ErrorCode::Invalid);

    Vector<Array<f32, 3>> flat = BoxPoints(1, 1, 1);
    for (usize i = 0; i < flat.Size(); ++i)
    {
        flat[i][2] = 0.0f;
    }
    CHECK(Build(flat, &hull).Code() == ErrorCode::Invalid);

    Vector<Array<f32, 3>> line;
    for (u32 i = 0; i < 6; ++i)
    {
        line.Push(P(static_cast<f32>(i), 2.0f * static_cast<f32>(i), 0.0f));
    }
    CHECK(Build(line, &hull).Code() == ErrorCode::Invalid);

    Vector<Array<f32, 3>> same;
    for (u32 i = 0; i < 6; ++i)
    {
        same.Push(P(1, 1, 1));
    }
    CHECK(Build(same, &hull).Code() == ErrorCode::Invalid);

    Vector<Array<f32, 3>> bad = BoxPoints(1, 1, 1);
    bad[3][1] = Limits<f32>::Infinity();
    CHECK(Build(bad, &hull).Code() == ErrorCode::Invalid);

    // More than 255 corners: a sphere of 400 points is all vertices.
    Vector<Array<f32, 3>> sphere;
    for (u32 i = 0; i < 20; ++i)
    {
        for (u32 j = 0; j < 20; ++j)
        {
            const f32 u = 3.14159265f * (static_cast<f32>(i) + 0.5f) / 20.0f;
            const f32 v = 6.2831853f * static_cast<f32>(j) / 20.0f;
            sphere.Push(P(Sin(u) * Cos(v), Cos(u), Sin(u) * Sin(v)));
        }
    }
    CHECK(Build(sphere, &hull).Code() == ErrorCode::LimitExceeded);
}

TEST_CASE("hull: the same points give the same bytes and the view reads them back", "[asset][hull]")
{
    TypeRegistry types;
    const Vector<byte> a = MintBlob(types, BoxPoints(1, 2, 3));
    const Vector<byte> b = MintBlob(types, BoxPoints(1, 2, 3));
    REQUIRE(a.Size() == b.Size());
    CHECK(MemCompare(a.Data(), b.Data(), a.Size()) == 0);

    HullView view;
    REQUIRE(view.Open(CopyShared(ByteSpan(a.Data(), a.Size())), types).Ok());
    CHECK(view.VertexCount() == 8);
    CHECK(view.FaceCount() == 6);
    CHECK(view.IndexCount() == 24);
    CHECK(view.Id() == ResourceId::FromValue(7));
    CHECK(Near(view.Volume(), 48.0f, 1e-3f));
    CHECK(view.Extent()[2] == 3.0f);
    for (u32 f = 0; f < view.FaceCount(); ++f)
    {
        CHECK(view.FaceVertexCount(f) == 4);
        const Array<f32, 4> pl = view.Plane(f);
        for (u32 k = 0; k < 4; ++k)
        {
            const Array<f32, 3> v = view.Vertex(view.Index(view.FaceFirst(f) + k));
            CHECK(Near(pl[0] * v[0] + pl[1] * v[1] + pl[2] * v[2], pl[3], 1e-5f));
        }
    }
}

TEST_CASE("hull: the minter reads the source text and reports line numbers", "[asset][hull]")
{
    TypeRegistry types;
    const char* good =
        "# a unit cube\nid 00000000000000bb\nv 0 0 0\nv 1 0 0\nv 0 1 0\nv 1 1 0\nv 0 0 1\nv 1 0 1\nv 0 1 1\nv 1 1 1\n";
    Vector<byte> blob;
    HullSourceError error;
    REQUIRE(MintHull(StringView(good), types, &blob, &error).Ok());
    Vector<byte> again;
    REQUIRE(MintHull(StringView(good), types, &again, &error).Ok());
    REQUIRE(blob.Size() == again.Size());
    CHECK(MemCompare(blob.Data(), again.Data(), blob.Size()) == 0);
    HullView view;
    REQUIRE(view.Open(CopyShared(ByteSpan(blob.Data(), blob.Size())), types).Ok());
    CHECK(Near(view.Volume(), 1.0f, 1e-5f));

    struct Case
    {
        const char* text;
        u32 line;
    };
    const Case cases[] = {{"v 0 0 0\n", 1},
                          {"id 00000000000000bb\nid 00000000000000bb\n", 2},
                          {"id 00000000000000bb\nv 0 0\n", 2},
                          {"id 00000000000000bb\nv 0 0 x\n", 2},
                          {"id 00000000000000bb\nw 0 0 0\n", 2},
                          {"id 00000000000000bb\n", 0},
                          {"v 1 2 3\n", 1},
                          {"id 00000000000000bb\nv 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 0.000001\n", 0}};
    for (const Case& c : cases)
    {
        const Status s = MintHull(StringView(c.text), types, &blob, &error);
        CHECK(s.Failed());
        CHECK(error.line == c.line);
    }
}

TEST_CASE("hull: hostile blobs are refused", "[asset][hull]")
{
    TypeRegistry types;
    const Vector<byte> good = MintBlob(types, BoxPoints(1, 1, 1));
    REQUIRE(OpenView(types, good).Ok());

    SECTION("an index past the vertices")
    {
        Vector<byte> indices = SectionCopy(types, good, kHullSectionIndices);
        indices[0] = 200;
        CHECK(OpenView(types, WithSection(types, good, kHullSectionIndices, indices)).Code() == ErrorCode::Corrupt);
    }
    SECTION("a face range that does not follow the previous one")
    {
        Vector<byte> faces = SectionCopy(types, good, kHullSectionFaces);
        faces[8] = 1;
        CHECK(OpenView(types, WithSection(types, good, kHullSectionFaces, faces)).Code() == ErrorCode::Corrupt);
    }
    SECTION("a plane whose normal is not a unit vector")
    {
        Vector<byte> planes = SectionCopy(types, good, kHullSectionPlanes);
        const f32 two = 2.0f;
        MemCopy(planes.Data(), &two, 4);
        CHECK(OpenView(types, WithSection(types, good, kHullSectionPlanes, planes)).Code() == ErrorCode::Corrupt);
    }
    SECTION("a vertex that is not finite")
    {
        Vector<byte> vertices = SectionCopy(types, good, kHullSectionVertices);
        const f32 nan = Limits<f32>::QuietNaN();
        MemCopy(vertices.Data() + 4, &nan, 4);
        CHECK(OpenView(types, WithSection(types, good, kHullSectionVertices, vertices)).Code() == ErrorCode::Corrupt);
    }
    SECTION("a header with a count above the limit or a zero volume or trailing bytes")
    {
        Vector<byte> many = SectionCopy(types, good, kHullSectionHeader);
        many[0] = 0;
        many[1] = 1;  // 256 vertices
        CHECK(OpenView(types, WithSection(types, good, kHullSectionHeader, many)).Code() == ErrorCode::Corrupt);
        Vector<byte> flat = SectionCopy(types, good, kHullSectionHeader);
        const f32 zero = 0.0f;
        MemCopy(flat.Data() + 16, &zero, 4);
        CHECK(OpenView(types, WithSection(types, good, kHullSectionHeader, flat)).Code() == ErrorCode::Corrupt);
        Vector<byte> extra = SectionCopy(types, good, kHullSectionHeader);
        extra[kHullHeaderBytes - 1] = 1;
        CHECK(OpenView(types, WithSection(types, good, kHullSectionHeader, extra)).Code() == ErrorCode::Corrupt);
    }
    SECTION("a missing section, another type and truncation")
    {
        BlobWriter partial(types, types::kHull, ResourceId::FromValue(7), 0);
        Vector<byte> header = SectionCopy(types, good, kHullSectionHeader);
        REQUIRE(partial.AddSection(kHullSectionHeader, ByteSpan(header.Data(), header.Size())).Ok());
        Vector<byte> blob;
        REQUIRE(partial.Finish(&blob).Ok());
        CHECK(OpenView(types, blob).Code() == ErrorCode::Corrupt);

        BlobWriter other(types, types::kNames, ResourceId::FromValue(5), 0);
        Vector<byte> not_a_hull;
        REQUIRE(other.Finish(&not_a_hull).Ok());
        CHECK(OpenView(types, not_a_hull).Failed());

        for (usize cut = 0; cut < good.Size(); cut += 11)
        {
            HullView view;
            CHECK(view.Open(CopyShared(ByteSpan(good.Data(), cut)), types).Failed());
        }
    }
}

TEST_CASE("hull: the writer refuses geometry that is not a hull", "[asset][hull]")
{
    TypeRegistry types;
    Vector<byte> out;
    HullGeometry bad;
    REQUIRE(Build(BoxPoints(1, 1, 1), &bad).Ok());
    bad.indices[0] = 99;
    CHECK(BuildHullBlob(types, ResourceId::FromValue(1), bad, &out).Code() == ErrorCode::Invalid);
    REQUIRE(Build(BoxPoints(1, 1, 1), &bad).Ok());
    bad.volume = 0.0f;
    CHECK(BuildHullBlob(types, ResourceId::FromValue(1), bad, &out).Code() == ErrorCode::Invalid);
    REQUIRE(Build(BoxPoints(1, 1, 1), &bad).Ok());
    bad.planes[2][0] = 3.0f;
    CHECK(BuildHullBlob(types, ResourceId::FromValue(1), bad, &out).Code() == ErrorCode::Invalid);
    REQUIRE(Build(BoxPoints(1, 1, 1), &bad).Ok());
    bad.face_count[1] = 2;
    CHECK(BuildHullBlob(types, ResourceId::FromValue(1), bad, &out).Code() == ErrorCode::Invalid);
}

}  // namespace engine_placeholder::asset
