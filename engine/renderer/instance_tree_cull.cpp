#include "renderer/instance_tree_cull.h"

#include "algorithm.h"
#include "jobs/parallel_for.h"
#include "lang/scalar.h"
#include "renderer/renderer_log.h"

namespace vse::renderer
{

namespace
{

constexpr u32 kTreeCullGroup = 16;  // roots a job takes
constexpr u8 kLevelMask = 0x07;
constexpr u8 kCulledBit = 0x08;
constexpr u8 kStandinBit = 0x10;

struct TreeDecision
{
    bool standin = false;
    bool culled = false;
    u32 level = 0;
};

// The level, the stand-in and the cull decision for a distance; `*state` (or null) holds the previous decision.
TreeDecision TreeDecide(const TreeLod& lod, f32 d, u8* state)
{
    TreeDecision r;
    const f32 h = state != nullptr ? lod.hysteresis : 0.0f;
    const u8 prev = state != nullptr ? *state : static_cast<u8>(0);
    if (lod.standin_mesh != kTreeNoMesh)
    {
        const bool was = (prev & kStandinBit) != 0;
        r.standin = was ? d > lod.standin_distance - h : d > lod.standin_distance + h;
    }
    if (lod.cull_distance > 0.0f)
    {
        const bool was = (prev & kCulledBit) != 0;
        r.culled = was ? d > lod.cull_distance - h : d > lod.cull_distance + h;
    }
    u32 level = state != nullptr ? static_cast<u32>(prev & kLevelMask) : 0u;
    if (level >= lod.level_count)
    {
        level = lod.level_count - 1;
    }
    while (level + 1 < lod.level_count && d > lod.switch_distance[level] + h)
    {
        ++level;
    }
    while (level > 0 && d <= lod.switch_distance[level - 1] - h)
    {
        --level;
    }
    r.level = level;
    if (state != nullptr)
    {
        *state = static_cast<u8>(level | (r.culled ? kCulledBit : 0) | (r.standin ? kStandinBit : 0));
    }
    return r;
}

struct TreeCullContext
{
    const TreeCullParams* params;
    u8* levels;  // by node id, or null
};

struct TreeGroupOut
{
    Vector<TreeVisibleNode> nodes;
    TreeCullStats stats;
};

void EmitNode(const InstanceTreeNode& node, u32 index, u32 mesh, u32 level, TreeGroupOut* out)
{
    TreeVisibleNode v;
    v.node = index;
    v.mesh = mesh;
    v.material_slot = node.material_slot;
    v.level = level;
    out->nodes.Push(v);
}

void CullRoot(const InstanceTree& tree, const TreeCullContext& ctx, u32 root, TreeGroupOut* out)
{
    const TreeCullParams& p = *ctx.params;
    const InstanceTreeNode& root_node = tree.Node(root);
    const Vec3 rel_root = (root_node.origin - p.render_origin).Narrow();
    const u32 end = root + root_node.subtree_size;
    u32 i = root;
    while (i < end)
    {
        const InstanceTreeNode& node = tree.Node(i);
        ++out->stats.nodes_looked_at;
        const Aabb& sb = node.subtree_bounds;
        if (sb.IsEmpty())
        {
            // A subtree that draws nothing.
            ++out->stats.subtrees_skipped;
            i += node.subtree_size;
            continue;
        }
        // A margin larger than the rounding of the plane tests (see CullInstances).
        const Vec3 lo = sb.min + rel_root;
        const Vec3 hi = sb.max + rel_root;
        const Vec3 centre = (lo + hi) * 0.5f;
        const Vec3 half = (hi - lo) * 0.5f;
        const f32 eps = 1.0e-4f * (1.0f + Abs(centre.x) + Abs(centre.y) + Abs(centre.z) + half.x + half.y + half.z);
        Aabb grown;
        grown.min = lo - Vec3(eps, eps, eps);
        grown.max = hi + Vec3(eps, eps, eps);
        const Containment where = p.frustum.TestAabb(grown);
        if (where == Containment::Outside)
        {
            ++out->stats.subtrees_skipped;
            i += node.subtree_size;
            continue;
        }
        if (where == Containment::Inside && !node.lod_in_subtree)
        {
            ++out->stats.subtrees_accepted;
            const u32 stop = i + node.subtree_size;
            for (; i < stop; ++i)
            {
                const InstanceTreeNode& n = tree.Node(i);
                ++out->stats.nodes_looked_at;
                if (n.mesh != kTreeNoMesh)
                {
                    EmitNode(n, i, n.mesh, 0, out);
                }
            }
            --out->stats.nodes_looked_at;  // the first entry was counted above
            continue;
        }
        u32 mesh = node.mesh;
        u32 level = 0;
        if (node.lod != kTreeNone)
        {
            const TreeLod& lod = tree.Lod(node.lod);
            const Vec3 at = rel_root + node.world.position;
            const f32 d = (at - p.camera_position).Length();
            const TreeDecision decision = TreeDecide(lod, d, ctx.levels != nullptr ? &ctx.levels[node.id] : nullptr);
            if (decision.standin)
            {
                EmitNode(node, i, lod.standin_mesh, kTreeStandinLevel, out);
                ++out->stats.standins;
                ++out->stats.subtrees_skipped;
                i += node.subtree_size;
                continue;
            }
            if (decision.culled)
            {
                if (lod.cull_children_beyond)
                {
                    ++out->stats.subtrees_skipped;
                    i += node.subtree_size;
                    continue;
                }
                mesh = kTreeNoMesh;
            }
            else
            {
                level = decision.level;
                mesh = level == 0 ? node.mesh : lod.mesh[level];
            }
        }
        if (mesh != kTreeNoMesh && !node.bounds.IsEmpty())
        {
            const Vec3 olo = node.bounds.min + rel_root;
            const Vec3 ohi = node.bounds.max + rel_root;
            const Vec3 ocentre = (olo + ohi) * 0.5f;
            const Vec3 ohalf = (ohi - olo) * 0.5f;
            const f32 oeps = 1.0e-4f *
                             (1.0f + Abs(ocentre.x) + Abs(ocentre.y) + Abs(ocentre.z) + ohalf.x + ohalf.y + ohalf.z);
            Aabb own;
            own.min = olo - Vec3(oeps, oeps, oeps);
            own.max = ohi + Vec3(oeps, oeps, oeps);
            if (p.frustum.TestAabb(own) != Containment::Outside)
            {
                EmitNode(node, i, mesh, level, out);
            }
        }
        ++i;
    }
}

void CullGroup(const InstanceTree& tree, const TreeCullContext& ctx, TreeGroupOut* groups, u32 group)
{
    const u32 begin = group * kTreeCullGroup;
    const u32 end = Min(begin + kTreeCullGroup, tree.RootCount());
    for (u32 r = begin; r < end; ++r)
    {
        CullRoot(tree, ctx, tree.RootIndex(r), &groups[group]);
    }
}

}  // namespace

u32 TreeLevelForDistance(const TreeLod& lod, f32 distance)
{
    if (lod.cull_distance > 0.0f && distance > lod.cull_distance)
    {
        return lod.level_count;
    }
    const TreeDecision d = TreeDecide(lod, distance, nullptr);
    return d.level;
}

void CullInstanceTree(const InstanceTree& tree, const TreeCullParams& params, TreeLodState* state,
                      jobs::Scheduler* scheduler, TreeCullResult* out)
{
    out->nodes.Clear();
    out->runs.Clear();
    out->stats = TreeCullStats();
    const u32 roots = tree.RootCount();
    if (roots == 0)
    {
        return;
    }
    u8* levels = nullptr;
    if (state != nullptr)
    {
        if (!state->IsFor(tree.Identity()))
        {
            RenderLog(LogVerbosity::Warn, "CullInstanceTree: the level state belongs to another tree; it was reset");
            state->Reset();
        }
        state->tree_ = tree.Identity();
        const usize n = tree.NodeCount();
        const usize old = state->levels_.Size();
        if (old != n)
        {
            state->levels_.Resize(n);
            for (usize k = old; k < n; ++k)
            {
                state->levels_[k] = 0;
            }
        }
        levels = state->levels_.Data();
    }
    const TreeCullContext ctx{&params, levels};
    const u32 groups = (roots + kTreeCullGroup - 1) / kTreeCullGroup;
    Vector<TreeGroupOut> parts;
    parts.Resize(groups);
    if (scheduler != nullptr && groups > 1)
    {
        auto body = [&tree, &ctx, &parts](usize begin, usize end)
        {
            for (usize g = begin; g < end; ++g)
            {
                CullGroup(tree, ctx, parts.Data(), static_cast<u32>(g));
            }
        };
        jobs::ParallelForBlocking(*scheduler, groups, 1, body);
    }
    else
    {
        for (u32 g = 0; g < groups; ++g)
        {
            CullGroup(tree, ctx, parts.Data(), g);
        }
    }
    Vector<TreeVisibleNode> joined;
    for (u32 g = 0; g < groups; ++g)
    {
        joined.Append(parts[g].nodes.Data(), parts[g].nodes.Size());
        out->stats.nodes_looked_at += parts[g].stats.nodes_looked_at;
        out->stats.subtrees_skipped += parts[g].stats.subtrees_skipped;
        out->stats.subtrees_accepted += parts[g].stats.subtrees_accepted;
        out->stats.standins += parts[g].stats.standins;
    }
    // Group by (mesh, level, slot): a stable counting sort by slot, then one by (mesh, level).
    const usize count = joined.Size();
    Vector<TreeVisibleNode> by_slot;
    by_slot.Resize(count);
    {
        Vector<u32> start;
        start.Resize(kMaxTreeMaterialSlots + 1);
        for (usize k = 0; k < start.Size(); ++k)
        {
            start[k] = 0;
        }
        for (usize k = 0; k < count; ++k)
        {
            ++start[joined[k].material_slot + 1];
        }
        for (usize k = 1; k < start.Size(); ++k)
        {
            start[k] += start[k - 1];
        }
        for (usize k = 0; k < count; ++k)
        {
            by_slot[start[joined[k].material_slot]++] = joined[k];
        }
    }
    out->nodes.Resize(count);
    {
        const u32 buckets = kMaxTreeMeshes * (kTreeStandinLevel + 1);
        Vector<u32> start;
        start.Resize(buckets + 1);
        for (usize k = 0; k < start.Size(); ++k)
        {
            start[k] = 0;
        }
        auto bucket = [](const TreeVisibleNode& v) { return v.mesh * (kTreeStandinLevel + 1) + v.level; };
        for (usize k = 0; k < count; ++k)
        {
            ++start[bucket(by_slot[k]) + 1];
        }
        for (usize k = 1; k < start.Size(); ++k)
        {
            start[k] += start[k - 1];
        }
        for (usize k = 0; k < count; ++k)
        {
            out->nodes[start[bucket(by_slot[k])]++] = by_slot[k];
        }
    }
    for (u32 k = 0; k < count;)
    {
        const TreeVisibleNode& first = out->nodes[k];
        u32 run_end = k + 1;
        while (run_end < count && out->nodes[run_end].mesh == first.mesh && out->nodes[run_end].level == first.level &&
               out->nodes[run_end].material_slot == first.material_slot)
        {
            ++run_end;
        }
        TreeDrawRun run;
        run.mesh = first.mesh;
        run.material_slot = first.material_slot;
        run.level = first.level;
        run.first = k;
        run.count = run_end - k;
        out->runs.Push(run);
        k = run_end;
    }
    out->stats.nodes_emitted = static_cast<u32>(count);
    out->stats.runs = static_cast<u32>(out->runs.Size());
}

void CompactTreeNodes(const InstanceTree& tree, Span<const TreeVisibleNode> nodes, const Vec3d& render_origin,
                      Vector<InstanceRecord>* records, Vector<InstanceExtra>* extras)
{
    records->Clear();
    if (extras != nullptr)
    {
        extras->Clear();
    }
    for (usize k = 0; k < nodes.Size(); ++k)
    {
        if (nodes[k].node >= tree.NodeCount())
        {
            continue;
        }
        const InstanceTreeNode& node = tree.Node(nodes[k].node);
        const InstanceTreeNode& root = tree.Node(tree.RootOf(nodes[k].node));
        InstanceRecord r;
        r.position = (root.origin - render_origin + Vec3d(node.world.position)).Narrow();
        r.rotation = node.world.rotation;
        r.scale = node.world.scale;
        records->Push(r);
        if (extras != nullptr)
        {
            extras->Push(node.extra);
        }
    }
}

}  // namespace vse::renderer
