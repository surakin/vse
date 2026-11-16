#include "physics/world.h"

#include "lang/scalar.h"
#include "physics/check.h"

namespace engine_placeholder::physics
{

namespace
{

void Report(BodyStatus* out, BodyStatus value)
{
    if (out != nullptr)
    {
        *out = value;
    }
}

}  // namespace

const char* BodyStatusName(BodyStatus status)
{
    switch (status)
    {
    case BodyStatus::Ok: return "ok";
    case BodyStatus::StaleId: return "stale_id";
    case BodyStatus::Invalid: return "invalid";
    case BodyStatus::WrongType: return "wrong_type";
    case BodyStatus::Unsupported: return "unsupported";
    case BodyStatus::OutOfMemory: return "out_of_memory";
    }
    return "unknown";
}

void PhysicsWorld::RefreshInertia(Body* body)
{
    body->inverse_inertia_world = body->mass.InverseInertiaWorld(body->orientation);
}

void PhysicsWorld::RecomputeMass(Body* body)
{
    MassProperties props;
    if (body->settings.type == BodyType::Dynamic)
    {
        const ColliderRecord* record = FindCollider(body->first_collider);
        if (record != nullptr)
        {
            props = MassFromDensity(record->value.shape, record->value.material.density, &hulls_);
            // The shape's centre is the collider's origin (a hull's is its centroid, seen through the collider); its
            // principal axes are the collider's own (a hull's turned by the collider).
            props.center = record->value.offset + record->value.rotation.RotateVector(props.center);
            props.principal_rotation = record->value.rotation * props.principal_rotation;
            if (body->settings.mass > 0.0f && props.mass > 0.0f)
            {
                const f32 scale = body->settings.mass / props.mass;
                props.inertia = props.inertia * scale;
                props.mass = body->settings.mass;
            }
        }
        else
        {
            // A body without a shape is a point: it has mass and no inertia (it does not turn).
            props.mass = body->settings.mass > 0.0f ? body->settings.mass : 1.0f;
        }
        if ((body->settings.flags & kBodyLockRotation) != 0)
        {
            props.inertia = Vec3(0.0f, 0.0f, 0.0f);
        }
    }
    else
    {
        // A static or kinematic body moves as if it were infinitely heavy.
        const ColliderRecord* record = FindCollider(body->first_collider);
        if (record != nullptr)
        {
            props.center = record->value.offset;
        }
    }
    body->mass = props;
    body->inverse_mass = props.InverseMass();
    RefreshInertia(body);
}

BodyId PhysicsWorld::CreateBody(const BodyDesc& desc, BodyStatus* status)
{
    if (desc.settings.Validate() != Validation::Ok || desc.velocity.Validate() != Validation::Ok ||
        !FiniteVec3(desc.position) || !IsUnitQuaternion(desc.orientation))
    {
        Report(status, BodyStatus::Invalid);
        return BodyId();
    }
    u32 index = 0;
    u32 generation = 0;
    Body* body = bodies_.Acquire(&index, &generation);
    if (body == nullptr)
    {
        Report(status, BodyStatus::OutOfMemory);
        return BodyId();
    }
    body->id = BodyId{index, generation};
    body->entity = desc.entity;
    body->settings = desc.settings;
    body->position = desc.position;
    body->orientation = desc.orientation;
    body->previous_position = desc.position;
    body->previous_orientation = desc.orientation;
    const bool moves = desc.settings.type != BodyType::Static;
    body->linear_velocity = moves ? desc.velocity.linear : Vec3(0.0f, 0.0f, 0.0f);
    body->angular_velocity = moves ? desc.velocity.angular : Vec3(0.0f, 0.0f, 0.0f);
    RecomputeMass(body);
    Report(status, BodyStatus::Ok);
    return body->id;
}

BodyStatus PhysicsWorld::DestroyBody(BodyId id)
{
    Body* body = bodies_.Find(id.index, id.generation);
    if (body == nullptr)
    {
        return BodyStatus::StaleId;
    }
    ColliderId next = body->first_collider;
    while (next.IsValid())
    {
        const ColliderRecord* record = colliders_.Find(next.index, next.generation);
        const ColliderId following = record != nullptr ? record->next : ColliderId();
        colliders_.Release(next.index, next.generation);
        next = following;
    }
    bodies_.Release(id.index, id.generation);
    return BodyStatus::Ok;
}

BodyStatus PhysicsWorld::AddCollider(BodyId body_id, const Collider& collider, ColliderId* out)
{
    Body* body = bodies_.Find(body_id.index, body_id.generation);
    if (body == nullptr)
    {
        return BodyStatus::StaleId;
    }
    if (collider.Validate() != Validation::Ok || !HullKnown(collider.shape))
    {
        return BodyStatus::Invalid;
    }
    if (body->collider_count != 0)
    {
        return BodyStatus::Unsupported;
    }
    u32 index = 0;
    u32 generation = 0;
    ColliderRecord* record = colliders_.Acquire(&index, &generation);
    if (record == nullptr)
    {
        return BodyStatus::OutOfMemory;
    }
    record->value = collider;
    record->owner = body_id;
    record->next = ColliderId();
    body->first_collider = ColliderId{index, generation};
    body->collider_count = 1;
    RecomputeMass(body);
    if (out != nullptr)
    {
        *out = body->first_collider;
    }
    return BodyStatus::Ok;
}

BodyStatus PhysicsWorld::SetCollider(BodyId body_id, const Collider& collider)
{
    Body* body = bodies_.Find(body_id.index, body_id.generation);
    if (body == nullptr)
    {
        return BodyStatus::StaleId;
    }
    ColliderRecord* record = colliders_.Find(body->first_collider.index, body->first_collider.generation);
    if (record == nullptr)
    {
        return AddCollider(body_id, collider);
    }
    if (collider.Validate() != Validation::Ok || !HullKnown(collider.shape))
    {
        return BodyStatus::Invalid;
    }
    record->value = collider;
    RecomputeMass(body);
    return BodyStatus::Ok;
}

BodyStatus PhysicsWorld::SetSettings(BodyId id, const RigidBody& settings)
{
    Body* body = bodies_.Find(id.index, id.generation);
    if (body == nullptr)
    {
        return BodyStatus::StaleId;
    }
    if (settings.Validate() != Validation::Ok)
    {
        return BodyStatus::Invalid;
    }
    body->settings = settings;
    if (settings.type == BodyType::Static)
    {
        body->linear_velocity = Vec3(0.0f, 0.0f, 0.0f);
        body->angular_velocity = Vec3(0.0f, 0.0f, 0.0f);
    }
    body->force = Vec3(0.0f, 0.0f, 0.0f);
    body->torque = Vec3(0.0f, 0.0f, 0.0f);
    RecomputeMass(body);
    return BodyStatus::Ok;
}

Body* PhysicsWorld::Find(BodyId id)
{
    return bodies_.Find(id.index, id.generation);
}

const Body* PhysicsWorld::Find(BodyId id) const
{
    return bodies_.Find(id.index, id.generation);
}

const ColliderRecord* PhysicsWorld::FindCollider(ColliderId id) const
{
    return colliders_.Find(id.index, id.generation);
}

ColliderRecord* PhysicsWorld::FindCollider(ColliderId id)
{
    return colliders_.Find(id.index, id.generation);
}

BodyId PhysicsWorld::BodyAtSlot(u32 index) const
{
    return BodyId{index, bodies_.LiveGeneration(index)};
}

ColliderId PhysicsWorld::ColliderAtSlot(u32 index) const
{
    return ColliderId{index, colliders_.LiveGeneration(index)};
}

Vec3 PhysicsWorld::WorldCenter(BodyId id) const
{
    const Body* body = Find(id);
    if (body == nullptr)
    {
        return Vec3(0.0f, 0.0f, 0.0f);
    }
    return body->position + body->orientation.ToMat3() * body->mass.center;
}

BodyStatus PhysicsWorld::SetVelocity(BodyId id, const Velocity& velocity)
{
    Body* body = Find(id);
    if (body == nullptr)
    {
        return BodyStatus::StaleId;
    }
    if (velocity.Validate() != Validation::Ok)
    {
        return BodyStatus::Invalid;
    }
    if (body->settings.type == BodyType::Static)
    {
        return BodyStatus::WrongType;
    }
    body->linear_velocity = velocity.linear;
    body->angular_velocity = velocity.angular;
    return BodyStatus::Ok;
}

BodyStatus PhysicsWorld::ApplyImpulse(BodyId id, const Vec3& impulse, const Vec3& point)
{
    Body* body = Find(id);
    if (body == nullptr)
    {
        return BodyStatus::StaleId;
    }
    if (!FiniteVec3(impulse) || !FiniteVec3(point))
    {
        return BodyStatus::Invalid;
    }
    if (body->settings.type != BodyType::Dynamic)
    {
        return BodyStatus::WrongType;
    }
    body->linear_velocity = body->linear_velocity + impulse * body->inverse_mass;
    const Vec3 arm = point - WorldCenter(id);
    body->angular_velocity = body->angular_velocity + body->inverse_inertia_world * arm.Cross(impulse);
    return BodyStatus::Ok;
}

BodyStatus PhysicsWorld::ApplyForce(BodyId id, const Vec3& force, const Vec3& point)
{
    Body* body = Find(id);
    if (body == nullptr)
    {
        return BodyStatus::StaleId;
    }
    if (!FiniteVec3(force) || !FiniteVec3(point))
    {
        return BodyStatus::Invalid;
    }
    if (body->settings.type != BodyType::Dynamic)
    {
        return BodyStatus::WrongType;
    }
    body->force = body->force + force;
    body->torque = body->torque + (point - WorldCenter(id)).Cross(force);
    return BodyStatus::Ok;
}

BodyStatus PhysicsWorld::SetTransform(BodyId id, const Vec3& position, const Quaternion& orientation)
{
    Body* body = Find(id);
    if (body == nullptr)
    {
        return BodyStatus::StaleId;
    }
    if (!FiniteVec3(position) || !IsUnitQuaternion(orientation))
    {
        return BodyStatus::Invalid;
    }
    body->position = position;
    body->orientation = orientation;
    body->previous_position = position;  // a teleport is not interpolated
    body->previous_orientation = orientation;
    RefreshInertia(body);
    ++body->teleports;
    return BodyStatus::Ok;
}

PhysicsWorldStats PhysicsWorld::Stats() const
{
    PhysicsWorldStats stats;
    stats.bodies = bodies_.Live();
    stats.body_slots = bodies_.Slots();
    stats.colliders = colliders_.Live();
    stats.collider_slots = colliders_.Slots();
    return stats;
}

}  // namespace engine_placeholder::physics
