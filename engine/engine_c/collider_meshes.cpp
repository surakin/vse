#include "engine_c/collider_meshes.h"

#include "algorithm.h"
#include "asset/mesh_collider.h"
#include "cstr/mem_ops.h"
#include "ecs/query.h"
#include "ecs/transform.h"
#include "lang/scalar.h"
#include "physics/triangle_mesh.h"

namespace engine_placeholder::engine_c
{

namespace
{

u32 Bits(f32 v)
{
    u32 b = 0;
    MemCopy(&b, &v, sizeof(b));
    return b;
}

// The world scale of a matrix: each column's length, a negative z for a mirror (the physics sync flips the third axis
// of the rotation then, so the shape carries the flip). False for a zero column or a shear.
bool ScaleOf(const Mat4& m, Vec3* out)
{
    Vec3 c[3];
    f32 length[3];
    for (u32 k = 0; k < 3; ++k)
    {
        c[k] = Vec3(m.col[k].x, m.col[k].y, m.col[k].z);
        length[k] = c[k].Length();
        if (!(length[k] > 1e-12f) || !IsFinite(length[k]))
        {
            return false;
        }
    }
    for (u32 a = 0; a < 3; ++a)
    {
        for (u32 b = a + 1; b < 3; ++b)
        {
            if (Abs(c[a].Dot(c[b])) > 1e-4f * length[a] * length[b])
            {
                return false;  // a shear: no rotation and scale give this matrix
            }
        }
    }
    const bool mirror = c[0].Cross(c[1]).Dot(c[2]) < 0.0f;
    *out = Vec3(length[0], length[1], mirror ? -length[2] : length[2]);
    return true;
}

// The physics mesh of an `MCOL` at a scale: from the world's cache, or made and added to its library.
bool MeshFor(WorldObject& object, ResourceHost& resources, u64 id, const Vec3& scale, u32* mesh, Vec3* extent,
             u32* dropped)
{
    const asset::MeshColliderView* view = nullptr;
    asset::ContentHash hash;
    if (resources.PinMeshCollider(id, &view, &hash) != ENGINE_OK)
    {
        return false;
    }
    const u32 key[3] = {Bits(scale.x), Bits(scale.y), Bits(scale.z)};
    for (usize i = 0; i < object.collider_meshes.Size(); ++i)
    {
        const ColliderMeshEntry& e = object.collider_meshes[i];
        if (MemCompare(e.hash.bytes, hash.bytes, sizeof(hash.bytes)) == 0 && e.scale[0] == key[0] &&
            e.scale[1] == key[1] && e.scale[2] == key[2])
        {
            *mesh = e.mesh;
            *extent = e.extent;
            return true;
        }
    }
    Vector<Vec3> positions;
    for (u32 v = 0; v < view->VertexCount(); ++v)
    {
        const Array<f32, 3> p = view->Position(v);
        positions.Push(Vec3(p[0] * scale.x, p[1] * scale.y, p[2] * scale.z));
    }
    const bool mirror = scale.z < 0.0f;  // one negative axis turns every triangle inside out: swap two corners
    Vector<u32> indices;
    Vector<u8> materials;
    for (u32 t = 0; t < view->TriangleCount(); ++t)
    {
        const u32 a = view->Index(t, 0);
        const u32 b = view->Index(t, mirror ? 2 : 1);
        const u32 c = view->Index(t, mirror ? 1 : 2);
        const Array<f32, 3> pa = {{positions[a].x, positions[a].y, positions[a].z}};
        const Array<f32, 3> pb = {{positions[b].x, positions[b].y, positions[b].z}};
        const Array<f32, 3> pc = {{positions[c].x, positions[c].y, positions[c].z}};
        if (!asset::MeshColliderTriangleHasArea(pa, pb, pc))
        {
            ++*dropped;  // a small scale made it degenerate
            continue;
        }
        indices.Push(a);
        indices.Push(b);
        indices.Push(c);
        materials.Push(view->Material(t));
    }
    if (indices.Size() == 0)
    {
        return false;
    }
    physics::TriangleMeshSpec spec;
    spec.positions = Span<const Vec3>(positions.Data(), positions.Size());
    spec.indices = Span<const u32>(indices.Data(), indices.Size());
    spec.materials = Span<const u8>(materials.Data(), materials.Size());
    const physics::TriangleMeshConstPtr made = physics::TriangleMesh::Create(spec, nullptr);
    if (!made)
    {
        return false;
    }
    physics::PhysicsWorld& physics = object.world.Resource<physics::PhysicsWorld>();
    ColliderMeshEntry entry;
    entry.hash = hash;
    entry.scale[0] = key[0];
    entry.scale[1] = key[1];
    entry.scale[2] = key[2];
    entry.mesh = physics.Meshes().Add(made);
    entry.extent = made->Extent();
    object.collider_meshes.Push(entry);
    *mesh = entry.mesh;
    *extent = entry.extent;
    return true;
}

// The physics collider of a source at a scale, or false.
bool Make(WorldObject& object, ResourceHost* resources, const ColliderSource& source, const Vec3& scale,
          physics::Collider* out, u32* dropped)
{
    const f32 sx = Abs(scale.x);
    const f32 sy = Abs(scale.y);
    const f32 sz = Abs(scale.z);
    switch (source.shape)
    {
    case 0:  // mesh
    {
        if (resources == nullptr)
        {
            return false;
        }
        u32 mesh = 0;
        Vec3 extent;
        if (!MeshFor(object, *resources, source.mesh, scale, &mesh, &extent, dropped))
        {
            return false;
        }
        out->shape = physics::Shape::MakeMesh(mesh, extent);
        break;
    }
    case 1:
        out->shape = physics::Shape::MakeBox(
            Vec3(source.half_extents.x * sx, source.half_extents.y * sy, source.half_extents.z * sz));
        break;
    case 2: out->shape = physics::Shape::MakeSphere(source.radius * Max(sx, Max(sy, sz))); break;
    case 3: out->shape = physics::Shape::MakeCapsule(source.half_height * sy, source.radius * Max(sx, sz)); break;
    default: return false;
    }
    out->offset = Vec3(source.offset.x * scale.x, source.offset.y * scale.y, source.offset.z * scale.z);
    out->rotation = source.rotation;
    out->material.friction = source.friction;
    out->material.restitution = source.restitution;
    out->material.density = source.density;
    out->layer = static_cast<u8>(source.layer);
    out->group = source.group;
    out->mask = source.mask;
    return out->Validate() == physics::Validation::Ok;
}

}  // namespace

void ResolveColliderSources(WorldObject& object, ResourceHost* resources, ColliderResolveCounts* counts)
{
    if (!object.collider_sources)
    {
        return;
    }
    struct Pending
    {
        ecs::Entity entity;
        ColliderSource source;
        Vec3 scale;
        bool scaled = false;
    };
    Vector<Pending> pending;
    auto query = object.world.Query<ecs::Read<ColliderSource>, ecs::Read<ecs::WorldTransform>>();
    query.ForEach(
        [&](ecs::Entity entity, const ColliderSource& source, const ecs::WorldTransform& placed)
        {
            if (source.state == 2)
            {
                return;
            }
            Pending p;
            p.entity = entity;
            p.source = source;
            p.scaled = ScaleOf(placed.matrix, &p.scale);
            if (source.state == 1)
            {
                if (source.scale_reported || !p.scaled)
                {
                    return;
                }
                const Vec3& m = source.made_scale;
                const auto differs = [](f32 a, f32 b) { return Abs(a - b) > 1e-6f * Max(Abs(a), Abs(b)); };
                if (!differs(m.x, p.scale.x) && !differs(m.y, p.scale.y) && !differs(m.z, p.scale.z))
                {
                    return;
                }
            }
            pending.Push(p);
        });
    // In entity order, so the meshes a world adds get the same ids in two worlds built the same way.
    Sort(Span<Pending>(pending.Data(), pending.Size()),
         [](const Pending& a, const Pending& b) { return a.entity.ToU64() < b.entity.ToU64(); });
    for (usize i = 0; i < pending.Size(); ++i)
    {
        Pending& p = pending[i];
        ColliderSource* source = object.world.Poke<ColliderSource>(p.entity);
        if (source == nullptr)
        {
            continue;
        }
        if (source->state == 1)
        {
            source->scale_reported = true;  // the scale changed: counted once, the collider stays
            ++counts->scale_ignored;
            continue;
        }
        physics::Collider collider;
        if (!p.scaled || !Make(object, resources, *source, p.scale, &collider, &counts->triangles_dropped))
        {
            source->state = 2;
            ++counts->refused;
            continue;
        }
        source->state = 1;
        source->made_scale = p.scale;
        const ecs::Status added = object.world.Has<physics::Collider>(p.entity)
                                      ? object.world.Set<physics::Collider>(p.entity, collider)
                                      : object.world.Add<physics::Collider>(p.entity, collider);
        if (added != ecs::Status::Ok)
        {
            source = object.world.Poke<ColliderSource>(p.entity);
            if (source != nullptr)
            {
                source->state = 2;
            }
            ++counts->refused;
            continue;
        }
        object.physics_used = true;
        ++counts->made;
    }
}

}  // namespace engine_placeholder::engine_c
