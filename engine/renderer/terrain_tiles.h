#pragma once

//! The tile quadtree of a terrain and the choice of which tiles to draw for a camera, on the CPU.
//!
//! The terrain is cut into tiles of 32 by 32 cells (`kTerrainTileCells`); a node of level `L` covers `2^L` tiles across
//! and is drawn with the same 32 by 32 grid stretched over them, so a higher level is a coarser picture. The quadtree
//! holds a box for every node, tight in height because it is built from the height map's block pyramid, and no
//! pointers: a node is a level and a position, its children are the four positions of the level below.
//!
//! Everything is in the terrain's own space: x and z in metres from the first sample, y the height in metres. A caller
//! with a placed terrain moves the camera and the frustum into that space first.

#include "containers/span.h"
#include "containers/vector.h"
#include "math/geometry.h"
#include "math/vector.h"
#include "types.h"

namespace vse::renderer
{

/// Cells across a tile of the finest level.
inline constexpr u32 kTerrainTileCells = 32;
/// The most levels a quadtree has (8192 samples across make 256 tiles: levels 0 to 8).
inline constexpr u32 kMaxTerrainLevels = 9;

/// What a terrain's quadtree is built from: its size and the box of every block of 8 by 8 cells.
///
/// - Thread-safety: thread-compatible plain data.
struct TerrainTilesDesc
{
    u32 samples_x = 0;      ///< Samples across (2 to 8192).
    u32 samples_z = 0;      ///< Samples down (2 to 8192).
    f32 cell_size = 1.0f;   ///< Metres between samples; finite, above 0.
    f32 range_min = 0.0f;   ///< The height of sample value 0.
    f32 range_max = 1.0f;   ///< The height of sample value 65535; not below `range_min`.
    u32 blocks_x = 0;       ///< Blocks across: `ceil((samples_x - 1) / 8)`.
    u32 blocks_z = 0;       ///< Blocks down.
    Vector<u16> block_min;  ///< `blocks_x * blocks_z` smallest samples, row by row.
    Vector<u16> block_max;  ///< The matching largest samples.
};

/// How tiles are chosen.
///
/// - Thread-safety: thread-compatible plain data.
struct TerrainSelectSettings
{
    /// The distance in metres to a node's box inside which the finest level is used; level `L` is used out to
    /// `first_range * 2^L`. At least three tile widths, which is what keeps neighbouring tiles within one level.
    f32 first_range = 128.0f;
    /// The fraction of each level's range, at its far end, over which a tile morphs into the next level; in [0, 1).
    f32 morph_fraction = 0.3f;
    /// Nodes farther than this are not drawn; 0 means no limit.
    f32 max_distance = 0.0f;
};

/// A tile chosen to be drawn.
///
/// - Thread-safety: thread-compatible plain data.
struct TerrainTile
{
    u32 level = 0;  ///< 0 for the finest.
    u32 x = 0;      ///< The node's column at its level.
    u32 z = 0;      ///< The node's row at its level.
};

/// The tile quadtree of one height map.
///
/// - Thread-safety: thread-compatible; `Select` and the accessors are thread-safe on an object nobody is building.
/// - Ownership: owns its boxes.
class TerrainTiles
{
public:
    /// Builds the quadtree.
    ///
    /// - Param desc: the map; the block counts must match the sample counts and the arrays their size.
    /// - Returns: false (and the object stays empty) for a size outside 2 to 8192, a cell size that is not finite and
    ///   above 0, a height range that is not finite or has its minimum above its maximum, or arrays of the wrong size.
    /// - Ownership: copies what it needs from `desc`.
    bool Build(const TerrainTilesDesc& desc);

    /// Whether `Build` succeeded.
    /// - Fails: never.
    bool Valid() const { return levels_ > 0; }

    /// The levels: 1 when one tile covers the map.
    /// - Fails: never.
    u32 LevelCount() const { return levels_; }

    /// Tiles across the finest level.
    /// - Fails: never.
    u32 TilesX() const { return tiles_x_; }

    /// Tiles down the finest level.
    /// - Fails: never.
    u32 TilesZ() const { return tiles_z_; }

    /// Nodes across a level.
    /// - Param level: below `LevelCount()`.
    /// - Aborts: `level` is `LevelCount()` or more.
    u32 NodesX(u32 level) const;

    /// Nodes down a level.
    /// - Param level: below `LevelCount()`.
    /// - Aborts: `level` is `LevelCount()` or more.
    u32 NodesZ(u32 level) const;

    /// The box of a node, in terrain space, clipped to the map.
    /// - Aborts: the node does not exist.
    Aabb NodeBounds(u32 level, u32 x, u32 z) const;

    /// The first and the last cell column and row a node covers, clipped to the map.
    /// - Param cells: set to `{first_x, first_z, end_x, end_z}`, the end exclusive.
    /// - Aborts: the node does not exist.
    void NodeCells(u32 level, u32 x, u32 z, u32 cells[4]) const;

    /// Chooses the tiles to draw, appending them to `out` in the order level, then z, then x.
    ///
    /// A node is split while the camera is nearer to its box than the range of the level below; a node whose box is
    /// outside the frustum (or beyond `max_distance`) is dropped with everything under it. The chosen nodes cover each
    /// cell of the map that is in view exactly once, and neighbouring tiles differ by at most one level when the
    /// settings are valid. The result depends on the arguments only, so the same arguments give the same list on every
    /// thread and every run.
    ///
    /// - Param camera: the camera position in terrain space.
    /// - Param frustum: the view frustum in terrain space, or null to select without culling.
    /// - Param settings: valid settings ([[renderer::TerrainSelectSettingsValid]]).
    /// - Param out: replaced by the tiles.
    /// - Aborts: the quadtree is empty or the settings are not valid.
    /// - Ownership: borrows its arguments for the call; `*out` is replaced.
    void Select(const Vec3& camera, const Frustum* frustum, const TerrainSelectSettings& settings,
                Vector<TerrainTile>* out) const;

private:
    struct Level
    {
        u32 nodes_x = 0;
        u32 nodes_z = 0;
        Vector<Aabb> boxes;
    };

    void SelectNode(u32 level, u32 x, u32 z, const Vec3& camera, const Frustum* frustum,
                    const TerrainSelectSettings& settings, Vector<TerrainTile>* out) const;

    u32 levels_ = 0;
    u32 tiles_x_ = 0;
    u32 tiles_z_ = 0;
    u32 cells_x_ = 0;
    u32 cells_z_ = 0;
    f32 cell_size_ = 1.0f;
    Level level_[kMaxTerrainLevels];
};

/// Whether the settings keep neighbouring tiles within one level: `first_range` at least three tile widths of
/// `cell_size` and finite, `morph_fraction` in [0, 1), `max_distance` finite and not negative.
///
/// - Param cell_size: metres between samples of the terrain the settings are for.
/// - Fails: never.
bool TerrainSelectSettingsValid(const TerrainSelectSettings& settings, f32 cell_size);

/// The distance out to which level `level` is used: `first_range * 2^level`.
/// - Fails: never.
f32 TerrainLevelRange(const TerrainSelectSettings& settings, u32 level);

/// The distance at which a tile of level `level` starts to morph toward level `level + 1`: its range minus the morph
/// fraction of the range it spans.
/// - Fails: never.
f32 TerrainMorphStart(const TerrainSelectSettings& settings, u32 level);

}  // namespace vse::renderer
