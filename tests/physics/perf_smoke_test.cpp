// A small version of the physics benchmark (Phase 25 Day 400) with a generous bound: a gross regression (a scan that
// grows with the square of the body count, a solver that stops converging) fails here; the real numbers come from
// `physics_bench` (docs/guide/physics.md, "Cost"). The bound is far above what any build, sanitizers included, takes.
#include <catch2/catch_test_macros.hpp>

#include "os/os.h"
#include "physics/world.h"

using namespace engine_placeholder;
using namespace engine_placeholder::physics;

TEST_CASE("Perf smoke: 500 bodies falling and landing step in bounded time", "[physics][perf]")
{
    PhysicsWorld world;
    BodyDesc floor_desc;
    floor_desc.settings.type = BodyType::Static;
    floor_desc.position = Vec3(0, -0.5f, 0);
    const BodyId floor = world.CreateBody(floor_desc);
    Collider floor_collider;
    floor_collider.shape = Shape::MakeBox(Vec3(40, 0.5f, 40));
    REQUIRE(world.AddCollider(floor, floor_collider) == BodyStatus::Ok);
    for (u32 i = 0; i < 500; ++i)
    {
        BodyDesc desc;
        desc.position = Vec3(-20.0f + 1.2f * static_cast<f32>(i % 30), 3.0f + 1.2f * static_cast<f32>(i / 900),
                             -20.0f + 1.2f * static_cast<f32>((i / 30) % 30));
        const BodyId id = world.CreateBody(desc);
        Collider c;
        c.shape = i % 2 == 0 ? Shape::MakeSphere(0.5f) : Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f));
        REQUIRE(world.AddCollider(id, c) == BodyStatus::Ok);
    }
    const u64 start = os::MonotonicTimeNanos();
    for (u32 step = 0; step < 120; ++step)
    {
        world.Step();
    }
    const double per_step_ms = static_cast<double>(os::MonotonicTimeNanos() - start) / 1.0e6 / 120.0;
    INFO("per step: " << per_step_ms << " ms");
    REQUIRE(per_step_ms < 150.0);
    const StepStats stats = world.GetStepStats();
    REQUIRE(stats.broad_ns + stats.narrow_ns + stats.solve_ns + stats.integrate_ns + stats.after_ns > 0);
}
