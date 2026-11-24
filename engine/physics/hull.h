// Convex hulls for the physics module (Phase 25 Day 393): the hull a shape of kind `Hull` refers to, and the library
// that holds them.
#pragma once

#include "containers/array.h"
#include "containers/shared_ptr.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "math/matrix.h"
#include "math/vector.h"
#include "physics/validation.h"
#include "types.h"

namespace vse::physics
{

/// The most vertices a hull holds (the same as the minted `HULL` asset).
inline constexpr u32 kMaxHullVertices = 255;
/// The most faces a hull holds.
inline constexpr u32 kMaxHullFaces = 512;
/// The most vertices of one face.
inline constexpr u32 kMaxHullFaceVertices = 64;

/// One face of a hull: a convex polygon on a plane.
///
/// - Thread-safety: thread-compatible plain data.
struct HullFace
{
    /// Where its vertices start in the hull's index list.
    u32 first = 0;
    /// How many vertices it has, 3 to `kMaxHullFaceVertices`.
    u32 count = 0;
    /// The outward unit normal.
    Vec3 normal = Vec3(0.0f, 1.0f, 0.0f);
    /// The offset of the plane: `Dot(normal, p) == offset` on the face.
    f32 offset = 0.0f;
};

/// The numbers a hull is made from, as spans (the minted `HULL` asset has exactly these, see `asset::HullView`).
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: borrows the spans for the call that takes it.
struct ConvexHullSpec
{
    /// The corners.
    Span<const Vec3> vertices;
    /// Per face: the outward unit normal.
    Span<const Vec3> normals;
    /// Per face: the plane offset.
    Span<const f32> offsets;
    /// Per face: where its vertices start in `indices`.
    Span<const u32> face_first;
    /// Per face: how many vertices.
    Span<const u32> face_count;
    /// The vertices of each face, counter-clockwise seen from outside.
    Span<const u16> indices;
    /// The volume.
    f32 volume = 0.0f;
    /// The centre of mass at uniform density.
    Vec3 centroid = Vec3(0.0f, 0.0f, 0.0f);
    /// The inertia tensor about the centroid at density 1: xx, yy, zz, xy, xz, yz.
    Array<f32, 6> inertia = {};
};

class ConvexHull;

/// The shared hull, as the shared-unit rule spells it (the mutable form, used while one is built).
using ConvexHullPtr = SharedPtr<ConvexHull>;
/// The shared hull, read-only: the way to hold one.
using ConvexHullConstPtr = SharedPtr<const ConvexHull>;

/// A convex hull: vertices, face polygons with planes, and the mass data. Immutable once made, shared by every collider
/// that uses it.
///
/// - Thread-safety: thread-safe after `Create` (immutable).
/// - Ownership: owns its arrays.
class ConvexHull
{
public:
    /// Checks the numbers and makes a hull.
    ///
    /// - Param spec: the numbers: 4 to 255 vertices, 4 to 512 faces of 3 to 64 vertices each, indices below the vertex
    /// count, finite numbers, unit normals (within 1e-3), faces in index order, a volume above zero, and planes that
    /// bound the hull: each face's corners on its plane and wound counter-clockwise about its normal, no vertex in
    /// front of any plane (within 0.4 % of the largest coordinate).
    /// - Param result: receives `Ok` or why the numbers were refused.
    /// - Returns: the hull, or null when refused.
    /// - Ownership: the hull is shared; `spec` is borrowed for the call only.
    /// - Fails: a refused spec returns null and sets `result`.
    static ConvexHullConstPtr Create(const ConvexHullSpec& spec, Validation* result);

    /// The number of vertices.
    /// - Fails: never.
    u32 VertexCount() const { return static_cast<u32>(vertices_.Size()); }

    /// The number of faces.
    /// - Fails: never.
    u32 FaceCount() const { return static_cast<u32>(faces_.Size()); }

    /// Vertex `i`.
    /// - Aborts: `i` is not below `VertexCount()`.
    const Vec3& Vertex(u32 i) const { return vertices_[i]; }

    /// Face `i`.
    /// - Aborts: `i` is not below `FaceCount()`.
    const HullFace& Face(u32 i) const { return faces_[i]; }

    /// The vertex number at position `k` of a face's list: the face's `first + j`.
    /// - Aborts: `k` is not below the index count.
    u32 FaceVertex(u32 k) const { return indices_[k]; }

    /// The volume.
    /// - Fails: never.
    f32 Volume() const { return volume_; }

    /// The centre of mass.
    /// - Fails: never.
    const Vec3& Centroid() const { return centroid_; }

    /// The inertia tensor about the centroid at density 1.
    /// - Fails: never.
    const Mat3& Inertia() const { return inertia_; }

    /// The largest absolute coordinate on each axis (the bound about the origin).
    /// - Fails: never.
    const Vec3& Extent() const { return extent_; }

    /// Returns the vertex farthest along a direction (the first one on a tie).
    ///
    /// - Param direction: any non-zero direction.
    /// - Returns: the vertex number.
    /// - Fails: never.
    u32 SupportVertex(const Vec3& direction) const;

    /// An empty hull: only `Create` makes a usable one (it is public so `MakeShared` can build it).
    ConvexHull() = default;

private:
    Vector<Vec3> vertices_;
    Vector<HullFace> faces_;
    Vector<u16> indices_;
    f32 volume_ = 0.0f;
    Vec3 centroid_ = Vec3(0.0f, 0.0f, 0.0f);
    Mat3 inertia_ = Mat3::Identity();
    Vec3 extent_ = Vec3(0.0f, 0.0f, 0.0f);
};

/// The hulls of a physics world, by id: a collider's shape names a hull by its id.
///
/// - Thread-safety: thread-compatible; read-only use from several threads is safe.
/// - Ownership: holds a shared pointer to every hull.
class HullLibrary
{
public:
    /// Adds a hull.
    ///
    /// - Param hull: the hull; not null.
    /// - Returns: its id (1, 2, ...), never 0; 0 for a null hull.
    /// - Fails: a null hull returns 0.
    u32 Add(const ConvexHullConstPtr& hull);

    /// Finds a hull.
    ///
    /// - Param id: an id `Add` returned.
    /// - Returns: the hull, or null for an unknown id.
    /// - Ownership: owned by the library; valid while the library is.
    /// - Fails: an unknown id returns null.
    const ConvexHull* Find(u32 id) const;

    /// Finds a hull and shares it.
    ///
    /// - Param id: an id `Add` returned.
    /// - Returns: the hull, or an empty pointer for an unknown id.
    /// - Fails: an unknown id returns an empty pointer.
    ConvexHullConstPtr Share(u32 id) const;

    /// The number of hulls.
    /// - Fails: never.
    u32 Count() const { return static_cast<u32>(hulls_.Size()); }

private:
    Vector<ConvexHullConstPtr> hulls_;
};

}  // namespace vse::physics
