#pragma once

//! The static triangle mesh of a collider (Phase 25 Day 394): an immutable, shared mesh with a bounding-volume
//! hierarchy over its triangles (`spatial::StaticBvh`), face normals and the triangle across each edge, and the library
//! a world keeps its meshes in.

#include "containers/shared_ptr.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "math/geometry.h"
#include "math/vector.h"
#include "physics/validation.h"
#include "spatial/static_bvh.h"
#include "types.h"

namespace vse::physics
{

/// The most vertices a mesh holds (the limit of the minted `MCOL` asset).
inline constexpr u32 kMaxMeshVertices = 1u << 20;
/// The most triangles a mesh holds.
inline constexpr u32 kMaxMeshTriangles = 1u << 20;
/// The "no neighbour" value of [[TriangleMesh::Neighbor]].
inline constexpr u32 kNoTriangle = 0xFFFFFFFFu;

/// The numbers a mesh is made from, as spans (the minted `MCOL` asset has exactly these, see
/// `asset::MeshColliderView`).
///
/// - Thread-safety: thread-compatible plain data.
struct TriangleMeshSpec
{
    Span<const Vec3> positions;  ///< The vertices.
    Span<const u32> indices;     ///< Three vertex indices per triangle.
    Span<const u8> materials;    ///< One material index per triangle; empty for all zero.
};

class TriangleMesh;
/// The shared mesh, mutable form (used while one is built).
using TriangleMeshPtr = SharedPtr<TriangleMesh>;
/// The shared mesh, read-only: the way to hold one.
using TriangleMeshConstPtr = SharedPtr<const TriangleMesh>;

/// A triangle mesh: positions, triangles, face normals, edge neighbours and a hierarchy. Immutable once made.
///
/// - Thread-safety: thread-safe to read.
/// - Ownership: shared by every collider that uses it, through [[TriangleMeshConstPtr]].
class TriangleMesh
{
public:
    TriangleMesh() = default;
    TriangleMesh(const TriangleMesh&) = delete;
    TriangleMesh& operator=(const TriangleMesh&) = delete;

    /// Checks and builds a mesh.
    ///
    /// - Param result: receives the reason on failure; may be null.
    /// - Returns: the mesh, or an empty pointer.
    /// - Ownership: copies the spec's numbers; nothing is borrowed after the call.
    /// - Fails: `OutOfRange` for no triangles, a count over the limits, an index outside the vertices or a spec with
    /// mismatched lengths; `NotFinite` for a non-finite position; `NotPositive` for a triangle with no area.
    static TriangleMeshConstPtr Create(const TriangleMeshSpec& spec, Validation* result);

    /// The vertex count.
    /// - Fails: never.
    u32 VertexCount() const { return static_cast<u32>(vertices_.Size()); }
    /// The triangle count.
    /// - Fails: never.
    u32 TriangleCount() const { return static_cast<u32>(indices_.Size() / 3); }
    /// Vertex `i`.
    ///
    /// - Aborts: `i` out of range.
    const Vec3& Vertex(u32 i) const;
    /// Corner `corner` (0-2) of triangle `t`, as a vertex index.
    ///
    /// - Aborts: out of range.
    u32 Index(u32 t, u32 corner) const;
    /// The unit normal of triangle `t` (counter-clockwise seen from the front).
    ///
    /// - Aborts: `t` out of range.
    const Vec3& Normal(u32 t) const;
    /// The material index of triangle `t`.
    ///
    /// - Aborts: `t` out of range.
    u8 Material(u32 t) const;
    /// The triangle that shares edge `edge` of `t` (edge `e` runs from corner `e` to corner `e + 1`), or
    /// [[kNoTriangle]] when the edge is a border or three or more triangles meet there.
    ///
    /// - Aborts: out of range.
    u32 Neighbor(u32 t, u32 edge) const;
    /// The box around the whole mesh.
    /// - Fails: never.
    const Aabb& Bounds() const { return bounds_; }
    /// The largest distance of a vertex from the origin on each axis (the bound a collider's extent holds), at least
    /// 1e-3.
    /// - Fails: never.
    const Vec3& Extent() const { return extent_; }

    /// Appends the indices of the triangles whose boxes overlap `box` (mesh space) to `out`, in increasing order.
    ///
    /// - Param box: the query box.
    /// - Param out: receives the triangle indices, appended.
    /// - Returns: the number of triangles appended.
    /// - Ownership: `out` is the caller's.
    /// - Fails: never.
    u32 QueryBox(const Aabb& box, Vector<u32>* out) const;

private:
    Vector<Vec3> vertices_;
    Vector<u32> indices_;
    Vector<Vec3> normals_;
    Vector<u8> materials_;
    Vector<u32> neighbors_;
    Aabb bounds_;
    Vec3 extent_ = Vec3(0.0f, 0.0f, 0.0f);
    spatial::StaticBvh bvh_;
};

/// The meshes a world's colliders refer to by id (1, 2, ...).
///
/// - Thread-safety: thread-compatible: reads are safe while nothing adds.
class MeshLibrary
{
public:
    /// Adds a mesh.
    ///
    /// - Returns: its id (1, 2, ...), never 0; 0 for a null mesh.
    /// - Fails: a null mesh returns 0.
    u32 Add(const TriangleMeshConstPtr& mesh);
    /// The number of meshes.
    /// - Fails: never.
    u32 Count() const { return static_cast<u32>(meshes_.Size()); }
    /// Finds a mesh.
    ///
    /// - Returns: the mesh, or null for an unknown id.
    /// - Ownership: owned by the library.
    /// - Fails: an unknown id returns null.
    const TriangleMesh* Find(u32 id) const;
    /// Finds a mesh and shares it.
    ///
    /// - Returns: the mesh, or an empty pointer for an unknown id.
    /// - Fails: an unknown id returns an empty pointer.
    TriangleMeshConstPtr Share(u32 id) const;

private:
    Vector<TriangleMeshConstPtr> meshes_;
};

}  // namespace vse::physics
