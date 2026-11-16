// The character controller (Phase 25 Day 392): flat ground, slopes, steps, a crease, platforms, pushing and the frame
// rate.
#include <catch2/catch_test_macros.hpp>

#include "algorithm.h"
#include "lang/scalar.h"
#include "physics/character.h"

using namespace engine_placeholder;
using namespace engine_placeholder::physics;

namespace
{

BodyId ChStatic(PhysicsWorld& world, const Vec3& position, const Vec3& half,
                const Quaternion& rotation = Quaternion::Identity())
{
    BodyDesc desc;
    desc.settings.type = BodyType::Static;
    desc.position = position;
    desc.orientation = rotation;
    const BodyId id = world.CreateBody(desc);
    Collider c;
    c.shape = Shape::MakeBox(half);
    REQUIRE(world.AddCollider(id, c) == BodyStatus::Ok);
    return id;
}

void ChFloor(PhysicsWorld& world)
{
    ChStatic(world, Vec3(0, -0.5f, 0), Vec3(50, 0.5f, 50));
}

struct Walker
{
    PhysicsWorld world;
    CharacterController controller;
    CharacterEvents last;
    u32 began = 0;
    u32 ended = 0;

    explicit Walker(const Vec3& at, const CharacterConfig& config = CharacterConfig())
    {
        REQUIRE(controller.Create(&world, config, at) == BodyStatus::Ok);
    }
    void Run(const Vec3& velocity, u32 steps)
    {
        const f32 dt = static_cast<f32>(1.0 / static_cast<f64>(world.GetStepConfig().step_hz));
        for (u32 i = 0; i < steps; ++i)
        {
            last = controller.Move(&world, velocity, dt);
            began += last.began_ground ? 1 : 0;
            ended += last.ended_ground ? 1 : 0;
            world.Step();
        }
    }
    Vec3 Position() const { return world.Find(controller.BodyHandle())->position; }
};

}  // namespace

TEST_CASE("Character: walking on flat ground keeps the height and the speed", "[physics][character]")
{
    Walker w(Vec3(0, 0.95f, 0));
    ChFloor(w.world);
    w.Run(Vec3(0, 0, 0), 20);
    CHECK(w.controller.State().on_ground);
    const Vec3 start = w.Position();
    CHECK(start.y > 0.88f);
    CHECK(start.y < 0.95f);
    w.Run(Vec3(2, 0, 0), 60);
    const Vec3 p = w.Position();
    CHECK(Abs(p.x - start.x - 2.0f) < 0.05f);
    CHECK(Abs(p.y - start.y) < 0.01f);
    CHECK(w.controller.State().on_ground);
    CHECK(w.controller.State().ground_normal.y > 0.99f);
}

TEST_CASE("Character: falling lands once, jumping leaves the ground", "[physics][character]")
{
    Walker w(Vec3(0, 2.0f, 0));
    ChFloor(w.world);
    w.Run(Vec3(0, 0, 0), 60);
    CHECK(w.controller.State().on_ground);
    CHECK(w.began == 1);
    CHECK(w.Position().y < 1.0f);
    const f32 rest = w.Position().y;
    w.controller.Jump(5.0f);
    w.Run(Vec3(0, 0, 0), 20);
    CHECK(w.Position().y > rest + 0.5f);
    CHECK(w.ended == 1);
    w.Run(Vec3(0, 0, 0), 120);
    CHECK(w.controller.State().on_ground);
    CHECK(Abs(w.Position().y - rest) < 0.02f);
    CHECK(w.began == 2);
}

TEST_CASE("Character: a gentle ramp is climbed and a steep one is a wall", "[physics][character]")
{
    for (const f32 degrees : {30.0f, 65.0f})
    {
        const f32 angle = degrees * 0.0174532925f;
        // The ramp's surface passes through the origin, rising toward +x.
        const Quaternion turn = Quaternion::FromAxisAngle(Vec3(0, 0, 1), angle);
        const f32 s = Sin(angle);
        const f32 c = Cos(angle);
        Walker w(Vec3(0, 1.2f, 0));
        ChStatic(w.world, Vec3(0.5f * s, -0.5f * c, 0), Vec3(15, 0.5f, 5), turn);
        w.Run(Vec3(0, 0, 0), 40);
        const Vec3 start = w.Position();
        w.Run(Vec3(2, 0, 0), 90);
        const Vec3 p = w.Position();
        if (degrees < 45.0f)
        {
            CHECK(p.y > start.y + 0.5f);
            CHECK(w.controller.State().on_ground);
        }
        else
        {
            CHECK(p.y < start.y + 0.3f);
        }
    }
}

TEST_CASE("Character: stairs of 15 cm are climbed with a 20 cm step height and not with 10", "[physics][character]")
{
    for (const f32 step : {0.2f, 0.1f})
    {
        CharacterConfig config;
        config.step_height = step;
        config.min_ground_y = 0.8f;  // a 15 cm edge under a round capsule is then a wall, not a slope
        Walker w(Vec3(0, 0.95f, 0), config);
        ChFloor(w.world);
        for (u32 i = 0; i < 5; ++i)
        {
            const f32 top = 0.15f * static_cast<f32>(i + 1);
            ChStatic(w.world, Vec3(2.0f + 0.5f * static_cast<f32>(i), 0.5f * top, 0), Vec3(0.25f, 0.5f * top, 3));
        }
        w.Run(Vec3(0, 0, 0), 20);
        f32 highest = 0.0f;
        for (u32 i = 0; i < 240; ++i)
        {
            w.Run(Vec3(1.5f, 0, 0), 1);
            highest = Max(highest, w.Position().y);
        }
        const Vec3 p = w.Position();
        if (step > 0.15f)
        {
            CHECK(p.x > 4.0f);
            CHECK(highest > 0.9f + 0.6f);
        }
        else
        {
            CHECK(p.x < 2.0f);
        }
    }
}

TEST_CASE("Character: a rejected step leaves the wall normal of the horizontal move", "[physics][character]")
{
    Walker w(Vec3(0, 0.95f, 0));
    ChFloor(w.world);
    ChStatic(w.world, Vec3(1.5f, 2.0f, 0), Vec3(0.25f, 2.0f, 3));  // a 4 m wall
    w.Run(Vec3(0, 0, 0), 20);
    w.Run(Vec3(3.0f, 0, 0), 120);
    CHECK(w.last.wall_hits > 0);
    CHECK(w.last.wall_normal.x < -0.99f);
}

TEST_CASE("Character: a character that is not moved stops", "[physics][character]")
{
    Walker w(Vec3(0, 0.95f, 0));
    ChFloor(w.world);
    w.Run(Vec3(0, 0, 0), 20);
    w.Run(Vec3(2.0f, 0, 0), 10);
    w.controller.Stop(&w.world);
    for (u32 i = 0; i < 3; ++i)
    {
        w.world.Step();
    }
    const f32 x = w.Position().x;
    for (u32 i = 0; i < 60; ++i)
    {
        w.world.Step();
    }
    CHECK(w.Position().x == x);
}

TEST_CASE("Character: a second Create does not leak the first body", "[physics][character]")
{
    Walker w(Vec3(0, 0.95f, 0));
    const u32 before = w.world.Stats().bodies;
    CHECK(w.controller.Create(&w.world, CharacterConfig(), Vec3(5, 0.95f, 0)) == BodyStatus::Ok);
    CHECK(w.world.Stats().bodies == before);
}

TEST_CASE("Character: a crease corner holds the capsule without jitter", "[physics][character]")
{
    Walker w(Vec3(0, 0.95f, 0));
    ChFloor(w.world);
    ChStatic(w.world, Vec3(4.5f, 3, 0), Vec3(0.5f, 3, 10));  // a wall at x = 4
    ChStatic(w.world, Vec3(0, 3, 4.5f), Vec3(10, 3, 0.5f));  // a wall at z = 4
    w.Run(Vec3(0, 0, 0), 20);
    w.Run(Vec3(2, 0, 2), 180);
    const Vec3 a = w.Position();
    CHECK(a.x < 4.0f - 0.4f + 0.05f);
    CHECK(a.z < 4.0f - 0.4f + 0.05f);
    CHECK(a.x > 3.0f);
    CHECK(a.z > 3.0f);
    w.Run(Vec3(2, 0, 2), 30);
    const Vec3 b = w.Position();
    CHECK(Abs(b.x - a.x) < 0.01f);
    CHECK(Abs(b.z - a.z) < 0.01f);
    CHECK(w.last.wall_hits > 0);
}

TEST_CASE("Character: a lift and a conveyor carry the character", "[physics][character]")
{
    for (const bool lift : {true, false})
    {
        Walker w(Vec3(0, 0.95f, 0));
        BodyDesc desc;
        desc.settings.type = BodyType::Kinematic;
        desc.position = Vec3(0, -0.5f, 0);
        const BodyId platform = w.world.CreateBody(desc);
        Collider c;
        c.shape = Shape::MakeBox(Vec3(30, 0.5f, 30));
        REQUIRE(w.world.AddCollider(platform, c) == BodyStatus::Ok);
        w.Run(Vec3(0, 0, 0), 20);
        const Vec3 start = w.Position();
        Velocity v;
        v.linear = lift ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
        REQUIRE(w.world.SetVelocity(platform, v) == BodyStatus::Ok);
        w.Run(Vec3(0, 0, 0), 60);
        const Vec3 p = w.Position();
        if (lift)
        {
            CHECK(Abs(p.y - start.y - 1.0f) < 0.08f);
            CHECK(Abs(p.x - start.x) < 0.05f);
        }
        else
        {
            CHECK(Abs(p.x - start.x - 1.0f) < 0.1f);
            CHECK(Abs(p.y - start.y) < 0.05f);
        }
        CHECK(w.controller.State().on_ground);
    }
}

TEST_CASE("Character: it pushes a crate and a crate does not push it", "[physics][character]")
{
    Walker w(Vec3(0, 0.95f, 0));
    ChFloor(w.world);
    BodyDesc desc;
    desc.position = Vec3(2, 0.5f, 0);
    desc.settings.mass = 10.0f;
    const BodyId crate = w.world.CreateBody(desc);
    Collider c;
    c.shape = Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f));
    REQUIRE(w.world.AddCollider(crate, c) == BodyStatus::Ok);
    w.Run(Vec3(0, 0, 0), 20);
    w.Run(Vec3(1.0f, 0, 0), 180);
    CHECK(w.world.Find(crate)->position.x > 3.0f);  // pushed along
    CHECK(w.Position().x > 2.0f);

    // A crate thrown at a standing character.
    Walker still(Vec3(0, 0.95f, 0));
    ChFloor(still.world);
    BodyDesc thrown;
    thrown.position = Vec3(3, 0.5f, 0);
    thrown.velocity.linear = Vec3(-6, 0, 0);
    thrown.settings.mass = 10.0f;
    const BodyId missile = still.world.CreateBody(thrown);
    REQUIRE(still.world.AddCollider(missile, c) == BodyStatus::Ok);
    still.Run(Vec3(0, 0, 0), 20);
    const Vec3 before = still.Position();
    still.Run(Vec3(0, 0, 0), 120);
    CHECK(Abs(still.Position().x - before.x) < 1e-3f);
    CHECK(still.world.Find(missile)->position.x > before.x);  // it did not pass through, it stopped or bounced back
}

TEST_CASE("Character: the walk does not depend on the frame rate", "[physics][character]")
{
    f32 end[2] = {0, 0};
    for (u32 run = 0; run < 2; ++run)
    {
        Walker w(Vec3(0, 0.95f, 0));
        ChFloor(w.world);
        StepConfig config = w.world.GetStepConfig();
        config.step_hz = run == 0 ? 60.0f : 120.0f;
        REQUIRE(w.world.SetStepConfig(config) == BodyStatus::Ok);
        w.Run(Vec3(0, 0, 0), run == 0 ? 20 : 40);
        const f32 start = w.Position().x;
        w.Run(Vec3(2, 0, 0), run == 0 ? 60 : 120);
        end[run] = w.Position().x - start;
    }
    CHECK(Abs(end[0] - 2.0f) < 0.05f);
    CHECK(Abs(end[1] - 2.0f) < 0.05f);
    CHECK(Abs(end[0] - end[1]) < 0.02f);
}

TEST_CASE("Character: a bad config is refused and a stale body moves nothing", "[physics][character]")
{
    PhysicsWorld world;
    CharacterController c;
    CharacterConfig bad;
    bad.height = 0.5f;  // less than twice the radius
    CHECK(c.Create(&world, bad, Vec3(0, 1, 0)) == BodyStatus::Invalid);
    CHECK(c.BodyHandle().IsValid() == false);
    const CharacterEvents events = c.Move(&world, Vec3(1, 0, 0), 1.0f / 60.0f);
    CHECK(events.wall_hits == 0);
    REQUIRE(c.Create(&world, CharacterConfig(), Vec3(0, 1, 0)) == BodyStatus::Ok);
    c.Destroy(&world);
    CHECK(world.Stats().bodies == 0);
}
