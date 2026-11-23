#include "renderer/instance_query.h"

#include "lang/scalar.h"
#include "spatial/query.h"

namespace vse::renderer
{

bool InstanceQuery::Refresh(const InstanceSet& set, f32 mesh_radius)
{
    if (!IsFinite(mesh_radius) || mesh_radius < 0.0f)
    {
        mesh_radius = 0.0f;
    }
    if (built_ && version_ == set.Version() && chunks_ == set.ChunkCount() && radius_ == mesh_radius)
    {
        return false;
    }
    Vector<spatial::Item> items;
    for (u32 c = 0; c < set.ChunkCount(); ++c)
    {
        const InstanceChunkInfo info = set.Chunk(c);
        if (info.count == 0)
        {
            continue;
        }
        const f64 reach = static_cast<f64>(mesh_radius) * static_cast<f64>(info.max_scale);
        spatial::Item item;
        item.box.min = info.origin + Vec3d(info.box.min) - Vec3d(reach, reach, reach);
        item.box.max = info.origin + Vec3d(info.box.max) + Vec3d(reach, reach, reach);
        item.key = c;
        items.Push(item);
    }
    tree_.Build(Span<const spatial::Item>(items.Data(), items.Size()));
    version_ = set.Version();
    chunks_ = set.ChunkCount();
    radius_ = mesh_radius;
    built_ = true;
    return true;
}

bool InstanceQuery::Raycast(const InstanceSet& set, const Rayd& ray, f64 max_t, InstanceHit* hit) const
{
    if (!built_)
    {
        return false;
    }
    Vector<spatial::RayHit> chunks;
    spatial::Raycast(tree_, ray, max_t, spatial::QueryFilter(), spatial::RayMode::All, spatial::Order::Traversal,
                     &chunks);
    bool found = false;
    f64 best = max_t;
    for (usize k = 0; k < chunks.Size(); ++k)
    {
        const u32 c = static_cast<u32>(chunks[k].key);
        if (c >= set.ChunkCount())
        {
            continue;
        }
        const InstanceChunkInfo info = set.Chunk(c);
        const Span<const InstanceRecord> records = set.Records(c);
        for (u32 i = 0; i < info.count; ++i)
        {
            const f64 reach = static_cast<f64>(radius_) *
                              static_cast<f64>(Max(records[i].scale.x, Max(records[i].scale.y, records[i].scale.z)));
            const Vec3d centre = info.origin + Vec3d(records[i].position);
            const Aabbd box(centre - Vec3d(reach, reach, reach), centre + Vec3d(reach, reach, reach));
            f64 t = 0.0;
            if (Intersect(ray, box, best, &t) && (!found || t < best))
            {
                best = t;
                found = true;
                hit->chunk = c;
                hit->slot = i;
                hit->t = t;
            }
        }
    }
    if (found)
    {
        hit->id = set.IdAt(hit->chunk, hit->slot);
    }
    return found;
}

}  // namespace vse::renderer
