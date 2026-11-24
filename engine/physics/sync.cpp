#include "physics/sync.h"

#include "containers/vector.h"
#include "ecs/query.h"
#include "ecs/transform.h"
#include "algorithm.h"
#include "lang/scalar.h"
#include "math/matrix.h"
#include "physics/query.h"

namespace vse::physics
{

namespace
{

struct Gathered
{
    ecs::Entity entity;
    RigidBody settings;
    Collider collider;
    Vec3d position;
    Quaternion rotation;
    Velocity velocity;
    BodyLink link;
    bool parented = false;  // a static body with a parent, placed by its world transform (glTF colliders Day 1)
    bool placeable = true;  // false when the world matrix gives no rotation (a column of zero length)
};

// The translation and rotation of a world matrix, the scale divided out of each column (the caller scales shapes).
bool PoseOfWorld(const Mat4& m, const Vec3d& place, Vec3d* position, Quaternion* rotation)
{
    *position = place;
    Vec3 axes[3];
    for (u32 c = 0; c < 3; ++c)
    {
        const Vec3 column(m.col[c].x, m.col[c].y, m.col[c].z);
        const f32 length = column.Length();
        if (!(length > 1e-12f) || !IsFinite(length))
        {
            return false;
        }
        axes[c] = column * (1.0f / length);
    }
    if (axes[0].Cross(axes[1]).Dot(axes[2]) < 0.0f)
    {
        axes[2] = axes[2] * -1.0f;  // a mirror: the rotation stays proper, the caller's shape carries the flip
    }
    *rotation = Quaternion::FromMat3(Mat3::FromBasis(axes[0], axes[1], axes[2])).Normalized();
    return true;
}

// Wakes the movable bodies whose colliders touch a static body's colliders at its new pose (the bodies touching it at
// the old pose are woken by SetTransform), in collider order.
void WakeAtNewPose(PhysicsWorld& physics, const Body& body)
{
    Vector<ColliderId> touched;
    PhysicsQueryFilter filter;
    filter.include_static = false;
    for (ColliderId c = body.first_collider; c.IsValid();)
    {
        const ColliderRecord* record = physics.FindCollider(c);
        if (record == nullptr)
        {
            break;
        }
        const Aabbd bounds = record->value.shape.Bounds(body.position, body.orientation);
        const Vec3d centre = (bounds.min + bounds.max) * 0.5;
        const Vec3 half = ((bounds.max - bounds.min) * 0.5).Narrow();
        Overlap(physics, Shape::MakeBox(Vec3(Max(half.x, 1e-4f), Max(half.y, 1e-4f), Max(half.z, 1e-4f))), centre,
                Quaternion::Identity(), filter, &touched);
        for (usize k = 0; k < touched.Size(); ++k)
        {
            if (const ColliderRecord* other = physics.FindCollider(touched[k]))
            {
                physics.Wake(other->owner);
            }
        }
        c = record->next;
    }
}

bool SameSettings(const RigidBody& a, const RigidBody& b)
{
    return a.type == b.type && a.flags == b.flags && a.mass == b.mass && a.linear_damping == b.linear_damping &&
           a.angular_damping == b.angular_damping && a.gravity_scale == b.gravity_scale;
}

bool SameVec(const Vec3& a, const Vec3& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool SameVec(const Vec3d& a, const Vec3d& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool SameCollider(const Collider& a, const Collider& b)
{
    return a.shape.kind == b.shape.kind && a.shape.radius == b.shape.radius &&
           a.shape.half_height == b.shape.half_height && a.shape.hull == b.shape.hull && a.shape.mesh == b.shape.mesh &&
           a.shape.compound == b.shape.compound && a.shape.height_field == b.shape.height_field &&
           SameVec(a.shape.half_extents, b.shape.half_extents) && SameVec(a.offset, b.offset) &&
           a.rotation.x == b.rotation.x && a.rotation.y == b.rotation.y && a.rotation.z == b.rotation.z &&
           a.rotation.w == b.rotation.w && a.material.friction == b.material.friction &&
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
        bool wanted = ecs.Exists(entity) && ecs.Has<RigidBody>(entity) && ecs.Has<Collider>(entity);
        if (wanted && ecs.Has<ecs::Parent>(entity))
        {  // a static body with a parent is placed by its world transform (glTF colliders Day 1)
            wanted = ecs.Peek<RigidBody>(entity)->type == BodyType::Static && ecs.Has<ecs::WorldTransform>(entity);
        }
        else if (wanted)
        {
            wanted = ecs.Has<ecs::LocalTransform>(entity);
        }
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
    // Static bodies with a parent: placed by the world transform's position and rotation.
    auto parented = ecs.Query<ecs::Read<RigidBody>, ecs::Read<Collider>, ecs::Read<ecs::WorldTransform>,
                              ecs::Read<ecs::Parent>, ecs::Optional<BodyLink>>();
    parented.ForEach(
        [&](ecs::Entity entity, const RigidBody& settings, const Collider& collider, const ecs::WorldTransform& placed,
            const ecs::Parent&, const BodyLink* link)
        {
            if (settings.type != BodyType::Static)
            {
                return;  // a moving body with a parent is not simulated (a body has no parent)
            }
            Gathered g;
            g.entity = entity;
            g.settings = settings;
            g.collider = collider;
            g.parented = true;
            g.placeable = PoseOfWorld(placed.matrix, placed.position, &g.position, &g.rotation);
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
            if (g.parented && g.placeable &&
                (!SameVec(body->position, g.position) || body->orientation.x != g.rotation.x ||
                 body->orientation.y != g.rotation.y || body->orientation.z != g.rotation.z ||
                 body->orientation.w != g.rotation.w))
            {  // follows its entity: a teleport (static bodies have no velocity), the bodies around it woken
                if (physics.SetTransform(g.link.id, g.position, g.rotation) == BodyStatus::Ok)
                {
                    WakeAtNewPose(physics, *physics.Find(g.link.id));
                    ++report.replaced;
                }
                else
                {
                    refused = true;
                }
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
        if (!g.placeable)
        {
            ++report.refused;
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

}  // namespace vse::physics
