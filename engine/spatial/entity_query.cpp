#include "spatial/entity_query.h"

#include "jobs/parallel_for.h"

namespace engine_placeholder::spatial
{

usize OverlapEntities(const SpatialWorld& spatial, SetId set, const Aabb& box, const QueryFilter& filter,
                      Vector<ecs::Entity>* out)
{
    out->Clear();
    const SpatialSet* s = spatial.Set(set);
    if (s == nullptr)
    {
        return 0;
    }
    Vector<SpatialKey> keys;
    OverlapKeys(*s, box, filter, Order::Ordered, &keys);
    for (usize i = 0; i < keys.Size(); ++i)
    {
        out->Push(ecs::Entity::FromU64(keys[i]));
    }
    return out->Size();
}

bool RaycastEntity(const SpatialWorld& spatial, SetId set, const Ray& ray, f32 max_t, const QueryFilter& filter,
                   EntityHit* hit)
{
    const SpatialSet* s = spatial.Set(set);
    if (s == nullptr)
    {
        return false;
    }
    Vector<RayHit> hits;
    Raycast(*s, ray, max_t, filter, RayMode::Closest, Order::Ordered, &hits);
    if (hits.Size() == 0)
    {
        return false;
    }
    hit->entity = ecs::Entity::FromU64(hits[0].key);
    hit->t = hits[0].t;
    hit->point = ray.At(hits[0].t);
    return true;
}

bool SweepEntity(const SpatialWorld& spatial, SetId set, const Aabb& box, const Vec3& motion, const QueryFilter& filter,
                 EntityHit* hit)
{
    const SpatialSet* s = spatial.Set(set);
    if (s == nullptr)
    {
        return false;
    }
    RayHit found;
    const QueryStats stats = SweepBox(*s, box, motion, filter, &found);
    if (stats.results == 0)
    {
        return false;
    }
    hit->entity = ecs::Entity::FromU64(found.key);
    hit->t = found.t;
    hit->point = box.Center() + motion * found.t;
    return true;
}

usize NearestEntities(const SpatialWorld& spatial, SetId set, const Vec3& point, u32 k, f32 max_distance,
                      const QueryFilter& filter, Vector<EntityDistance>* out)
{
    out->Clear();
    const SpatialSet* s = spatial.Set(set);
    if (s == nullptr)
    {
        return 0;
    }
    Vector<NearestHit> hits;
    Nearest(*s, point, k, max_distance, filter, &hits);
    for (usize i = 0; i < hits.Size(); ++i)
    {
        EntityDistance d;
        d.entity = ecs::Entity::FromU64(hits[i].key);
        d.distance = hits[i].distance;
        out->Push(d);
    }
    return out->Size();
}

namespace
{

struct RayBatch
{
    const SpatialSet* set;
    const Ray* rays;
    BatchRayHit* out;
    f32 max_t;
    const QueryFilter* filter;
};

void RunRays(void* user, usize begin, usize end)
{
    const RayBatch& b = *static_cast<const RayBatch*>(user);
    Vector<RayHit> hits;
    for (usize i = begin; i < end; ++i)
    {
        BatchRayHit answer;
        if (b.set != nullptr)
        {
            Raycast(*b.set, b.rays[i], b.max_t, *b.filter, RayMode::Closest, Order::Ordered, &hits);
            if (hits.Size() != 0)
            {
                answer.hit = true;
                answer.key = hits[0].key;
                answer.t = hits[0].t;
            }
        }
        b.out[i] = answer;
    }
}

struct OverlapBatchState
{
    const SpatialSet* set;
    const Aabb* boxes;
    const QueryFilter* filter;
    u32* counts;         // phase 1: keys per query
    const u32* offsets;  // phase 2: where each query's keys go
    SpatialKey* keys;
};

void CountOverlaps(void* user, usize begin, usize end)
{
    const OverlapBatchState& b = *static_cast<const OverlapBatchState*>(user);
    for (usize i = begin; i < end; ++i)
    {
        u32 n = 0;
        if (b.set != nullptr)
        {
            Overlap(*b.set, b.boxes[i], *b.filter,
                    [&](const Item&)
                    {
                        ++n;
                        return true;
                    });
        }
        b.counts[i] = n;
    }
}

void FillOverlaps(void* user, usize begin, usize end)
{
    const OverlapBatchState& b = *static_cast<const OverlapBatchState*>(user);
    Vector<SpatialKey> keys;
    for (usize i = begin; i < end; ++i)
    {
        if (b.set == nullptr)
        {
            continue;
        }
        OverlapKeys(*b.set, b.boxes[i], *b.filter, Order::Ordered, &keys);
        const usize room = b.offsets[i + 1] - b.offsets[i];  // never past the slots counted for this query, even if the
                                                             // set changed between the passes
        for (usize k = 0; k < keys.Size() && k < room; ++k)
        {
            b.keys[b.offsets[i] + k] = keys[k];
        }
    }
}

void RunLoop(jobs::Scheduler* scheduler, usize count, jobs::ParallelForFn fn, void* user)
{
    if (scheduler == nullptr)
    {
        fn(user, 0, count);
    }
    else
    {
        jobs::ParallelForBlocking(*scheduler, count, 256, fn, user);
    }
}

}  // namespace

void RaycastBatch(const SpatialWorld& spatial, SetId set, Span<const Ray> rays, f32 max_t, const QueryFilter& filter,
                  Span<BatchRayHit> out, jobs::Scheduler* scheduler)
{
    ENGINE_ASSERT(out.Size() == rays.Size(), "RaycastBatch: out and rays differ in size");
    RayBatch batch{spatial.Set(set), rays.Data(), out.Data(), max_t, &filter};
    RunLoop(scheduler, rays.Size(), &RunRays, &batch);
}

void OverlapBatch(const SpatialWorld& spatial, SetId set, Span<const Aabb> boxes, const QueryFilter& filter,
                  Vector<SpatialKey>* keys, Vector<u32>* offsets, jobs::Scheduler* scheduler)
{
    keys->Clear();
    offsets->Clear();
    Vector<u32> counts;
    counts.Resize(boxes.Size());
    OverlapBatchState state{spatial.Set(set), boxes.Data(), &filter, counts.Data(), nullptr, nullptr};
    RunLoop(scheduler, boxes.Size(), &CountOverlaps, &state);
    u64 total = 0;
    for (usize i = 0; i < boxes.Size(); ++i)
    {
        offsets->Push(static_cast<u32>(total));
        total += counts[i];
        ENGINE_ASSERT(total <= 0xFFFFFFFFull, "OverlapBatch: more than 2^32 keys in one batch");
    }
    offsets->Push(static_cast<u32>(total));
    keys->Resize(static_cast<usize>(total));
    state.offsets = offsets->Data();
    state.keys = keys->Data();
    RunLoop(scheduler, boxes.Size(), &FillOverlaps, &state);
}

}  // namespace engine_placeholder::spatial
