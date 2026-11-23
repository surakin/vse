// Convex hulls in the narrow phase (Phase 25 Day 393): GJK for the distance, EPA for the penetration, and a face
// manifold by clipping when both shapes have faces. Only `+ - * /` and `Sqrt` are used.
#include "containers/array.h"
#include "lang/scalar.h"
#include "physics/collide_math.h"
#include "physics/contact.h"
#include "physics/hull.h"

namespace vse::physics::collide_math
{

namespace
{

// A face whose normal is within this cosine of the contact normal makes a face manifold; other contacts are one point.
constexpr f32 kFaceAlignment = 0.9f;
constexpr u32 kClipCapacity = 160;
constexpr u32 kMaxGjkIterations = 64;
constexpr u32 kMaxEpaIterations = 64;

enum class CoreKind : u8
{
    Point,
    Segment,
    Box,
    Hull,
    Triangle
};

// The core of a shape: the convex set that GJK works on. The sphere and the capsule are a point and a segment with a
// radius that is added afterwards, which keeps the cores' distance well away from zero in a shallow contact.
struct Core
{
    CoreKind kind = CoreKind::Point;
    f32 radius = 0.0f;
    Vec3 position = Vec3(0.0f, 0.0f, 0.0f);
    Mat3 rotation = Mat3::Identity();
    Vec3 p = Vec3(0.0f, 0.0f, 0.0f);  // a segment's ends
    Vec3 q = Vec3(0.0f, 0.0f, 0.0f);
    Vec3 half = Vec3(0.0f, 0.0f, 0.0f);
    ConvexHullConstPtr hull;
    Vec3 r = Vec3(0.0f, 0.0f, 0.0f);   // a triangle's third corner (p and q are the others)
    Vec3 tn = Vec3(0.0f, 1.0f, 0.0f);  // a triangle's front normal
};

Core MakeCore(const Shape& shape, const Pose& pose, const HullLibrary* hulls)
{
    Core c;
    c.position = pose.position;
    c.rotation = pose.rotation.ToMat3();
    switch (shape.kind)
    {
    case ShapeKind::Sphere:
        c.kind = CoreKind::Point;
        c.radius = shape.radius;
        break;
    case ShapeKind::Capsule:
    {
        c.kind = CoreKind::Segment;
        c.radius = shape.radius;
        const Segment s = CapsuleSegment(shape, pose);
        c.p = s.p;
        c.q = s.q;
        break;
    }
    case ShapeKind::Box:
        c.kind = CoreKind::Box;
        c.half = shape.half_extents;
        break;
    case ShapeKind::Compound:  // never reached: composites go through their own pair functions
    case ShapeKind::Mesh:
    case ShapeKind::HeightField:
    case ShapeKind::Hull:
        c.kind = CoreKind::Hull;
        c.hull = hulls != nullptr ? hulls->Share(shape.hull) : ConvexHullConstPtr();
        break;
    }
    return c;
}

// A triangle of a mesh in the world: radius zero, no pose (its corners are world points), two faces (front and back).
Core TriangleCore(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& normal)
{
    Core core;
    core.kind = CoreKind::Triangle;
    core.p = a;
    core.q = b;
    core.r = c;
    core.tn = normal;
    core.position = (a + b + c) * (1.0f / 3.0f);
    return core;
}

Vec3 ToLocal(const Core& c, const Vec3& d)
{
    return Vec3(c.rotation.col[0].Dot(d), c.rotation.col[1].Dot(d), c.rotation.col[2].Dot(d));
}

// The index of the supporting vertex (a hull's vertex number, a box's corner bits, 0 otherwise), for the feature ids.
u32 SupportFeature(const Core& c, const Vec3& dir)
{
    switch (c.kind)
    {
    case CoreKind::Point: return 0;
    case CoreKind::Segment: return dir.Dot(c.q - c.p) >= 0.0f ? 1u : 0u;
    case CoreKind::Box:
    {
        const Vec3 l = ToLocal(c, dir);
        return (l.x >= 0.0f ? 1u : 0u) | (l.y >= 0.0f ? 2u : 0u) | (l.z >= 0.0f ? 4u : 0u);
    }
    case CoreKind::Hull: return c.hull->SupportVertex(ToLocal(c, dir));
    case CoreKind::Triangle:
    {
        const f32 dq = dir.Dot(c.q);
        const f32 dr = dir.Dot(c.r);
        const f32 dp = dir.Dot(c.p);
        return dp >= dq && dp >= dr ? 0u : (dq >= dr ? 1u : 2u);
    }
    }
    return 0;
}

Vec3 Support(const Core& c, const Vec3& dir)
{
    switch (c.kind)
    {
    case CoreKind::Point: return c.position;
    case CoreKind::Segment: return dir.Dot(c.q - c.p) >= 0.0f ? c.q : c.p;
    case CoreKind::Box:
    {
        const Vec3 l = ToLocal(c, dir);
        return c.position + c.rotation * Vec3(l.x >= 0.0f ? c.half.x : -c.half.x, l.y >= 0.0f ? c.half.y : -c.half.y,
                                              l.z >= 0.0f ? c.half.z : -c.half.z);
    }
    case CoreKind::Hull: return c.position + c.rotation * c.hull->Vertex(c.hull->SupportVertex(ToLocal(c, dir)));
    case CoreKind::Triangle:
    {
        const f32 dq = dir.Dot(c.q);
        const f32 dr = dir.Dot(c.r);
        const f32 dp = dir.Dot(c.p);
        return dp >= dq && dp >= dr ? c.p : (dq >= dr ? c.q : c.r);
    }
    }
    return c.position;
}

// ---- GJK ----

struct SimplexVertex
{
    Vec3 w;  // the point of the Minkowski difference A - B
    Vec3 a;  // its point of A
    Vec3 b;  // its point of B
};

struct Simplex
{
    Array<SimplexVertex, 4> v;
    u32 n = 0;
};

SimplexVertex SupportDifference(const Core& a, const Core& b, const Vec3& dir)
{
    SimplexVertex s;
    s.a = Support(a, dir);
    s.b = Support(b, Negate(dir));
    s.w = s.a - s.b;
    return s;
}

// Reduces a segment to the feature closest to the origin; `lambda` holds the weights of the remaining vertices.
void ReduceSegment(Simplex* s, f32* lambda)
{
    const Vec3 a = s->v[0].w;
    const Vec3 ab = s->v[1].w - a;
    const f32 len2 = ab.LengthSquared();
    const f32 t = len2 > kTiny ? -a.Dot(ab) / len2 : 0.0f;
    if (t <= 0.0f)
    {
        s->n = 1;
        lambda[0] = 1.0f;
    }
    else if (t >= 1.0f)
    {
        s->v[0] = s->v[1];
        s->n = 1;
        lambda[0] = 1.0f;
    }
    else
    {
        lambda[0] = 1.0f - t;
        lambda[1] = t;
    }
}

// The same for a triangle (Ericson's regions, with the origin as the point).
void ReduceTriangle(Simplex* s, f32* lambda)
{
    const Vec3 a = s->v[0].w;
    const Vec3 b = s->v[1].w;
    const Vec3 c = s->v[2].w;
    const Vec3 ab = b - a;
    const Vec3 ac = c - a;
    const f32 d1 = -ab.Dot(a);
    const f32 d2 = -ac.Dot(a);
    if (d1 <= 0.0f && d2 <= 0.0f)
    {
        s->n = 1;
        lambda[0] = 1.0f;
        return;
    }
    const f32 d3 = -ab.Dot(b);
    const f32 d4 = -ac.Dot(b);
    if (d3 >= 0.0f && d4 <= d3)
    {
        s->v[0] = s->v[1];
        s->n = 1;
        lambda[0] = 1.0f;
        return;
    }
    const f32 vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
    {
        const f32 t = d1 / (d1 - d3);
        s->n = 2;
        lambda[0] = 1.0f - t;
        lambda[1] = t;
        return;
    }
    const f32 d5 = -ab.Dot(c);
    const f32 d6 = -ac.Dot(c);
    if (d6 >= 0.0f && d5 <= d6)
    {
        s->v[0] = s->v[2];
        s->n = 1;
        lambda[0] = 1.0f;
        return;
    }
    const f32 vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
    {
        const f32 t = d2 / (d2 - d6);
        s->v[1] = s->v[2];
        s->n = 2;
        lambda[0] = 1.0f - t;
        lambda[1] = t;
        return;
    }
    const f32 va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
    {
        const f32 t = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        s->v[0] = s->v[1];
        s->v[1] = s->v[2];
        s->n = 2;
        lambda[0] = 1.0f - t;
        lambda[1] = t;
        return;
    }
    const f32 sum = va + vb + vc;
    if (!(Abs(sum) > 1e-30f))
    {
        s->n = 1;
        lambda[0] = 1.0f;
        return;
    }
    const f32 inverse = 1.0f / sum;
    lambda[0] = va * inverse;
    lambda[1] = vb * inverse;
    lambda[2] = vc * inverse;
    s->n = 3;
}

// The same for a tetrahedron: false when the origin is inside it. Otherwise the closest of the four faces' closest
// points wins (the origin is outside, so that is the closest point of the tetrahedron); a flat tetrahedron is never
// "inside".
bool ReduceTetrahedron(Simplex* s, f32* lambda)
{
    static const u32 faces[4][4] = {{0, 1, 2, 3}, {0, 3, 1, 2}, {0, 2, 3, 1}, {1, 3, 2, 0}};
    bool inside = true;
    for (u32 f = 0; f < 4; ++f)
    {
        const Vec3 a = s->v[faces[f][0]].w;
        const Vec3 b = s->v[faces[f][1]].w;
        const Vec3 c = s->v[faces[f][2]].w;
        const Vec3 d = s->v[faces[f][3]].w;
        Vec3 n = (b - a).Cross(c - a);
        const f32 side = n.Dot(d - a);
        if (side > 0.0f)
        {
            n = Negate(n);  // away from the fourth vertex
        }
        const f32 scale = Sqrt(n.LengthSquared());
        // The origin is strictly on the inner side of this face, and the tetrahedron has some thickness.
        inside = inside && Abs(side) > 1e-9f * scale && n.Dot(a) > 1e-9f * scale;
    }
    if (inside)
    {
        return false;
    }
    bool found = false;
    f32 best = 0.0f;
    Simplex best_simplex;
    f32 best_lambda[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    for (u32 f = 0; f < 4; ++f)
    {
        Simplex t;
        t.v[0] = s->v[faces[f][0]];
        t.v[1] = s->v[faces[f][1]];
        t.v[2] = s->v[faces[f][2]];
        t.n = 3;
        f32 l[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        ReduceTriangle(&t, l);
        Vec3 point(0.0f, 0.0f, 0.0f);
        for (u32 i = 0; i < t.n; ++i)
        {
            point = point + t.v[i].w * l[i];
        }
        const f32 d2 = point.LengthSquared();
        if (!found || d2 < best)
        {
            found = true;
            best = d2;
            best_simplex = t;
            for (u32 i = 0; i < 4; ++i)
            {
                best_lambda[i] = l[i];
            }
        }
    }
    *s = best_simplex;
    for (u32 i = 0; i < 4; ++i)
    {
        lambda[i] = best_lambda[i];
    }
    return true;
}

struct GjkResult
{
    bool overlap = false;
    Simplex simplex;
    Vec3 point_a = Vec3(0.0f, 0.0f, 0.0f);  // the closest points of the two cores when apart
    Vec3 point_b = Vec3(0.0f, 0.0f, 0.0f);
    f32 distance = 0.0f;
};

GjkResult Gjk(const Core& a, const Core& b)
{
    GjkResult out;
    Vec3 dir = a.position - b.position;
    if (dir.LengthSquared() < kTiny)
    {
        dir = Vec3(1.0f, 0.0f, 0.0f);
    }
    Simplex s;
    s.v[0] = SupportDifference(a, b, dir);
    s.n = 1;
    f32 lambda[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    Vec3 v = s.v[0].w;
    for (u32 iteration = 0; iteration < kMaxGjkIterations; ++iteration)
    {
        if (s.n == 2)
        {
            ReduceSegment(&s, lambda);
        }
        else if (s.n == 3)
        {
            ReduceTriangle(&s, lambda);
        }
        else if (s.n == 4)
        {
            if (!ReduceTetrahedron(&s, lambda))
            {
                out.overlap = true;
                break;
            }
        }
        else
        {
            lambda[0] = 1.0f;
        }
        v = Vec3(0.0f, 0.0f, 0.0f);
        for (u32 i = 0; i < s.n; ++i)
        {
            v = v + s.v[i].w * lambda[i];
        }
        const f32 d2 = v.LengthSquared();
        if (d2 < 1e-12f)
        {
            out.overlap = true;
            break;
        }
        const Vec3 dir_next = Negate(v);
        const SimplexVertex w = SupportDifference(a, b, dir_next);
        // No progress toward the origin: v is the closest point.
        if (d2 - w.w.Dot(v) <= 1e-5f * d2 + 1e-12f)
        {
            break;
        }
        bool repeated = false;
        for (u32 i = 0; i < s.n; ++i)
        {
            repeated = repeated || (s.v[i].w - w.w).LengthSquared() < 1e-14f;
        }
        if (repeated)
        {
            break;
        }
        s.v[s.n++] = w;
    }
    out.simplex = s;
    if (!out.overlap)
    {
        Vec3 pa(0.0f, 0.0f, 0.0f);
        Vec3 pb(0.0f, 0.0f, 0.0f);
        for (u32 i = 0; i < s.n; ++i)
        {
            pa = pa + s.v[i].a * lambda[i];
            pb = pb + s.v[i].b * lambda[i];
        }
        out.point_a = pa;
        out.point_b = pb;
        out.distance = Sqrt(v.LengthSquared());
    }
    return out;
}

// ---- EPA ----

struct EpaFace
{
    u32 a = 0;
    u32 b = 0;
    u32 c = 0;
    Vec3 n = Vec3(0.0f, 1.0f, 0.0f);
    f32 d = 0.0f;
    bool alive = false;
};

struct EpaResult
{
    Vec3 normal = Vec3(0.0f, 1.0f, 0.0f);  // from A toward B
    f32 depth = 0.0f;
    Vec3 point_a = Vec3(0.0f, 0.0f, 0.0f);
    Vec3 point_b = Vec3(0.0f, 0.0f, 0.0f);
};

// Makes the simplex a tetrahedron around the origin's neighbourhood; false when the shapes are flat in a way that gives
// no volume (the caller then falls back to the centres).
bool ExpandSimplex(const Core& a, const Core& b, Simplex* s)
{
    static const Vec3 axes[6] = {Vec3(1, 0, 0),  Vec3(-1, 0, 0), Vec3(0, 1, 0),
                                 Vec3(0, -1, 0), Vec3(0, 0, 1),  Vec3(0, 0, -1)};
    while (s->n < 4)
    {
        bool added = false;
        f32 best = 1e-6f;
        SimplexVertex best_vertex;
        for (u32 k = 0; k < 6; ++k)
        {
            const SimplexVertex w = SupportDifference(a, b, axes[k]);
            // How far the new point is from the current feature.
            f32 measure = 0.0f;
            if (s->n == 1)
            {
                measure = (w.w - s->v[0].w).LengthSquared();
            }
            else if (s->n == 2)
            {
                const Vec3 e = s->v[1].w - s->v[0].w;
                const Vec3 r = w.w - s->v[0].w;
                measure = e.Cross(r).LengthSquared();
            }
            else
            {
                const Vec3 nrm = (s->v[1].w - s->v[0].w).Cross(s->v[2].w - s->v[0].w);
                const f32 h = nrm.Dot(w.w - s->v[0].w);
                measure = h * h;
            }
            if (measure > best)
            {
                best = measure;
                best_vertex = w;
                added = true;
            }
        }
        if (!added)
        {
            return false;
        }
        s->v[s->n++] = best_vertex;
    }
    return true;
}

bool Epa(const Core& a, const Core& b, const Simplex& start, EpaResult* result)
{
    Simplex s = start;
    if (!ExpandSimplex(a, b, &s))
    {
        return false;
    }
    Array<SimplexVertex, 72> verts;
    u32 vertex_count = 4;
    for (u32 i = 0; i < 4; ++i)
    {
        verts[i] = s.v[i];
    }
    const Vec3 inside = (verts[0].w + verts[1].w + verts[2].w + verts[3].w) * 0.25f;
    Array<EpaFace, 192> faces;
    u32 face_count = 0;
    auto add_face = [&](u32 ia, u32 ib, u32 ic)
    {
        if (face_count >= 192)
        {
            return;
        }
        EpaFace f;
        f.a = ia;
        f.b = ib;
        f.c = ic;
        Vec3 n = (verts[ib].w - verts[ia].w).Cross(verts[ic].w - verts[ia].w);
        const f32 len2 = n.LengthSquared();
        if (!(len2 > 1e-30f))
        {
            return;  // a sliver with no normal: left out of the polytope
        }
        n = n * (1.0f / Sqrt(len2));
        if (n.Dot(verts[ia].w - inside) < 0.0f)
        {
            n = Negate(n);
            f.b = ic;
            f.c = ib;
        }
        f.n = n;
        f.d = n.Dot(verts[ia].w);
        f.alive = true;
        faces[face_count++] = f;
    };
    add_face(0, 1, 2);
    add_face(0, 3, 1);
    add_face(0, 2, 3);
    add_face(1, 3, 2);

    u32 closest = 0;
    for (u32 iteration = 0; iteration < kMaxEpaIterations; ++iteration)
    {
        bool any = false;
        f32 best = 1e30f;
        for (u32 f = 0; f < face_count; ++f)
        {
            if (faces[f].alive && faces[f].d < best)
            {
                best = faces[f].d;
                closest = f;
                any = true;
            }
        }
        if (!any)
        {
            return false;
        }
        const EpaFace face = faces[closest];
        const SimplexVertex w = SupportDifference(a, b, face.n);
        const f32 reach = w.w.Dot(face.n);
        if (reach - face.d <= 1e-5f + 1e-5f * face.d || vertex_count >= 72)
        {
            break;
        }
        verts[vertex_count] = w;
        const u32 added = vertex_count++;
        // Remove the faces that see the new point and join the rim to it.
        Array<u32, 384> rim;
        u32 rim_count = 0;
        Array<bool, 192> removed;
        for (u32 f = 0; f < face_count; ++f)
        {
            removed[f] = faces[f].alive && faces[f].n.Dot(w.w - verts[faces[f].a].w) > 1e-7f;
        }
        bool none = true;
        for (u32 f = 0; f < face_count; ++f)
        {
            none = none && !removed[f];
        }
        if (none)
        {
            break;
        }
        for (u32 f = 0; f < face_count; ++f)
        {
            if (!removed[f])
            {
                continue;
            }
            const u32 edge[3][2] = {{faces[f].a, faces[f].b}, {faces[f].b, faces[f].c}, {faces[f].c, faces[f].a}};
            for (u32 e = 0; e < 3; ++e)
            {
                bool shared = false;
                for (u32 g = 0; g < face_count && !shared; ++g)
                {
                    if (!removed[g] || g == f)
                    {
                        continue;
                    }
                    const u32 other[3][2] = {
                        {faces[g].a, faces[g].b}, {faces[g].b, faces[g].c}, {faces[g].c, faces[g].a}};
                    for (u32 k = 0; k < 3; ++k)
                    {
                        shared = shared || (other[k][0] == edge[e][1] && other[k][1] == edge[e][0]);
                    }
                }
                if (!shared && rim_count + 2 <= 384)
                {
                    rim[rim_count++] = edge[e][0];
                    rim[rim_count++] = edge[e][1];
                }
            }
        }
        for (u32 f = 0; f < face_count; ++f)
        {
            if (removed[f])
            {
                faces[f].alive = false;
            }
        }
        for (u32 k = 0; k + 1 < rim_count; k += 2)
        {
            add_face(rim[k], rim[k + 1], added);
        }
    }
    const EpaFace face = faces[closest];
    // The point of the face nearest the origin, as weights of the three corners.
    const Vec3 p = face.n * face.d;
    const Vec3 va = verts[face.a].w;
    const Vec3 vb = verts[face.b].w;
    const Vec3 vc = verts[face.c].w;
    const Vec3 v0 = vb - va;
    const Vec3 v1 = vc - va;
    const Vec3 v2 = p - va;
    const f32 d00 = v0.Dot(v0);
    const f32 d01 = v0.Dot(v1);
    const f32 d11 = v1.Dot(v1);
    const f32 d20 = v2.Dot(v0);
    const f32 d21 = v2.Dot(v1);
    const f32 denom = d00 * d11 - d01 * d01;
    f32 u = 1.0f / 3.0f;
    f32 v = 1.0f / 3.0f;
    if (Abs(denom) > 1e-30f)
    {
        v = (d11 * d20 - d01 * d21) / denom;
        u = (d00 * d21 - d01 * d20) / denom;
    }
    f32 wa = Clamp(1.0f - u - v, 0.0f, 1.0f);
    f32 wb = Clamp(v, 0.0f, 1.0f);
    f32 wc = Clamp(u, 0.0f, 1.0f);
    const f32 sum = wa + wb + wc;
    wa /= sum;
    wb /= sum;
    wc /= sum;
    result->normal = face.n;
    result->depth = face.d;
    result->point_a = verts[face.a].a * wa + verts[face.b].a * wb + verts[face.c].a * wc;
    result->point_b = verts[face.a].b * wa + verts[face.b].b * wb + verts[face.c].b * wc;
    return true;
}

// ---- face manifolds ----

struct Polygon
{
    Array<Vec3, kMaxHullFaceVertices> v;
    Array<u32, kMaxHullFaceVertices> id;
    u32 count = 0;
    Vec3 normal = Vec3(0.0f, 1.0f, 0.0f);
};

bool HasFaces(const Core& c)
{
    return c.radius == 0.0f && (c.kind == CoreKind::Box || c.kind == CoreKind::Hull || c.kind == CoreKind::Triangle);
}

u32 FaceTotal(const Core& c)
{
    return c.kind == CoreKind::Box ? 6u : (c.kind == CoreKind::Triangle ? 2u : c.hull->FaceCount());
}

Vec3 LocalFaceNormal(const Core& c, u32 face)
{
    if (c.kind == CoreKind::Box)
    {
        Vec3 n(0.0f, 0.0f, 0.0f);
        n[face / 2] = (face % 2 == 0) ? 1.0f : -1.0f;
        return n;
    }
    if (c.kind == CoreKind::Triangle)
    {
        return face == 0 ? c.tn : Negate(c.tn);
    }
    return c.hull->Face(face).normal;
}

// The face most aligned with a world direction, and that alignment.
u32 BestFace(const Core& c, const Vec3& dir, f32* alignment)
{
    const Vec3 l = ToLocal(c, dir);
    u32 best = 0;
    f32 best_dot = -2.0f;
    for (u32 f = 0; f < FaceTotal(c); ++f)
    {
        const f32 d = LocalFaceNormal(c, f).Dot(l);
        if (d > best_dot + 1e-6f)
        {
            best_dot = d;
            best = f;
        }
    }
    *alignment = best_dot;
    return best;
}

void BuildPolygon(const Core& c, u32 face, Polygon* out)
{
    out->normal = c.rotation * LocalFaceNormal(c, face);
    if (c.kind == CoreKind::Box)
    {
        const u32 axis = face / 2;
        const f32 sign = (face % 2 == 0) ? 1.0f : -1.0f;
        const u32 u = (axis + 1) % 3;
        const u32 w = (axis + 2) % 3;
        const Vec3 centre = c.position + c.rotation.col[axis] * (sign * c.half[axis]);
        const Vec3 eu = c.rotation.col[u] * c.half[u];
        const Vec3 ew = c.rotation.col[w] * c.half[w];
        const Vec3 corners[4] = {centre + eu + ew, centre - eu + ew, centre - eu - ew, centre + eu - ew};
        out->count = 4;
        for (u32 k = 0; k < 4; ++k)
        {
            const u32 at = sign > 0.0f ? k : 3 - k;  // counter-clockwise seen from outside
            out->v[k] = corners[at];
            out->id[k] = at;
        }
        return;
    }
    if (c.kind == CoreKind::Triangle)
    {
        out->count = 3;
        out->v[0] = c.p;
        out->v[1] = face == 0 ? c.q : c.r;
        out->v[2] = face == 0 ? c.r : c.q;
        out->id[0] = 0;
        out->id[1] = face == 0 ? 1u : 2u;
        out->id[2] = face == 0 ? 2u : 1u;
        return;
    }
    const HullFace& f = c.hull->Face(face);
    out->count = f.count;
    for (u32 k = 0; k < f.count; ++k)
    {
        const u32 vertex = c.hull->FaceVertex(f.first + k);
        out->v[k] = c.position + c.rotation * c.hull->Vertex(vertex);
        out->id[k] = vertex;
    }
}

// The manifold of two shapes with faces whose contact normal is close to a face's: the incident polygon clipped to the
// reference face. Returns false when nothing is left within the margin (the caller keeps its single point).
bool FaceManifold(const Core& ca, const Core& cb, const Vec3& n, f32 margin, ContactManifold* out)
{
    f32 align_a = 0.0f;
    f32 align_b = 0.0f;
    const u32 face_a = BestFace(ca, n, &align_a);
    const u32 face_b = BestFace(cb, Negate(n), &align_b);
    if (FaceTotal(ca) == 0 || FaceTotal(cb) == 0 || FMax(align_a, align_b) < kFaceAlignment)
    {
        return false;
    }
    const bool ref_is_b = align_b > align_a + 1e-3f;
    const Core& ref = ref_is_b ? cb : ca;
    const Core& inc = ref_is_b ? ca : cb;
    const u32 ref_face = ref_is_b ? face_b : face_a;
    Polygon ref_poly;
    BuildPolygon(ref, ref_face, &ref_poly);
    const Vec3 nr = ref_poly.normal;  // out of the reference shape, toward the incident one
    f32 inc_align = 0.0f;
    const u32 inc_face = BestFace(inc, Negate(nr), &inc_align);
    Polygon inc_poly;
    BuildPolygon(inc, inc_face, &inc_poly);

    Array<ClipVertex, kClipCapacity> buf0;
    Array<ClipVertex, kClipCapacity> buf1;
    u32 count = inc_poly.count;
    for (u32 k = 0; k < count; ++k)
    {
        buf0[k].p = inc_poly.v[k];
        buf0[k].id = inc_poly.id[k];
    }
    bool in_first = true;
    for (u32 e = 0; e < ref_poly.count && count > 0; ++e)
    {
        const Vec3 from = ref_poly.v[e];
        const Vec3 edge = ref_poly.v[(e + 1) % ref_poly.count] - from;
        Vec3 side = edge.Cross(nr);  // outward in the face's plane
        const f32 len2 = side.LengthSquared();
        if (!(len2 > kTiny))
        {
            continue;
        }
        side = side * (1.0f / Sqrt(len2));
        const f32 offset = side.Dot(from);
        const ClipVertex* source = in_first ? buf0.Data() : buf1.Data();
        ClipVertex* dest = in_first ? buf1.Data() : buf0.Data();
        count = ClipPolygon(source, count, side, offset, e, dest, kClipCapacity);
        in_first = !in_first;
    }
    const ClipVertex* clipped = in_first ? buf0.Data() : buf1.Data();
    Array<Candidate, kClipCapacity> cand;
    u32 kept = 0;
    for (u32 k = 0; k < count; ++k)
    {
        const f32 depth = -(clipped[k].p - ref_poly.v[0]).Dot(nr);
        if (depth >= -margin)
        {
            cand[kept].p = clipped[k].p;
            cand[kept].depth = depth;
            cand[kept].id = clipped[k].id;
            ++kept;
        }
    }
    if (kept == 0)
    {
        return false;
    }
    Array<u32, 4> pick;
    const u32 picked = ReduceCandidates(cand.Data(), kept, nr, &pick);
    out->normal = ref_is_b ? Negate(nr) : nr;
    out->count = picked;
    const u32 code = (ref_is_b ? 0x80000000u : 0u) | ((ref_face & 0x3FFu) << 21) | ((inc_face & 0x3FFu) << 11);
    for (u32 k = 0; k < picked; ++k)
    {
        const Candidate& c = cand[pick[k]];
        const Vec3 on_ref = c.p + nr * c.depth;
        out->points[k].position_a = ref_is_b ? c.p : on_ref;
        out->points[k].position_b = ref_is_b ? on_ref : c.p;
        out->points[k].depth = c.depth;
        out->points[k].feature_id = code | (c.id & 0x7FFu);
    }
    return true;
}

// The contact of two cores (with their radii): the body of every convex pair.
bool CoresContact(const Core& ca, const Core& cb, f32 margin, ContactManifold* out)
{
    const f32 radii = ca.radius + cb.radius;
    const GjkResult gjk = Gjk(ca, cb);
    Vec3 n;
    Vec3 point_a;
    Vec3 point_b;
    const Vec3 gap = gjk.point_b - gjk.point_a;
    const f32 gap2 = gap.LengthSquared();
    // Cores closer than 1e-5 apart count as touching (their direction is rounding), and are handled by EPA.
    const bool apart = !gjk.overlap && gap2 > 1e-10f;
    if (apart)
    {
        const f32 distance = Sqrt(gap2);
        if (distance > radii + margin)
        {
            return false;
        }
        n = gap * (1.0f / distance);
        point_a = gjk.point_a;
        point_b = gjk.point_b;
    }
    else
    {
        EpaResult epa;
        if (Epa(ca, cb, gjk.simplex, &epa))
        {
            n = epa.normal;
            point_a = epa.point_a;
            point_b = epa.point_b;
        }
        else
        {
            // No volume to expand: the cores touch; push apart along the line of the centres.
            const Vec3 d = cb.position - ca.position;
            const f32 len2 = d.LengthSquared();
            n = len2 > kTiny ? d * (1.0f / Sqrt(len2)) : Vec3(0.0f, 1.0f, 0.0f);
            point_a = ca.position;
            point_b = cb.position;
        }
    }
    if (HasFaces(ca) && HasFaces(cb) && FaceManifold(ca, cb, n, margin, out))
    {
        return true;
    }
    out->normal = n;
    out->count = 1;
    out->points[0].position_a = point_a + n * ca.radius;
    out->points[0].position_b = point_b - n * cb.radius;
    out->points[0].depth = (out->points[0].position_a - out->points[0].position_b).Dot(n);
    out->points[0].feature_id = 0x40000000u | ((SupportFeature(ca, n) & 0x7FFFu) << 15) |
                                (SupportFeature(cb, Negate(n)) & 0x7FFFu);
    return true;
}

}  // namespace

bool ConvexPair(const Shape& a, const Pose& pa, const Shape& b, const Pose& pb, f32 margin, const HullLibrary* hulls,
                ContactManifold* out)
{
    const Core ca = MakeCore(a, pa, hulls);
    const Core cb = MakeCore(b, pb, hulls);
    if ((ca.kind == CoreKind::Hull && !ca.hull) || (cb.kind == CoreKind::Hull && !cb.hull))
    {
        return false;  // a hull the library does not hold
    }
    return CoresContact(ca, cb, margin, out);
}

namespace
{

// How `TrianglesPair` reads the triangles of a mesh and of a height field: static functions over the shared object,
// which is a parameter (a member would be a pointer into another class's storage).
struct MeshTriangles
{
    // Whether the surface has solid ground below it only (a height field): a body is always pushed out upward.
    static constexpr bool kSolidBelow = false;
    static u32 Query(const TriangleMesh& mesh, const Aabb& box, Vector<u32>* out) { return mesh.QueryBox(box, out); }
    static void Corners(const TriangleMesh& mesh, u32 t, Vec3* a, Vec3* b, Vec3* c)
    {
        *a = mesh.Vertex(mesh.Index(t, 0));
        *b = mesh.Vertex(mesh.Index(t, 1));
        *c = mesh.Vertex(mesh.Index(t, 2));
    }
    static Vec3 Normal(const TriangleMesh& mesh, u32 t) { return mesh.Normal(t); }
    // Whether the triangle across edge `e` of `t` is (nearly) coplanar with it.
    static bool FlatAcross(const TriangleMesh& mesh, u32 t, u32 e)
    {
        const u32 nb = mesh.Neighbor(t, e);
        return nb != kNoTriangle && mesh.Normal(nb).Dot(mesh.Normal(t)) > 0.99f;
    }
};

struct FieldTriangles
{
    static constexpr bool kSolidBelow = true;
    // At most this many triangles are asked for: the pair tests 256 of them, so a huge shape over a huge map does not
    // list the whole map.
    static constexpr u32 kMaxListed = 1024;
    static u32 Query(const HeightField& field, const Aabb& box, Vector<u32>* out)
    {
        return field.QueryBox(box, out, kMaxListed);
    }
    static void Corners(const HeightField& field, u32 t, Vec3* a, Vec3* b, Vec3* c) { field.Triangle(t, a, b, c); }
    static Vec3 Normal(const HeightField& field, u32 t) { return field.Normal(t); }
    static bool FlatAcross(const HeightField& field, u32 t, u32 e)
    {
        const u32 nb = field.Neighbor(t, e);
        return nb != kNoFieldTriangle && field.Normal(nb).Dot(field.Normal(t)) > 0.99f;
    }
};

// A static triangle soup (placed by `mesh_pose`) against one other shape.
template <typename Triangles, typename Object>
bool TrianglesPair(const Object& source, const Pose& mesh_pose, const Shape& other, const Pose& other_pose, f32 margin,
                   const HullLibrary* hulls, ContactManifold* out)
{
    const Core cb = MakeCore(other, other_pose, hulls);
    if (cb.kind == CoreKind::Hull && !cb.hull)
    {
        return false;
    }
    // The other shape's box in the mesh's space.
    const Aabb world_box = other.Bounds(other_pose.position, other_pose.rotation);
    const Mat3 rot = mesh_pose.rotation.ToMat3();
    const Vec3 centre = world_box.Center() - mesh_pose.position;
    const Vec3 half = world_box.HalfExtents() + Vec3(margin, margin, margin);
    const Vec3 local_centre(rot.col[0].Dot(centre), rot.col[1].Dot(centre), rot.col[2].Dot(centre));
    const Vec3 local_half(Abs(rot.col[0].x) * half.x + Abs(rot.col[0].y) * half.y + Abs(rot.col[0].z) * half.z,
                          Abs(rot.col[1].x) * half.x + Abs(rot.col[1].y) * half.y + Abs(rot.col[1].z) * half.z,
                          Abs(rot.col[2].x) * half.x + Abs(rot.col[2].y) * half.y + Abs(rot.col[2].z) * half.z);
    Vector<u32> triangles;
    Triangles::Query(source, Aabb::FromMinMax(local_centre - local_half, local_centre + local_half), &triangles);

    constexpr u32 kMaxFound = 64;
    constexpr u32 kMaxTriangles = 256;
    Array<FoundContact, kMaxFound> found;
    u32 found_count = 0;
    const u32 tested = triangles.Size() < kMaxTriangles ? static_cast<u32>(triangles.Size()) : kMaxTriangles;
    for (u32 k = 0; k < tested && found_count < kMaxFound; ++k)
    {
        const u32 t = triangles[k];
        Vec3 la;
        Vec3 lb;
        Vec3 lc;
        Triangles::Corners(source, t, &la, &lb, &lc);
        const Vec3 wa = mesh_pose.position + rot * la;
        const Vec3 wb = mesh_pose.position + rot * lb;
        const Vec3 wc = mesh_pose.position + rot * lc;
        const Vec3 face = rot * Triangles::Normal(source, t);
        const Core tc = TriangleCore(wa, wb, wc, face);
        ContactManifold tm;
        if (!CoresContact(tc, cb, margin, &tm))
        {
            continue;
        }
        // A flat triangle is left by the plane too: when that exit is shorter than the one the cores' EPA found (which
        // can pick a longer way out of a thin sliver of the Minkowski difference), the plane's exit is the contact.
        {
            f32 deepest = tm.points[0].depth;
            for (u32 j = 1; j < tm.count; ++j)
            {
                deepest = FMax(deepest, tm.points[j].depth);
            }
            const Vec3 low = Support(cb, Negate(face));
            const Vec3 high = Support(cb, face);
            const f32 plane = wa.Dot(face);
            const f32 up_depth = plane - (low.Dot(face) - cb.radius);
            const f32 down_depth = (high.Dot(face) + cb.radius) - plane;
            if (FMin(up_depth, down_depth) < deepest - 1e-4f)
            {
                const bool up = up_depth <= down_depth;
                const f32 depth = up ? up_depth : down_depth;
                const Vec3 pb = up ? low - face * cb.radius : high + face * cb.radius;
                const u32 id = tm.points[0].feature_id;
                tm.count = 1;
                tm.normal = up ? face : Negate(face);
                tm.points[0].position_b = pb;
                tm.points[0].position_a = up ? pb + face * depth : pb - face * depth;
                tm.points[0].depth = depth;
                tm.points[0].feature_id = id;
            }
        }
        // A height field is solid below its surface, with no underside: a body that sank deep enough for its contact to
        // point down (a fast one, or one pushed into a steep face) is brought out upward instead of falling through.
        if constexpr (Triangles::kSolidBelow)
        {
            if (tm.normal.Dot(face) < 0.0f)
            {
                const Vec3 low = Support(cb, Negate(face));
                const f32 plane = wa.Dot(face);
                const f32 up_depth = plane - (low.Dot(face) - cb.radius);
                const Vec3 pb = low - face * cb.radius;
                const u32 id = tm.points[0].feature_id;
                tm.count = 1;
                tm.normal = face;
                tm.points[0].position_b = pb;
                tm.points[0].position_a = pb + face * up_depth;
                tm.points[0].depth = up_depth;
                tm.points[0].feature_id = id;
            }
        }
        // The internal-edge rule: a single contact on an edge shared with a (nearly) coplanar triangle takes the face's
        // normal, so a body does not catch on the seam.
        // The side the other shape's centre is on (a sideways normal says nothing about it).
        const Vec3 oriented = (cb.position - wa).Dot(face) >= 0.0f ? face : Negate(face);
        if (tm.count == 1 && tm.normal.Dot(oriented) < 0.9995f)
        {
            const Vec3 point = tm.points[0].position_a;
            f32 w[3];
            const Vec3 e1 = wb - wa;
            const Vec3 e2 = wc - wa;
            const Vec3 d = point - wa;
            const f32 d11 = e1.Dot(e1);
            const f32 d12 = e1.Dot(e2);
            const f32 d22 = e2.Dot(e2);
            const f32 denom = d11 * d22 - d12 * d12;
            bool flat = false;
            if (Abs(denom) > 1e-30f)
            {
                const f32 v = (d22 * d.Dot(e1) - d12 * d.Dot(e2)) / denom;
                const f32 u = (d11 * d.Dot(e2) - d12 * d.Dot(e1)) / denom;
                w[0] = 1.0f - u - v;
                w[1] = v;
                w[2] = u;
                for (u32 e = 0; e < 3; ++e)
                {
                    if (w[(e + 2) % 3] < 1e-3f)
                    {
                        flat = flat || Triangles::FlatAcross(source, t, e);
                    }
                }
            }
            if (flat)
            {
                const Vec3 toward = Negate(oriented);
                const Vec3 pb = Support(cb, toward) + toward * cb.radius;
                const f32 depth = (wa - pb).Dot(oriented);
                if (depth < -margin)
                {
                    continue;
                }
                tm.normal = oriented;
                tm.points[0].position_b = pb;
                tm.points[0].position_a = pb + oriented * depth;
                tm.points[0].depth = depth;
            }
        }
        for (u32 j = 0; j < tm.count && found_count < kMaxFound; ++j)
        {
            FoundContact& f = found[found_count++];
            f.normal = tm.normal;
            f.pa = tm.points[j].position_a;
            f.pb = tm.points[j].position_b;
            f.depth = tm.points[j].depth;
            f.id = tm.points[j].feature_id * 2654435761u ^ (t + 1u) * 40503u;
        }
    }
    return MergeContacts(found.Data(), found_count, out);
}

}  // namespace

bool MeshPair(const Shape& mesh_shape, const Pose& mesh_pose, const Shape& other, const Pose& other_pose, f32 margin,
              const HullLibrary* hulls, const MeshLibrary* meshes, ContactManifold* out)
{
    const TriangleMesh* mesh = meshes != nullptr ? meshes->Find(mesh_shape.mesh) : nullptr;
    return mesh != nullptr && TrianglesPair<MeshTriangles>(*mesh, mesh_pose, other, other_pose, margin, hulls, out);
}

bool HeightFieldPair(const Shape& field_shape, const Pose& field_pose, const Shape& other, const Pose& other_pose,
                     f32 margin, const HullLibrary* hulls, const HeightFieldLibrary* fields, ContactManifold* out)
{
    const HeightField* field = fields != nullptr ? fields->Find(field_shape.height_field) : nullptr;
    return field != nullptr && TrianglesPair<FieldTriangles>(*field, field_pose, other, other_pose, margin, hulls, out);
}

}  // namespace vse::physics::collide_math
