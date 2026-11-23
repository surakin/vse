// Bodies and characters on a height field: contact quality on a tilted plane, resting, a bowl, no falling through a
// field of random bumps, fast bodies, the character controller and the debug drawing of cells.
#include <catch2/catch_test_macros.hpp>

#include "algorithm.h"
#include "lang/scalar.h"
#include "physics/character.h"
#include "physics/debug_draw.h"
#include "physics/height_field.h"
#include "physics/query.h"
#include "physics/world.h"
#include "test_values.h"

using namespace vse;
using namespace vse::physics;
using vse::physics::testing::Rng;

namespace
{

bool FNear(f32 a, f32 b, f32 tolerance)
{
    return Abs(a - b) <= tolerance;
}

template <class Value> HeightFieldConstPtr FieldOf(u32 w, u32 d, f32 hi, f32 cell, Value value)
{
    Vector<u16> samples;
    for (u32 z = 0; z < d; ++z)
    {
        for (u32 x = 0; x < w; ++x)
        {
            samples.Push(static_cast<u16>(value(x, z)));
        }
    }
    Validation v = Validation::Ok;
    HeightFieldConstPtr field = HeightField::CreateFromSamples(Span<const u16>(samples.Data(), samples.Size()), w, d,
                                                               0.0f, hi, cell, &v);
    REQUIRE(field);
    return field;
}

// A plane rising along +x by `slope` metres a metre: 41 by 21 samples, one metre a cell.
HeightFieldConstPtr TiltedField(f32 slope)
{
    const f32 hi = slope * 40.0f;
    return FieldOf(41, 21, hi, 1.0f, [](u32 x, u32) { return x * 65535u / 40u; });
}

void AddField(PhysicsWorld& world, const HeightFieldConstPtr& field, f32 friction = 0.6f)
{
    BodyDesc desc;
    desc.settings.type = BodyType::Static;
    const BodyId id = world.CreateBody(desc);
    Collider collider;
    collider.shape = Shape::MakeHeightField(world.HeightFields().Add(field), field->Extent());
    collider.material.friction = friction;
    REQUIRE(world.AddCollider(id, collider) == BodyStatus::Ok);
}

BodyId AddDynamic(PhysicsWorld& world, const Shape& shape, const Vec3& at,
                  const Quaternion& turn = Quaternion::Identity(), f32 friction = 0.6f)
{
    BodyDesc desc;
    desc.settings.type = BodyType::Dynamic;
    desc.position = at;
    desc.orientation = turn;
    const BodyId id = world.CreateBody(desc);
    Collider collider;
    collider.shape = shape;
    collider.material.friction = friction;
    REQUIRE(world.AddCollider(id, collider) == BodyStatus::Ok);
    return id;
}

}  // namespace

TEST_CASE("Height field contact: a sphere rolls down a tilted plane as down the same plane made of one box",
          "[physics][heightfield][collision]")
{
    const f32 slope = 0.364f;  // about 20 degrees
    const f32 angle = Atan2(slope, 1.0f);
    const Vec3 up(-Sin(angle), Cos(angle), 0.0f);
    const f32 radius = 0.5f;
    f32 field_x = 0.0f;
    f32 box_x = 0.0f;
    for (u32 variant = 0; variant < 2; ++variant)
    {
        PhysicsWorld world;
        // The surface passes through (0, h0, 0): the field's height at the centre of the map.
        const HeightFieldConstPtr field = TiltedField(slope);
        const f32 h0 = field->HeightAt(0.0f, 0.0f);
        if (variant == 0)
        {
            AddField(world, field);
        }
        else
        {
            BodyDesc desc;
            desc.settings.type = BodyType::Static;
            desc.position = Vec3(0.0f, h0, 0.0f) - up * 0.5f;
            desc.orientation = Quaternion::FromAxisAngle(Vec3(0, 0, 1), angle);
            const BodyId id = world.CreateBody(desc);
            Collider collider;
            collider.shape = Shape::MakeBox(Vec3(30.0f, 0.5f, 10.0f));
            REQUIRE(world.AddCollider(id, collider) == BodyStatus::Ok);
        }
        const Vec3 start = Vec3(0.0f, h0, 0.0f) + up * (radius + 0.01f);
        const BodyId ball = AddDynamic(world, Shape::MakeSphere(radius), start);
        for (u32 i = 0; i < 90; ++i)
        {
            world.Step();
        }
        const Body* body = world.Find(ball);
        REQUIRE(body != nullptr);
        (variant == 0 ? field_x : box_x) = body->position.x;
        // It stays on the surface the whole way: its centre is one radius above the plane.
        const f32 plane = field->HeightAt(body->position.x, 0.0f) + radius / Cos(angle);
        CHECK(FNear(body->position.y, plane, 0.03f));
    }
    // It went downhill (toward -x) and by about the same distance on both.
    CHECK(field_x < -1.0f);
    CHECK(FNear(field_x, box_x, 0.05f * Abs(box_x)));
}

TEST_CASE("Height field contact: a stack of boxes rests on a slope", "[physics][heightfield][collision]")
{
    PhysicsWorld world;
    const f32 slope = 0.268f;  // 15 degrees
    const f32 angle = Atan2(slope, 1.0f);
    const HeightFieldConstPtr field = TiltedField(slope);
    AddField(world, field);
    const Quaternion turn = Quaternion::FromAxisAngle(Vec3(0, 0, 1), angle);
    const Vec3 up(-Sin(angle), Cos(angle), 0.0f);
    const f32 h0 = field->HeightAt(0.0f, 0.0f);
    BodyId ids[3];
    Vec3 start[3];
    for (u32 i = 0; i < 3; ++i)
    {
        start[i] = Vec3(0.0f, h0, 0.0f) + up * (0.5f + 1.0f * static_cast<f32>(i) + 0.01f * static_cast<f32>(i + 1));
        ids[i] = AddDynamic(world, Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f)), start[i], turn);
    }
    for (u32 i = 0; i < 300; ++i)
    {
        world.Step();
    }
    for (u32 i = 0; i < 3; ++i)
    {
        const Body* body = world.Find(ids[i]);
        REQUIRE(body != nullptr);
        CHECK((body->position - start[i]).Length() < 0.15f);
        CHECK(body->linear_velocity.Length() < 0.1f);
    }
}

TEST_CASE("Height field contact: a sphere comes to rest in a bowl", "[physics][heightfield][collision]")
{
    PhysicsWorld world;
    const HeightFieldConstPtr bowl = FieldOf(33, 33, 8.0f, 1.0f,
                                             [](u32 x, u32 z)
                                             {
                                                 const f32 dx = static_cast<f32>(x) - 16.0f;
                                                 const f32 dz = static_cast<f32>(z) - 16.0f;
                                                 const f32 r2 = dx * dx + dz * dz;
                                                 return static_cast<u32>(Min(r2 / 512.0f, 1.0f) * 65535.0f);
                                             });
    AddField(world, bowl, 1.0f);
    BodyDesc ball_desc;
    ball_desc.settings.type = BodyType::Dynamic;
    ball_desc.settings.linear_damping = 0.3f;
    ball_desc.settings.angular_damping = 0.5f;
    ball_desc.position = Vec3(5.0f, 5.0f, 2.0f);
    const BodyId ball = world.CreateBody(ball_desc);
    Collider ball_collider;
    ball_collider.shape = Shape::MakeSphere(0.4f);
    ball_collider.material.friction = 1.0f;
    REQUIRE(world.AddCollider(ball, ball_collider) == BodyStatus::Ok);
    for (u32 i = 0; i < 1800; ++i)
    {
        world.Step();
    }
    const Body* body = world.Find(ball);
    REQUIRE(body != nullptr);
    CHECK(Abs(body->position.x) < 0.6f);
    CHECK(Abs(body->position.z) < 0.6f);
    CHECK(body->linear_velocity.Length() < 0.3f);
    CHECK(body->position.y > bowl->HeightAt(body->position.x, body->position.z) - 0.05f);
}

TEST_CASE("Height field contact: nothing falls through a field of 64 by 64 random bumps in ten seconds",
          "[physics][heightfield][collision][random]")
{
    PhysicsWorld world;
    Rng rng(5);
    const HeightFieldConstPtr field = FieldOf(65, 65, 3.0f, 1.0f,
                                              [&rng](u32, u32) { return static_cast<u32>(rng.Range(0.0f, 65535.0f)); });
    AddField(world, field);
    Vector<BodyId> bodies;
    for (u32 i = 0; i < 60; ++i)
    {
        const f32 rx = rng.Range(-28.0f, 28.0f);
        const f32 rz = rng.Range(-28.0f, 28.0f);
        const f32 ry = 4.0f + rng.Range(0.0f, 6.0f);
        const Vec3 at(rx, ry, rz);
        switch (i % 3)
        {
        case 0: bodies.Push(AddDynamic(world, Shape::MakeSphere(0.3f), at)); break;
        case 1: bodies.Push(AddDynamic(world, Shape::MakeBox(Vec3(0.3f, 0.2f, 0.3f)), at)); break;
        default: bodies.Push(AddDynamic(world, Shape::MakeCapsule(0.25f, 0.2f), at)); break;
        }
    }
    const u32 steps = static_cast<u32>(world.GetStepConfig().step_hz) * 10;
    for (u32 i = 0; i < steps; ++i)
    {
        world.Step();
        if (i % 30 == 0)
        {
            for (usize k = 0; k < bodies.Size(); ++k)
            {
                const Body* body = world.Find(bodies[k]);
                REQUIRE(body != nullptr);
                REQUIRE(body->position.y > field->HeightAt(body->position.x, body->position.z) - 0.1f);
            }
        }
    }
    for (usize k = 0; k < bodies.Size(); ++k)
    {
        const Body* body = world.Find(bodies[k]);
        CHECK(body->position.y > field->HeightAt(body->position.x, body->position.z) - 0.1f);
    }
}

TEST_CASE("Height field contact: a fast small sphere does not tunnel through the thin surface",
          "[physics][heightfield][collision][ccd]")
{
    PhysicsWorld world;
    const HeightFieldConstPtr field = TiltedField(0.1f);
    AddField(world, field);
    BodyDesc bullet_desc;
    bullet_desc.settings.type = BodyType::Dynamic;
    bullet_desc.settings.flags = kBodyContinuous;
    bullet_desc.position = Vec3(2.0f, 30.0f, 3.0f);
    const BodyId bullet = world.CreateBody(bullet_desc);
    Collider bullet_collider;
    bullet_collider.shape = Shape::MakeSphere(0.05f);
    REQUIRE(world.AddCollider(bullet, bullet_collider) == BodyStatus::Ok);
    Velocity v;
    v.linear = Vec3(0.0f, -400.0f, 0.0f);
    REQUIRE(world.SetVelocity(bullet, v) == BodyStatus::Ok);
    for (u32 i = 0; i < 60; ++i)
    {
        world.Step();
    }
    const Body* body = world.Find(bullet);
    REQUIRE(body != nullptr);
    CHECK(body->position.y > field->HeightAt(body->position.x, body->position.z) - 0.05f);
}

TEST_CASE("Height field character: walks up a ramp, stops at the slope limit, crosses a diagonal smoothly",
          "[physics][heightfield][character]")
{
    const f32 dt = 1.0f / 60.0f;
    {
        // A gentle ramp: the character climbs along it and stays on the ground.
        PhysicsWorld world;
        const HeightFieldConstPtr ramp = TiltedField(0.3f);
        AddField(world, ramp);
        CharacterController walker;
        REQUIRE(walker.Create(&world, CharacterConfig(), Vec3(-15.0f, ramp->HeightAt(-15.0f, 0.0f) + 1.0f, 0.0f)) ==
                BodyStatus::Ok);
        for (u32 i = 0; i < 180; ++i)
        {
            walker.Move(&world, Vec3(3.0f, 0, 0), dt);
            world.Step();
        }
        const Body* body = world.Find(walker.BodyHandle());
        REQUIRE(body != nullptr);
        CHECK(body->position.x > -15.0f + 7.0f);
        CHECK(walker.State().on_ground);
        // The capsule's centre sits half its height above the surface (to the slope's cosine).
        CHECK(FNear(body->position.y - ramp->HeightAt(body->position.x, 0.0f), 0.9f, 0.25f));
    }
    {
        // A ramp steeper than the limit (a normal.y of 0.64 is about 50 degrees): it does not climb it.
        PhysicsWorld world;
        const HeightFieldConstPtr wall = TiltedField(1.8f);  // 61 degrees
        AddField(world, wall);
        CharacterController walker;
        REQUIRE(walker.Create(&world, CharacterConfig(), Vec3(-10.0f, wall->HeightAt(-10.0f, 0.0f) + 1.0f, 0.0f)) ==
                BodyStatus::Ok);
        for (u32 i = 0; i < 240; ++i)
        {
            walker.Move(&world, Vec3(3.0f, 0, 0), dt);
            world.Step();
        }
        const Body* body = world.Find(walker.BodyHandle());
        REQUIRE(body != nullptr);
        CHECK(body->position.x < -10.0f + 3.0f);
    }
    {
        // Across the diagonals of the cells: the height changes by the same amount every step, no hop.
        PhysicsWorld world;
        const HeightFieldConstPtr ramp = TiltedField(0.25f);
        AddField(world, ramp);
        CharacterController walker;
        REQUIRE(walker.Create(&world, CharacterConfig(), Vec3(-12.0f, ramp->HeightAt(-12.0f, -5.0f) + 1.0f, -5.0f)) ==
                BodyStatus::Ok);
        for (u32 i = 0; i < 60; ++i)  // settle onto the ground first
        {
            walker.Move(&world, Vec3(0, 0, 0), dt);
            world.Step();
        }
        f32 previous = world.Find(walker.BodyHandle())->position.y;
        f32 lowest = 1e9f;
        f32 highest = -1e9f;
        for (u32 i = 0; i < 180; ++i)
        {
            walker.Move(&world, Vec3(2.0f, 0, 2.0f), dt);
            world.Step();
            const f32 y = world.Find(walker.BodyHandle())->position.y;
            lowest = Min(lowest, y - previous);
            highest = Max(highest, y - previous);
            previous = y;
        }
        CHECK(lowest > -0.005f);
        CHECK(highest < 0.02f);
        CHECK(highest - lowest < 0.01f);
    }
}

TEST_CASE("Height field debug draw: the cells under a body are drawn", "[physics][heightfield][debug]")
{
    PhysicsWorld world;
    const HeightFieldConstPtr field = TiltedField(0.1f);
    AddField(world, field);
    AddDynamic(world, Shape::MakeSphere(0.5f), Vec3(2.0f, 1.0f, 2.0f));
    Vector<spatial::DebugLine> without;
    DebugDrawOptions options;
    options.field_cells = false;
    const u32 base = DebugDraw(world, options, &without);
    Vector<spatial::DebugLine> with;
    options.field_cells = true;
    const u32 more = DebugDraw(world, options, &with);
    CHECK(more > base);
    CHECK(more - base <= 3 * 64);
    // No dynamic body, no cells.
    PhysicsWorld empty;
    AddField(empty, field);
    Vector<spatial::DebugLine> none;
    CHECK(DebugDraw(empty, options, &none) == DebugDraw(empty, DebugDrawOptions(), &none) - 0);
}

TEST_CASE("Terrain queries: the ground under a point, the ray at the terrain only", "[physics][heightfield][query]")
{
    PhysicsWorld world;
    const HeightFieldConstPtr field = TiltedField(0.3f);  // 0.3 m up for each metre along x, centred on the body
    BodyDesc desc;
    desc.settings.type = BodyType::Static;
    desc.position = Vec3(20.0f, 0.0f, 10.0f);  // the map covers x 0 to 40 and z 0 to 20 of the world
    const BodyId id = world.CreateBody(desc);
    Collider collider;
    collider.shape = Shape::MakeHeightField(world.HeightFields().Add(field), field->Extent());
    REQUIRE(world.AddCollider(id, collider) == BodyStatus::Ok);
    // A box above the ground that an ordinary ray hits first.
    BodyDesc box_desc;
    box_desc.settings.type = BodyType::Static;
    box_desc.position = Vec3(25.0f, 15.0f, 10.0f);
    const BodyId box = world.CreateBody(box_desc);
    Collider box_collider;
    box_collider.shape = Shape::MakeBox(Vec3(1.0f, 1.0f, 1.0f));
    REQUIRE(world.AddCollider(box, box_collider) == BodyStatus::Ok);

    const PhysicsQueryFilter filter;
    f64 height = 0.0;
    Vec3 normal;
    REQUIRE(TerrainHeight(world, 25.0f, 10.0f, filter, &height, &normal));
    CHECK(FNear(static_cast<f32>(height), 7.5f, 1e-3f));
    CHECK(normal.y > 0.9f);
    CHECK(FNear(normal.x, -0.3f / Sqrt(1.09f), 1e-3f));
    CHECK(FNear(static_cast<f32>(height), field->HeightAtMap(25.0f, 10.0f),
                0.0f));  // the surface the renderer draws, bit for bit
    CHECK_FALSE(TerrainHeight(world, 41.0f, 10.0f, filter, &height, &normal));  // past the map's east edge
    CHECK_FALSE(TerrainHeight(world, 25.0f, -1.0f, filter, &height, &normal));
    CHECK_FALSE(TerrainHeight(world, nanf(""), 10.0f, filter, &height, &normal));

    PhysicsHit hit;
    REQUIRE(RayCast(world, Vec3(25.0f, 30.0f, 10.0f), Vec3(0.0f, -1.0f, 0.0f), 100.0f, filter, &hit));
    CHECK(hit.body.index == box.index);  // the ordinary ray stops at the box
    REQUIRE(TerrainRayCast(world, Vec3(25.0f, 30.0f, 10.0f), Vec3(0.0f, -1.0f, 0.0f), 100.0f, filter, &hit));
    CHECK(hit.body.index == id.index);
    CHECK(FNear(hit.distance, 30.0f - 7.5f, 1e-3f));
    CHECK(hit.normal.y > 0.9f);
    // From below the surface the ray hits it from below, and a short ray finds nothing.
    REQUIRE(TerrainRayCast(world, Vec3(25.0f, -5.0f, 10.0f), Vec3(0.0f, 1.0f, 0.0f), 100.0f, filter, &hit));
    CHECK(FNear(hit.distance, 12.5f, 1e-3f));
    CHECK_FALSE(TerrainRayCast(world, Vec3(25.0f, 30.0f, 10.0f), Vec3(0.0f, -1.0f, 0.0f), 10.0f, filter, &hit));
    CHECK_FALSE(TerrainRayCast(world, Vec3(25.0f, 30.0f, 10.0f), Vec3(0.0f, 0.0f, 0.0f), 100.0f, filter, &hit));
}
