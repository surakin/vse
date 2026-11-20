#pragma once

//! The minted triangle mesh of a static physics collider (`MCOL`, Phase 25 Day 394): positions, triangle indices and a
//! material index per triangle. The bounding-volume hierarchy is built by the physics module when the mesh is added
//! (deterministically, from these bytes). The reader treats the bytes as hostile.

#include "asset/blob.h"
#include "asset/resource_id.h"
#include "asset/resource_type.h"
#include "asset/status.h"
#include "containers/array.h"
#include "containers/shared_bytes.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "types.h"

namespace vse::asset
{

/// The most vertices a mesh collider holds.
inline constexpr u32 kMaxMeshColliderVertices = 1u << 20;
/// The most triangles a mesh collider holds.
inline constexpr u32 kMaxMeshColliderTriangles = 1u << 20;
/// The size of the header section.
inline constexpr usize kMeshColliderHeaderBytes = 32;

/// Section ids of an `MCOL` blob.
enum MeshColliderSection : u32
{
    kMeshColliderSectionHeader = 16,     ///< Vertex count, triangle count, the bounds (32 bytes).
    kMeshColliderSectionPositions = 17,  ///< `f32 x3` per vertex.
    kMeshColliderSectionIndices = 18,    ///< `u32 x3` per triangle.
    kMeshColliderSectionMaterials = 19,  ///< One `u8` per triangle.
};

/// Tells whether a triangle has an area, by the physics module's own rule (glTF colliders Day 2).
///
/// The cross product of `b - a` and `c - a` in `f32`, its squared length finite and above `1e-20`: exactly
/// `physics::TriangleMesh::Create`'s test, so a triangle this accepts never makes a physics mesh refuse the collider.
///
/// - Param a: the first corner.
/// - Param b: the second corner.
/// - Param c: the third corner.
/// - Fails: never.
bool MeshColliderTriangleHasArea(const Array<f32, 3>& a, const Array<f32, 3>& b, const Array<f32, 3>& c);

/// Builds an `MCOL` blob.
///
/// - Param materials: one per triangle, or empty for all zero.
/// - Ownership: the inputs are read during the call only; `out` is the caller's.
/// - Fails: `Invalid` for no triangles, a count over the limits, a non-finite position, an index outside the vertices
/// or a triangle with no area ([[asset::MeshColliderTriangleHasArea]]); `LimitExceeded` is not used.
Status BuildMeshColliderBlob(const TypeRegistry& types, ResourceId id, Span<const Array<f32, 3>> positions,
                             Span<const u32> indices, Span<const u8> materials, Vector<byte>* out, u32 target = 0);

/// A read-only view of an `MCOL` blob.
///
/// - Thread-safety: thread-compatible; reads are safe while nothing opens it again.
class MeshColliderView
{
public:
    /// Creates an empty view.
    MeshColliderView() = default;

    /// Validates `bytes` as a mesh-collider blob and keeps a share of them.
    ///
    /// - Fails: the errors of [[asset::BlobReader::Open]]; `TypeMismatch` for another type; `Corrupt` for a missing or
    /// mis-sized section, a count over its limit, a non-finite position, an index outside the vertices, a triangle with
    /// no area ([[asset::MeshColliderTriangleHasArea]]) or a non-zero reserved header field.
    /// - Ownership: shares `bytes`.
    Status Open(const SharedBytesPtr& bytes, const TypeRegistry& types);

    /// The vertex count.
    /// - Fails: never.
    u32 VertexCount() const { return vertex_count_; }
    /// The triangle count.
    /// - Fails: never.
    u32 TriangleCount() const { return triangle_count_; }
    /// The resource id.
    /// - Fails: never.
    ResourceId Id() const { return id_; }
    /// The whole blob the view reads (glTF colliders Day 5: a host hashes it to know two loads apart).
    /// - Fails: never; empty for a view that is not open.
    ByteSpan Bytes() const { return bytes_ ? bytes_->Bytes() : ByteSpan(); }
    /// Vertex `i`.
    ///
    /// - Aborts: `i` outside `[0, VertexCount())`.
    Array<f32, 3> Position(u32 i) const;
    /// Corner `corner` (0-2) of triangle `t`: a vertex index.
    ///
    /// - Aborts: `t` or `corner` out of range.
    u32 Index(u32 t, u32 corner) const;
    /// The material index of triangle `t`.
    ///
    /// - Aborts: `t` out of range.
    u8 Material(u32 t) const;

private:
    const byte* Base() const { return bytes_->Bytes().Data(); }

    SharedBytesPtr bytes_;
    u32 vertex_count_ = 0;
    u32 triangle_count_ = 0;
    ResourceId id_;
    usize positions_ = 0;
    usize indices_ = 0;
    usize materials_ = 0;
};

}  // namespace vse::asset
