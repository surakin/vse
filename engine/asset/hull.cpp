#include "asset/hull.h"

#include "cstr/mem_ops.h"
#include "lang/scalar.h"

namespace engine_placeholder::asset
{
namespace
{

struct D3
{
    f64 x = 0;
    f64 y = 0;
    f64 z = 0;
};

D3 Make(f64 x, f64 y, f64 z)
{
    D3 v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}

D3 Sub(const D3& a, const D3& b)
{
    return Make(a.x - b.x, a.y - b.y, a.z - b.z);
}

D3 Add(const D3& a, const D3& b)
{
    return Make(a.x + b.x, a.y + b.y, a.z + b.z);
}

D3 Scale(const D3& a, f64 s)
{
    return Make(a.x * s, a.y * s, a.z * s);
}

f64 Dot(const D3& a, const D3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

D3 Cross(const D3& a, const D3& b)
{
    return Make(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

f64 Length(const D3& a)
{
    return Sqrt(Dot(a, a));
}

struct Tri
{
    u32 v[3] = {0, 0, 0};
    D3 n;
    f64 d = 0;
    bool alive = true;
};

Status HullInvalid(const char* message)
{
    return Status::Make(ErrorCode::Invalid, message);
}

Status HullCorrupt(const char* message)
{
    return Status::Make(ErrorCode::Corrupt, message);
}

void StoreU32(byte* p, u32 value)
{
    for (u32 i = 0; i < 4; ++i)
    {
        p[i] = static_cast<byte>(value >> (8 * i));
    }
}

u32 LoadU32(const byte* p)
{
    u32 v = 0;
    for (u32 i = 0; i < 4; ++i)
    {
        v |= static_cast<u32>(p[i]) << (8 * i);
    }
    return v;
}

f32 LoadF32(const byte* p)
{
    f32 v;
    MemCopy(&v, p, 4);
    return v;
}

bool AllFinite(const f32* values, usize count)
{
    for (usize i = 0; i < count; ++i)
    {
        if (!IsFinite(values[i]))
        {
            return false;
        }
    }
    return true;
}

void AppendF32(Vector<byte>* out, const f32* values, usize count)
{
    const usize at = out->Size();
    out->Resize(at + count * 4);
    MemCopy(out->Data() + at, values, count * 4);
}

Tri MakeTri(const Vector<D3>& p, u32 a, u32 b, u32 c)
{
    Tri t;
    t.v[0] = a;
    t.v[1] = b;
    t.v[2] = c;
    D3 n = Cross(Sub(p[b], p[a]), Sub(p[c], p[a]));
    const f64 len = Length(n);
    if (len > 1e-300)
    {
        n = Scale(n, 1.0 / len);
    }
    t.n = n;
    t.d = Dot(n, p[a]);
    return t;
}

bool HasEdge(const Tri& t, u32 a, u32 b)
{
    for (u32 k = 0; k < 3; ++k)
    {
        if (t.v[k] == a && t.v[(k + 1) % 3] == b)
        {
            return true;
        }
    }
    return false;
}

}  // namespace

Status BuildHullGeometry(Span<const Array<f32, 3>> points, HullGeometry* out)
{
    *out = HullGeometry();
    if (points.Size() < 4)
    {
        return HullInvalid("a hull needs at least four points");
    }
    if (points.Size() > kMaxHullInputPoints)
    {
        return Status::Make(ErrorCode::LimitExceeded, "a hull takes at most 4096 input points");
    }
    D3 lo = Make(1e300, 1e300, 1e300);
    D3 hi = Make(-1e300, -1e300, -1e300);
    for (usize i = 0; i < points.Size(); ++i)
    {
        if (!AllFinite(points[i].Data(), 3))
        {
            return HullInvalid("a point is not finite");
        }
        lo = Make(FMin(lo.x, f64{points[i][0]}), FMin(lo.y, f64{points[i][1]}), FMin(lo.z, f64{points[i][2]}));
        hi = Make(FMax(hi.x, f64{points[i][0]}), FMax(hi.y, f64{points[i][1]}), FMax(hi.z, f64{points[i][2]}));
    }
    const f64 scale = FMax(FMax(hi.x - lo.x, hi.y - lo.y), hi.z - lo.z);
    if (!(scale > 0.0))
    {
        return HullInvalid("the points are all the same point");
    }

    // Weld: a point within 1e-6 of the size of an earlier kept point is dropped. `original` keeps input order.
    Vector<D3> p;
    Vector<u32> original;
    const f64 weld2 = (1e-6 * scale) * (1e-6 * scale);
    for (usize i = 0; i < points.Size(); ++i)
    {
        const D3 q = Make(points[i][0], points[i][1], points[i][2]);
        bool near = false;
        for (usize j = 0; j < p.Size() && !near; ++j)
        {
            const D3 d = Sub(q, p[j]);
            near = Dot(d, d) <= weld2;
        }
        if (!near)
        {
            p.Push(q);
            original.Push(static_cast<u32>(i));
        }
    }
    const u32 m = static_cast<u32>(p.Size());
    if (m < 4)
    {
        return HullInvalid("the points are on a line or a plane or too close together");
    }

    // The first simplex: the two most distant extreme points, the point farthest from their line, the one farthest from
    // that plane.
    const f64 flat = 1e-5 * scale;
    u32 extreme[6] = {0, 0, 0, 0, 0, 0};
    for (u32 i = 1; i < m; ++i)
    {
        if (p[i].x < p[extreme[0]].x)
        {
            extreme[0] = i;
        }
        if (p[i].x > p[extreme[1]].x)
        {
            extreme[1] = i;
        }
        if (p[i].y < p[extreme[2]].y)
        {
            extreme[2] = i;
        }
        if (p[i].y > p[extreme[3]].y)
        {
            extreme[3] = i;
        }
        if (p[i].z < p[extreme[4]].z)
        {
            extreme[4] = i;
        }
        if (p[i].z > p[extreme[5]].z)
        {
            extreme[5] = i;
        }
    }
    u32 i0 = extreme[0];
    u32 i1 = extreme[1];
    f64 best = -1.0;
    for (u32 a = 0; a < 6; ++a)
    {
        for (u32 b = a + 1; b < 6; ++b)
        {
            const D3 d = Sub(p[extreme[a]], p[extreme[b]]);
            if (Dot(d, d) > best)
            {
                best = Dot(d, d);
                i0 = extreme[a];
                i1 = extreme[b];
            }
        }
    }
    const D3 axis = Sub(p[i1], p[i0]);
    const f64 axis_len = Length(axis);
    if (axis_len < flat)
    {
        return HullInvalid("the points are all the same point");
    }
    u32 i2 = i0;
    best = -1.0;
    for (u32 i = 0; i < m; ++i)
    {
        const f64 dist = Length(Cross(axis, Sub(p[i], p[i0]))) / axis_len;
        if (dist > best)
        {
            best = dist;
            i2 = i;
        }
    }
    if (best < flat)
    {
        return HullInvalid("the points are on a line");
    }
    D3 plane_n = Cross(axis, Sub(p[i2], p[i0]));
    plane_n = Scale(plane_n, 1.0 / Length(plane_n));
    u32 i3 = i0;
    best = -1.0;
    for (u32 i = 0; i < m; ++i)
    {
        const f64 dist = Abs(Dot(plane_n, Sub(p[i], p[i0])));
        if (dist > best)
        {
            best = dist;
            i3 = i;
        }
    }
    if (best < flat)
    {
        return HullInvalid("the points are on a plane (the hull would have no volume)");
    }

    // Four faces, wound so that every normal points away from the middle of the simplex.
    Vector<Tri> tris;
    {
        const D3 inside = Scale(Add(Add(p[i0], p[i1]), Add(p[i2], p[i3])), 0.25);
        const u32 faces[4][3] = {{i0, i1, i2}, {i0, i3, i1}, {i1, i3, i2}, {i2, i3, i0}};
        for (u32 f = 0; f < 4; ++f)
        {
            Tri t = MakeTri(p, faces[f][0], faces[f][1], faces[f][2]);
            if (Dot(t.n, Sub(inside, p[faces[f][0]])) > 0.0)
            {
                t = MakeTri(p, faces[f][0], faces[f][2], faces[f][1]);
            }
            tris.Push(t);
        }
    }

    // Add the rest, in order: a point outside some faces removes them and joins the hole's rim to itself.
    const f64 visible_eps = 1e-7 * scale;
    for (u32 i = 0; i < m; ++i)
    {
        if (i == i0 || i == i1 || i == i2 || i == i3)
        {
            continue;
        }
        Vector<u32> visible;
        for (u32 t = 0; t < tris.Size(); ++t)
        {
            if (tris[t].alive && Dot(tris[t].n, p[i]) - tris[t].d > visible_eps)
            {
                visible.Push(t);
            }
        }
        if (visible.Size() == 0)
        {
            continue;
        }
        Vector<bool> is_visible;
        is_visible.Resize(tris.Size());
        for (u32 t = 0; t < tris.Size(); ++t)
        {
            is_visible[t] = false;
        }
        for (usize k = 0; k < visible.Size(); ++k)
        {
            is_visible[visible[k]] = true;
        }
        Vector<u32> rim;  // pairs (a, b) of directed edges of the visible faces whose twin is not visible
        for (usize k = 0; k < visible.Size(); ++k)
        {
            const Tri t = tris[visible[k]];
            for (u32 e = 0; e < 3; ++e)
            {
                const u32 a = t.v[e];
                const u32 b = t.v[(e + 1) % 3];
                bool twin_visible = false;
                for (u32 o = 0; o < tris.Size() && !twin_visible; ++o)
                {
                    twin_visible = tris[o].alive && is_visible[o] && HasEdge(tris[o], b, a);
                }
                if (!twin_visible)
                {
                    rim.Push(a);
                    rim.Push(b);
                }
            }
        }
        for (usize k = 0; k < visible.Size(); ++k)
        {
            tris[visible[k]].alive = false;
        }
        for (usize k = 0; k + 1 < rim.Size(); k += 2)
        {
            tris.Push(MakeTri(p, rim[k], rim[k + 1], i));
        }
    }

    // Merge the triangles of one plane into polygons.
    Vector<u32> group_of;
    group_of.Resize(tris.Size());
    Vector<u32> group_rep;  // the first triangle of each group
    for (u32 t = 0; t < tris.Size(); ++t)
    {
        group_of[t] = 0xFFFFFFFFu;
        if (!tris[t].alive)
        {
            continue;
        }
        u32 found = 0xFFFFFFFFu;
        for (u32 g = 0; g < group_rep.Size() && found == 0xFFFFFFFFu; ++g)
        {
            const Tri& r = tris[group_rep[g]];
            if (1.0 - Dot(r.n, tris[t].n) < 1e-8 && Abs(r.d - tris[t].d) < 1e-6 * scale)
            {
                found = g;
            }
        }
        if (found == 0xFFFFFFFFu)
        {
            found = static_cast<u32>(group_rep.Size());
            group_rep.Push(t);
        }
        group_of[t] = found;
    }

    Vector<Vector<u32>> loops;  // each a face's vertices in `p` numbering
    for (u32 g = 0; g < group_rep.Size(); ++g)
    {
        // The boundary of the group: its directed edges whose twin is not in the group.
        Vector<u32> edges;
        for (u32 t = 0; t < tris.Size(); ++t)
        {
            if (group_of[t] != g)
            {
                continue;
            }
            for (u32 e = 0; e < 3; ++e)
            {
                const u32 a = tris[t].v[e];
                const u32 b = tris[t].v[(e + 1) % 3];
                bool twin = false;
                for (u32 o = 0; o < tris.Size() && !twin; ++o)
                {
                    twin = group_of[o] == g && HasEdge(tris[o], b, a);
                }
                if (!twin)
                {
                    edges.Push(a);
                    edges.Push(b);
                }
            }
        }
        // Follow the edges from the smallest start vertex.
        Vector<u32> loop;
        bool simple = edges.Size() >= 6;
        if (simple)
        {
            u32 start = edges[0];
            for (usize k = 0; k < edges.Size(); k += 2)
            {
                start = edges[k] < start ? edges[k] : start;
            }
            u32 at = start;
            do
            {
                loop.Push(at);
                u32 next = 0xFFFFFFFFu;
                u32 starts = 0;
                for (usize k = 0; k < edges.Size(); k += 2)
                {
                    if (edges[k] == at)
                    {
                        next = edges[k + 1];
                        ++starts;
                    }
                }
                if (starts != 1 || loop.Size() > edges.Size() / 2)
                {
                    simple = false;
                    break;
                }
                at = next;
            } while (at != start);
            simple = simple && loop.Size() == edges.Size() / 2;
        }
        if (simple)
        {
            loops.Push(Move(loop));
        }
        else
        {
            // A group that does not make one clean loop stays as its triangles.
            for (u32 t = 0; t < tris.Size(); ++t)
            {
                if (group_of[t] == g)
                {
                    Vector<u32> tri;
                    tri.Push(tris[t].v[0]);
                    tri.Push(tris[t].v[1]);
                    tri.Push(tris[t].v[2]);
                    loops.Push(Move(tri));
                }
            }
        }
    }

    // The vertices that remain, in input order.
    Vector<u32> remap;
    remap.Resize(m);
    for (u32 i = 0; i < m; ++i)
    {
        remap[i] = 0xFFFFFFFFu;
    }
    for (usize f = 0; f < loops.Size(); ++f)
    {
        for (usize k = 0; k < loops[f].Size(); ++k)
        {
            remap[loops[f][k]] = 0;
        }
    }
    u32 vertex_total = 0;
    Vector<u32> kept;
    for (u32 i = 0; i < m; ++i)
    {
        if (remap[i] == 0)
        {
            remap[i] = vertex_total++;
            kept.Push(i);
        }
    }
    if (vertex_total > kMaxHullVertices)
    {
        return Status::Make(ErrorCode::LimitExceeded, "the hull has more than 255 vertices");
    }
    if (loops.Size() > kMaxHullFaces)
    {
        return Status::Make(ErrorCode::LimitExceeded, "the hull has more than 512 faces");
    }

    // Volume, centroid and inertia from the faces as fans of triangles, each with the origin as the fourth corner.
    f64 volume = 0;
    D3 moment = Make(0, 0, 0);
    f64 c[6] = {0, 0, 0, 0, 0, 0};  // xx yy zz xy xz yz of the second moment about the origin
    for (usize f = 0; f < loops.Size(); ++f)
    {
        const Vector<u32>& loop = loops[f];
        if (loop.Size() > kMaxHullFaceVertices)
        {
            return Status::Make(ErrorCode::LimitExceeded, "a hull face has more than 64 vertices");
        }
        const D3& a = p[loop[0]];
        for (usize k = 1; k + 1 < loop.Size(); ++k)
        {
            const D3& b = p[loop[k]];
            const D3& cc = p[loop[k + 1]];
            const f64 v = Dot(a, Cross(b, cc)) / 6.0;
            volume += v;
            moment = Add(moment, Scale(Add(Add(a, b), cc), v * 0.25));
            const D3 s = Add(Add(a, b), cc);
            const D3 vs[4] = {a, b, cc, s};
            for (u32 q = 0; q < 4; ++q)
            {
                c[0] += v / 20.0 * vs[q].x * vs[q].x;
                c[1] += v / 20.0 * vs[q].y * vs[q].y;
                c[2] += v / 20.0 * vs[q].z * vs[q].z;
                c[3] += v / 20.0 * vs[q].x * vs[q].y;
                c[4] += v / 20.0 * vs[q].x * vs[q].z;
                c[5] += v / 20.0 * vs[q].y * vs[q].z;
            }
        }
    }
    if (!(volume > 1e-12 * scale * scale * scale))
    {
        return HullInvalid("the hull has no volume");
    }
    const D3 g = Scale(moment, 1.0 / volume);
    c[0] -= volume * g.x * g.x;
    c[1] -= volume * g.y * g.y;
    c[2] -= volume * g.z * g.z;
    c[3] -= volume * g.x * g.y;
    c[4] -= volume * g.x * g.z;
    c[5] -= volume * g.y * g.z;

    HullGeometry hull;
    for (u32 i = 0; i < kept.Size(); ++i)
    {
        const u32 src = original[kept[i]];
        Array<f32, 3> v = {{points[src][0], points[src][1], points[src][2]}};
        hull.vertices.Push(v);
        for (u32 k = 0; k < 3; ++k)
        {
            hull.extent[k] = FMax(hull.extent[k], Abs(v[k]));
        }
    }
    for (usize f = 0; f < loops.Size(); ++f)
    {
        const Vector<u32>& loop = loops[f];
        // The face normal by Newell's method, and the plane through the mean of the corners.
        D3 n = Make(0, 0, 0);
        D3 mean = Make(0, 0, 0);
        for (usize k = 0; k < loop.Size(); ++k)
        {
            const D3& a = p[loop[k]];
            const D3& b = p[loop[(k + 1) % loop.Size()]];
            n = Add(n, Make((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y)));
            mean = Add(mean, a);
        }
        n = Scale(n, 1.0 / Length(n));
        mean = Scale(mean, 1.0 / static_cast<f64>(loop.Size()));
        Array<f32, 4> plane = {
            {static_cast<f32>(n.x), static_cast<f32>(n.y), static_cast<f32>(n.z), static_cast<f32>(Dot(n, mean))}};
        hull.planes.Push(plane);
        hull.face_first.Push(static_cast<u32>(hull.indices.Size()));
        hull.face_count.Push(static_cast<u32>(loop.Size()));
        for (usize k = 0; k < loop.Size(); ++k)
        {
            hull.indices.Push(static_cast<u16>(remap[loop[k]]));
        }
    }
    hull.volume = static_cast<f32>(volume);
    hull.centroid = {{static_cast<f32>(g.x), static_cast<f32>(g.y), static_cast<f32>(g.z)}};
    // The tensor: I = tr(C) E - C, so xx = Cyy + Czz and xy = -Cxy.
    hull.inertia = {{static_cast<f32>(c[1] + c[2]), static_cast<f32>(c[0] + c[2]), static_cast<f32>(c[0] + c[1]),
                     static_cast<f32>(-c[3]), static_cast<f32>(-c[4]), static_cast<f32>(-c[5])}};
    *out = Move(hull);
    return Status::Success();
}

Status BuildHullBlob(const TypeRegistry& types, ResourceId id, const HullGeometry& hull, Vector<byte>* out, u32 target)
{
    const usize nv = hull.vertices.Size();
    const usize nf = hull.planes.Size();
    const usize ni = hull.indices.Size();
    if (nv < 4 || nv > kMaxHullVertices)
    {
        return HullInvalid("a hull has 4 to 255 vertices");
    }
    if (nf < 4 || nf > kMaxHullFaces || hull.face_first.Size() != nf || hull.face_count.Size() != nf)
    {
        return HullInvalid("a hull has 4 to 512 faces, each with a plane, a first index and a count");
    }
    for (usize v = 0; v < nv; ++v)
    {
        if (!AllFinite(hull.vertices[v].Data(), 3))
        {
            return HullInvalid("a hull vertex is not finite");
        }
    }
    usize used = 0;
    for (usize f = 0; f < nf; ++f)
    {
        const u32 first = hull.face_first[f];
        const u32 count = hull.face_count[f];
        if (count < 3 || count > kMaxHullFaceVertices || first != used || first + count > ni)
        {
            return HullInvalid("a hull face has fewer than 3 or more than 64 vertices or its range is not in order");
        }
        used += count;
        for (u32 k = 0; k < count; ++k)
        {
            if (hull.indices[first + k] >= nv)
            {
                return HullInvalid("a hull face names a vertex that does not exist");
            }
        }
        const Array<f32, 4>& pl = hull.planes[f];
        const f32 len = Sqrt(pl[0] * pl[0] + pl[1] * pl[1] + pl[2] * pl[2]);
        if (!AllFinite(pl.Data(), 4) || Abs(len - 1.0f) > 1e-3f)
        {
            return HullInvalid("a hull plane is not finite or its normal is not a unit vector");
        }
    }
    if (used != ni)
    {
        return HullInvalid("the hull index list has entries that no face uses");
    }
    if (!IsFinite(hull.volume) || !(hull.volume > 0.0f) || !AllFinite(hull.centroid.Data(), 3) ||
        !AllFinite(hull.inertia.Data(), 6) || !AllFinite(hull.extent.Data(), 3))
    {
        return HullInvalid("the hull volume is not positive or a mass value is not finite");
    }

    Vector<byte> header;
    header.Resize(kHullHeaderBytes);
    MemSet(header.Data(), 0, header.Size());
    StoreU32(header.Data(), static_cast<u32>(nv));
    StoreU32(header.Data() + 4, static_cast<u32>(nf));
    StoreU32(header.Data() + 8, static_cast<u32>(ni));
    MemCopy(header.Data() + 16, &hull.volume, 4);
    MemCopy(header.Data() + 20, hull.centroid.Data(), 12);
    MemCopy(header.Data() + 32, hull.inertia.Data(), 24);
    MemCopy(header.Data() + 56, hull.extent.Data(), 12);

    Vector<byte> vertices;
    Vector<byte> planes;
    Vector<byte> faces;
    Vector<byte> indices;
    for (usize v = 0; v < nv; ++v)
    {
        AppendF32(&vertices, hull.vertices[v].Data(), 3);
    }
    faces.Resize(nf * 8);
    for (usize f = 0; f < nf; ++f)
    {
        AppendF32(&planes, hull.planes[f].Data(), 4);
        StoreU32(faces.Data() + f * 8, hull.face_first[f]);
        StoreU32(faces.Data() + f * 8 + 4, hull.face_count[f]);
    }
    indices.Resize(ni * 2);
    for (usize k = 0; k < ni; ++k)
    {
        indices[k * 2] = static_cast<byte>(hull.indices[k] & 0xFF);
        indices[k * 2 + 1] = static_cast<byte>(hull.indices[k] >> 8);
    }

    BlobWriter writer(types, types::kHull, id, target);
    ENGINE_TRY(writer.AddSection(kHullSectionHeader, ByteSpan(header.Data(), header.Size())));
    ENGINE_TRY(writer.AddSection(kHullSectionVertices, ByteSpan(vertices.Data(), vertices.Size())));
    ENGINE_TRY(writer.AddSection(kHullSectionPlanes, ByteSpan(planes.Data(), planes.Size())));
    ENGINE_TRY(writer.AddSection(kHullSectionFaces, ByteSpan(faces.Data(), faces.Size())));
    ENGINE_TRY(writer.AddSection(kHullSectionIndices, ByteSpan(indices.Data(), indices.Size())));
    return writer.Finish(out);
}

Status HullView::Open(const SharedBytesPtr& bytes, const TypeRegistry& types)
{
    *this = HullView();
    if (!bytes)
    {
        return Status::Make(ErrorCode::Invalid, "no bytes were given");
    }
    const ByteSpan data = bytes->Bytes();
    BlobReader blob;
    ENGINE_TRY(blob.Open(data, types));
    if (blob.Type() != types::kHull)
    {
        return Status::Make(ErrorCode::TypeMismatch, "the blob is not a hull");
    }
    ByteSpan header;
    if (!blob.FindSection(kHullSectionHeader, &header) || header.Size() != kHullHeaderBytes)
    {
        return HullCorrupt("the hull header section is missing or has the wrong size");
    }
    const u32 nv = LoadU32(header.Data());
    const u32 nf = LoadU32(header.Data() + 4);
    const u32 ni = LoadU32(header.Data() + 8);
    if (nv < 4 || nv > kMaxHullVertices || nf < 4 || nf > kMaxHullFaces || ni < nf * 3 ||
        ni > nf * kMaxHullFaceVertices)
    {
        return HullCorrupt("a hull count is outside its limits");
    }
    if (LoadU32(header.Data() + 12) != 0)
    {
        return HullCorrupt("a hull header field holds a value this version does not define");
    }
    for (usize k = 68; k < kHullHeaderBytes; ++k)
    {
        if (header.Data()[k] != 0)
        {
            return HullCorrupt("a hull header field holds a value this version does not define");
        }
    }
    const f32 volume = LoadF32(header.Data() + 16);
    Array<f32, 3> centroid = {};
    Array<f32, 6> inertia = {};
    Array<f32, 3> extent = {};
    MemCopy(centroid.Data(), header.Data() + 20, 12);
    MemCopy(inertia.Data(), header.Data() + 32, 24);
    MemCopy(extent.Data(), header.Data() + 56, 12);
    if (!IsFinite(volume) || !(volume > 0.0f) || !AllFinite(centroid.Data(), 3) || !AllFinite(inertia.Data(), 6) ||
        !AllFinite(extent.Data(), 3))
    {
        return HullCorrupt("a hull mass value is not finite or the volume is not positive");
    }
    ByteSpan vertices;
    ByteSpan planes;
    ByteSpan faces;
    ByteSpan indices;
    struct Need
    {
        u32 id;
        usize size;
        ByteSpan* out;
    };
    const Need needs[] = {{kHullSectionVertices, usize{nv} * 12, &vertices},
                          {kHullSectionPlanes, usize{nf} * 16, &planes},
                          {kHullSectionFaces, usize{nf} * 8, &faces},
                          {kHullSectionIndices, usize{ni} * 2, &indices}};
    for (const Need& need : needs)
    {
        if (!blob.FindSection(need.id, need.out) || need.out->Size() != need.size)
        {
            return HullCorrupt("a hull section is missing or has the wrong size");
        }
    }
    for (u32 v = 0; v < nv; ++v)
    {
        f32 x[3];
        MemCopy(x, vertices.Data() + v * 12, 12);
        if (!AllFinite(x, 3))
        {
            return HullCorrupt("a hull vertex is not finite");
        }
    }
    u32 next = 0;
    for (u32 f = 0; f < nf; ++f)
    {
        f32 pl[4];
        MemCopy(pl, planes.Data() + f * 16, 16);
        const f32 len = Sqrt(pl[0] * pl[0] + pl[1] * pl[1] + pl[2] * pl[2]);
        if (!AllFinite(pl, 4) || Abs(len - 1.0f) > 1e-3f)
        {
            return HullCorrupt("a hull plane is not finite or its normal is not a unit vector");
        }
        const u32 first = LoadU32(faces.Data() + f * 8);
        const u32 count = LoadU32(faces.Data() + f * 8 + 4);
        if (first != next || count < 3 || count > kMaxHullFaceVertices || count > ni - first)
        {
            return HullCorrupt("a hull face range is out of order or outside the index list");
        }
        next += count;
    }
    if (next != ni)
    {
        return HullCorrupt("the hull index list has entries that no face uses");
    }
    for (u32 k = 0; k < ni; ++k)
    {
        const u32 index = static_cast<u32>(indices.Data()[k * 2]) | (static_cast<u32>(indices.Data()[k * 2 + 1]) << 8);
        if (index >= nv)
        {
            return HullCorrupt("a hull face names a vertex that does not exist");
        }
    }
    vertex_count_ = nv;
    face_count_ = nf;
    index_count_ = ni;
    id_ = blob.Id();
    volume_ = volume;
    centroid_ = centroid;
    inertia_ = inertia;
    extent_ = extent;
    bytes_ = bytes;
    vertices_ = static_cast<usize>(vertices.Data() - data.Data());
    planes_ = static_cast<usize>(planes.Data() - data.Data());
    faces_ = static_cast<usize>(faces.Data() - data.Data());
    indices_ = static_cast<usize>(indices.Data() - data.Data());
    return Status::Success();
}

Array<f32, 3> HullView::Vertex(u32 i) const
{
    ENGINE_ASSERT(i < vertex_count_, "HullView::Vertex: index out of range");
    Array<f32, 3> v;
    MemCopy(v.Data(), Base() + vertices_ + i * 12, 12);
    return v;
}

Array<f32, 4> HullView::Plane(u32 i) const
{
    ENGINE_ASSERT(i < face_count_, "HullView::Plane: face index out of range");
    Array<f32, 4> v;
    MemCopy(v.Data(), Base() + planes_ + i * 16, 16);
    return v;
}

u32 HullView::FaceFirst(u32 i) const
{
    ENGINE_ASSERT(i < face_count_, "HullView::FaceFirst: face index out of range");
    return LoadU32(Base() + faces_ + i * 8);
}

u32 HullView::FaceVertexCount(u32 i) const
{
    ENGINE_ASSERT(i < face_count_, "HullView::FaceVertexCount: face index out of range");
    return LoadU32(Base() + faces_ + i * 8 + 4);
}

u32 HullView::Index(u32 k) const
{
    ENGINE_ASSERT(k < index_count_, "HullView::Index: index out of range");
    return static_cast<u32>(Base()[indices_ + k * 2]) | (static_cast<u32>(Base()[indices_ + k * 2 + 1]) << 8);
}

}  // namespace engine_placeholder::asset
