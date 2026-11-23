#pragma once
//! The height map resource: 16-bit terrain heights over a height range, their mip chain, and a pyramid of block
//! minima and maxima. [[asset::BuildHeightMapBlob]] writes an `HGHT` blob, [[asset::HeightMapView]] reads one in
//! place (the renderer makes its height texture from the levels, the physics module its height field from level 0 and
//! the blocks, without copying).
//!
//! Layout of the blob's sections (little-endian):
//!
//! | Section | Content |
//! |---|---|
//! | 16 header | 32 bytes: width `u32`, height `u32` (samples, 2 to 8192), mip count `u32` (the full chain), block
//! cells `u32` (8), range minimum `f32`, range maximum `f32`, cell size `f32` (metres between samples), reserved `u32`
//! (zero) | | 17 heights | the levels packed from level 0, each `width * height` `u16` samples in rows; a level is half
//! the one before (rounded down, at least 1), each sample the rounded mean of the four above it (the edge repeated);
//! 64-byte aligned | | 18 blocks | for each block of 8 by 8 cells of level 0 (the last ones smaller), row by row:
//! minimum `u16` then maximum `u16` of the samples the block touches, its edges included |
//!
//! A sample `s` is the height `range_min + s / 65535 * (range_max - range_min)`.

#include "asset/blob.h"
#include "asset/resource_id.h"
#include "asset/resource_type.h"
#include "asset/status.h"
#include "containers/shared_bytes.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "math/vector.h"
#include "types.h"

namespace vse::asset
{

/// The size of the header section.
inline constexpr usize kHeightMapHeaderBytes = 32;
/// The fewest samples across a side of a height map.
inline constexpr u32 kMinHeightMapSide = 2;
/// The most samples across a side of a height map.
inline constexpr u32 kMaxHeightMapSide = 8192;
/// The cells across a block of the pyramid.
inline constexpr u32 kHeightMapBlockCells = 8;

/// The section ids of a height map blob.
enum HeightMapSection : u32
{
    kHeightMapSectionHeader = 16,   ///< The description (32 bytes).
    kHeightMapSectionHeights = 17,  ///< The mip levels of samples.
    kHeightMapSectionBlocks = 18,   ///< The pyramid of block minima and maxima.
};

/// What a height map blob says about its samples.
///
/// - Thread-safety: thread-compatible plain data.
struct HeightMapHeader
{
    u32 width = 0;         ///< Samples across (the x axis), 2 to `kMaxHeightMapSide`.
    u32 height = 0;        ///< Samples down (the z axis), 2 to `kMaxHeightMapSide`.
    f32 range_min = 0.0f;  ///< The height of sample value 0, in metres; finite.
    f32 range_max = 1.0f;  ///< The height of sample value 65535, in metres; finite, not below `range_min`.
    f32 cell_size = 1.0f;  ///< Metres between neighbouring samples; finite, above 0.
};

/// The levels of a full mip chain for a map of `width` by `height` samples.
///
/// - Fails: never.
/// - Thread-safety: thread-safe. A pure function of its arguments.
u32 HeightMapMipCount(u32 width, u32 height);

/// Writes the blob of a height map, deterministically: the mip chain and the block pyramid are made here.
///
/// - Param types: the registry; it must hold `kHeightMap`.
/// - Param id: the map's resource id.
/// - Param header: the description; every field is checked.
/// - Param samples: exactly `width * height` samples of 16 bits, little-endian, rows from z = 0.
/// - Param out: receives the blob bytes.
/// - Param target: the target platform code of the blob header.
/// - Fails: `Invalid` naming the first broken rule; the errors of [[asset::BlobWriter::Finish]].
/// - Ownership: `samples` is read during the call; `out` receives a copy.
Status BuildHeightMapBlob(const TypeRegistry& types, ResourceId id, const HeightMapHeader& header, ByteSpan samples,
                          Vector<byte>* out, u32 target = 0);

/// A height map blob validated and read in place.
///
/// - Thread-safety: thread-compatible; immutable after `Open`.
/// - Ownership: shares the buffer it was opened on.
class HeightMapView
{
public:
    /// Creates an empty view.
    HeightMapView() = default;

    /// Validates `bytes` as a height map blob of the current version and keeps a share of them. The samples are not
    /// scanned; the pyramid is checked to hold a minimum not above its maximum.
    ///
    /// - Fails: the errors of [[asset::BlobReader::Open]]; `TypeMismatch` when the blob is not an `HGHT`; `Corrupt`
    ///   for a missing or mis-sized section, a field outside its limits or a header field this version does not
    ///   define.
    /// - Ownership: shares `bytes`.
    Status Open(const SharedBytesPtr& bytes, const TypeRegistry& types);

    /// The description.
    /// - Fails: never.
    const HeightMapHeader& Header() const { return header_; }

    /// The resource id recorded in the blob header.
    /// - Fails: never.
    ResourceId Id() const { return id_; }

    /// The buffer the view shares.
    /// - Fails: never.
    /// - Ownership: borrowed.
    const SharedBytesPtr& Bytes() const { return bytes_; }

    /// The levels stored.
    /// - Fails: never.
    u32 MipCount() const { return mips_; }

    /// Samples across a level.
    /// - Param level: below `MipCount()`.
    /// - Aborts: `level` is `MipCount()` or more.
    u32 MipWidth(u32 level) const;

    /// Samples down a level.
    /// - Param level: below `MipCount()`.
    /// - Aborts: `level` is `MipCount()` or more.
    u32 MipHeight(u32 level) const;

    /// Where a level starts in `Bytes()`.
    /// - Param level: below `MipCount()`.
    /// - Aborts: `level` is `MipCount()` or more.
    usize MipOffset(u32 level) const;

    /// The sample at `(x, z)` of level 0.
    /// - Param x: below the width.
    /// - Param z: below the height.
    /// - Aborts: a coordinate is out of range, or the view is empty.
    u16 Sample(u32 x, u32 z) const;

    /// The height in metres of a sample value.
    /// - Fails: never.
    f32 SampleToHeight(u16 sample) const;

    /// The terrain height at a point, by the two triangles of the cell, each cell cut from its minimum-x, minimum-z
    /// corner to its maximum-x, maximum-z corner; the point is clamped to the map. Uses `+ - * /` only, so it is the
    /// same on every machine.
    ///
    /// - Param x: metres along x from the first sample.
    /// - Param z: metres along z from the first sample.
    /// - Aborts: the view is empty.
    f32 HeightAt(f32 x, f32 z) const;

    /// The unit surface normal at a point: the central differences of the heights one sample either side of the
    /// point's cell corner, the point clamped to the map (what the terrain shader derives from four taps).
    ///
    /// - Param x: metres along x from the first sample.
    /// - Param z: metres along z from the first sample.
    /// - Aborts: the view is empty.
    Vec3 NormalAt(f32 x, f32 z) const;

    /// Blocks across the pyramid.
    /// - Fails: never.
    u32 BlocksX() const { return blocks_x_; }

    /// Blocks down the pyramid.
    /// - Fails: never.
    u32 BlocksZ() const { return blocks_z_; }

    /// The smallest sample of a block, its edges included.
    /// - Aborts: the block is out of range.
    u16 BlockMin(u32 bx, u32 bz) const;

    /// The largest sample of a block, its edges included.
    /// - Aborts: the block is out of range.
    u16 BlockMax(u32 bx, u32 bz) const;

private:
    HeightMapHeader header_;
    ResourceId id_;
    SharedBytesPtr bytes_;
    u32 mips_ = 0;
    usize heights_ = 0;  // offset of level 0 in the buffer
    usize blocks_ = 0;   // offset of the pyramid in the buffer
    u32 blocks_x_ = 0;
    u32 blocks_z_ = 0;
};

}  // namespace vse::asset
