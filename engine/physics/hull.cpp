#include "physics/hull.h"

#include "lang/scalar.h"

namespace vse::physics
{

namespace
{

bool FiniteVec(const Vec3& v)
{
    return IsFinite(v.x) && IsFinite(v.y) && IsFinite(v.z);
}

}  // namespace

ConvexHullConstPtr ConvexHull::Create(const ConvexHullSpec& spec, Validation* result)
{
    Validation ignored;
    Validation& r = result != nullptr ? *result : ignored;
    r = Validation::Ok;
    const usize nv = spec.vertices.Size();
    const usize nf = spec.normals.Size();
    if (nv < 4 || nv > kMaxHullVertices || nf < 4 || nf > kMaxHullFaces || spec.offsets.Size() != nf ||
        spec.face_first.Size() != nf || spec.face_count.Size() != nf)
    {
        r = Validation::OutOfRange;
        return ConvexHullConstPtr();
    }
    for (usize i = 0; i < nv; ++i)
    {
        if (!FiniteVec(spec.vertices[i]))
        {
            r = Validation::NotFinite;
            return ConvexHullConstPtr();
        }
    }
    usize next = 0;
    for (usize f = 0; f < nf; ++f)
    {
        const Vec3& n = spec.normals[f];
        if (!FiniteVec(n) || !IsFinite(spec.offsets[f]))
        {
            r = Validation::NotFinite;
            return ConvexHullConstPtr();
        }
        if (Abs(Sqrt(n.LengthSquared()) - 1.0f) > 1e-3f)
        {
            r = Validation::NotNormalized;
            return ConvexHullConstPtr();
        }
        const u32 first = spec.face_first[f];
        const u32 count = spec.face_count[f];
        if (first != next || count < 3 || count > kMaxHullFaceVertices || first + count > spec.indices.Size())
        {
            r = Validation::OutOfRange;
            return ConvexHullConstPtr();
        }
        next += count;
    }
    if (next != spec.indices.Size())
    {
        r = Validation::OutOfRange;
        return ConvexHullConstPtr();
    }
    for (usize k = 0; k < spec.indices.Size(); ++k)
    {
        if (spec.indices[k] >= nv)
        {
            r = Validation::OutOfRange;
            return ConvexHullConstPtr();
        }
    }
    if (!IsFinite(spec.volume) || !FiniteVec(spec.centroid))
    {
        r = Validation::NotFinite;
        return ConvexHullConstPtr();
    }
    for (usize k = 0; k < 6; ++k)
    {
        if (!IsFinite(spec.inertia[k]))
        {
            r = Validation::NotFinite;
            return ConvexHullConstPtr();
        }
    }
    if (!(spec.volume > 0.0f))
    {
        r = Validation::NotPositive;
        return ConvexHullConstPtr();
    }
    // The planes must bound the hull: every face's corners on its plane, every vertex on or behind every plane, and
    // each face wound counter-clockwise about its outward normal (the face's own normal by Newell's method agrees).
    // The tolerance covers the 1e-3 the normals may be off unit length; a face turned inside out is off by the hull's
    // size.
    f32 scale = 0.0f;
    for (usize i = 0; i < nv; ++i)
    {
        scale = FMax(scale, FMax(Abs(spec.vertices[i].x), FMax(Abs(spec.vertices[i].y), Abs(spec.vertices[i].z))));
    }
    const f32 tolerance = 4e-3f * scale + 1e-6f;
    for (usize f = 0; f < nf; ++f)
    {
        const Vec3& n = spec.normals[f];
        const f32 d = spec.offsets[f];
        for (usize i = 0; i < nv; ++i)
        {
            if (n.Dot(spec.vertices[i]) - d > tolerance)
            {
                r = Validation::OutOfRange;
                return ConvexHullConstPtr();
            }
        }
        const u32 first = spec.face_first[f];
        const u32 count = spec.face_count[f];
        Vec3 newell(0.0f, 0.0f, 0.0f);
        for (u32 k = 0; k < count; ++k)
        {
            const Vec3& a = spec.vertices[spec.indices[first + k]];
            const Vec3& b = spec.vertices[spec.indices[first + (k + 1) % count]];
            if (Abs(n.Dot(a) - d) > tolerance)
            {
                r = Validation::OutOfRange;
                return ConvexHullConstPtr();
            }
            newell = newell + Vec3((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y));
        }
        if (!(newell.Dot(n) > 0.0f))
        {
            r = Validation::OutOfRange;
            return ConvexHullConstPtr();
        }
    }

    ConvexHullPtr hull = MakeShared<ConvexHull>();
    Vec3 extent(0.0f, 0.0f, 0.0f);
    for (usize i = 0; i < nv; ++i)
    {
        hull->vertices_.Push(spec.vertices[i]);
        extent = Vec3(FMax(extent.x, Abs(spec.vertices[i].x)), FMax(extent.y, Abs(spec.vertices[i].y)),
                      FMax(extent.z, Abs(spec.vertices[i].z)));
    }
    for (usize f = 0; f < nf; ++f)
    {
        HullFace face;
        face.first = spec.face_first[f];
        face.count = spec.face_count[f];
        face.normal = spec.normals[f];
        face.offset = spec.offsets[f];
        hull->faces_.Push(face);
    }
    for (usize k = 0; k < spec.indices.Size(); ++k)
    {
        hull->indices_.Push(spec.indices[k]);
    }
    hull->volume_ = spec.volume;
    hull->centroid_ = spec.centroid;
    hull->extent_ = extent;
    const Array<f32, 6>& t = spec.inertia;
    hull->inertia_ = Mat3::FromBasis(Vec3(t[0], t[3], t[4]), Vec3(t[3], t[1], t[5]), Vec3(t[4], t[5], t[2]));
    // A hull with a zero extent cannot be bounded.
    if (!(extent.x > 0.0f) || !(extent.y > 0.0f) || !(extent.z > 0.0f))
    {
        r = Validation::NotPositive;
        return ConvexHullConstPtr();
    }
    return ConvexHullConstPtr(hull);
}

u32 ConvexHull::SupportVertex(const Vec3& direction) const
{
    u32 best = 0;
    f32 best_dot = vertices_[0].Dot(direction);
    for (u32 i = 1; i < vertices_.Size(); ++i)
    {
        const f32 d = vertices_[i].Dot(direction);
        if (d > best_dot)
        {
            best_dot = d;
            best = i;
        }
    }
    return best;
}

u32 HullLibrary::Add(const ConvexHullConstPtr& hull)
{
    if (!hull)
    {
        return 0;
    }
    hulls_.Push(hull);
    return static_cast<u32>(hulls_.Size());
}

const ConvexHull* HullLibrary::Find(u32 id) const
{
    return (id >= 1 && id <= hulls_.Size()) ? hulls_[id - 1].Get() : nullptr;
}

ConvexHullConstPtr HullLibrary::Share(u32 id) const
{
    return (id >= 1 && id <= hulls_.Size()) ? hulls_[id - 1] : ConvexHullConstPtr();
}

}  // namespace vse::physics
