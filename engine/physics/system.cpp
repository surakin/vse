#include "physics/system.h"

#include "ecs/query.h"
#include "ecs/transform.h"

namespace engine_placeholder::physics
{

namespace
{

bool WriteOne(const PhysicsWorld& physics, f32 alpha, const BodyLink& link, ecs::LocalTransform& transform)
{
    const Body* body = physics.Find(link.id);
    if (body == nullptr || body->settings.type != BodyType::Dynamic)
    {
        return false;
    }
    Vec3 position;
    Quaternion rotation;
    if (!physics.Interpolated(link.id, alpha, &position, &rotation))
    {
        return false;
    }
    transform.position = position;
    transform.rotation = rotation;
    return true;
}

}  // namespace

u32 WriteBackTransforms(ecs::World& ecs, const PhysicsWorld& physics, f32 alpha)
{
    u32 written = 0;
    auto query = ecs.Query<ecs::Read<BodyLink>, ecs::Write<ecs::LocalTransform>>();
    query.ForEach(
        [&](const BodyLink& link, ecs::LocalTransform& transform)
        {
            if (WriteOne(physics, alpha, link, transform))
            {
                ++written;
            }
        });
    return written;
}

ecs::SystemDesc MakePhysicsStepSystem()
{
    return ecs::MakeSystem("physics_step",
                           [](ecs::SystemContext& c)
                           {
                               PhysicsWorld& physics = c.Resource<PhysicsWorld>();
                               physics.Update(c.Dt());
                               const f32 alpha = physics.Alpha();
                               auto query = c.Query<ecs::Read<BodyLink>, ecs::Write<ecs::LocalTransform>>();
                               query.ForEach([&](const BodyLink& link, ecs::LocalTransform& transform)
                                             { WriteOne(physics, alpha, link, transform); });
                           })
        .Reads<RigidBody, Collider, BodyLink>()
        .Writes<ecs::LocalTransform>()
        .WritesResource<PhysicsWorld>();
}

}  // namespace engine_placeholder::physics
