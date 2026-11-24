#include "renderer/terrain_tiles.h"

#include "algorithm.h"
#include "lang/scalar.h"
#include "log/assert.h"

namespace vse::renderer
{

namespace
{

constexpr u32 kBlockCells = 8;
constexpr u32 kBlocksPerTile = kTerrainTileCells / kBlockCells;

u32 TtCeilDiv(u32 a, u32 b)
{
    return (a + b - 1) / b;
}

f32 TtBoxDistance(const Aabb& box, const Vec3& p)
{
    const f32 dx = p.x < box.min.x ? box.min.x - p.x : (p.x > box.max.x ? p.x - box.max.x : 0.0f);
    const f32 dy = p.y < box.min.y ? box.min.y - p.y : (p.y > box.max.y ? p.y - box.max.y : 0.0f);
    const f32 dz = p.z < box.min.z ? box.min.z - p.z : (p.z > box.max.z ? p.z - box.max.z : 0.0f);
    return Sqrt(dx * dx + dy * dy + dz * dz);
}

}  // namespace

bool TerrainTiles::Build(const TerrainTilesDesc& d)
{
    *this = TerrainTiles();
    if (d.samples_x < 2 || d.samples_x > 8192 || d.samples_z < 2 || d.samples_z > 8192 || !IsFinite(d.cell_size) ||
        !(d.cell_size > 0.0f) || !IsFinite(d.range_min) || !IsFinite(d.range_max) || d.range_min > d.range_max)
    {
        return false;
    }
    const u32 cells_x = d.samples_x - 1;
    const u32 cells_z = d.samples_z - 1;
    if (d.blocks_x != TtCeilDiv(cells_x, kBlockCells) || d.blocks_z != TtCeilDiv(cells_z, kBlockCells) ||
        d.block_min.Size() != static_cast<usize>(d.blocks_x) * d.blocks_z || d.block_max.Size() != d.block_min.Size())
    {
        return false;
    }
    const u32 tiles_x = TtCeilDiv(cells_x, kTerrainTileCells);
    const u32 tiles_z = TtCeilDiv(cells_z, kTerrainTileCells);
    u32 levels = 1;
    while ((1u << (levels - 1)) < (tiles_x > tiles_z ? tiles_x : tiles_z))
    {
        ++levels;
    }
    cells_x_ = cells_x;
    cells_z_ = cells_z;
    tiles_x_ = tiles_x;
    tiles_z_ = tiles_z;
    cell_size_ = d.cell_size;
    const f32 scale = (d.range_max - d.range_min) / 65535.0f;
    // Level 0 from the blocks.
    Level& base = level_[0];
    base.nodes_x = tiles_x;
    base.nodes_z = tiles_z;
    base.boxes.Resize(static_cast<usize>(tiles_x) * tiles_z);
    for (u32 tz = 0; tz < tiles_z; ++tz)
    {
        for (u32 tx = 0; tx < tiles_x; ++tx)
        {
            u32 lo = 0xFFFF;
            u32 hi = 0;
            const u32 bx_end = (tx + 1) * kBlocksPerTile < d.blocks_x ? (tx + 1) * kBlocksPerTile : d.blocks_x;
            const u32 bz_end = (tz + 1) * kBlocksPerTile < d.blocks_z ? (tz + 1) * kBlocksPerTile : d.blocks_z;
            for (u32 bz = tz * kBlocksPerTile; bz < bz_end; ++bz)
            {
                for (u32 bx = tx * kBlocksPerTile; bx < bx_end; ++bx)
                {
                    const usize at = static_cast<usize>(bz) * d.blocks_x + bx;
                    lo = d.block_min[at] < lo ? d.block_min[at] : lo;
                    hi = d.block_max[at] > hi ? d.block_max[at] : hi;
                }
            }
            const u32 x1 = (tx + 1) * kTerrainTileCells < cells_x ? (tx + 1) * kTerrainTileCells : cells_x;
            const u32 z1 = (tz + 1) * kTerrainTileCells < cells_z ? (tz + 1) * kTerrainTileCells : cells_z;
            Aabb box;
            box.min = Vec3(static_cast<f32>(tx * kTerrainTileCells) * d.cell_size,
                           d.range_min + static_cast<f32>(lo) * scale,
                           static_cast<f32>(tz * kTerrainTileCells) * d.cell_size);
            box.max = Vec3(static_cast<f32>(x1) * d.cell_size, d.range_min + static_cast<f32>(hi) * scale,
                           static_cast<f32>(z1) * d.cell_size);
            base.boxes[static_cast<usize>(tz) * tiles_x + tx] = box;
        }
    }
    for (u32 l = 1; l < levels; ++l)
    {
        const Level& below = level_[l - 1];
        Level& here = level_[l];
        here.nodes_x = TtCeilDiv(below.nodes_x, 2);
        here.nodes_z = TtCeilDiv(below.nodes_z, 2);
        here.boxes.Resize(static_cast<usize>(here.nodes_x) * here.nodes_z);
        for (u32 z = 0; z < here.nodes_z; ++z)
        {
            for (u32 x = 0; x < here.nodes_x; ++x)
            {
                Aabb box = below.boxes[static_cast<usize>(2 * z) * below.nodes_x + 2 * x];
                for (u32 dz = 0; dz < 2; ++dz)
                {
                    for (u32 dx = 0; dx < 2; ++dx)
                    {
                        const u32 cx = 2 * x + dx;
                        const u32 cz = 2 * z + dz;
                        if (cx >= below.nodes_x || cz >= below.nodes_z)
                        {
                            continue;
                        }
                        const Aabb& c = below.boxes[static_cast<usize>(cz) * below.nodes_x + cx];
                        box.min = Vec3(box.min.x < c.min.x ? box.min.x : c.min.x,
                                       box.min.y < c.min.y ? box.min.y : c.min.y,
                                       box.min.z < c.min.z ? box.min.z : c.min.z);
                        box.max = Vec3(box.max.x > c.max.x ? box.max.x : c.max.x,
                                       box.max.y > c.max.y ? box.max.y : c.max.y,
                                       box.max.z > c.max.z ? box.max.z : c.max.z);
                    }
                }
                here.boxes[static_cast<usize>(z) * here.nodes_x + x] = box;
            }
        }
    }
    // A tile's vertices read the mip of its level (and, while morphing, of the next) with a bilinear filter, which
    // reaches up to two of that mip's texels past the tile's edge: the height range of every box covers the blocks
    // that far around it, so a mesh never leaves its box.
    for (u32 l = 0; l < levels; ++l)
    {
        Level& here = level_[l];
        const u32 side = kTerrainTileCells << l;
        const u32 pad = 2u << l;
        for (u32 z = 0; z < here.nodes_z; ++z)
        {
            for (u32 x = 0; x < here.nodes_x; ++x)
            {
                const u32 cx0 = x * side > pad ? x * side - pad : 0;
                const u32 cz0 = z * side > pad ? z * side - pad : 0;
                const u32 bx0 = cx0 / kBlockCells;
                const u32 bz0 = cz0 / kBlockCells;
                const u32 bx1 = Min(d.blocks_x, TtCeilDiv((x + 1) * side + pad, kBlockCells));
                const u32 bz1 = Min(d.blocks_z, TtCeilDiv((z + 1) * side + pad, kBlockCells));
                u32 lo = 0xFFFF;
                u32 hi = 0;
                for (u32 bz = bz0; bz < bz1; ++bz)
                {
                    for (u32 bx = bx0; bx < bx1; ++bx)
                    {
                        const usize at = static_cast<usize>(bz) * d.blocks_x + bx;
                        lo = d.block_min[at] < lo ? d.block_min[at] : lo;
                        hi = d.block_max[at] > hi ? d.block_max[at] : hi;
                    }
                }
                Aabb& box = here.boxes[static_cast<usize>(z) * here.nodes_x + x];
                box.min.y = Min(box.min.y, d.range_min + static_cast<f32>(lo) * scale);
                box.max.y = Max(box.max.y, d.range_min + static_cast<f32>(hi) * scale);
            }
        }
    }
    levels_ = levels;
    return true;
}

u32 TerrainTiles::NodesX(u32 level) const
{
    ENGINE_ASSERT(level < levels_, "TerrainTiles::NodesX: the level is out of range");
    return level_[level].nodes_x;
}

u32 TerrainTiles::NodesZ(u32 level) const
{
    ENGINE_ASSERT(level < levels_, "TerrainTiles::NodesZ: the level is out of range");
    return level_[level].nodes_z;
}

Aabb TerrainTiles::NodeBounds(u32 level, u32 x, u32 z) const
{
    ENGINE_ASSERT(level < levels_ && x < level_[level].nodes_x && z < level_[level].nodes_z,
                  "TerrainTiles::NodeBounds: the node does not exist");
    return level_[level].boxes[static_cast<usize>(z) * level_[level].nodes_x + x];
}

void TerrainTiles::NodeCells(u32 level, u32 x, u32 z, u32 cells[4]) const
{
    ENGINE_ASSERT(level < levels_ && x < level_[level].nodes_x && z < level_[level].nodes_z,
                  "TerrainTiles::NodeCells: the node does not exist");
    const u32 side = kTerrainTileCells << level;
    cells[0] = x * side;
    cells[1] = z * side;
    cells[2] = (x + 1) * side < cells_x_ ? (x + 1) * side : cells_x_;
    cells[3] = (z + 1) * side < cells_z_ ? (z + 1) * side : cells_z_;
}

bool TerrainSelectSettingsValid(const TerrainSelectSettings& s, f32 cell_size)
{
    return IsFinite(s.first_range) && IsFinite(cell_size) && cell_size > 0.0f &&
           s.first_range >= 3.0f * static_cast<f32>(kTerrainTileCells) * cell_size && IsFinite(s.morph_fraction) &&
           s.morph_fraction >= 0.0f && s.morph_fraction < 1.0f && IsFinite(s.max_distance) && s.max_distance >= 0.0f;
}

f32 TerrainMaxMorphFraction(f32 first_range, f32 cell_size)
{
    if (!IsFinite(first_range) || !IsFinite(cell_size) || !(cell_size > 0.0f) || !(first_range > 0.0f))
    {
        return 0.0f;
    }
    // 64 * sqrt(2): the diagonal of a parent node in tile widths of cells
    const f32 fraction = 1.0f - 90.50967f * cell_size / first_range;
    return fraction > 0.0f ? (fraction < 0.99f ? fraction : 0.99f) : 0.0f;
}

f32 TerrainLevelRange(const TerrainSelectSettings& settings, u32 level)
{
    return settings.first_range * static_cast<f32>(1u << level);
}

f32 TerrainMorphStart(const TerrainSelectSettings& settings, u32 level)
{
    const f32 end = TerrainLevelRange(settings, level);
    const f32 start = level == 0 ? 0.0f : TerrainLevelRange(settings, level - 1);
    return end - (end - start) * settings.morph_fraction;
}

void TerrainTiles::SelectNode(u32 level, u32 x, u32 z, const Vec3& camera, const Frustum* frustum,
                              const TerrainSelectSettings& settings, Vector<TerrainTile>* out) const
{
    const Aabb& box = level_[level].boxes[static_cast<usize>(z) * level_[level].nodes_x + x];
    if (frustum != nullptr && frustum->TestAabb(box) == Containment::Outside)
    {
        return;
    }
    const f32 distance = TtBoxDistance(box, camera);
    if (settings.max_distance > 0.0f && distance > settings.max_distance)
    {
        return;
    }
    if (level > 0 && distance < TerrainLevelRange(settings, level - 1))
    {
        const Level& below = level_[level - 1];
        for (u32 dz = 0; dz < 2; ++dz)
        {
            for (u32 dx = 0; dx < 2; ++dx)
            {
                const u32 cx = 2 * x + dx;
                const u32 cz = 2 * z + dz;
                if (cx < below.nodes_x && cz < below.nodes_z)
                {
                    SelectNode(level - 1, cx, cz, camera, frustum, settings, out);
                }
            }
        }
        return;
    }
    TerrainTile tile;
    tile.level = level;
    tile.x = x;
    tile.z = z;
    out->Push(tile);
}

namespace
{

constexpr u8 kNoLevel = 255;

}  // namespace

void TerrainTiles::Balance(Vector<TerrainTile>* tiles) const
{
    // The level of every finest-level tile the selection covers; then a neighbour may be at most one level finer.
    const u32 nx = tiles_x_;
    const u32 nz = tiles_z_;
    Vector<u8> grid;
    grid.Resize(static_cast<usize>(nx) * nz);
    for (usize i = 0; i < grid.Size(); ++i)
    {
        grid[i] = kNoLevel;
    }
    bool mixed = false;
    for (const TerrainTile& t : *tiles)
    {
        mixed = mixed || t.level != (*tiles)[0].level;
        const u32 size = 1u << t.level;
        const u32 x1 = Min(nx, (t.x + 1) * size);
        const u32 z1 = Min(nz, (t.z + 1) * size);
        for (u32 z = t.z * size; z < z1; ++z)
        {
            for (u32 x = t.x * size; x < x1; ++x)
            {
                grid[static_cast<usize>(z) * nx + x] = static_cast<u8>(t.level);
            }
        }
    }
    if (!mixed)
    {
        return;
    }
    bool changed = true;
    bool any = false;
    while (changed)
    {
        changed = false;
        for (u32 pass = 0; pass < 2; ++pass)
        {
            for (u32 zi = 0; zi < nz; ++zi)
            {
                const u32 z = pass == 0 ? zi : nz - 1 - zi;
                for (u32 xi = 0; xi < nx; ++xi)
                {
                    const u32 x = pass == 0 ? xi : nx - 1 - xi;
                    u8& v = grid[static_cast<usize>(z) * nx + x];
                    if (v == kNoLevel)
                    {
                        continue;
                    }
                    const u8 neighbours[4] = {x > 0 ? grid[static_cast<usize>(z) * nx + x - 1] : kNoLevel,
                                              x + 1 < nx ? grid[static_cast<usize>(z) * nx + x + 1] : kNoLevel,
                                              z > 0 ? grid[static_cast<usize>(z - 1) * nx + x] : kNoLevel,
                                              z + 1 < nz ? grid[static_cast<usize>(z + 1) * nx + x] : kNoLevel};
                    for (const u8 n : neighbours)
                    {
                        if (n != kNoLevel && v > n + 1)
                        {
                            v = static_cast<u8>(n + 1);
                            changed = true;
                            any = true;
                        }
                    }
                }
            }
        }
    }
    if (!any)
    {
        return;
    }
    Vector<TerrainTile> balanced;
    EmitBalanced(levels_ - 1, 0, 0, grid, &balanced);
    tiles->Swap(balanced);
}

void TerrainTiles::EmitBalanced(u32 level, u32 x, u32 z, const Vector<u8>& grid, Vector<TerrainTile>* out) const
{
    const u32 size = 1u << level;
    const u32 x1 = Min(tiles_x_, (x + 1) * size);
    const u32 z1 = Min(tiles_z_, (z + 1) * size);
    u8 lowest = kNoLevel;
    for (u32 cz = z * size; cz < z1; ++cz)
    {
        for (u32 cx = x * size; cx < x1; ++cx)
        {
            lowest = Min(lowest, grid[static_cast<usize>(cz) * tiles_x_ + cx]);
        }
    }
    if (lowest == kNoLevel)
    {
        return;
    }
    if (lowest < level)
    {
        const Level& below = level_[level - 1];
        for (u32 dz = 0; dz < 2; ++dz)
        {
            for (u32 dx = 0; dx < 2; ++dx)
            {
                if (2 * x + dx < below.nodes_x && 2 * z + dz < below.nodes_z)
                {
                    EmitBalanced(level - 1, 2 * x + dx, 2 * z + dz, grid, out);
                }
            }
        }
        return;
    }
    TerrainTile tile;
    tile.level = level;
    tile.x = x;
    tile.z = z;
    out->Push(tile);
}

u32 TerrainTiles::KeepNearest(const Vec3& camera, u32 room, Vector<TerrainTile>* list) const
{
    if (list->Size() <= room)
    {
        return 0;
    }
    struct Keyed
    {
        f32 distance;
        u32 index;
    };
    Vector<Keyed> keyed;
    keyed.Reserve(list->Size());
    for (u32 i = 0; i < list->Size(); ++i)
    {
        const TerrainTile& t = (*list)[i];
        keyed.Push(Keyed{TtBoxDistance(NodeBounds(t.level, t.x, t.z), camera), i});
    }
    Sort(Span<Keyed>(keyed.Data(), keyed.Size()), [](const Keyed& a, const Keyed& b)
         { return a.distance != b.distance ? a.distance < b.distance : a.index < b.index; });
    Vector<u8> keep;
    keep.Resize(list->Size());
    for (usize i = 0; i < keep.Size(); ++i)
    {
        keep[i] = 0;
    }
    for (u32 i = 0; i < room; ++i)
    {
        keep[keyed[i].index] = 1;
    }
    Vector<TerrainTile> kept;
    kept.Reserve(room);
    for (u32 i = 0; i < list->Size(); ++i)
    {
        if (keep[i] != 0)
        {
            kept.Push((*list)[i]);
        }
    }
    const u32 removed = static_cast<u32>(list->Size()) - room;
    list->Swap(kept);
    return removed;
}

void TerrainTiles::Select(const Vec3& camera, const Frustum* frustum, const TerrainSelectSettings& settings,
                          Vector<TerrainTile>* out) const
{
    ENGINE_ASSERT(Valid(), "TerrainTiles::Select: the quadtree is empty");
    ENGINE_ASSERT(TerrainSelectSettingsValid(settings, cell_size_), "TerrainTiles::Select: the settings are not valid");
    out->Clear();
    SelectNode(levels_ - 1, 0, 0, camera, frustum, settings, out);
    if (out->Size() > 1)
    {
        Balance(out);
    }
    Sort(Span<TerrainTile>(out->Data(), out->Size()),
         [](const TerrainTile& a, const TerrainTile& b)
         {
             if (a.level != b.level)
             {
                 return a.level < b.level;
             }
             if (a.z != b.z)
             {
                 return a.z < b.z;
             }
             return a.x < b.x;
         });
}

}  // namespace vse::renderer
