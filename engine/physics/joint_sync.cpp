#include "physics/joint_sync.h"

#include "algorithm.h"
#include "containers/vector.h"
#include "ecs/query.h"
#include "physics/sync.h"

namespace engine_placeholder::physics
{

namespace
{

struct Found
{
    ecs::Entity entity;
    Joint joint;
    JointLink link;
};

// The body of an entity: an invalid entity is the world (valid, a null body); a body that does not exist yet is not.
bool BodyOf(const ecs::World& ecs, ecs::Entity entity, BodyId* out)
{
    *out = BodyId();
    if (!entity.IsValid())
    {
        return true;
    }
    const BodyLink* link = ecs.Exists(entity) ? ecs.Get<BodyLink>(entity) : nullptr;
    if (link == nullptr)
    {
        return false;
    }
    *out = link->id;
    return true;
}

// Whether the entities a joint names exist (an invalid entity is the world).
bool EntitiesAlive(const ecs::World& ecs, const Joint& joint)
{
    return (!joint.entity_a.IsValid() || ecs.Exists(joint.entity_a)) &&
           (!joint.entity_b.IsValid() || ecs.Exists(joint.entity_b));
}

JointDesc Describe(const PhysicsWorld& physics, const Joint& joint, BodyId a, BodyId b)
{
    JointDesc desc;
    switch (joint.type)
    {
    case JointType::Ball: desc = physics.MakeBallJoint(a, b, joint.point_a); break;
    case JointType::Distance:
        desc = physics.MakeDistanceJoint(a, b, joint.point_a, joint.point_b);
        if (joint.min_length >= 0.0f)
        {
            desc.min_length = joint.min_length;
        }
        if (joint.max_length >= 0.0f)
        {
            desc.max_length = joint.max_length;
        }
        break;
    case JointType::Fixed: desc = physics.MakeFixedJoint(a, b, joint.point_a); break;
    case JointType::Hinge: desc = physics.MakeHingeJoint(a, b, joint.point_a, joint.axis); break;
    }
    desc.hertz = joint.hertz;
    desc.damping_ratio = joint.damping_ratio;
    desc.break_force = joint.break_force;
    desc.break_torque = joint.break_torque;
    desc.collide_connected = joint.collide_connected;
    desc.enable_limit = joint.enable_limit;
    desc.lower_angle = joint.lower_angle;
    desc.upper_angle = joint.upper_angle;
    desc.enable_motor = joint.enable_motor;
    desc.motor_speed = joint.motor_speed;
    desc.max_motor_torque = joint.max_motor_torque;
    return desc;
}

}  // namespace

JointSyncReport SyncJoints(ecs::World& ecs, PhysicsWorld& physics)
{
    JointSyncReport report;
    Vector<Found> found;
    auto with_joint = ecs.Query<ecs::Read<Joint>, ecs::Optional<JointLink>>();
    with_joint.ForEach(
        [&](ecs::Entity entity, const Joint& joint, const JointLink* link)
        {
            Found f;
            f.entity = entity;
            f.joint = joint;
            if (link != nullptr)
            {
                f.link = *link;
            }
            found.Push(f);
        });
    // A link whose `Joint` was removed: its physics joint goes.
    auto link_only = ecs.Query<ecs::Read<JointLink>, ecs::Without<Joint>>();
    link_only.ForEach(
        [&](ecs::Entity entity, const JointLink& link)
        {
            Found f;
            f.entity = entity;
            f.link = link;
            found.Push(f);
        });
    Sort(Span<Found>(found.Data(), found.Size()),
         [](const Found& x, const Found& y)
         {
             return x.entity.index != y.entity.index ? x.entity.index < y.entity.index
                                                     : x.entity.generation < y.entity.generation;
         });
    for (usize i = 0; i < found.Size(); ++i)
    {
        const Found& f = found[i];
        const bool has_joint = ecs.Has<Joint>(f.entity);
        const bool has_link = ecs.Has<JointLink>(f.entity);
        if (has_link)
        {
            const JointRecord* record = physics.FindJoint(f.link.id);
            BodyId a;
            BodyId b;
            const bool ready = has_joint && BodyOf(ecs, f.joint.entity_a, &a) && BodyOf(ecs, f.joint.entity_b, &b);
            const bool same = record != nullptr && ready && record->desc.body_a == a && record->desc.body_b == b;
            if (same)
            {
                continue;
            }
            if (record != nullptr)
            {
                physics.DestroyJoint(f.link.id);
                ++report.destroyed;
            }
            else if (has_joint && (ready || !EntitiesAlive(ecs, f.joint)))
            {
                // Both bodies are there and the joint is not: the step removed it, it broke. It is not made again. A
                // joint whose entity is destroyed is dropped the same way.
                ecs.Remove<Joint>(f.entity);
                ++report.broken;
            }
            // else a body was destroyed while its entity lives (a removed component: its joints went with it): the link
            // goes and the component stays, so the joint is made again when both bodies exist.
            ecs.Remove<JointLink>(f.entity);
            if (!has_joint || record == nullptr)
            {
                continue;
            }
        }
        if (!has_joint)
        {
            continue;
        }
        BodyId a;
        BodyId b;
        if (!BodyOf(ecs, f.joint.entity_a, &a) || !BodyOf(ecs, f.joint.entity_b, &b))
        {
            ++report.pending;
            continue;
        }
        BodyStatus status = BodyStatus::Ok;
        const JointId id = physics.CreateJoint(Describe(physics, f.joint, a, b), &status);
        if (!id.IsValid())
        {
            ++report.refused;
            continue;
        }
        ecs.Add(f.entity, JointLink{id});
        ++report.created;
    }
    return report;
}

}  // namespace engine_placeholder::physics
