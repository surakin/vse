// The contact solver (Phase 25 Day 384): bounce, friction on a slope, head-on collisions, momentum and energy, and
// independence of the order the contacts arrive in.
#include <catch2/catch_test_macros.hpp>

#include "lang/scalar.h"
#include "physics/collide.h"
#include "physics/solver.h"
#include "physics/world.h"

using namespace engine_placeholder;
using namespace engine_placeholder::physics;

namespace
{

bool SNear(f32 a, f32 b, f32 relative)
{
    return Abs(a - b) <= relative * FMax(Abs(b), 1e-3f);
}

void NoGravity(PhysicsWorld& world)
{
    StepConfig config = world.GetStepConfig();
    config.gravity = Vec3(0.0f, 0.0f, 0.0f);
    REQUIRE(world.SetStepConfig(config) == BodyStatus::Ok);
}

BodyId Ball(PhysicsWorld& world, const Vec3& position, const Vec3& velocity, f32 restitution, f32 friction = 0.0f)
{
    BodyDesc desc;
    desc.position = position;
    desc.velocity.linear = velocity;
    const BodyId id = world.CreateBody(desc);
    REQUIRE(id.IsValid());
    Collider c;
    c.shape = Shape::MakeSphere(0.5f);
    c.material.restitution = restitution;
    c.material.friction = friction;
    REQUIRE(world.AddCollider(id, c) == BodyStatus::Ok);
    return id;
}

BodyId Slab(PhysicsWorld& world, const Vec3& position, const Quaternion& rotation, const Vec3& half, BodyType type,
            f32 friction, f32 restitution = 0.0f)
{
    BodyDesc desc;
    desc.settings.type = type;
    desc.position = position;
    desc.orientation = rotation;
    const BodyId id = world.CreateBody(desc);
    REQUIRE(id.IsValid());
    Collider c;
    c.shape = Shape::MakeBox(half);
    c.material.friction = friction;
    c.material.restitution = restitution;
    REQUIRE(world.AddCollider(id, c) == BodyStatus::Ok);
    return id;
}

Vec3 VelocityOf(const PhysicsWorld& world, BodyId id)
{
    return world.Find(id)->linear_velocity;
}

}  // namespace

TEST_CASE("Solver: a ball bounces back with e times its speed", "[physics][solver]")
{
    for (const f32 e : {0.3f, 0.5f, 0.8f})
    {
        PhysicsWorld world;
        NoGravity(world);
        Slab(world, Vec3(0, -0.5f, 0), Quaternion::Identity(), Vec3(10, 0.5f, 10), BodyType::Static, 0.0f, e);
        const BodyId ball = Ball(world, Vec3(0, 1.0f, 0), Vec3(0, -4.0f, 0), e);
        f32 rebound = 0.0f;
        for (int i = 0; i < 60 && rebound == 0.0f; ++i)
        {
            world.Step();
            if (VelocityOf(world, ball).y > 0.0f)
            {
                rebound = VelocityOf(world, ball).y;
            }
        }
        REQUIRE(rebound > 0.0f);
        CHECK(SNear(rebound, 4.0f * e, 0.03f));
    }
}

TEST_CASE("Solver: a box slides down a slope at g (sin - mu cos)", "[physics][solver]")
{
    const f32 angle = 0.5235988f;  // 30 degrees
    const f32 s = Sin(angle);
    const f32 c = Cos(angle);
    const Quaternion tilt(0.0f, 0.0f, Sin(angle * 0.5f), Cos(angle * 0.5f));
    for (const f32 mu : {0.0f, 0.2f, 0.4f})
    {
        PhysicsWorld world;
        Slab(world, Vec3(0, 0, 0), tilt, Vec3(40, 0.5f, 40), BodyType::Static, mu);
        const Vec3 normal(-s, c, 0.0f);
        const Vec3 down(-c, -s, 0.0f);
        const BodyId box = Slab(world, normal * 1.0f, tilt, Vec3(0.5f, 0.5f, 0.5f), BodyType::Dynamic, mu);
        // The acceleration between 0.5 s and 1.5 s, after the contact has settled into its soft equilibrium.
        f32 v_start = 0.0f;
        for (int i = 0; i < 90; ++i)
        {
            world.Step();
            if (i == 29)
            {
                v_start = VelocityOf(world, box).Dot(down);
            }
        }
        const f32 acceleration = (VelocityOf(world, box).Dot(down) - v_start) / 1.0f;
        const f32 expected = 9.81f * (s - mu * c);
        INFO("mu " << mu << " acceleration " << acceleration << " expected " << expected);
        CHECK(SNear(acceleration, expected, 0.03f));
    }
}

TEST_CASE("Solver: a box stays on a slope that friction holds", "[physics][solver]")
{
    const f32 angle = 0.3f;  // tan = 0.31, below mu = 0.6
    const Quaternion tilt(0.0f, 0.0f, Sin(angle * 0.5f), Cos(angle * 0.5f));
    PhysicsWorld world;
    Slab(world, Vec3(0, 0, 0), tilt, Vec3(40, 0.5f, 40), BodyType::Static, 0.6f);
    const BodyId box = Slab(world, Vec3(-Sin(angle), Cos(angle), 0.0f), tilt, Vec3(0.5f, 0.5f, 0.5f), BodyType::Dynamic,
                            0.6f);
    for (int i = 0; i < 180; ++i)
    {
        world.Step();
    }
    CHECK(VelocityOf(world, box).Dot(VelocityOf(world, box)) < 0.01f);
}

TEST_CASE("Solver: equal balls head on exchange velocities at e = 1 and stick at e = 0", "[physics][solver]")
{
    {
        PhysicsWorld world;
        NoGravity(world);
        const BodyId a = Ball(world, Vec3(-1.5f, 0, 0), Vec3(3, 0, 0), 1.0f);
        const BodyId b = Ball(world, Vec3(1.5f, 0, 0), Vec3(-1, 0, 0), 1.0f);
        for (int i = 0; i < 90; ++i)
        {
            world.Step();
        }
        CHECK(SNear(VelocityOf(world, a).x, -1.0f, 0.02f));
        CHECK(SNear(VelocityOf(world, b).x, 3.0f, 0.02f));
    }
    {
        PhysicsWorld world;
        NoGravity(world);
        const BodyId a = Ball(world, Vec3(-1.5f, 0, 0), Vec3(3, 0, 0), 0.0f);
        const BodyId b = Ball(world, Vec3(1.5f, 0, 0), Vec3(-1, 0, 0), 0.0f);
        for (int i = 0; i < 90; ++i)
        {
            world.Step();
        }
        // Momentum is exact; the bodies move together except for the position correction's push out of the small
        // penetration the 4 m/s approach made before the contact was seen (the soft contacts of Day 385 shrink it).
        const f32 va = VelocityOf(world, a).x;
        const f32 vb = VelocityOf(world, b).x;
        CHECK(SNear(0.5f * (va + vb), 1.0f, 0.001f));
        CHECK(Abs(vb - va) < 0.05f * 4.0f);
    }
}

TEST_CASE("Solver: momentum is conserved and energy is conserved at e = 1", "[physics][solver]")
{
    PhysicsWorld world;
    NoGravity(world);
    const BodyId a = Ball(world, Vec3(-1.5f, 0, 0), Vec3(2.5f, 0.3f, 0), 1.0f);
    const BodyId b = Ball(world, Vec3(1.5f, 0.2f, 0), Vec3(-1.5f, 0, 0), 1.0f);
    const f32 ma = world.Find(a)->mass.mass;
    const f32 mb = world.Find(b)->mass.mass;
    const Vec3 p0 = VelocityOf(world, a) * ma + VelocityOf(world, b) * mb;
    const f32 e0 = 0.5f * ma * VelocityOf(world, a).LengthSquared() + 0.5f * mb * VelocityOf(world, b).LengthSquared();
    for (int i = 0; i < 120; ++i)
    {
        world.Step();
    }
    const Vec3 p1 = VelocityOf(world, a) * ma + VelocityOf(world, b) * mb;
    CHECK((p1 - p0).Length() < 1e-3f * p0.Length());
    const f32 e1 = 0.5f * ma * VelocityOf(world, a).LengthSquared() + 0.5f * mb * VelocityOf(world, b).LengthSquared();
    CHECK(SNear(e1, e0, 0.05f));
}

TEST_CASE("Solver: the order of the contacts does not change the result", "[physics][solver]")
{
    u64 hashes[3] = {0, 0, 0};
    for (u32 variant = 0; variant < 3; ++variant)
    {
        PhysicsWorld world;
        Vector<BodyId> bodies;
        Vector<ColliderId> colliders;
        const auto add = [&](const Vec3& p, BodyType type)
        {
            BodyDesc desc;
            desc.settings.type = type;
            desc.position = p;
            desc.velocity.linear = Vec3(0.1f * static_cast<f32>(bodies.Size()), 0, 0);
            const BodyId id = world.CreateBody(desc);
            REQUIRE(id.IsValid());
            Collider c;
            c.shape = Shape::MakeBox(type == BodyType::Static ? Vec3(10, 0.5f, 10) : Vec3(0.5f, 0.5f, 0.5f));
            ColliderId cid;
            REQUIRE(world.AddCollider(id, c, &cid) == BodyStatus::Ok);
            bodies.Push(id);
            colliders.Push(cid);
        };
        add(Vec3(0, -0.5f, 0), BodyType::Static);
        for (u32 i = 0; i < 4; ++i)
        {
            add(Vec3(0.05f * static_cast<f32>(i), 0.49f + 0.99f * static_cast<f32>(i), 0), BodyType::Dynamic);
        }
        Vector<ContactInput> inputs;
        for (u32 i = 0; i < bodies.Size(); ++i)
        {
            for (u32 j = i + 1; j < bodies.Size(); ++j)
            {
                const Body* bi = world.Find(bodies[i]);
                const Body* bj = world.Find(bodies[j]);
                ContactInput in;
                Pose pa;
                pa.position = bi->position;
                pa.rotation = bi->orientation;
                Pose pb;
                pb.position = bj->position;
                pb.rotation = bj->orientation;
                if (Collide(world.FindCollider(colliders[i])->value.shape, pa,
                            world.FindCollider(colliders[j])->value.shape, pb, 0.02f,
                            &in.manifold) != CollideResult::Contact)
                {
                    continue;
                }
                in.manifold.friction = 0.5f;
                in.a = colliders[i];
                in.b = colliders[j];
                in.body_a = bodies[i];
                in.body_b = bodies[j];
                inputs.Push(in);
            }
        }
        REQUIRE(inputs.Size() >= 4);
        if (variant == 1)
        {
            for (usize i = 0; i < inputs.Size() / 2; ++i)
            {
                const ContactInput t = inputs[i];
                inputs[i] = inputs[inputs.Size() - 1 - i];
                inputs[inputs.Size() - 1 - i] = t;
            }
        }
        else if (variant == 2)
        {
            // Flip the sides of every other contact: the solver orders the colliders itself.
            for (usize i = 0; i < inputs.Size(); i += 2)
            {
                ContactInput& in = inputs[i];
                in.manifold.Flip();
                const ColliderId c = in.a;
                in.a = in.b;
                in.b = c;
                const BodyId b = in.body_a;
                in.body_a = in.body_b;
                in.body_b = b;
            }
        }
        ContactSolver solver;
        SolverConfig config;
        solver.Solve(&world, Span<const ContactInput>(inputs.Data(), inputs.Size()), config);
        hashes[variant] = world.StateHash();
    }
    CHECK(hashes[0] == hashes[1]);
    CHECK(hashes[0] == hashes[2]);
}

TEST_CASE("Solver: warm starting carries the impulses over", "[physics][solver]")
{
    PhysicsWorld world;
    Slab(world, Vec3(0, -0.5f, 0), Quaternion::Identity(), Vec3(10, 0.5f, 10), BodyType::Static, 0.5f);
    Slab(world, Vec3(0, 0.5f, 0), Quaternion::Identity(), Vec3(0.5f, 0.5f, 0.5f), BodyType::Dynamic, 0.5f);
    world.Step();
    world.Step();
    CHECK(world.ContactCacheSize() >= 1);
    CHECK(world.GetSolverStats().warm_started == world.GetSolverStats().points);
    CHECK(world.GetSolverStats().iterations ==
          world.GetStepConfig().substeps * world.GetStepConfig().velocity_iterations);
    CHECK(world.GetSolverStats().max_impulse > 0.0f);
}

TEST_CASE("Solver: no body moves faster than the speed limit", "[physics][solver]")
{
    PhysicsWorld world;
    NoGravity(world);
    const BodyId free_ball = Ball(world, Vec3(0, 50, 0), Vec3(500, 0, 0), 0.0f);
    world.Step();
    CHECK(VelocityOf(world, free_ball).Length() <= world.GetStepConfig().max_velocity * 1.0001f);
    // A body that touches is limited in the solver's substeps as well.
    Slab(world, Vec3(0, -0.5f, 0), Quaternion::Identity(), Vec3(1000, 0.5f, 1000), BodyType::Static, 0.0f);
    const BodyId sliding = Ball(world, Vec3(0, 0.5f, 0), Vec3(500, 0, 0), 0.0f);
    world.Step();
    CHECK(VelocityOf(world, sliding).Length() <= world.GetStepConfig().max_velocity * 1.0001f);
}
