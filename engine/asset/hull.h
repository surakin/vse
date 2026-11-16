#pragma once
//! The convex hull resource (Phase 25 Day 381): the vertices, the face polygons with their planes and the mass data of
//! a convex hull, for the physics module's collision shape. [[asset::BuildHullGeometry]] computes the hull of a point
//! cloud, [[asset::BuildHullBlob]] writes a `HULL` blob, [[asset::HullView]] reads one in place.
//!
//! Layout of the blob's sections (little-endian, one section each):
//!
//! | Section | Content |
//! |---|---|
//! | 16 header | 80 bytes: vertex count `u32`, face count `u32`, index count `u32`, flags `u32` (zero), volume `f32`,
//! centroid `f32 x3`, inertia `f32 x6` (xx, yy, zz, xy, xz, yz of the tensor about the centroid at unit density),
//! extent `f32 x3` (the largest absolute coordinate on each axis), 12 zero bytes | | 17 vertices | `f32 x3` per vertex
//! | | 18 planes | `f32 x4` per face: the outward unit normal and `d`, with `dot(n, p) == d` on the face | | 19 faces |
//! two `u32` per face: the first index and the number of vertices | | 20 indices | `u16` per index: the vertices of
//! each face, counter-clockwise seen from outside |
#include "asset/blob.h"
#include "asset/resource_id.h"
#include "asset/resource_type.h"
#include "asset/status.h"
#include "containers/array.h"
#include "containers/shared_bytes.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "types.h"

namespace engine_placeholder::asset
{

/// The most vertices a hull holds.
inline constexpr u32 kMaxHullVertices = 255;
/// The most faces a hull holds.
inline constexpr u32 kMaxHullFaces = 512;
/// The most vertices of one face.
inline constexpr u32 kMaxHullFaceVertices = 64;
/// The most points `BuildHullGeometry` takes.
inline constexpr u32 kMaxHullInputPoints = 4096;
/// The size of the header section.
inline constexpr usize kHullHeaderBytes = 80;

/// The section ids of a hull blob.
enum HullSection : u32
{
    kHullSectionHeader = 16,    ///< The counts, the mass data and the extent (80 bytes).
    kHullSectionVertices = 17,  ///< `f32 x3` per vertex.
    kHullSectionPlanes = 18,    ///< `f32 x4` per face: the outward unit normal and the offset.
    kHullSectionFaces = 19,     ///< Two `u32` per face: first index, vertex count.
    kHullSectionIndices = 20,   ///< `u16` per index.
};

/// A convex hull in memory: what the builder makes and the blob writer takes.
///
/// - Thread-safety: thread-compatible (a plain value).
struct HullGeometry
{
    Vector<Array<f32, 3>> vertices;  ///< The corners.
    Vector<Array<f32, 4>> planes;    ///< Per face: the outward unit normal and `d` (`dot(n, p) == d`).
    Vector<u32> face_first;          ///< Per face: where its vertices start in `indices`.
    Vector<u32> face_count;          ///< Per face: how many vertices, at least 3.
    Vector<u16> indices;             ///< The face vertices, counter-clockwise seen from outside.
    f32 volume = 0.0f;               ///< The volume.
    Array<f32, 3> centroid = {};     ///< The centre of mass at uniform density.
    Array<f32, 6> inertia = {};      ///< xx, yy, zz, xy, xz, yz of the inertia tensor about the centroid, density 1.
    Array<f32, 3> extent = {};       ///< The largest absolute coordinate on each axis.
};

/// Computes the convex hull of a point cloud, deterministically.
///
/// Points closer together than 1e-6 of the cloud's size are welded (the first stays); the hull is built incrementally
/// in double precision; coplanar triangles merge into one polygon face; the vertices that remain are numbered in input
/// order. The volume, centroid and inertia are computed from the faces.
///
/// - Param points: 4 to `kMaxHullInputPoints` points, finite.
/// - Param out: receives the hull.
/// - Returns: `Invalid` for fewer than four points, a non-finite point, or points that are all on a line or a plane
/// (thinner than 1e-5 of their size); `LimitExceeded` for too many input points, more than `kMaxHullVertices`
/// vertices, `kMaxHullFaces` faces or `kMaxHullFaceVertices` vertices on a face.
/// - Ownership: `*out` is replaced; nothing is borrowed after the call.
/// - Fails: as the return says.
Status BuildHullGeometry(Span<const Array<f32, 3>> points, HullGeometry* out);

/// Writes the blob of a hull, deterministically.
///
/// - Param types: the registry; it must hold `kHull`.
/// - Param id: the hull's resource id.
/// - Param hull: a hull as `BuildHullGeometry` makes it; every count, index, plane and number is checked.
/// - Fails: `Invalid` naming the first broken rule; the errors of [[asset::BlobWriter::Finish]].
/// - Ownership: `out` receives the bytes; nothing is borrowed after the call.
Status BuildHullBlob(const TypeRegistry& types, ResourceId id, const HullGeometry& hull, Vector<byte>* out,
                     u32 target = 0);

/// A hull blob validated and read in place. Every section is a span of the caller's buffer; nothing is copied.
///
/// - Thread-safety: thread-compatible; immutable after `Open`.
/// - Ownership: borrows the buffer, which must outlive the view.
class HullView
{
public:
    /// Creates an empty view.
    HullView() = default;

    /// Validates `bytes` as a hull blob of the current version and keeps a share of them (with the offsets of the
    /// sections).
    ///
    /// - Fails: the errors of [[asset::BlobReader::Open]]; `TypeMismatch` when the blob is not a `HULL`; `Corrupt` for
    /// a missing or mis-sized section, a count above its limit, a face with fewer than three vertices or a range
    /// outside the index section, an index outside the vertices, a non-unit plane normal, a non-finite or non-positive
    /// number, or a header field this version does not define.
    /// - Ownership: shares `bytes`; the view reads them until it is destroyed or opened again.
    Status Open(const SharedBytesPtr& bytes, const TypeRegistry& types);

    /// The number of vertices.
    /// - Fails: never.
    u32 VertexCount() const { return vertex_count_; }

    /// The number of faces.
    /// - Fails: never.
    u32 FaceCount() const { return face_count_; }

    /// The number of face indices.
    /// - Fails: never.
    u32 IndexCount() const { return index_count_; }

    /// The resource id recorded in the blob header.
    /// - Fails: never.
    ResourceId Id() const { return id_; }

    /// The volume.
    /// - Fails: never.
    f32 Volume() const { return volume_; }

    /// The centre of mass.
    /// - Fails: never.
    Array<f32, 3> Centroid() const { return centroid_; }

    /// The inertia tensor about the centroid at density 1: xx, yy, zz, xy, xz, yz.
    /// - Fails: never.
    Array<f32, 6> Inertia() const { return inertia_; }

    /// The largest absolute coordinate on each axis.
    /// - Fails: never.
    Array<f32, 3> Extent() const { return extent_; }

    /// Vertex `i`.
    /// - Aborts: `i` is not below `VertexCount()`.
    Array<f32, 3> Vertex(u32 i) const;

    /// The plane of face `i`: the outward unit normal and `d`.
    /// - Aborts: `i` is not below `FaceCount()`.
    Array<f32, 4> Plane(u32 i) const;

    /// Where the vertices of face `i` start in the index list.
    /// - Aborts: `i` is not below `FaceCount()`.
    u32 FaceFirst(u32 i) const;

    /// How many vertices face `i` has.
    /// - Aborts: `i` is not below `FaceCount()`.
    u32 FaceVertexCount(u32 i) const;

    /// Index `k` of the face index list (a vertex number).
    /// - Aborts: `k` is not below `IndexCount()`.
    u32 Index(u32 k) const;

private:
    u32 vertex_count_ = 0;
    u32 face_count_ = 0;
    u32 index_count_ = 0;
    ResourceId id_;
    f32 volume_ = 0.0f;
    Array<f32, 3> centroid_ = {};
    Array<f32, 6> inertia_ = {};
    Array<f32, 3> extent_ = {};
    const byte* Base() const { return bytes_->Bytes().Data(); }

    SharedBytesPtr bytes_;
    usize vertices_ = 0;  // offsets of the sections in the bytes
    usize planes_ = 0;
    usize faces_ = 0;
    usize indices_ = 0;
};

}  // namespace engine_placeholder::asset
