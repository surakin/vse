#pragma once

//! A terrain's immutable data as the renderer draws it: the quadtree and the height texture, shared by every terrain
//! object that draws the same height map.
//!
//! [[renderer::TerrainSource]] is made once from a height map (`asset::MakeTerrainSource`), is never changed, and is
//! held by `SharedPtr` ([[renderer::TerrainSourcePtr]]): a reload of the height map makes a new source, and a terrain
//! object that is given it draws the new heights.

#include "containers/shared_ptr.h"
#include "math/matrix.h"
#include "renderer/terrain_tiles.h"
#include "renderer/texture.h"
#include "types.h"

namespace vse::renderer
{

/// The data of one height map as the renderer uses it.
///
/// - Thread-safety: thread-compatible; immutable once shared.
/// - Ownership: owns the quadtree and shares the height texture.
class TerrainSource
{
public:
    TerrainTiles tiles;    ///< The quadtree; built.
    GpuTexturePtr height;  ///< The height texture (`R16_UNorm`, the map's mip levels).
    u32 samples_x = 0;     ///< Samples across.
    u32 samples_z = 0;     ///< Samples down.
    f32 cell_size = 1.0f;  ///< Metres between samples.
    f32 range_min = 0.0f;  ///< The height of sample value 0.
    f32 range_max = 1.0f;  ///< The height of sample value 65535.

    /// Returns whether the quadtree is built and the texture exists.
    /// - Fails: never.
    bool Valid() const { return tiles.Valid() && height && height->Valid() && samples_x >= 2 && samples_z >= 2; }
};

/// How a terrain source is shared.
using TerrainSourcePtr = SharedPtr<TerrainSource>;
/// How a terrain source is shared when it is only read.
using TerrainSourceConstPtr = SharedPtr<const TerrainSource>;

/// Whether `world` is a placement a terrain accepts: a translation and a turn about the y axis, nothing else (no
/// scale, no tilt, no shear), with finite entries.
///
/// - Param world: the terrain-space to world-space matrix.
/// - Returns: true if its columns are the turned unit x and z axes, the unit y axis and a finite translation.
/// - Fails: never.
/// - Thread-safety: thread-safe.
bool TerrainWorldValid(const Mat4& world);

}  // namespace vse::renderer
