#pragma once

//! The height field of a collider: a grid of 16-bit height samples read in place, a pyramid of block minima and
//! maxima over it, the surface the grid defines (two triangles a cell) and the queries on that surface; and the library
//! a world keeps its fields in.
//!
//! The field lives in its own space: x and z in metres from the CENTRE of the map (so the shape's bound is centred on
//! the shape's origin like every other shape's), y in metres as the height. Sample `(i, j)` is at
//! `(i * cell_size - half_x, height(i, j), j * cell_size - half_z)`. The terrain asset's own space starts at the first
//! sample; its point `(x, z)` is this field's `(x - half_x, z - half_z)` (`HeightAtMap` takes the asset's coordinates).
//!
//! The surface: the cell between samples `(cx, cz)` and `(cx + 1, cz + 1)` is cut along the diagonal from its
//! minimum-x, minimum-z corner to its maximum-x, maximum-z corner into triangle `2 * cell` (corners `(0,0)`, `(1,1)`,
//! `(1,0)`: the half where the fraction along x is at least the one along z) and triangle `2 * cell + 1` (corners
//! `(0,0)`, `(0,1)`, `(1,1)`), both counter-clockwise seen from above, so their normals point up. A cell is
//! `cz * (width - 1) + cx`. The height at a point is the plane of its triangle: the same function as
//! `asset::HeightMapView::HeightAt`.

#include "containers/shared_bytes.h"
#include "containers/shared_ptr.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "math/geometry.h"
#include "math/vector.h"
#include "physics/validation.h"
#include "types.h"

namespace vse::physics
{

/// The fewest samples across a side of a field.
inline constexpr u32 kMinHeightFieldSide = 2;
/// The most samples across a side of a field.
inline constexpr u32 kMaxHeightFieldSide = 8192;
/// The cells across a block of the pyramid.
inline constexpr u32 kHeightFieldBlockCells = 8;
/// The "no triangle" value of the field's neighbour query (the same number as for meshes).
inline constexpr u32 kNoFieldTriangle = 0xFFFFFFFFu;

/// The numbers a field is made from. The samples are read in place from a shared region, never copied.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: `bytes` is shared with whoever else reads the region (the loaded terrain asset, the renderer).
struct HeightFieldSpec
{
    SharedBytesPtr bytes;  ///< The region holding the samples.
    usize offset = 0;  ///< Where level 0 starts in the region: `width * height` little-endian `u16`, rows from z = 0.
    u32 width = 0;     ///< Samples across (x), 2 to `kMaxHeightFieldSide`.
    u32 height = 0;    ///< Samples down (z), 2 to `kMaxHeightFieldSide`.
    f32 range_min = 0.0f;  ///< The height of sample value 0, finite.
    f32 range_max = 1.0f;  ///< The height of sample value 65535, finite, not below `range_min`.
    f32 cell_size = 1.0f;  ///< Metres between neighbouring samples, finite, above 0.
};

class HeightField;
/// The shared field, read-only: the way to hold one.
using HeightFieldConstPtr = SharedPtr<const HeightField>;

/// A height field: the samples, the pyramid, the surface and its queries. Immutable once made.
///
/// - Thread-safety: thread-safe to read.
/// - Ownership: shares the region of samples through `HeightFieldSpec::bytes`; owns its pyramid.
class HeightField
{
public:
    HeightField() = default;
    HeightField(const HeightField&) = delete;
    HeightField& operator=(const HeightField&) = delete;

    /// Checks and builds a field over samples in a shared region.
    ///
    /// - Param result: receives the reason on failure; may be null.
    /// - Returns: the field, or an empty pointer.
    /// - Ownership: shares `spec.bytes`; the pyramid is built here.
    /// - Fails: `OutOfRange` for a side outside its limits, no region, a region too short for the samples or a field
    ///   whose half size passes `kMaxExtent`; `NotFinite` for a non-finite range or cell size; `NotPositive` for a cell
    ///   size not above 0 or a range whose maximum is below its minimum.
    static HeightFieldConstPtr Create(const HeightFieldSpec& spec, Validation* result);

    /// Checks and builds a field over samples that are copied (for a test or a host that holds plain numbers).
    ///
    /// - Param samples: `width * height` samples, rows from z = 0.
    /// - Param width: samples across.
    /// - Param height: samples down.
    /// - Param range_min: the height of sample value 0.
    /// - Param range_max: the height of sample value 65535.
    /// - Param cell_size: metres between samples.
    /// - Param result: receives the reason on failure; may be null.
    /// - Returns: the field, or an empty pointer.
    /// - Ownership: copies the samples; nothing is borrowed after the call.
    /// - Fails: as [[HeightField::Create]], and `OutOfRange` for a sample count that is not `width * height`.
    static HeightFieldConstPtr CreateFromSamples(Span<const u16> samples, u32 width, u32 height, f32 range_min,
                                                 f32 range_max, f32 cell_size, Validation* result);

    /// Samples across.
    /// - Fails: never.
    u32 Width() const { return width_; }
    /// Samples down.
    /// - Fails: never.
    u32 Depth() const { return depth_; }
    /// Metres between samples.
    /// - Fails: never.
    f32 CellSize() const { return cell_size_; }
    /// The height of sample value 0.
    /// - Fails: never.
    f32 RangeMin() const { return range_min_; }
    /// The height of sample value 65535.
    /// - Fails: never.
    f32 RangeMax() const { return range_max_; }
    /// The number of cells, `(width - 1) * (depth - 1)`.
    /// - Fails: never.
    u32 CellCount() const { return (width_ - 1) * (depth_ - 1); }
    /// The number of triangles, two a cell.
    /// - Fails: never.
    u32 TriangleCount() const { return CellCount() * 2; }
    /// The box around the whole surface, in the field's space.
    /// - Fails: never.
    const Aabb& Bounds() const { return bounds_; }
    /// The largest distance of the surface from the origin on each axis (the bound a collider's extent holds), at least
    /// 1e-3.
    /// - Fails: never.
    const Vec3& Extent() const { return extent_; }

    /// The sample at `(x, z)`.
    /// - Param x: below `Width()`.
    /// - Param z: below `Depth()`.
    /// - Aborts: a coordinate is out of range.
    u16 Sample(u32 x, u32 z) const;

    /// The height in metres of a sample value.
    /// - Fails: never.
    f32 SampleToHeight(u16 sample) const;

    /// The surface height at a point of the map's own space (metres from the first sample, as
    /// `asset::HeightMapView::HeightAt`), the point clamped to the map. Uses `+ - * /` only.
    ///
    /// - Param x: metres along x from the first sample.
    /// - Param z: metres along z from the first sample.
    /// - Fails: never.
    f32 HeightAtMap(f32 x, f32 z) const;

    /// The surface height at a point of the field's space (metres from the map's centre), the point clamped to the map.
    ///
    /// - Param x: metres along x from the centre.
    /// - Param z: metres along z from the centre.
    /// - Fails: never.
    f32 HeightAt(f32 x, f32 z) const;

    /// The corners of triangle `t`, in the field's space, counter-clockwise seen from above.
    ///
    /// - Param t: below `TriangleCount()`.
    /// - Param a: receives the first corner.
    /// - Param b: receives the second.
    /// - Param c: receives the third.
    /// - Ownership: the corners are written to the caller's objects.
    /// - Aborts: `t` is out of range.
    void Triangle(u32 t, Vec3* a, Vec3* b, Vec3* c) const;

    /// The unit normal of triangle `t` (it points up).
    ///
    /// - Param t: below `TriangleCount()`.
    /// - Aborts: `t` is out of range.
    Vec3 Normal(u32 t) const;

    /// The triangle that shares edge `edge` of `t` (edge `e` runs from corner `e` to corner `e + 1`), or
    /// [[kNoFieldTriangle]] on the border of the map.
    ///
    /// - Aborts: out of range.
    u32 Neighbor(u32 t, u32 edge) const;

    /// Appends the triangles that may touch `box` (field space) to `out`, in increasing order: the triangles of the
    /// cells under the box's footprint whose block and corner heights reach the box's height range.
    ///
    /// - Param box: the query box.
    /// - Param out: receives the triangle indices, appended.
    /// - Param limit: the most triangles to append; the cells are taken row by row, so the rest are those of the last
    ///   rows.
    /// - Returns: the number appended.
    /// - Ownership: `out` is the caller's.
    /// - Fails: never.
    u32 QueryBox(const Aabb& box, Vector<u32>* out, u32 limit = 0xFFFFFFFFu) const;

    /// Casts a ray at the surface, from either side, and returns the closest hit. The cells are walked front to back
    /// and a block of the pyramid the ray passes above or below is skipped without testing its cells. A ray lying in a
    /// triangle's plane misses it.
    ///
    /// - Param origin: where the ray starts (field space).
    /// - Param direction: the direction; need not be a unit vector, must be finite and not zero.
    /// - Param max_t: how far along `direction` to look, at least 0.
    /// - Param t: receives the hit's parameter along the ray.
    /// - Param triangle: receives the triangle hit; may be null.
    /// - Returns: whether the surface was hit.
    /// - Ownership: `t` and `triangle` are the caller's.
    /// - Fails: a non-finite ray or a zero direction returns false.
    bool RayCast(const Vec3& origin, const Vec3& direction, f32 max_t, f32* t, u32* triangle) const;

    /// A hash of the field's content (sizes, range, cell size and every sample), the same on every machine for the
    /// same samples however they were stored.
    ///
    /// - Fails: never.
    u64 ContentHash() const;

private:
    u16 SampleAt(u32 x, u32 z) const;
    u16 BlockMin(u32 bx, u32 bz) const { return block_min_[usize{bz} * blocks_x_ + bx]; }
    u16 BlockMax(u32 bx, u32 bz) const { return block_max_[usize{bz} * blocks_x_ + bx]; }
    void BuildPyramid();

    SharedBytesPtr bytes_;
    usize offset_ = 0;
    u32 width_ = 0;
    u32 depth_ = 0;
    f32 range_min_ = 0.0f;
    f32 range_max_ = 0.0f;
    f32 cell_size_ = 1.0f;
    f32 half_x_ = 0.0f;
    f32 half_z_ = 0.0f;
    Aabb bounds_;
    Vec3 extent_ = Vec3(0.0f, 0.0f, 0.0f);
    u32 blocks_x_ = 0;
    u32 blocks_z_ = 0;
    Vector<u16> block_min_;
    Vector<u16> block_max_;
};

/// The fields a world's colliders refer to by id (1, 2, ...).
///
/// - Thread-safety: thread-compatible: reads are safe while nothing adds.
class HeightFieldLibrary
{
public:
    /// Adds a field.
    ///
    /// - Returns: its id (1, 2, ...), never 0; 0 for a null field.
    /// - Fails: a null field returns 0.
    u32 Add(const HeightFieldConstPtr& field);
    /// The number of fields.
    /// - Fails: never.
    u32 Count() const { return static_cast<u32>(fields_.Size()); }
    /// Finds a field.
    ///
    /// - Returns: the field, or null for an unknown id.
    /// - Ownership: owned by the library.
    /// - Fails: an unknown id returns null.
    const HeightField* Find(u32 id) const;
    /// Finds a field and shares it.
    ///
    /// - Returns: the field, or an empty pointer for an unknown id.
    /// - Fails: an unknown id returns an empty pointer.
    HeightFieldConstPtr Share(u32 id) const;

private:
    Vector<HeightFieldConstPtr> fields_;
};

}  // namespace vse::physics
