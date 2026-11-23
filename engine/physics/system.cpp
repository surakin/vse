#include "physics/system.h"

#include "ecs/query.h"
#include "ecs/transform.h"

namespace vse::physics
{

namespace
{

bool SameValue(const Vec3& a, const Vec3& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool SameValue(const Vec3d& a, const Vec3d& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

// Writes the interpolated pose of a dynamic body into a transform; true when the transform's value changed (a body
// at rest leaves its transform and its chunk's change stamp alone).
bool WriteOne(const PhysicsWorld& physics, f32 alpha, const BodyLink& link, ecs::LocalTransform& transform)
{
    const Body* body = physics.Find(link.id);
    if (body == nullptr || body->settings.type != BodyType::Dynamic)
    {
        return false;
    }
    Vec3d position;
    Quaternion rotation;
    if (!physics.Interpolated(link.id, alpha, &position, &rotation))
    {
        return false;
    }
    const bool same = SameValue(transform.position, position) && transform.rotation.x == rotation.x &&
                      transform.rotation.y == rotation.y && transform.rotation.z == rotation.z &&
                      transform.rotation.w == rotation.w;
    if (same)
    {
        return false;
    }
    transform.position = position;
    transform.rotation = rotation;
    return true;
}

u32 WriteChunk(const PhysicsWorld& physics, f32 alpha, ecs::ChunkView& view)
{
    const Span<const BodyLink> links = view.Read<BodyLink>();
    const Span<ecs::LocalTransform> transforms = view.Write<ecs::LocalTransform>();
    u32 written = 0;
    for (usize i = 0; i < links.Size(); ++i)
    {
        if (WriteOne(physics, alpha, links[i], transforms[i]))
        {
            ++written;
        }
    }
    if (written > 0)
    {
        view.MarkWritten<ecs::LocalTransform>();
    }
    return written;
}

}  // namespace

u32 WriteBackTransforms(ecs::World& ecs, const PhysicsWorld& physics, f32 alpha)
{
    u32 written = 0;
    auto query = ecs.Query<ecs::Read<BodyLink>, ecs::LazyWrite<ecs::LocalTransform>>();
    query.ForEachChunk([&](ecs::ChunkView& view) { written += WriteChunk(physics, alpha, view); });
    return written;
}

u32 DriveKinematicBodies(ecs::World& ecs, PhysicsWorld& physics, f32 dt)
{
    if (!IsFinite(dt) || !(dt > 0.0f))
    {
        return 0;
    }
    u32 driven = 0;
    auto query = ecs.Query<ecs::Read<BodyLink>, ecs::Read<ecs::LocalTransform>>();
    query.ForEach(
        [&](const BodyLink& link, const ecs::LocalTransform& transform)
        {
            const Body* body = physics.Find(link.id);
            if (body == nullptr || body->settings.type != BodyType::Kinematic)
            {
                return;
            }
            Velocity velocity;
            velocity.linear = (transform.position - body->position).Narrow() * (1.0f / dt);
            // The turn from the pose to the target, as an angular velocity (twice the vector part of the quaternion:
            // exact for small turns, which is what one step makes).
            Quaternion turn = transform.rotation * body->orientation.Conjugate();
            if (turn.w < 0.0f)
            {
                turn = Quaternion(-turn.x, -turn.y, -turn.z, -turn.w);
            }
            velocity.angular = Vec3(turn.x, turn.y, turn.z) * (2.0f / dt);
            if (SameValue(velocity.linear, body->linear_velocity) &&
                SameValue(velocity.angular, body->angular_velocity))
            {
                return;
            }
            if (physics.SetVelocity(link.id, velocity) == BodyStatus::Ok)
            {
                ++driven;
            }
        });
    return driven;
}

ecs::SystemDesc MakePhysicsStepSystem()
{
    return ecs::MakeSystem("physics_step",
                           [](ecs::SystemContext& c)
                           {
                               PhysicsWorld& physics = c.Resource<PhysicsWorld>();
                               physics.Update(c.Dt());
                               const f32 alpha = physics.Alpha();
                               auto query = c.Query<ecs::Read<BodyLink>, ecs::LazyWrite<ecs::LocalTransform>>();
                               query.ForEachChunk([&](ecs::ChunkView& view) { WriteChunk(physics, alpha, view); });
                           })
        .Reads<RigidBody, Collider, BodyLink>()
        .Writes<ecs::LocalTransform>()
        .WritesResource<PhysicsWorld>();
}

}  // namespace vse::physics
