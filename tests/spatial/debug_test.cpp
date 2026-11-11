// Phase 18 Day 248: the statistics, the debug lines and the settings of the spatial module.
#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "settings/registry.h"
#include "settings/setting.h"
#include "spatial/spatial_debug.h"
#include "spatial/spatial_world.h"

using namespace engine_placeholder;

namespace
{
Aabb UnitBoxAt(f32 x)
{
    return Aabb{Vec3(x, 0, 0), Vec3(x + 1, 1, 1)};
}
}  // namespace

TEST_CASE("Spatial debug: DescribeSet counts the items and the tree", "[spatial][debug]")
{
    spatial::SpatialSet set;
    for (u32 i = 0; i < 100; ++i)
    {
        set.AddDynamic(UnitBoxAt(static_cast<f32>(i) * 3.0f), i, 0);
    }
    for (u32 i = 0; i < 20; ++i)
    {
        set.AddStatic(UnitBoxAt(static_cast<f32>(i) * 3.0f + 1000.0f), 1000 + i, 0);
    }
    set.RebuildStatic();
    const spatial::SpatialStats stats = spatial::DescribeSet(set);
    CHECK(stats.set.dynamic_items == 100);
    CHECK(stats.set.static_items == 20);
    CHECK(stats.static_nodes > 0);
    CHECK(stats.dynamic_nodes >= 100);
    CHECK(stats.dynamic_height > 0);
    CHECK(stats.memory_bytes > 0);
}

TEST_CASE("Spatial debug: a box has 12 edges and DebugLines draws every item and stops at the cap", "[spatial][debug]")
{
    Vector<spatial::DebugLine> lines;
    spatial::AppendBoxLines(UnitBoxAt(0), spatial::kDebugItemColor, &lines);
    CHECK(lines.Size() == 12);

    spatial::SpatialSet set;
    for (u32 i = 0; i < 50; ++i)
    {
        set.AddDynamic(UnitBoxAt(static_cast<f32>(i) * 3.0f), i, 0);
    }
    spatial::DebugOptions items_only;
    items_only.nodes = false;
    CHECK(spatial::DebugLines(set, items_only, &lines) == 50 * 12);

    spatial::DebugOptions all;
    CHECK(spatial::DebugLines(set, all, &lines) > 50 * 12);  // the node boxes too

    spatial::DebugOptions capped;
    capped.max_lines = 100;
    CHECK(spatial::DebugLines(set, capped, &lines) <= 100);

    spatial::DebugOptions query;
    query.nodes = false;
    query.items = false;
    query.has_query = true;
    query.query = UnitBoxAt(5);
    const Aabb hits[2] = {UnitBoxAt(1), UnitBoxAt(2)};
    query.hits = Span<const Aabb>(hits, 2);
    CHECK(spatial::DebugLines(set, query, &lines) == 3 * 12);

    spatial::DebugOptions crowded;  // the query and its hits survive a cap the structure would fill
    crowded.max_lines = 36;
    crowded.query = query.query;
    crowded.has_query = true;
    crowded.hits = query.hits;
    spatial::DebugLines(set, crowded, &lines);
    CHECK(lines.Size() == 36);
    CHECK(lines[0].color == spatial::kDebugQueryColor);
}

TEST_CASE("Spatial debug: the settings are declared, drive the world's parallel flag and the tree margin",
          "[spatial][debug][settings]")
{
    settings::Registry registry;
    spatial::RegisterSpatialSettings(&registry);
    CHECK(registry.Find(StringView("spatial.parallel")).IsValid());
    CHECK(registry.Find(StringView("spatial.stats")).IsValid());
    CHECK(registry.Find(StringView("spatial.debug_draw")).IsValid());

    CHECK(spatial::SetConfigFromSettings(registry).bvh.fat_margin == 0.25f);
    registry.Publish(registry.Find(StringView("spatial.fat_margin")), settings::Value::FromFloat(0.5));
    CHECK(spatial::SetConfigFromSettings(registry).bvh.fat_margin == 0.5f);

    spatial::SpatialWorld world;
    CHECK(world.Parallel());
    world.UseSettings(registry);
    CHECK(world.Parallel());
    registry.Publish(registry.Find(StringView("spatial.parallel")), settings::Value::FromBool(false));
    CHECK_FALSE(world.Parallel());

    settings::Registry empty;  // no settings declared: the default configuration
    CHECK(spatial::SetConfigFromSettings(empty).bvh.fat_margin == 0.25f);
}
