#include "renderer/instance_cull.h"

#include "algorithm.h"
#include "jobs/parallel_for.h"
#include "lang/scalar.h"

namespace vse::renderer
{

namespace
{

constexpr u32 kInstanceCullGroup = 32;  // chunks a job takes

struct InstanceCullGroupOut
{
    Vector<InstanceRun> runs;
    InstanceCullStats stats;
};

struct InstanceCullContext
{
    const InstanceSet* set;
    const InstanceCullParams* params;
    InstanceCullGroupOut* groups;
    u32 chunk_count;
};

f32 InstanceMaxScale(const Vec3& s)
{
    return Max(s.x, Max(s.y, s.z));
}

f32 BoxDistance(const Vec3& p, const Aabb& box, bool farthest)
{
    f32 sum = 0.0f;
    for (int a = 0; a < 3; ++a)
    {
        const f32 v = a == 0 ? p.x : (a == 1 ? p.y : p.z);
        const f32 lo = a == 0 ? box.min.x : (a == 1 ? box.min.y : box.min.z);
        const f32 hi = a == 0 ? box.max.x : (a == 1 ? box.max.y : box.max.z);
        f32 d = 0.0f;
        if (farthest)
        {
            d = Max(Abs(v - lo), Abs(v - hi));
        }
        else
        {
            d = v < lo ? lo - v : (v > hi ? v - hi : 0.0f);
        }
        sum += d * d;
    }
    return Sqrt(sum);
}

void CullGroup(const InstanceCullContext& ctx, u32 group)
{
    const InstanceSet& set = *ctx.set;
    const InstanceCullParams& p = *ctx.params;
    InstanceCullGroupOut& out = ctx.groups[group];
    const u32 begin = group * kInstanceCullGroup;
    const u32 end = Min(begin + kInstanceCullGroup, ctx.chunk_count);
    const bool by_distance = p.max_distance > 0.0f;
    const bool by_size = p.min_size_ratio > 0.0f;
    for (u32 c = begin; c < end; ++c)
    {
        const InstanceChunkInfo info = set.Chunk(c);
        if (info.count == 0)
        {
            continue;
        }
        ++out.stats.chunks_tested;
        const Vec3 rel = (info.origin - p.render_origin).Narrow();
        Aabb box;
        box.min = info.box.min + rel;
        box.max = info.box.max + rel;
        const f32 reach = p.mesh_radius * info.max_scale;
        // A margin larger than the rounding of the plane tests, so a chunk is rejected only when every instance would
        // be and accepted whole only when every instance would be.
        const Vec3 centre = (box.min + box.max) * 0.5f;
        const Vec3 half = (box.max - box.min) * 0.5f;
        const f32 eps = 1.0e-4f *
                        (1.0f + Abs(centre.x) + Abs(centre.y) + Abs(centre.z) + half.x + half.y + half.z + reach);
        Aabb grown;
        grown.min = box.min - Vec3(reach + eps, reach + eps, reach + eps);
        grown.max = box.max + Vec3(reach + eps, reach + eps, reach + eps);
        const Containment where = p.frustum.TestAabb(grown);
        bool skip = where == Containment::Outside;
        if (!skip && by_distance && BoxDistance(p.camera_position, box, false) > p.max_distance + reach + eps)
        {
            skip = true;
        }
        if (!skip && by_size)
        {
            const f32 closest = BoxDistance(p.camera_position, box, false);
            if (reach + eps < p.min_size_ratio * closest)
            {
                skip = true;
            }
        }
        if (skip)
        {
            ++out.stats.chunks_skipped;
            continue;
        }
        bool whole = where == Containment::Inside && !by_size;
        if (whole && by_distance && BoxDistance(p.camera_position, box, true) + eps > p.max_distance)
        {
            whole = false;
        }
        if (whole)
        {
            // Shrinking the margin cannot matter here: the grown box is inside every plane.
            InstanceRun run;
            run.chunk = c;
            run.first = 0;
            run.count = info.count;
            out.runs.Push(run);
            ++out.stats.chunks_accepted;
            out.stats.instances_emitted += info.count;
            continue;
        }
        ++out.stats.chunks_partial;
        const Span<const InstanceRecord> records = set.Records(c);
        InstanceRun run;
        run.chunk = c;
        for (u32 i = 0; i < info.count; ++i)
        {
            const InstanceRecord& r = records[i];
            const Vec3 pos = rel + r.position;
            const f32 radius = p.mesh_radius * InstanceMaxScale(r.scale);
            ++out.stats.instances_tested;
            bool visible = p.frustum.TestSphere(BoundingSphere{pos, radius}) != Containment::Outside;
            if (visible && by_distance && (pos - p.camera_position).Length() > p.max_distance)
            {
                visible = false;
            }
            if (visible && by_size && radius < p.min_size_ratio * (pos - p.camera_position).Length())
            {
                visible = false;
            }
            if (visible)
            {
                if (run.count == 0)
                {
                    run.first = i;
                }
                ++run.count;
                ++out.stats.instances_emitted;
            }
            else if (run.count != 0)
            {
                out.runs.Push(run);
                run.count = 0;
            }
        }
        if (run.count != 0)
        {
            out.runs.Push(run);
        }
    }
}

}  // namespace

f32 InstanceSizeRatio(f32 pixels, f32 viewport_height, f32 fov_y)
{
    if (!(viewport_height > 0.0f) || !(fov_y > 0.0f) || !(pixels > 0.0f) || !IsFinite(pixels))
    {
        return 0.0f;
    }
    // A sphere of radius r at distance d spans about 2 r / (2 d tan(fov / 2)) of the view's height.
    return pixels / (viewport_height * Tan(fov_y * 0.5f));
}

void CullInstances(const InstanceSet& set, const InstanceCullParams& params, jobs::Scheduler* scheduler,
                   InstanceCullResult* out)
{
    out->runs.Clear();
    out->stats = InstanceCullStats();
    if (!IsFinite(params.mesh_radius) || params.mesh_radius < 0.0f || !IsFinite(params.max_distance) ||
        params.max_distance < 0.0f || !IsFinite(params.min_size_ratio) || params.min_size_ratio < 0.0f)
    {
        return;
    }
    const u32 chunk_count = set.ChunkCount();
    const u32 groups = (chunk_count + kInstanceCullGroup - 1) / kInstanceCullGroup;
    Vector<InstanceCullGroupOut> parts;
    parts.Resize(groups);
    InstanceCullContext ctx{&set, &params, parts.Data(), chunk_count};
    if (scheduler != nullptr && groups > 1)
    {
        auto body = [&ctx](usize begin, usize end)
        {
            for (usize g = begin; g < end; ++g)
            {
                CullGroup(ctx, static_cast<u32>(g));
            }
        };
        jobs::ParallelForBlocking(*scheduler, groups, 1, body);
    }
    else
    {
        for (u32 g = 0; g < groups; ++g)
        {
            CullGroup(ctx, g);
        }
    }
    for (u32 g = 0; g < groups; ++g)
    {
        const InstanceCullGroupOut& part = parts[g];
        out->runs.Append(part.runs.Data(), part.runs.Size());
        out->stats.chunks_tested += part.stats.chunks_tested;
        out->stats.chunks_skipped += part.stats.chunks_skipped;
        out->stats.chunks_accepted += part.stats.chunks_accepted;
        out->stats.chunks_partial += part.stats.chunks_partial;
        out->stats.instances_tested += part.stats.instances_tested;
        out->stats.instances_emitted += part.stats.instances_emitted;
    }
    out->stats.runs = static_cast<u32>(out->runs.Size());
}

void CompactInstances(const InstanceSet& set, Span<const InstanceRun> runs, const Vec3d& render_origin,
                      Vector<InstanceRecord>* out)
{
    out->Clear();
    for (usize r = 0; r < runs.Size(); ++r)
    {
        const InstanceRun& run = runs[r];
        if (run.chunk >= set.ChunkCount())
        {
            continue;
        }
        const InstanceChunkInfo info = set.Chunk(run.chunk);
        if (run.first >= info.count || run.count > info.count - run.first)
        {
            continue;
        }
        const Span<const InstanceRecord> records = set.Records(run.chunk);
        for (u32 i = 0; i < run.count; ++i)
        {
            InstanceRecord rec = records[run.first + i];
            rec.position = (info.origin + Vec3d(rec.position) - render_origin).Narrow();
            out->Push(rec);
        }
    }
}

}  // namespace vse::renderer
