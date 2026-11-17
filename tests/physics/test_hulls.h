// Hulls for the physics tests (Phase 25 Day 393): a convex polyhedron from its faces, with the mass data computed the
// way the minter does (the tests do not link the asset module).
#pragma once

#include "containers/vector.h"
#include "lang/scalar.h"
#include "math/vector.h"
#include "physics/hull.h"
#include "types.h"

namespace engine_placeholder::physics::testing
{

// One face as the vertex numbers, counter-clockwise seen from outside.
struct TestFace
{
    Vector<u16> v;
};

inline ConvexHullConstPtr MakeHullFrom(const Vector<Vec3>& vertices, const Vector<TestFace>& faces)
{
    Vector<Vec3> normals;
    Vector<f32> offsets;
    Vector<u32> first;
    Vector<u32> count;
    Vector<u16> indices;
    f64 volume = 0.0;
    f64 mx = 0.0;
    f64 my = 0.0;
    f64 mz = 0.0;
    f64 c[6] = {0, 0, 0, 0, 0, 0};
    for (usize f = 0; f < faces.Size(); ++f)
    {
        const Vector<u16>& loop = faces[f].v;
        f64 nx = 0.0;
        f64 ny = 0.0;
        f64 nz = 0.0;
        f64 cx = 0.0;
        f64 cy = 0.0;
        f64 cz = 0.0;
        for (usize k = 0; k < loop.Size(); ++k)
        {
            const Vec3 a = vertices[loop[k]];
            const Vec3 b = vertices[loop[(k + 1) % loop.Size()]];
            nx += static_cast<f64>(a.y - b.y) * (a.z + b.z);
            ny += static_cast<f64>(a.z - b.z) * (a.x + b.x);
            nz += static_cast<f64>(a.x - b.x) * (a.y + b.y);
            cx += a.x;
            cy += a.y;
            cz += a.z;
        }
        const f64 len = Sqrt(nx * nx + ny * ny + nz * nz);
        nx /= len;
        ny /= len;
        nz /= len;
        const f64 n = static_cast<f64>(loop.Size());
        normals.Push(Vec3(static_cast<f32>(nx), static_cast<f32>(ny), static_cast<f32>(nz)));
        offsets.Push(static_cast<f32>(nx * cx / n + ny * cy / n + nz * cz / n));
        first.Push(static_cast<u32>(indices.Size()));
        count.Push(static_cast<u32>(loop.Size()));
        for (usize k = 0; k < loop.Size(); ++k)
        {
            indices.Push(loop[k]);
        }
        const Vec3 a = vertices[loop[0]];
        for (usize k = 1; k + 1 < loop.Size(); ++k)
        {
            const Vec3 b = vertices[loop[k]];
            const Vec3 d = vertices[loop[k + 1]];
            const f64 v = (static_cast<f64>(a.x) * (static_cast<f64>(b.y) * d.z - static_cast<f64>(b.z) * d.y) -
                           static_cast<f64>(a.y) * (static_cast<f64>(b.x) * d.z - static_cast<f64>(b.z) * d.x) +
                           static_cast<f64>(a.z) * (static_cast<f64>(b.x) * d.y - static_cast<f64>(b.y) * d.x)) /
                          6.0;
            volume += v;
            mx += v * (static_cast<f64>(a.x) + b.x + d.x) * 0.25;
            my += v * (static_cast<f64>(a.y) + b.y + d.y) * 0.25;
            mz += v * (static_cast<f64>(a.z) + b.z + d.z) * 0.25;
            const f64 sx = static_cast<f64>(a.x) + b.x + d.x;
            const f64 sy = static_cast<f64>(a.y) + b.y + d.y;
            const f64 sz = static_cast<f64>(a.z) + b.z + d.z;
            const f64 px[4] = {a.x, b.x, d.x, sx};
            const f64 py[4] = {a.y, b.y, d.y, sy};
            const f64 pz[4] = {a.z, b.z, d.z, sz};
            for (u32 q = 0; q < 4; ++q)
            {
                c[0] += v / 20.0 * px[q] * px[q];
                c[1] += v / 20.0 * py[q] * py[q];
                c[2] += v / 20.0 * pz[q] * pz[q];
                c[3] += v / 20.0 * px[q] * py[q];
                c[4] += v / 20.0 * px[q] * pz[q];
                c[5] += v / 20.0 * py[q] * pz[q];
            }
        }
    }
    const f64 gx = mx / volume;
    const f64 gy = my / volume;
    const f64 gz = mz / volume;
    c[0] -= volume * gx * gx;
    c[1] -= volume * gy * gy;
    c[2] -= volume * gz * gz;
    c[3] -= volume * gx * gy;
    c[4] -= volume * gx * gz;
    c[5] -= volume * gy * gz;
    ConvexHullSpec spec;
    spec.vertices = Span<const Vec3>(vertices.Data(), vertices.Size());
    spec.normals = Span<const Vec3>(normals.Data(), normals.Size());
    spec.offsets = Span<const f32>(offsets.Data(), offsets.Size());
    spec.face_first = Span<const u32>(first.Data(), first.Size());
    spec.face_count = Span<const u32>(count.Data(), count.Size());
    spec.indices = Span<const u16>(indices.Data(), indices.Size());
    spec.volume = static_cast<f32>(volume);
    spec.centroid = Vec3(static_cast<f32>(gx), static_cast<f32>(gy), static_cast<f32>(gz));
    spec.inertia = {{static_cast<f32>(c[1] + c[2]), static_cast<f32>(c[0] + c[2]), static_cast<f32>(c[0] + c[1]),
                     static_cast<f32>(-c[3]), static_cast<f32>(-c[4]), static_cast<f32>(-c[5])}};
    Validation result = Validation::Ok;
    ConvexHullConstPtr made = ConvexHull::Create(spec, &result);
    return made;
}

// Makes the hull from faces wound either way: a hull wound inward has a negative volume and is refused, so the faces
// are turned around and tried again.
inline ConvexHullConstPtr MakeHullAuto(const Vector<Vec3>& vertices, const Vector<TestFace>& faces)
{
    ConvexHullConstPtr hull = MakeHullFrom(vertices, faces);
    if (hull)
    {
        return hull;
    }
    Vector<TestFace> turned;
    for (usize f = 0; f < faces.Size(); ++f)
    {
        TestFace t;
        for (usize k = faces[f].v.Size(); k > 0; --k)
        {
            t.v.Push(faces[f].v[k - 1]);
        }
        turned.Push(Move(t));
    }
    return MakeHullFrom(vertices, turned);
}

inline TestFace Quad(u16 a, u16 b, u16 c, u16 d)
{
    TestFace f;
    f.v.Push(a);
    f.v.Push(b);
    f.v.Push(c);
    f.v.Push(d);
    return f;
}

// A box as a hull: eight corners (bit 0 x, bit 1 y, bit 2 z, set means positive), six quads.
inline ConvexHullConstPtr BoxHull(const Vec3& h, const Vec3& offset = Vec3(0.0f, 0.0f, 0.0f))
{
    Vector<Vec3> v;
    for (u32 i = 0; i < 8; ++i)
    {
        v.Push(Vec3(((i & 1) ? h.x : -h.x) + offset.x, ((i & 2) ? h.y : -h.y) + offset.y,
                    ((i & 4) ? h.z : -h.z) + offset.z));
    }
    Vector<TestFace> f;
    f.Push(Quad(1, 3, 7, 5));  // +x
    f.Push(Quad(0, 4, 6, 2));  // -x
    f.Push(Quad(2, 6, 7, 3));  // +y
    f.Push(Quad(0, 1, 5, 4));  // -y
    f.Push(Quad(4, 5, 7, 6));  // +z
    f.Push(Quad(0, 2, 3, 1));  // -z
    return MakeHullAuto(v, f);
}

// A prism with `sides` corners on a circle of radius `r` and half height `h`: two caps and `sides` quads.
inline ConvexHullConstPtr PrismHull(u32 sides, f32 r, f32 h)
{
    Vector<Vec3> v;
    for (u32 i = 0; i < sides; ++i)
    {
        const f32 a = 6.2831853f * static_cast<f32>(i) / static_cast<f32>(sides);
        v.Push(Vec3(r * Cos(a), -h, r * Sin(a)));
        v.Push(Vec3(r * Cos(a), h, r * Sin(a)));
    }
    Vector<TestFace> f;
    for (u32 i = 0; i < sides; ++i)
    {
        const u16 b0 = static_cast<u16>(2 * i);
        const u16 b1 = static_cast<u16>(2 * ((i + 1) % sides));
        // Seen from outside (x, z around y): the corners go bottom, bottom next, top next, top; the order is chosen so
        // that the normal faces outward (the helper normalizes whichever way the face is wound, so check by volume).
        f.Push(Quad(b0, b1, static_cast<u16>(b1 + 1), static_cast<u16>(b0 + 1)));
    }
    TestFace top;
    TestFace bottom;
    for (u32 i = 0; i < sides; ++i)
    {
        top.v.Push(static_cast<u16>(2 * i + 1));
        bottom.v.Push(static_cast<u16>(2 * (sides - 1 - i)));
    }
    f.Push(Move(top));
    f.Push(Move(bottom));
    return MakeHullAuto(v, f);
}

// A tetrahedron with corners (0,0,0), (1,0,0), (0,1,0), (0,0,1).
inline ConvexHullConstPtr TetraHull()
{
    Vector<Vec3> v;
    v.Push(Vec3(0, 0, 0));
    v.Push(Vec3(1, 0, 0));
    v.Push(Vec3(0, 1, 0));
    v.Push(Vec3(0, 0, 1));
    Vector<TestFace> f;
    auto tri = [](u16 a, u16 b, u16 c)
    {
        TestFace t;
        t.v.Push(a);
        t.v.Push(b);
        t.v.Push(c);
        return t;
    };
    f.Push(tri(0, 2, 1));  // z = 0, outward -z
    f.Push(tri(0, 1, 3));  // y = 0
    f.Push(tri(0, 3, 2));  // x = 0
    f.Push(tri(1, 2, 3));  // the slanted face
    return MakeHullAuto(v, f);
}

}  // namespace engine_placeholder::physics::testing
