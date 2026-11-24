#include "physics/world.h"

#include "lang/scalar.h"
#include "physics/check.h"
#include "physics/query_index.h"

namespace vse::physics
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
            const CompoundShape* compound = record->value.shape.kind == ShapeKind::Compound
                                                ? compounds_.Find(record->value.shape.compound)
                                                : nullptr;
            props = compound != nullptr ? compound->Mass()
                                        : MassFromDensity(record->value.shape, record->value.material.density, &hulls_);
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
        !InsideWorld(desc.position) || !IsUnitQuaternion(desc.orientation))
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
    Disturb(body);
    ColliderId next = body->first_collider;
    while (next.IsValid())
    {
        const ColliderRecord* record = colliders_.Find(next.index, next.generation);
        const ColliderId following = record != nullptr ? record->next : ColliderId();
        colliders_.Release(next.index, next.generation);
        RefreshQueryIndex(next);  // gone: the index drops it
        next = following;
    }
    // The joints of the body go with it (found through the body's list, in slot order).
    if (const Vector<u32>* listed = body_joints_.Find(id.index))
    {
        Vector<u32> slots;
        for (usize i = 0; i < listed->Size(); ++i)
        {
            if (JointNames((*listed)[i], id))
            {
                slots.Push((*listed)[i]);
            }
        }
        for (usize i = 1; i < slots.Size(); ++i)  // ascending, as the scan over every slot released them
        {
            const u32 held = slots[i];
            usize j = i;
            while (j > 0 && slots[j - 1] > held)
            {
                slots[j] = slots[j - 1];
                --j;
            }
            slots[j] = held;
        }
        for (usize i = 0; i < slots.Size(); ++i)
        {
            if (i > 0 && slots[i] == slots[i - 1])
            {
                continue;  // listed twice (both ends on the body, or a slot reused)
            }
            joints_.Release(slots[i], joints_.LiveGeneration(slots[i]));
        }
        body_joints_.Remove(id.index);
    }
    DropEventsOf(id);
    bodies_.Release(id.index, id.generation);
    return BodyStatus::Ok;
}

bool PhysicsWorld::JointNames(u32 slot, BodyId body) const
{
    if (slot >= joints_.Slots())
    {
        return false;
    }
    const u32 generation = joints_.LiveGeneration(slot);
    if (generation == 0)
    {
        return false;
    }
    const JointRecord* joint = joints_.Find(slot, generation);
    return joint != nullptr && (joint->desc.body_a == body || joint->desc.body_b == body);
}

void PhysicsWorld::IndexJoint(u32 slot, const JointDesc& desc)
{
    const BodyId ends[2] = {desc.body_a, desc.body_b};
    for (u32 e = 0; e < 2; ++e)
    {
        if (!ends[e].IsValid() || (e == 1 && ends[1] == ends[0]))
        {
            continue;
        }
        Vector<u32>& list = *body_joints_.Emplace(ends[e].index).value;
        // Slots whose joints are gone pile up in the list of a body that keeps living: drop them each time the list
        // doubles past eight entries, so it stays within twice the joints the body has.
        if (list.Size() >= 8 && (list.Size() & (list.Size() - 1)) == 0)
        {
            usize kept = 0;
            for (usize i = 0; i < list.Size(); ++i)
            {
                if (JointNames(list[i], ends[e]))
                {
                    list[kept++] = list[i];
                }
            }
            list.Resize(kept);
        }
        list.Push(slot);
    }
}

void PhysicsWorld::RebuildJointIndex()
{
    body_joints_ = HashMap<u32, Vector<u32>, HeapAllocator>(HeapAllocator::Default());
    for (u32 slot = 0; slot < joints_.Slots(); ++slot)
    {
        const u32 generation = joints_.LiveGeneration(slot);
        if (generation != 0)
        {
            IndexJoint(slot, joints_.Find(slot, generation)->desc);
        }
    }
}

JointId PhysicsWorld::CreateJoint(const JointDesc& desc, BodyStatus* status)
{
    const auto fail = [&](BodyStatus s)
    {
        if (status != nullptr)
        {
            *status = s;
        }
        return JointId();
    };
    const Body* a = desc.body_a.IsValid() ? Find(desc.body_a) : nullptr;
    if (desc.body_a.IsValid() && a == nullptr)
    {
        return fail(BodyStatus::StaleId);
    }
    const Body* b = desc.body_b.IsValid() ? Find(desc.body_b) : nullptr;
    if (desc.body_b.IsValid() && b == nullptr)
    {
        return fail(BodyStatus::StaleId);
    }
    if (desc.Validate() != Validation::Ok)
    {
        return fail(BodyStatus::Invalid);
    }
    if ((a == nullptr || a->settings.type != BodyType::Dynamic) &&
        (b == nullptr || b->settings.type != BodyType::Dynamic))
    {
        return fail(BodyStatus::Invalid);
    }
    u32 index = 0;
    u32 generation = 0;
    JointRecord* record = joints_.Acquire(&index, &generation);
    if (record == nullptr)
    {
        return fail(BodyStatus::OutOfMemory);
    }
    record->desc = desc;
    IndexJoint(index, desc);
    if (a != nullptr)
    {
        WakeIsland(Find(desc.body_a));
    }
    if (b != nullptr)
    {
        WakeIsland(Find(desc.body_b));
    }
    if (desc.type == JointType::Hinge)
    {
        // The angle starts from the poses the hinge is made at.
        record->angle_raw = HingeRawAngle(desc, a != nullptr ? a->orientation : Quaternion::Identity(),
                                          b != nullptr ? b->orientation : Quaternion::Identity());
        record->angle = record->angle_raw;
    }
    if (status != nullptr)
    {
        *status = BodyStatus::Ok;
    }
    JointId id;
    id.index = index;
    id.generation = generation;
    return id;
}

BodyStatus PhysicsWorld::DestroyJoint(JointId id)
{
    const JointRecord* joint = joints_.Find(id.index, id.generation);
    if (joint == nullptr)
    {
        return BodyStatus::StaleId;
    }
    // The bodies may now move apart, so they leave sleep.
    if (joint->desc.body_a.IsValid() && Find(joint->desc.body_a) != nullptr)
    {
        WakeIsland(Find(joint->desc.body_a));
    }
    if (joint->desc.body_b.IsValid() && Find(joint->desc.body_b) != nullptr)
    {
        WakeIsland(Find(joint->desc.body_b));
    }
    joints_.Release(id.index, id.generation);
    return BodyStatus::Ok;
}

const JointRecord* PhysicsWorld::FindJoint(JointId id) const
{
    return joints_.Find(id.index, id.generation);
}

BodyStatus PhysicsWorld::JointReaction(JointId id, Vec3* force, Vec3* torque) const
{
    const JointRecord* joint = FindJoint(id);
    if (joint == nullptr)
    {
        return BodyStatus::StaleId;
    }
    if (force != nullptr)
    {
        *force = joint->reaction_force;
    }
    if (torque != nullptr)
    {
        *torque = joint->reaction_torque;
    }
    return BodyStatus::Ok;
}

namespace
{

// A world point in the frame of a body (the origin and axes of the body); for no body (the world) it is zero: the
// world side of a joint is measured from `JointDesc::world_origin`, which the caller sets.
Vec3 ToLocal(const Body* body, const Vec3d& point)
{
    return body == nullptr ? Vec3(0.0f, 0.0f, 0.0f)
                           : body->orientation.Conjugate().RotateVector((point - body->position).Narrow());
}

}  // namespace

JointDesc PhysicsWorld::MakeBallJoint(BodyId a, BodyId b, const Vec3d& world_point) const
{
    JointDesc desc;
    desc.type = JointType::Ball;
    desc.body_a = a;
    desc.body_b = b;
    desc.anchor_a = ToLocal(a.IsValid() ? Find(a) : nullptr, world_point);
    desc.anchor_b = ToLocal(b.IsValid() ? Find(b) : nullptr, world_point);
    desc.world_origin = world_point;
    return desc;
}

JointDesc PhysicsWorld::MakeDistanceJoint(BodyId a, BodyId b, const Vec3d& point_a, const Vec3d& point_b) const
{
    JointDesc desc;
    desc.type = JointType::Distance;
    desc.body_a = a;
    desc.body_b = b;
    desc.anchor_a = ToLocal(a.IsValid() ? Find(a) : nullptr, point_a);
    desc.anchor_b = ToLocal(b.IsValid() ? Find(b) : nullptr, point_b);
    desc.world_origin = a.IsValid() ? point_b : point_a;
    const Vec3 d = (point_b - point_a).Narrow();
    const f32 length = Sqrt(d.LengthSquared());
    desc.length = length > 1e-6f ? length : 1e-6f;
    desc.min_length = desc.length;
    desc.max_length = desc.length;
    return desc;
}

BodyStatus PhysicsWorld::JointAngle(JointId id, f32* angle) const
{
    const JointRecord* joint = FindJoint(id);
    if (joint == nullptr)
    {
        return BodyStatus::StaleId;
    }
    if (joint->desc.type != JointType::Hinge)
    {
        return BodyStatus::WrongType;
    }
    *angle = joint->angle;
    return BodyStatus::Ok;
}

JointDesc PhysicsWorld::MakeHingeJoint(BodyId a, BodyId b, const Vec3d& world_point, const Vec3& world_axis) const
{
    JointDesc desc = MakeFixedJoint(a, b, world_point);
    desc.type = JointType::Hinge;
    const f32 length2 = world_axis.LengthSquared();
    const Vec3 axis = length2 > 1e-12f ? world_axis * (1.0f / Sqrt(length2)) : Vec3(0.0f, 1.0f, 0.0f);
    const Body* body_a = a.IsValid() ? Find(a) : nullptr;
    const Body* body_b = b.IsValid() ? Find(b) : nullptr;
    const Quaternion qa = body_a != nullptr ? body_a->orientation : Quaternion::Identity();
    const Quaternion qb = body_b != nullptr ? body_b->orientation : Quaternion::Identity();
    // The frame of A has its Z axis along the hinge axis (seen from A); the frame of B is the same frame seen from B.
    const Vec3 local = qa.Conjugate().RotateVector(axis);
    Quaternion turn;
    if (local.z < -0.999999f)
    {
        turn = Quaternion(1.0f, 0.0f, 0.0f, 0.0f);  // half a turn about X, exact (no libm: Day 411)
    }
    else
    {
        turn = Quaternion(-local.y, local.x, 0.0f, 1.0f + local.z).Normalized();
    }
    desc.frame_a = turn;
    desc.frame_b = qb.Conjugate() * qa * turn;
    return desc;
}

JointDesc PhysicsWorld::MakeFixedJoint(BodyId a, BodyId b, const Vec3d& world_point) const
{
    JointDesc desc = MakeBallJoint(a, b, world_point);
    desc.type = JointType::Fixed;
    const Body* body_a = a.IsValid() ? Find(a) : nullptr;
    const Body* body_b = b.IsValid() ? Find(b) : nullptr;
    // The frames are chosen so the joint is at rest now: the world frame is body A's, seen from each body.
    const Quaternion qa = body_a != nullptr ? body_a->orientation : Quaternion::Identity();
    const Quaternion qb = body_b != nullptr ? body_b->orientation : Quaternion::Identity();
    desc.frame_a = Quaternion::Identity();
    desc.frame_b = qb.Conjugate() * qa;
    return desc;
}

BodyStatus PhysicsWorld::AddCollider(BodyId body_id, const Collider& collider, ColliderId* out)
{
    Body* body = bodies_.Find(body_id.index, body_id.generation);
    if (body == nullptr)
    {
        return BodyStatus::StaleId;
    }
    if (collider.Validate() != Validation::Ok || !HullKnown(collider.shape, body->settings.type))
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
    Disturb(body);
    record->owner = body_id;
    record->next = ColliderId();
    body->first_collider = ColliderId{index, generation};
    body->collider_count = 1;
    RecomputeMass(body);
    RefreshQueryIndex(body->first_collider);
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
    if (collider.Validate() != Validation::Ok || !HullKnown(collider.shape, body->settings.type))
    {
        return BodyStatus::Invalid;
    }
    Disturb(body);
    record->value = collider;
    RecomputeMass(body);
    RefreshQueryIndex(body->first_collider);
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
    const ColliderRecord* held = FindCollider(body->first_collider);
    if (held != nullptr &&
        (held->value.shape.kind == ShapeKind::Mesh || held->value.shape.kind == ShapeKind::HeightField) &&
        settings.type != BodyType::Static)
    {
        return BodyStatus::Invalid;  // a mesh collider stays on a static body
    }
    Disturb(body);
    body->settings = settings;
    body->island = 0;
    if (settings.type == BodyType::Static)
    {
        body->linear_velocity = Vec3(0.0f, 0.0f, 0.0f);
        body->angular_velocity = Vec3(0.0f, 0.0f, 0.0f);
    }
    body->force = Vec3(0.0f, 0.0f, 0.0f);
    body->torque = Vec3(0.0f, 0.0f, 0.0f);
    RecomputeMass(body);
    RefreshQueryIndex(body->first_collider);  // a change of class moves the collider across the index's two sides
    return BodyStatus::Ok;
}

void PhysicsWorld::RefreshQueryIndex(ColliderId id)
{
    if (!id.IsValid())
    {
        return;
    }
    if (query_index_.Get() == nullptr)
    {
        query_index_ = MakeUnique<QueryIndex>();
    }
    query_index_->Refresh(*this, id);
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

JointId PhysicsWorld::JointAtSlot(u32 index) const
{
    return JointId{index, joints_.LiveGeneration(index)};
}

Vec3d PhysicsWorld::WorldCenter(BodyId id) const
{
    const Body* body = Find(id);
    if (body == nullptr)
    {
        return Vec3d(0.0, 0.0, 0.0);
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
    WakeIsland(body);
    body->linear_velocity = velocity.linear;
    body->angular_velocity = velocity.angular;
    return BodyStatus::Ok;
}

BodyStatus PhysicsWorld::ApplyImpulse(BodyId id, const Vec3& impulse, const Vec3d& point)
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
    WakeIsland(body);
    body->linear_velocity = body->linear_velocity + impulse * body->inverse_mass;
    const Vec3 arm = (point - WorldCenter(id)).Narrow();
    body->angular_velocity = body->angular_velocity + body->inverse_inertia_world * arm.Cross(impulse);
    return BodyStatus::Ok;
}

BodyStatus PhysicsWorld::ApplyForce(BodyId id, const Vec3& force, const Vec3d& point)
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
    WakeIsland(body);
    body->force = body->force + force;
    body->torque = body->torque + (point - WorldCenter(id)).Narrow().Cross(force);
    return BodyStatus::Ok;
}

BodyStatus PhysicsWorld::SetTransform(BodyId id, const Vec3d& position, const Quaternion& orientation)
{
    Body* body = Find(id);
    if (body == nullptr)
    {
        return BodyStatus::StaleId;
    }
    if (!InsideWorld(position) || !IsUnitQuaternion(orientation))
    {
        return BodyStatus::Invalid;
    }
    Disturb(body);
    body->position = position;
    body->orientation = orientation;
    body->previous_position = position;  // a teleport is not interpolated
    body->previous_orientation = orientation;
    RefreshInertia(body);
    ++body->teleports;
    RefreshQueryIndex(body->first_collider);
    return BodyStatus::Ok;
}

PhysicsWorldStats PhysicsWorld::Stats() const
{
    PhysicsWorldStats stats;
    stats.bodies = bodies_.Live();
    stats.body_slots = bodies_.Slots();
    stats.colliders = colliders_.Live();
    stats.collider_slots = colliders_.Slots();
    stats.joint_slots = joints_.Slots();
    return stats;
}

}  // namespace vse::physics
