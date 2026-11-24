// The terrain tile quadtree and the choice of tiles (Phase 28 Day 487): counts at known distances, coverage exactly
// once, the one-level rule between neighbours, culling, determinism across threads and the cost of a selection.

#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "cstr/format.h"
#include "lang/scalar.h"
#include "log/output.h"
#include "os/os.h"
#include "renderer/camera.h"
#include "renderer/terrain_tiles.h"
#include "sync/thread.h"

namespace vse::renderer
{

namespace
{

// A map of `samples` by `samples` whose blocks all span the sample values [lo, hi].
TerrainTilesDesc TtDesc(u32 samples, f32 cell, u16 lo, u16 hi)
{
    TerrainTilesDesc d;
    d.samples_x = samples;
    d.samples_z = samples;
    d.cell_size = cell;
    d.range_min = 0.0f;
    d.range_max = 65535.0f;
    d.blocks_x = (samples - 1 + 7) / 8;
    d.blocks_z = d.blocks_x;
    d.block_min.Resize(static_cast<usize>(d.blocks_x) * d.blocks_z);
    d.block_max.Resize(d.block_min.Size());
    for (usize i = 0; i < d.block_min.Size(); ++i)
    {
        d.block_min[i] = lo;
        d.block_max[i] = hi;
    }
    return d;
}

TerrainSelectSettings TtSettings(f32 first)
{
    TerrainSelectSettings s;
    s.first_range = first;
    return s;
}

// The level of the tile covering each cell, 0xFF where none; counts the cells covered more than once in *overlaps.
Vector<u8> TtCover(const TerrainTiles& t, const Vector<TerrainTile>& tiles, u32 cells, u32* overlaps)
{
    Vector<u8> cover;
    cover.Resize(static_cast<usize>(cells) * cells);
    for (usize i = 0; i < cover.Size(); ++i)
    {
        cover[i] = 0xFF;
    }
    *overlaps = 0;
    for (const TerrainTile& tile : tiles)
    {
        u32 r[4];
        t.NodeCells(tile.level, tile.x, tile.z, r);
        for (u32 z = r[1]; z < r[3]; ++z)
        {
            for (u32 x = r[0]; x < r[2]; ++x)
            {
                u8& c = cover[static_cast<usize>(z) * cells + x];
                *overlaps += c != 0xFF ? 1 : 0;
                c = static_cast<u8>(tile.level);
            }
        }
    }
    return cover;
}

}  // namespace

TEST_CASE("terrain tiles: a bad description is refused", "[renderer][terrain]")
{
    TerrainTiles t;
    TerrainTilesDesc d = TtDesc(65, 1.0f, 0, 100);
    d.samples_x = 1;
    CHECK(!t.Build(d));
    CHECK(!t.Valid());
    d = TtDesc(65, 1.0f, 0, 100);
    d.cell_size = 0.0f;
    CHECK(!t.Build(d));
    d = TtDesc(65, 1.0f, 0, 100);
    d.range_min = 10.0f;
    d.range_max = 5.0f;
    CHECK(!t.Build(d));
    d = TtDesc(65, 1.0f, 0, 100);
    d.blocks_x = 3;
    CHECK(!t.Build(d));
    d = TtDesc(65, 1.0f, 0, 100);
    d.block_max.Resize(3);
    CHECK(!t.Build(d));
    CHECK(t.Build(TtDesc(65, 1.0f, 0, 100)));
}

TEST_CASE("terrain tiles: the quadtree of a map", "[renderer][terrain]")
{
    TerrainTiles t;
    REQUIRE(t.Build(TtDesc(1025, 2.0f, 0, 65535)));
    CHECK(t.TilesX() == 32);
    CHECK(t.TilesZ() == 32);
    CHECK(t.LevelCount() == 6);
    CHECK(t.NodesX(5) == 1);
    CHECK(t.NodesX(1) == 16);
    // 100 samples: 99 cells, 4 tiles, 3 levels; the last tile is clipped to the map
    REQUIRE(t.Build(TtDesc(100, 1.0f, 0, 10)));
    CHECK(t.TilesX() == 4);
    CHECK(t.LevelCount() == 3);
    u32 cells[4];
    t.NodeCells(0, 3, 3, cells);
    CHECK(cells[0] == 96);
    CHECK(cells[2] == 99);
    const Aabb box = t.NodeBounds(0, 3, 3);
    CHECK(box.max.x == 99.0f);
    CHECK(box.max.y == 10.0f);
    // the root holds everything
    const Aabb root = t.NodeBounds(2, 0, 0);
    CHECK(root.min.x == 0.0f);
    CHECK(root.max.x == 99.0f);
    // a single tile
    REQUIRE(t.Build(TtDesc(33, 1.0f, 0, 10)));
    CHECK(t.LevelCount() == 1);
}

TEST_CASE("terrain tiles: the boxes are tight in height", "[renderer][terrain]")
{
    TerrainTilesDesc d = TtDesc(65, 1.0f, 100, 200);  // 2 by 2 tiles; make one block of the first tile tall
    d.block_max[0] = 40000;
    TerrainTiles t;
    REQUIRE(t.Build(d));
    CHECK(t.NodeBounds(0, 0, 0).max.y == 40000.0f);
    CHECK(t.NodeBounds(0, 1, 0).max.y == 200.0f);
    CHECK(t.NodeBounds(0, 1, 1).min.y == 100.0f);
    CHECK(t.NodeBounds(1, 0, 0).max.y == 40000.0f);
}

TEST_CASE("terrain tiles: the counts at known distances on a flat map", "[renderer][terrain]")
{
    TerrainTiles t;
    REQUIRE(t.Build(TtDesc(1025, 1.0f, 0, 0)));  // 1024 m square, flat at y = 0, 6 levels
    const TerrainSelectSettings s = TtSettings(100.0f);
    REQUIRE(TerrainSelectSettingsValid(s, 1.0f));
    Vector<TerrainTile> tiles;
    // far away: the root alone
    t.Select(Vec3(512.0f, 20000.0f, 512.0f), nullptr, s, &tiles);
    REQUIRE(tiles.Size() == 1);
    CHECK(tiles[0].level == 5);
    // beyond max_distance: nothing
    TerrainSelectSettings limited = s;
    limited.max_distance = 1000.0f;
    t.Select(Vec3(512.0f, 20000.0f, 512.0f), nullptr, limited, &tiles);
    CHECK(tiles.Size() == 0);
    // standing on the map: the finest tiles near the camera, coarser farther, the list sorted
    t.Select(Vec3(512.0f, 5.0f, 512.0f), nullptr, s, &tiles);
    CHECK(tiles.Size() > 10);
    u32 finest = 0;
    for (usize i = 0; i < tiles.Size(); ++i)
    {
        finest += tiles[i].level == 0 ? 1 : 0;
        if (i > 0)
        {
            const bool ordered = tiles[i - 1].level < tiles[i].level ||
                                 (tiles[i - 1].level == tiles[i].level &&
                                  (tiles[i - 1].z < tiles[i].z ||
                                   (tiles[i - 1].z == tiles[i].z && tiles[i - 1].x < tiles[i].x)));
            CHECK(ordered);
        }
    }
    CHECK(finest > 0);
    // the finest tiles are all within the first range of the camera, give or take their parent's size
    for (const TerrainTile& tile : tiles)
    {
        if (tile.level == 0)
        {
            const Aabb b = t.NodeBounds(0, tile.x, tile.z);
            const f32 dx = FMax(FMax(b.min.x - 512.0f, 512.0f - b.max.x), 0.0f);
            const f32 dz = FMax(FMax(b.min.z - 512.0f, 512.0f - b.max.z), 0.0f);
            CHECK(Sqrt(dx * dx + 25.0f + dz * dz) < 100.0f + 64.0f * 1.5f);  // its parent was within the range
        }
    }
}

TEST_CASE("terrain tiles: every cell is covered exactly once, neighbours differ by one level at most",
          "[renderer][terrain]")
{
    TerrainTiles t;
    REQUIRE(t.Build(TtDesc(1025, 1.0f, 0, 3000)));
    Vector<TerrainTile> tiles;
    u32 seed = 12345;
    auto rnd = [&seed]() -> f32
    {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<f32>(seed >> 8) / static_cast<f32>(1u << 24);
    };
    for (u32 trial = 0; trial < 90; ++trial)
    {
        // the smallest range the rule allows, a usual one and a large one
        const TerrainSelectSettings s = TtSettings(trial % 3 == 0 ? 96.0f : (trial % 3 == 1 ? 100.0f : 300.0f));
        const Vec3 camera(rnd() * 1400.0f - 200.0f, rnd() * 300.0f, rnd() * 1400.0f - 200.0f);
        t.Select(camera, nullptr, s, &tiles);
        u32 overlaps = 0;
        const Vector<u8> cover = TtCover(t, tiles, 1024, &overlaps);
        CHECK(overlaps == 0);
        u32 holes = 0;
        for (u8 c : cover)
        {
            holes += c == 0xFF ? 1 : 0;
        }
        CHECK(holes == 0);
        u32 jumps = 0;
        for (u32 z = 0; z < 1023; ++z)
        {
            for (u32 x = 0; x < 1023; ++x)
            {
                const u32 a = cover[static_cast<usize>(z) * 1024 + x];
                const u32 r = cover[static_cast<usize>(z) * 1024 + x + 1];
                const u32 d = cover[static_cast<usize>(z + 1) * 1024 + x];
                jumps += (a > r ? a - r : r - a) > 1 ? 1 : 0;
                jumps += (a > d ? a - d : d - a) > 1 ? 1 : 0;
            }
        }
        INFO("trial " << trial);
        CHECK(jumps == 0);
    }
}

TEST_CASE("terrain tiles: neighbours differ by one level at most with tall boxes beside flat ones",
          "[renderer][terrain]")
{
    // Blocks whose heights jump from 0 to 40,000 m in a random pattern: a tile's box is far taller than it is wide, so
    // the distance to a box is not the distance to its neighbour's box.
    TerrainTilesDesc d = TtDesc(1025, 1.0f, 0, 0);
    for (u32 bz = 0; bz < d.blocks_z; ++bz)
    {
        for (u32 bx = 0; bx < d.blocks_x; ++bx)
        {
            d.block_max[static_cast<usize>(bz) * d.blocks_x + bx] = bx >= d.blocks_x / 2 ? 40000 : 0;  // a plateau
        }
    }
    u32 seed = 777;
    TerrainTiles t;
    REQUIRE(t.Build(d));
    Vector<TerrainTile> tiles;
    u32 jumps_total = 0;
    for (u32 trial = 0; trial < 60; ++trial)
    {
        seed = seed * 1664525u + 1013904223u;
        const Vec3 camera(static_cast<f32>((seed >> 8) % 400) + 250.0f, static_cast<f32>((seed >> 4) % 150) + 100.0f,
                          static_cast<f32>((seed >> 12) % 1000) + 12.0f);
        t.Select(camera, nullptr, TtSettings(96.0f), &tiles);
        u32 overlaps = 0;
        const Vector<u8> cover = TtCover(t, tiles, 1024, &overlaps);
        CHECK(overlaps == 0);
        for (u32 z = 0; z < 1023; ++z)
        {
            for (u32 x = 0; x < 1023; ++x)
            {
                const u32 a = cover[static_cast<usize>(z) * 1024 + x];
                const u32 r = cover[static_cast<usize>(z) * 1024 + x + 1];
                const u32 dn = cover[static_cast<usize>(z + 1) * 1024 + x];
                jumps_total += (a > r ? a - r : r - a) > 1 ? 1 : 0;
                jumps_total += (a > dn ? a - dn : dn - a) > 1 ? 1 : 0;
                CHECK(a != 0xFF);
            }
        }
    }
    CHECK(jumps_total == 0);
}

TEST_CASE("terrain tiles: neighbours differ by one level at most over a random pattern of tall and flat blocks",
          "[renderer][terrain]")
{
    // Balancing lowers cells of a tile and splits it into four: the tiles that come out are balanced again until
    // nothing changes, so no jump of two survives beside a split tile.
    u32 seed = 4242;
    u32 jumps_total = 0;
    for (u32 pattern = 0; pattern < 12; ++pattern)
    {
        TerrainTilesDesc d = TtDesc(1025, 1.0f, 0, 0);
        for (u32 i = 0; i < d.blocks_x * d.blocks_z; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            d.block_max[i] = ((seed >> 16) % 100) < 20 + pattern * 5 ? 40000 : 0;
        }
        TerrainTiles t;
        REQUIRE(t.Build(d));
        Vector<TerrainTile> tiles;
        for (u32 trial = 0; trial < 40; ++trial)
        {
            seed = seed * 1664525u + 1013904223u;
            const Vec3 camera(static_cast<f32>((seed >> 8) % 1024), static_cast<f32>((seed >> 4) % 150) + 5.0f,
                              static_cast<f32>((seed >> 14) % 1024));
            t.Select(camera, nullptr, TtSettings(96.0f), &tiles);
            u32 overlaps = 0;
            const Vector<u8> cover = TtCover(t, tiles, 1024, &overlaps);
            CHECK(overlaps == 0);
            for (u32 z = 0; z < 1023; ++z)
            {
                for (u32 x = 0; x < 1023; ++x)
                {
                    const u32 a = cover[static_cast<usize>(z) * 1024 + x];
                    const u32 r = cover[static_cast<usize>(z) * 1024 + x + 1];
                    const u32 dn = cover[static_cast<usize>(z + 1) * 1024 + x];
                    jumps_total += (a > r ? a - r : r - a) > 1 ? 1 : 0;
                    jumps_total += (a > dn ? a - dn : dn - a) > 1 ? 1 : 0;
                }
            }
        }
    }
    CHECK(jumps_total == 0);
}

TEST_CASE("terrain tiles: keeping the nearest tiles drops the farthest, in the list's order", "[renderer][terrain]")
{
    TerrainTiles t;
    REQUIRE(t.Build(TtDesc(1025, 1.0f, 0, 0)));
    Vector<TerrainTile> tiles;
    const Vec3 camera(10.0f, 5.0f, 10.0f);
    t.Select(camera, nullptr, TtSettings(96.0f), &tiles);
    const usize all = tiles.Size();
    REQUIRE(all > 20);
    Vector<TerrainTile> copy;
    copy.Append(tiles.Data(), tiles.Size());
    CHECK(t.KeepNearest(camera, static_cast<u32>(all), &copy) == 0);
    CHECK(copy.Size() == all);
    CHECK(t.KeepNearest(camera, 20, &copy) == all - 20);
    REQUIRE(copy.Size() == 20);
    // every kept tile is at least as near as every removed one, and the kept ones keep the list's order
    f32 farthest_kept = 0.0f;
    u32 previous = 0;
    for (const TerrainTile& k : copy)
    {
        const Aabb b = t.NodeBounds(k.level, k.x, k.z);
        const f32 dx = FMax(FMax(b.min.x - camera.x, camera.x - b.max.x), 0.0f);
        const f32 dz = FMax(FMax(b.min.z - camera.z, camera.z - b.max.z), 0.0f);
        farthest_kept = FMax(farthest_kept, dx * dx + dz * dz);
        u32 at = 0;
        while (!(tiles[at].level == k.level && tiles[at].x == k.x && tiles[at].z == k.z))
        {
            ++at;
        }
        CHECK(at >= previous);
        previous = at;
    }
    for (const TerrainTile& r : tiles)
    {
        bool kept = false;
        for (const TerrainTile& k : copy)
        {
            kept = kept || (k.level == r.level && k.x == r.x && k.z == r.z);
        }
        if (!kept)
        {
            const Aabb b = t.NodeBounds(r.level, r.x, r.z);
            const f32 dx = FMax(FMax(b.min.x - camera.x, camera.x - b.max.x), 0.0f);
            const f32 dz = FMax(FMax(b.min.z - camera.z, camera.z - b.max.z), 0.0f);
            CHECK(dx * dx + dz * dz >= farthest_kept);
        }
    }
}

TEST_CASE("terrain tiles: a box covers the heights its mesh can reach past its edge", "[renderer][terrain]")
{
    // One tall block just outside the tile at the origin: the coarse levels filter across the tile's edge.
    TerrainTilesDesc d = TtDesc(1025, 1.0f, 0, 0);
    d.block_max[0 * d.blocks_x + 4] = 30000;  // cells 32 to 40 of the first row: the tile 1 of level 0, not tile 0
    TerrainTiles t;
    REQUIRE(t.Build(d));
    CHECK(t.NodeBounds(0, 1, 0).max.y == 30000.0f);
    // tile 0 of level 0 morphs toward level 1, which reads two texels (four cells) past the edge: it sees the block
    CHECK(t.NodeBounds(0, 0, 0).max.y == 30000.0f);
    CHECK(t.NodeBounds(1, 0, 0).max.y == 30000.0f);
    // a tile farther away does not
    CHECK(t.NodeBounds(0, 0, 5).max.y == 0.0f);
}

namespace
{

// The morph factor the terrain vertex shader computes for a vertex at distance `d` in a tile of `level`.
f32 TtMorph(const TerrainSelectSettings& s, u32 level, f32 d)
{
    const f32 range = TerrainLevelRange(s, level);
    const f32 start = TerrainMorphStart(s, level);
    const f32 m = (d - start) / FMax(range - start, 1e-4f);
    return m < 0.0f ? 0.0f : (m > 1.0f ? 1.0f : m);
}

// The cracks along the edges between a tile and a coarser neighbour: a vertex of the finer tile that the coarser tile
// has no vertex at must have morphed all the way (it sits on the coarser edge), and the coarser tile's own vertices on
// that edge must not have started to morph (they stay where the finer tile puts its even vertices).
void TtSeamFaults(const TerrainTiles& t, const Vector<TerrainTile>& tiles, const TerrainSelectSettings& s,
                  const Vec3& camera, f32 cell, u32 cells, u32* unmorphed, u32* started)
{
    u32 overlaps = 0;
    const Vector<u8> cover = TtCover(t, tiles, cells, &overlaps);
    for (const TerrainTile& tile : tiles)
    {
        u32 r[4];
        t.NodeCells(tile.level, tile.x, tile.z, r);
        const f32 step = static_cast<f32>(r[2] - r[0]) / 32.0f;
        const f32 stepz = static_cast<f32>(r[3] - r[1]) / 32.0f;
        for (u32 side = 0; side < 4; ++side)
        {
            // the cell just outside the middle of this side
            const i64 ox = side == 0 ? static_cast<i64>(r[0]) - 1
                                     : (side == 1 ? static_cast<i64>(r[2]) : (r[0] + r[2]) / 2);
            const i64 oz = side == 2 ? static_cast<i64>(r[1]) - 1
                                     : (side == 3 ? static_cast<i64>(r[3]) : (r[1] + r[3]) / 2);
            if (ox < 0 || oz < 0 || ox >= cells || oz >= cells ||
                cover[static_cast<usize>(oz) * cells + static_cast<usize>(ox)] != tile.level + 1)
            {
                continue;
            }
            for (u32 i = 0; i <= 32; ++i)
            {
                const f32 px = side < 2 ? static_cast<f32>(side == 0 ? r[0] : r[2]) * cell
                                        : (static_cast<f32>(r[0]) + step * static_cast<f32>(i)) * cell;
                const f32 pz = side < 2 ? (static_cast<f32>(r[1]) + stepz * static_cast<f32>(i)) * cell
                                        : static_cast<f32>(side == 2 ? r[1] : r[3]) * cell;
                const Vec3 d3 = Vec3(px, 0.0f, pz) - camera;
                const f32 d = Sqrt(d3.x * d3.x + d3.y * d3.y + d3.z * d3.z);
                *unmorphed += (i % 2 == 1 && TtMorph(s, tile.level, d) < 1.0f) ? 1 : 0;
                *started += TtMorph(s, tile.level + 1, d) > 0.0f ? 1 : 0;
            }
        }
    }
}

}  // namespace

TEST_CASE("terrain tiles: the seams between levels are closed by the morph up to the fraction the bound allows",
          "[renderer][terrain]")
{
    TerrainTiles t;
    REQUIRE(t.Build(TtDesc(1025, 1.0f, 0, 0)));
    Vector<TerrainTile> tiles;
    u32 seed = 4242;
    u32 unmorphed = 0;
    u32 started = 0;
    for (u32 trial = 0; trial < 60; ++trial)
    {
        seed = seed * 1664525u + 1013904223u;
        TerrainSelectSettings s = TtSettings(96.0f + static_cast<f32>(trial % 4) * 40.0f);
        s.morph_fraction = FMin(0.1f + 0.2f * static_cast<f32>(trial % 5),
                                TerrainMaxMorphFraction(s.first_range, 1.0f));
        REQUIRE(TerrainSelectSettingsValid(s, 1.0f));
        const Vec3 camera(static_cast<f32>((seed >> 8) % 1400) - 200.0f, static_cast<f32>((seed >> 4) % 80),
                          static_cast<f32>((seed >> 12) % 1400) - 200.0f);
        t.Select(camera, nullptr, s, &tiles);
        TtSeamFaults(t, tiles, s, camera, 1.0f, 1024, &unmorphed, &started);
    }
    CHECK(unmorphed == 0);
    CHECK(started == 0);
    // the detector works: a morph past the bound starts the coarser tile's morph before the finer tile's far corner
    u32 faults = 0;
    for (u32 trial = 0; trial < 20; ++trial)
    {
        seed = seed * 1664525u + 1013904223u;
        TerrainSelectSettings s = TtSettings(216.0f);
        s.morph_fraction = 0.9f;
        const Vec3 camera(static_cast<f32>((seed >> 8) % 1400) - 200.0f, static_cast<f32>((seed >> 4) % 80),
                          static_cast<f32>((seed >> 12) % 1400) - 200.0f);
        t.Select(camera, nullptr, s, &tiles);
        TtSeamFaults(t, tiles, s, camera, 1.0f, 1024, &unmorphed, &faults);
    }
    CHECK(faults > 0);
    CHECK(TerrainMaxMorphFraction(96.0f, 1.0f) < 0.1f);
    CHECK(TerrainMaxMorphFraction(1000.0f, 1.0f) > 0.9f);
    CHECK(TerrainMaxMorphFraction(50.0f, 1.0f) == 0.0f);
}

TEST_CASE("terrain tiles: a camera inside the map, above it and far away", "[renderer][terrain]")
{
    TerrainTiles t;
    REQUIRE(t.Build(TtDesc(513, 1.0f, 0, 0)));
    const TerrainSelectSettings s = TtSettings(100.0f);
    Vector<TerrainTile> inside;
    Vector<TerrainTile> above;
    Vector<TerrainTile> far_away;
    t.Select(Vec3(256.0f, 2.0f, 256.0f), nullptr, s, &inside);
    t.Select(Vec3(256.0f, 800.0f, 256.0f), nullptr, s, &above);
    t.Select(Vec3(100000.0f, 0.0f, 100000.0f), nullptr, s, &far_away);
    CHECK(inside.Size() > above.Size());
    CHECK(above.Size() >= 1);
    CHECK(far_away.Size() == 1);
    u32 overlaps = 0;
    TtCover(t, above, 512, &overlaps);
    CHECK(overlaps == 0);
}

TEST_CASE("terrain tiles: the frustum drops tiles that cannot be seen", "[renderer][terrain]")
{
    TerrainTiles t;
    REQUIRE(t.Build(TtDesc(1025, 1.0f, 0, 0)));
    const TerrainSelectSettings s = TtSettings(100.0f);
    Camera camera = MakeLookAtCamera(Vec3(512.0f, 30.0f, 512.0f), Vec3(512.0f, 30.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    camera.far_plane = 2000.0f;
    const Frustum frustum = Frustum::FromViewProjection(camera.ViewProjection());
    Vector<TerrainTile> culled;
    Vector<TerrainTile> all;
    t.Select(camera.position, &frustum, s, &culled);
    t.Select(camera.position, nullptr, s, &all);
    CHECK(culled.Size() < all.Size());
    CHECK(culled.Size() > 0);
    for (const TerrainTile& tile : culled)
    {
        CHECK(frustum.TestAabb(t.NodeBounds(tile.level, tile.x, tile.z)) != Containment::Outside);
        // looking along -z from z = 512: nothing wholly behind the camera
        CHECK(t.NodeBounds(tile.level, tile.x, tile.z).min.z < 512.0f);
    }
}

TEST_CASE("terrain tiles: a moving camera changes the list by whole tiles", "[renderer][terrain]")
{
    TerrainTiles t;
    REQUIRE(t.Build(TtDesc(1025, 1.0f, 0, 0)));
    const TerrainSelectSettings s = TtSettings(100.0f);
    Vector<TerrainTile> a;
    Vector<TerrainTile> b;
    t.Select(Vec3(300.0f, 5.0f, 300.0f), nullptr, s, &a);
    t.Select(Vec3(300.0f, 5.0f, 300.0f), nullptr, s, &b);
    REQUIRE(a.Size() == b.Size());
    bool same = true;
    for (usize i = 0; i < a.Size(); ++i)
    {
        same = same && a[i].level == b[i].level && a[i].x == b[i].x && a[i].z == b[i].z;
    }
    CHECK(same);
    t.Select(Vec3(700.0f, 5.0f, 300.0f), nullptr, s, &b);
    bool differs = a.Size() != b.Size();
    for (usize i = 0; !differs && i < a.Size(); ++i)
    {
        differs = a[i].level != b[i].level || a[i].x != b[i].x || a[i].z != b[i].z;
    }
    CHECK(differs);
    u32 overlaps = 0;
    TtCover(t, b, 1024, &overlaps);
    CHECK(overlaps == 0);
}

TEST_CASE("terrain tiles: the same list on one thread and on eight", "[renderer][terrain]")
{
    TerrainTiles t;
    REQUIRE(t.Build(TtDesc(1025, 1.0f, 0, 5000)));
    const TerrainSelectSettings s = TtSettings(100.0f);
    Camera camera = MakeLookAtCamera(Vec3(400.0f, 40.0f, 400.0f), Vec3(900.0f, 0.0f, 100.0f), Vec3(0.0f, 1.0f, 0.0f));
    const Frustum frustum = Frustum::FromViewProjection(camera.ViewProjection());
    Vector<TerrainTile> reference;
    t.Select(camera.position, &frustum, s, &reference);
    constexpr u32 kThreads = 8;
    Vector<TerrainTile> results[kThreads];
    Thread threads[kThreads];
    for (u32 i = 0; i < kThreads; ++i)
    {
        threads[i] = Thread(
            [&t, &camera, &frustum, &s, &results, i]()
            {
                for (u32 round = 0; round < 20; ++round)
                {
                    t.Select(camera.position, &frustum, s, &results[i]);
                }
            });
    }
    for (u32 i = 0; i < kThreads; ++i)
    {
        threads[i].Join();
        REQUIRE(results[i].Size() == reference.Size());
        bool same = true;
        for (usize k = 0; k < reference.Size(); ++k)
        {
            same = same && results[i][k].level == reference[k].level && results[i][k].x == reference[k].x &&
                   results[i][k].z == reference[k].z;
        }
        CHECK(same);
    }
}

TEST_CASE("terrain tiles: the settings that keep one-level neighbours", "[renderer][terrain]")
{
    TerrainSelectSettings s;
    CHECK(TerrainSelectSettingsValid(s, 1.0f));
    s.first_range = 95.0f;  // under three tile widths of 32 m
    CHECK(!TerrainSelectSettingsValid(s, 1.0f));
    s = TerrainSelectSettings();
    s.morph_fraction = 1.0f;
    CHECK(!TerrainSelectSettingsValid(s, 1.0f));
    s = TerrainSelectSettings();
    s.max_distance = -1.0f;
    CHECK(!TerrainSelectSettingsValid(s, 1.0f));
    s = TerrainSelectSettings();
    CHECK(!TerrainSelectSettingsValid(s, 0.0f));
    CHECK(TerrainLevelRange(s, 3) == s.first_range * 8.0f);
    CHECK(TerrainMorphStart(s, 0) == s.first_range * 0.7f);
    CHECK(TerrainMorphStart(s, 2) == s.first_range * 4.0f - (s.first_range * 4.0f - s.first_range * 2.0f) * 0.3f);
}

TEST_CASE("terrain tiles: 1000 selections on a 4096 map (a bench line, not a limit)", "[renderer][terrain][bench]")
{
    TerrainTiles t;
    REQUIRE(t.Build(TtDesc(4097, 1.0f, 0, 2000)));
    const TerrainSelectSettings s = TtSettings(128.0f);
    Camera camera = MakeLookAtCamera(Vec3(2000.0f, 60.0f, 2000.0f), Vec3(2500.0f, 0.0f, 2200.0f),
                                     Vec3(0.0f, 1.0f, 0.0f));
    camera.far_plane = 3000.0f;
    Vector<TerrainTile> tiles;
    u64 total = 0;
    const u64 start = os::MonotonicTimeNanos();
    for (u32 i = 0; i < 1000; ++i)
    {
        camera.position = Vec3(2000.0f + static_cast<f32>(i), 60.0f, 2000.0f);
        const Frustum frustum = Frustum::FromViewProjection(camera.ViewProjection());
        t.Select(camera.position, &frustum, s, &tiles);
        total += tiles.Size();
    }
    const u64 nanos = os::MonotonicTimeNanos() - start;
    out::Print("terrain tiles: 1000 selections on 4097 x 4097 samples: %u us each, %u tiles on average\n",
               static_cast<unsigned>(nanos / 1000 / 1000), static_cast<unsigned>(total / 1000));
    CHECK(total > 0);
}

}  // namespace vse::renderer
