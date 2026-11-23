#include "spatial/spatial_debug.h"

#include "settings/desc.h"
#include "settings/setting.h"

namespace vse::spatial
{

SpatialStats DescribeSet(const SpatialSet& set)
{
    SpatialStats s;
    s.set = set.Stats();
    s.static_nodes = set.StaticTree().NodeCount();
    s.static_depth = set.StaticTree().Depth();
    if (set.Kind() == DynamicKind::Bvh)
    {
        const DynamicBvhStats d = set.DynamicTree().Stats();
        s.dynamic_nodes = d.nodes;
        s.dynamic_height = d.height;
        s.dynamic_sah_cost = d.sah_cost;
        s.dynamic_built_sah_cost = d.built_sah_cost;
        s.moves = d.moves;
        s.reinserts = d.reinserts;
        s.full_rebuilds = d.full_rebuilds;
        s.memory_bytes = d.memory_bytes;
    }
    return s;
}

void AppendBoxLines(const Aabbd& box, u32 color, Vector<DebugLine>* out)
{
    const Vec3d lo = box.min;
    const Vec3d hi = box.max;
    const Vec3d c[8] = {Vec3d(lo.x, lo.y, lo.z), Vec3d(hi.x, lo.y, lo.z), Vec3d(hi.x, hi.y, lo.z),
                        Vec3d(lo.x, hi.y, lo.z), Vec3d(lo.x, lo.y, hi.z), Vec3d(hi.x, lo.y, hi.z),
                        Vec3d(hi.x, hi.y, hi.z), Vec3d(lo.x, hi.y, hi.z)};
    const u32 edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                              {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& e : edges)
    {
        DebugLine line;
        line.a = c[e[0]];
        line.b = c[e[1]];
        line.color = color;
        out->Push(line);
    }
}

usize DebugLines(const SpatialSet& set, const DebugOptions& options, Vector<DebugLine>* out)
{
    out->Clear();
    auto room = [&]() { return out->Size() + 12 <= options.max_lines; };
    if (options.has_query && room())
    {
        AppendBoxLines(options.query, kDebugQueryColor, out);
    }
    for (usize i = 0; i < options.hits.Size() && room(); ++i)
    {
        AppendBoxLines(options.hits[i], kDebugHitColor, out);
    }
    // The query and its hits come first: they are what the caller most wants to see when the structure fills the cap.
    if (options.nodes)
    {
        set.Traverse(
            [&](const Aabbd& node)
            {
                if (!room())
                {
                    return NodeVisit::Reject;
                }
                AppendBoxLines(node, kDebugNodeColor, out);
                return NodeVisit::Descend;
            },
            [&](SetHandle, const Item& item)
            {
                if (options.items && room())
                {
                    AppendBoxLines(item.box, kDebugItemColor, out);
                }
                return room();
            });
    }
    else if (options.items)
    {
        set.Traverse([](const Aabbd&) { return NodeVisit::Descend; },
                     [&](SetHandle, const Item& item)
                     {
                         if (room())
                         {
                             AppendBoxLines(item.box, kDebugItemColor, out);
                         }
                         return room();
                     });
    }
    return out->Size();
}

void RegisterSpatialSettings(settings::Registry* registry)
{
    settings::SettingDesc parallel = settings::MakeBool("spatial.parallel", true);
    parallel.env = "ENGINE_SPATIAL_PARALLEL";
    parallel.flag = "spatial-parallel";
    parallel.label = "Parallel spatial sync";
    parallel.description = "compute the entities' world boxes as jobs (the sets are identical either way)";
    parallel.group = "spatial";
    registry->Register(parallel);
    settings::SettingDesc margin = settings::MakeFloat("spatial.fat_margin", 0.25, 0.0, 2.0);
    margin.env = "ENGINE_SPATIAL_FAT_MARGIN";
    margin.flag = "spatial-fat-margin";
    margin.label = "Dynamic tree margin";
    margin.description =
        "how far the dynamic trees grow an item's box, as a fraction of its size, before a move re-inserts it (applies to sets made afterwards)";
    margin.group = "spatial";
    margin.flags = settings::kAdvanced;
    registry->Register(margin);
    settings::SettingDesc stats = settings::MakeBool("spatial.stats", false);
    stats.env = "ENGINE_SPATIAL_STATS";
    stats.flag = "spatial-stats";
    stats.label = "Spatial statistics";
    stats.description = "time the phases of the spatial sync";
    stats.group = "spatial";
    stats.flags = settings::kAdvanced;
    registry->Register(stats);
    settings::SettingDesc debug = settings::MakeBool("spatial.debug_draw", false);
    debug.env = "ENGINE_SPATIAL_DEBUG_DRAW";
    debug.flag = "spatial-debug-draw";
    debug.label = "Draw spatial structures";
    debug.description = "programs that can draw the sets' boxes as lines do";
    debug.group = "spatial";
    debug.flags = settings::kAdvanced;
    registry->Register(debug);
}

SpatialSetConfig SetConfigFromSettings(const settings::Registry& registry)
{
    SpatialSetConfig config;
    const settings::SettingId id = registry.Find(StringView("spatial.fat_margin"));
    if (id.IsValid())
    {
        config.bvh.fat_margin = static_cast<f64>(settings::Setting<f64>(registry, id).Get());
    }
    return config;
}

}  // namespace vse::spatial
