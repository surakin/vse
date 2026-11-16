#include "physics/sync.h"

#include "containers/vector.h"
#include "ecs/query.h"
#include "ecs/transform.h"
#include "algorithm.h"

namespace engine_placeholder::physics
{

namespace
{

struct Gathered
{
    ecs::Entity entity;
    RigidBody settings;
    Collider collider;
    Vec3 position;
    Quaternion rotation;
    Velocity velocity;
    BodyLink link;
};

bool SameSettings(const RigidBody& a, const RigidBody& b)
{
    return a.type == b.type && a.flags == b.flags && a.mass == b.mass && a.linear_damping == b.linear_damping &&
           a.angular_damping == b.angular_damping && a.gravity_scale == b.gravity_scale;
}

bool SameVec(const Vec3& a, const Vec3& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool SameCollider(const Collider& a, const Collider& b)
{
    return a.shape.kind == b.shape.kind && a.shape.radius == b.shape.radius &&
           a.shape.half_height == b.shape.half_height && SameVec(a.shape.half_extents, b.shape.half_extents) &&
           SameVec(a.offset, b.offset) && a.rotation.x == b.rotation.x && a.rotation.y == b.rotation.y &&
           a.rotation.z == b.rotation.z && a.rotation.w == b.rotation.w && a.material.friction == b.material.friction &&
           a.material.restitution == b.material.restitution && a.material.density == b.material.density &&
           a.material.friction_rule == b.material.friction_rule &&
           a.material.restitution_rule == b.material.restitution_rule && a.layer == b.layer && a.group == b.group &&
           a.mask == b.mask && a.is_trigger == b.is_trigger;
}

}  // namespace

SyncReport SyncBodies(ecs::World& ecs, PhysicsWorld& physics)
{
    SyncReport report;

    // 1. Bodies whose entity is gone, or no longer has what a body needs.
    const u32 slots = physics.Stats().body_slots;
    for (u32 i = 0; i < slots; ++i)
    {
        const BodyId id = physics.BodyAtSlot(i);
        const Body* body = physics.Find(id);
        if (body == nullptr || !body->entity.IsValid())
        {
            continue;  // free, or a body made by hand
        }
        const ecs::Entity entity = body->entity;
        const bool wanted = ecs.Exists(entity) && ecs.Has<RigidBody>(entity) && ecs.Has<Collider>(entity) &&
                            ecs.Has<ecs::LocalTransform>(entity) && !ecs.Has<ecs::Parent>(entity);
        if (!wanted)
        {
            physics.DestroyBody(id);
            ++report.destroyed;
            if (ecs.Exists(entity) && ecs.Has<BodyLink>(entity))
            {
                ecs.Remove<BodyLink>(entity);
            }
        }
    }

    // 2. Every entity that should have a body, in entity order.
    Vector<Gathered> found;
    auto query = ecs.Query<ecs::Read<RigidBody>, ecs::Read<Collider>, ecs::Read<ecs::LocalTransform>,
                           ecs::Optional<Velocity>, ecs::Optional<BodyLink>, ecs::Without<ecs::Parent>>();
    query.ForEach(
        [&](ecs::Entity entity, const RigidBody& settings, const Collider& collider,
            const ecs::LocalTransform& transform, const Velocity* velocity, const BodyLink* link)
        {
            Gathered g;
            g.entity = entity;
            g.settings = settings;
            g.collider = collider;
            g.position = transform.position;
            g.rotation = transform.rotation;
            if (velocity != nullptr)
            {
                g.velocity = *velocity;
            }
            if (link != nullptr)
            {
                g.link = *link;
            }
            found.Push(g);
        });
    Sort(Span<Gathered>(found.Data(), found.Size()),
         [](const Gathered& a, const Gathered& b)
         {
             return a.entity.index != b.entity.index ? a.entity.index < b.entity.index
                                                     : a.entity.generation < b.entity.generation;
         });

    // 3. Follow, or create.
    for (usize i = 0; i < found.Size(); ++i)
    {
        const Gathered& g = found[i];
        Body* body = g.link.id.IsValid() ? physics.Find(g.link.id) : nullptr;
        if (body != nullptr && body->entity.ToU64() == g.entity.ToU64())
        {
            bool changed = false;
            bool refused = false;
            if (!SameSettings(body->settings, g.settings))
            {
                const BodyStatus result = physics.SetSettings(g.link.id, g.settings);
                changed = result == BodyStatus::Ok || changed;
                refused = result != BodyStatus::Ok || refused;
            }
            const ColliderRecord* record = physics.FindCollider(body->first_collider);
            if (record == nullptr || !SameCollider(record->value, g.collider))
            {
                const BodyStatus result = physics.SetCollider(g.link.id, g.collider);
                changed = result == BodyStatus::Ok || changed;
                refused = result != BodyStatus::Ok || refused;
            }
            if (refused)
            {
                ++report.refused;  // the body keeps its last valid state
            }
            if (changed)
            {
                ++report.updated;
            }
            continue;
        }
        BodyDesc desc;
        desc.settings = g.settings;
        desc.position = g.position;
        desc.orientation = g.rotation;
        desc.velocity = g.velocity;
        desc.entity = g.entity;
        const BodyId id = physics.CreateBody(desc);
        if (!id.IsValid())
        {
            ++report.refused;
            continue;
        }
        if (physics.AddCollider(id, g.collider) != BodyStatus::Ok)
        {
            physics.DestroyBody(id);
            ++report.refused;
            continue;
        }
        const BodyLink link{id};
        if (g.link.id.IsValid() || ecs.Has<BodyLink>(g.entity))
        {
            ecs.Set(g.entity, link);
        }
        else
        {
            ecs.Add(g.entity, link);
        }
        ++report.created;
    }
    return report;
}

}  // namespace engine_placeholder::physics
