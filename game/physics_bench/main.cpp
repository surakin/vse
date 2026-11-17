// physics_bench: the Phase 25 Day 412 benchmark of the physics module. Prints one line per measurement, with the step's
// parts (broad phase, narrow phase, solver, integration, the rest) from `StepStats`. Nothing is asserted by the
// measuring scenes: the numbers are recorded in plans/phase-25-physics/day-412-benchmarks-and-stress.md and
// docs/guide/physics.md ("Cost"). The `large` scene (Phase 25 Day 413) is the Release-only stress run: it checks the
// state and exits 1 when a check fails (ctest `physics_bench_large`, registered in optimized builds only).
//
//   physics_bench [--bodies N]   the body count of the drop scenes (default: 1000, 5000, 20000 and 50000)
//                 [--steps N]    timed steps per measurement (default 60)
//                 [--threads N]  the worker count (default: the machine's participant count; 1 runs on the caller)
//                 [--scene S]    drop, mesh, joints, rays, characters, large or all (default all; `all` leaves out
//                 `large`)
//                 [--long-steps N] the steps of the long run of `large` (default 1000000)
#include "containers/vector.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/number.h"
#include "debug/program_setup.h"
#include "lang/scalar.h"
#include "jobs/scheduler.h"
#include "mem/heap_allocator.h"
#include "os/command_line.h"
#include "os/console.h"
#include "os/os.h"
#include "physics/character.h"
#include "physics/query.h"
#include "physics/world.h"

using namespace engine_placeholder;
using namespace engine_placeholder::physics;

namespace
{

u32 g_steps = 60;
usize g_threads = 1;
jobs::Scheduler* g_scheduler = nullptr;

struct Rng
{
    u64 state = 0x2545f4914f6cdd1dull;
    f32 Next()
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<f32>((state >> 40) & 0xffffff) / 16777216.0f;
    }
};

BodyId Add(PhysicsWorld& world, BodyType type, const Shape& shape, const Vec3& at)
{
    BodyDesc desc;
    desc.settings.type = type;
    desc.position = at;
    const BodyId id = world.CreateBody(desc);
    Collider c;
    c.shape = shape;
    world.AddCollider(id, c);
    return id;
}

void Configure(PhysicsWorld& world)
{
    StepConfig config = world.GetStepConfig();
    config.threads = g_threads > 1 ? static_cast<u32>(g_threads) : 1;
    world.SetStepConfig(config);
    if (g_threads > 1)
    {
        world.SetScheduler(g_scheduler);
    }
}

double Ms(u64 ns, u32 steps)
{
    return steps == 0 ? 0.0 : static_cast<double>(ns) / 1.0e6 / static_cast<double>(steps);
}

// Steps `count` times and prints the time per step and its parts, measured by the difference of the cumulative
// counters.
void Measure(const char* label, PhysicsWorld& world, u32 count)
{
    const StepStats before = world.GetStepStats();
    const u64 start = os::MonotonicTimeNanos();
    for (u32 i = 0; i < count; ++i)
    {
        world.Step();
    }
    const u64 total = os::MonotonicTimeNanos() - start;
    const StepStats after = world.GetStepStats();
    const PhysicsStats stats = world.AllStats();
    os::PrintError(
        "%-26s bodies %6u  step %8.3f ms  broad %7.3f  narrow %7.3f  solve %7.3f  integrate %7.3f  after %7.3f  "
        "contacts %6u  asleep %6u\n",
        label, stats.world.bodies, Ms(total, count), Ms(after.broad_ns - before.broad_ns, count),
        Ms(after.narrow_ns - before.narrow_ns, count), Ms(after.solve_ns - before.solve_ns, count),
        Ms(after.integrate_ns - before.integrate_ns, count), Ms(after.after_ns - before.after_ns, count),
        stats.contacts, after.sleeping_bodies);
}

void BuildDrop(PhysicsWorld& world, u32 n)
{
    const f32 side = static_cast<f32>(n) / 40.0f;
    const f32 half = 30.0f;
    Add(world, BodyType::Static, Shape::MakeBox(Vec3(half, 0.5f, half)), Vec3(0, -0.5f, 0));
    Rng rng;
    for (u32 i = 0; i < n; ++i)
    {
        const u32 col = i % 40;
        const u32 row = (i / 40) % 40;
        const u32 layer = i / 1600;
        const f32 rx = rng.Next();
        const f32 rz = rng.Next();
        const Vec3 at(-24.0f + 1.2f * static_cast<f32>(col) + 0.1f * rx, 3.0f + 1.2f * static_cast<f32>(layer),
                      -24.0f + 1.2f * static_cast<f32>(row) + 0.1f * rz);
        (void)side;
        if (i % 3 == 0)
        {
            Add(world, BodyType::Dynamic, Shape::MakeSphere(0.5f), at);
        }
        else
        {
            Add(world, BodyType::Dynamic, Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f)), at);
        }
    }
}

void SceneDrop(u32 n)
{
    PhysicsWorld world;
    Configure(world);
    const u64 build = os::MonotonicTimeNanos();
    BuildDrop(world, n);
    os::PrintError("drop %u: built in %.1f ms\n", n, static_cast<double>(os::MonotonicTimeNanos() - build) / 1.0e6);
    for (u32 i = 0; i < 20; ++i)
    {
        world.Step();  // the bodies fall
    }
    Measure("drop falling", world, g_steps);
    for (u32 i = 0; i < 60; ++i)
    {
        world.Step();  // and land
    }
    Measure("drop landing", world, g_steps);
    u32 guard = 0;
    while (world.GetStepStats().sleeping_bodies < world.Stats().bodies - 1 && guard++ < 900)
    {
        world.Step();
    }
    Measure("drop asleep", world, g_steps);
}

void SceneMesh()
{
    const u32 nx = 1000;
    const u32 nz = 500;  // 1,000,000 triangles
    Vector<Vec3> v;
    v.Reserve((nx + 1) * (nz + 1));
    for (u32 j = 0; j <= nz; ++j)
    {
        for (u32 i = 0; i <= nx; ++i)
        {
            v.Push(Vec3(static_cast<f32>(i) * 0.2f - 100.0f, 0.05f * static_cast<f32>((i + j) % 2),
                        static_cast<f32>(j) * 0.2f - 50.0f));
        }
    }
    Vector<u32> idx;
    idx.Reserve(nx * nz * 6);
    for (u32 j = 0; j < nz; ++j)
    {
        for (u32 i = 0; i < nx; ++i)
        {
            const u32 a = j * (nx + 1) + i;
            const u32 d = a + (nx + 1);
            idx.Push(a);
            idx.Push(d);
            idx.Push(d + 1);
            idx.Push(a);
            idx.Push(d + 1);
            idx.Push(a + 1);
        }
    }
    TriangleMeshSpec spec;
    spec.positions = Span<const Vec3>(v.Data(), v.Size());
    spec.indices = Span<const u32>(idx.Data(), idx.Size());
    Validation r = Validation::Ok;
    const u64 start = os::MonotonicTimeNanos();
    TriangleMeshConstPtr mesh = TriangleMesh::Create(spec, &r);
    os::PrintError("mesh: %u triangles built in %.1f ms (%s)\n", nx * nz * 2,
                   static_cast<double>(os::MonotonicTimeNanos() - start) / 1.0e6,
                   r == Validation::Ok ? "ok" : "refused");
    if (r != Validation::Ok)
    {
        return;
    }
    PhysicsWorld world;
    Configure(world);
    const u32 id = world.Meshes().Add(mesh);
    Add(world, BodyType::Static, Shape::MakeMesh(id, mesh->Extent()), Vec3(0, 0, 0));
    Rng rng;
    for (u32 i = 0; i < 1000; ++i)
    {
        const f32 rx = rng.Next();
        const f32 rz = rng.Next();
        Add(world, BodyType::Dynamic, i % 2 == 0 ? Shape::MakeSphere(0.3f) : Shape::MakeBox(Vec3(0.3f, 0.3f, 0.3f)),
            Vec3(-60.0f + 120.0f * rx, 1.0f + 0.7f * static_cast<f32>(i % 10), -30.0f + 60.0f * rz));
    }
    for (u32 i = 0; i < 60; ++i)
    {
        world.Step();
    }
    Measure("mesh 1M tris, 1000 bodies", world, g_steps);
}

void SceneJoints()
{
    const u32 chains[] = {50, 200, 500};
    for (u32 count : chains)
    {
        PhysicsWorld world;
        Configure(world);
        Add(world, BodyType::Static, Shape::MakeBox(Vec3(60, 0.5f, 60)), Vec3(0, -0.5f, 0));
        for (u32 c = 0; c < count; ++c)
        {
            const f32 x = -50.0f + 100.0f * static_cast<f32>(c) / static_cast<f32>(count);
            BodyId previous = Add(world, BodyType::Static, Shape::MakeBox(Vec3(0.1f, 0.1f, 0.1f)), Vec3(x, 12, 0));
            for (u32 k = 0; k < 10; ++k)
            {
                const BodyId link = Add(world, BodyType::Dynamic, Shape::MakeBox(Vec3(0.1f, 0.4f, 0.1f)),
                                        Vec3(x, 11.5f - static_cast<f32>(k), 0));
                world.CreateJoint(world.MakeBallJoint(previous, link, Vec3(x, 12.0f - static_cast<f32>(k), 0)));
                previous = link;
            }
        }
        for (u32 i = 0; i < 30; ++i)
        {
            world.Step();
        }
        char label[48];
        FormatTo(label, sizeof(label), "joints: %u chains of 10", count);
        Measure(label, world, g_steps);
    }
}

void SceneRays()
{
    PhysicsWorld world;
    Configure(world);
    BuildDrop(world, 5000);
    for (u32 i = 0; i < 90; ++i)
    {
        world.Step();
    }
    Rng rng;
    const u32 rays = 1000000;
    u32 hits = 0;
    PhysicsQueryFilter filter;
    const u64 start = os::MonotonicTimeNanos();
    for (u32 i = 0; i < rays; ++i)
    {
        const f32 rx = rng.Next();
        const f32 rz = rng.Next();
        PhysicsHit hit;
        if (RayCast(world, Vec3(-25.0f + 50.0f * rx, 12.0f, -25.0f + 50.0f * rz), Vec3(0, -1, 0), 40.0f, filter, &hit))
        {
            ++hits;
        }
    }
    const double seconds = static_cast<double>(os::MonotonicTimeNanos() - start) / 1.0e9;
    os::PrintError("rays: %u rays against 5000 bodies in %.3f s = %.0f rays/s (%u hits)\n", rays, seconds,
                   static_cast<double>(rays) / seconds, hits);
}

void SceneCharacters()
{
    const u32 counts[] = {100, 500, 2000};
    for (u32 n : counts)
    {
        PhysicsWorld world;
        Configure(world);
        Add(world, BodyType::Static, Shape::MakeBox(Vec3(80, 0.5f, 80)), Vec3(0, -0.5f, 0));
        for (u32 i = 0; i < 40; ++i)
        {
            Add(world, BodyType::Static, Shape::MakeBox(Vec3(0.5f, 0.5f, 3.0f)),
                Vec3(-60.0f + 3.0f * static_cast<f32>(i), 0.5f, 0));
        }
        Vector<CharacterController> people;
        people.Resize(n);
        for (u32 i = 0; i < n; ++i)
        {
            people[i].Create(&world, CharacterConfig(),
                             Vec3(-60.0f + 120.0f * static_cast<f32>(i) / static_cast<f32>(n), 1.0f,
                                  -20.0f + static_cast<f32>(i % 40)));
        }
        const f32 dt = 1.0f / 60.0f;
        for (u32 i = 0; i < 30; ++i)
        {
            for (u32 k = 0; k < n; ++k)
            {
                people[k].Move(&world, Vec3(1.5f, 0, 0), dt);
            }
            world.Step();
        }
        const u64 start = os::MonotonicTimeNanos();
        for (u32 i = 0; i < g_steps; ++i)
        {
            for (u32 k = 0; k < n; ++k)
            {
                people[k].Move(&world, Vec3(1.5f, 0, 0), dt);
            }
            world.Step();
        }
        os::PrintError("characters %5u: move + step %8.3f ms per step\n", n,
                       static_cast<double>(os::MonotonicTimeNanos() - start) / 1.0e6 / static_cast<double>(g_steps));
    }
}

// Whether every body of `bodies` is finite, inside `bound` of the origin and has a unit orientation, and the world's
// state hash is valid.
bool Sane(const PhysicsWorld& world, const Vector<BodyId>& bodies, f32 bound)
{
    for (usize i = 0; i < bodies.Size(); ++i)
    {
        const Body* b = world.Find(bodies[i]);
        if (b == nullptr || !IsFinite(b->position.x) || !IsFinite(b->position.y) || !IsFinite(b->position.z) ||
            !IsFinite(b->linear_velocity.x) || !IsFinite(b->linear_velocity.y) || !IsFinite(b->linear_velocity.z) ||
            Abs(b->position.x) > bound || Abs(b->position.y) > bound || Abs(b->position.z) > bound)
        {
            return false;
        }
        const Quaternion& q = b->orientation;
        if (Abs(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w - 1.0f) > 1e-3f)
        {
            return false;
        }
    }
    bool valid = false;
    world.StateHash(&valid);
    return valid;
}

// The Release-only stress run: `n` bodies fall on a floor between walls and pile up, then a small world is stepped
// `long_steps` times with a body kicked now and then, so it never sleeps for good. Both check the state as they go
// (finite, inside the walls, unit orientations, no collider slots or bodies appearing from nowhere). Returns whether
// every check held.
bool SceneLarge(u32 n, u32 long_steps)
{
    bool ok = true;
    {
        PhysicsWorld world;
        Configure(world);
        const f32 half = 30.0f;
        Add(world, BodyType::Static, Shape::MakeBox(Vec3(half + 2.0f, 0.5f, half + 2.0f)), Vec3(0, -0.5f, 0));
        Add(world, BodyType::Static, Shape::MakeBox(Vec3(0.5f, 40, half)), Vec3(half + 0.5f, 20, 0));
        Add(world, BodyType::Static, Shape::MakeBox(Vec3(0.5f, 40, half)), Vec3(-half - 0.5f, 20, 0));
        Add(world, BodyType::Static, Shape::MakeBox(Vec3(half, 40, 0.5f)), Vec3(0, 20, half + 0.5f));
        Add(world, BodyType::Static, Shape::MakeBox(Vec3(half, 40, 0.5f)), Vec3(0, 20, -half - 0.5f));
        Vector<BodyId> bodies;
        Rng rng;
        for (u32 i = 0; i < n; ++i)
        {
            const Vec3 at(-24.0f + 1.2f * static_cast<f32>(i % 40) + 0.1f * rng.Next(),
                          3.0f + 1.2f * static_cast<f32>(i / 1600),
                          -24.0f + 1.2f * static_cast<f32>((i / 40) % 40) + 0.1f * rng.Next());
            bodies.Push(Add(world, BodyType::Dynamic,
                            i % 3 == 0 ? Shape::MakeSphere(0.5f) : Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f)), at));
        }
        const u32 slots = world.Stats().collider_slots;
        const u32 steps = g_steps < 300 ? 300 : g_steps;
        for (u32 step = 0; step < steps; ++step)
        {
            world.Step();
            if (step % 50 == 49 && !Sane(world, bodies, 200.0f))
            {
                os::PrintError("large: %u bodies insane at step %u\n", n, step);
                ok = false;
                break;
            }
        }
        if (world.Stats().bodies != n + 5 || world.Stats().collider_slots != slots)
        {
            os::PrintError("large: the body or collider count changed (%u bodies, %u slots)\n", world.Stats().bodies,
                           world.Stats().collider_slots);
            ok = false;
        }
        Measure("large piled", world, 20);
    }
    {
        PhysicsWorld world;
        Configure(world);
        const f32 half = 8.0f;
        Add(world, BodyType::Static, Shape::MakeBox(Vec3(half + 1.0f, 0.5f, half + 1.0f)), Vec3(0, -0.5f, 0));
        Add(world, BodyType::Static, Shape::MakeBox(Vec3(0.5f, 20, half)), Vec3(half + 0.5f, 10, 0));
        Add(world, BodyType::Static, Shape::MakeBox(Vec3(0.5f, 20, half)), Vec3(-half - 0.5f, 10, 0));
        Add(world, BodyType::Static, Shape::MakeBox(Vec3(half, 20, 0.5f)), Vec3(0, 10, half + 0.5f));
        Add(world, BodyType::Static, Shape::MakeBox(Vec3(half, 20, 0.5f)), Vec3(0, 10, -half - 0.5f));
        Vector<BodyId> bodies;
        Rng rng;
        for (u32 i = 0; i < 100; ++i)
        {
            const Vec3 at(-6.0f + 12.0f * rng.Next(), 1.0f + 8.0f * rng.Next(), -6.0f + 12.0f * rng.Next());
            bodies.Push(Add(world, BodyType::Dynamic,
                            i % 2 == 0 ? Shape::MakeSphere(0.4f) : Shape::MakeBox(Vec3(0.4f, 0.4f, 0.4f)), at));
        }
        const u32 slots = world.Stats().collider_slots;
        const u64 start = os::MonotonicTimeNanos();
        for (u32 step = 0; step < long_steps; ++step)
        {
            if (step % 500 == 0)
            {
                const u32 k = static_cast<u32>(rng.Next() * 99.0f);
                world.ApplyImpulse(bodies[k], Vec3(3.0f * (rng.Next() - 0.5f), 4.0f, 3.0f * (rng.Next() - 0.5f)),
                                   world.Find(bodies[k])->position);
            }
            world.Step();
            if (step % 50000 == 49999 && !Sane(world, bodies, 40.0f))
            {
                os::PrintError("large: the long run is insane at step %u\n", step);
                ok = false;
                break;
            }
        }
        if (!Sane(world, bodies, 40.0f) || world.Stats().bodies != 105 || world.Stats().collider_slots != slots)
        {
            os::PrintError("large: the long run ended insane or with changed counts\n");
            ok = false;
        }
        os::PrintError("large: %u steps of 100 bodies in %.1f s\n", long_steps,
                       static_cast<double>(os::MonotonicTimeNanos() - start) / 1.0e9);
    }
    os::PrintError("large: %s\n", ok ? "ok" : "FAILED");
    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    static const os::CommandLineOption kCliOptions[] = {
        {"bodies", true},           {"steps", true},        {"threads", true}, {"scene", true}, {"long-steps", true},
        debug::kCrashHandlerOption, debug::kCrashTestOption};
    const os::CommandLine<HeapAllocator> line(HeapAllocator::Default(), argc, argv,
                                              Span<const os::CommandLineOption>(kCliOptions, 7));
    if (!line.Ok() || line.ArgCount() != 0)
    {
        os::PrintError(
            "usage: %s [--bodies N] [--steps N] [--threads N] [--scene drop|mesh|joints|rays|characters|large|all] [--long-steps N]\n",
            line.ProgramName().Data());
        return 2;
    }
    u32 bodies = 0;
    usize threads = jobs::DefaultParticipantCount();
    const char* scene = "all";
    StringView value;
    if (line.TryGetOption("bodies", value))
    {
        bodies = static_cast<u32>(CStrToLong(value.Data()));
    }
    if (line.TryGetOption("steps", value))
    {
        g_steps = static_cast<u32>(CStrToLong(value.Data()));
    }
    if (line.TryGetOption("threads", value))
    {
        threads = static_cast<usize>(CStrToLong(value.Data()));
    }
    if (line.TryGetOption("scene", value))
    {
        scene = value.Data();
    }
    u32 long_steps = 1000000;
    if (line.TryGetOption("long-steps", value))
    {
        long_steps = static_cast<u32>(CStrToLong(value.Data()));
    }
    g_threads = threads == 0 ? 1 : threads;
    jobs::Scheduler::Config config;
    config.participants = g_threads;
    config.pin = false;
    jobs::Scheduler scheduler(config);
    g_scheduler = &scheduler;
    os::PrintError("physics_bench: %zu thread(s), %u timed steps\n", static_cast<size_t>(g_threads), g_steps);
    const bool all = CStrCompare(scene, "all") == 0;
    if (all || CStrCompare(scene, "drop") == 0)
    {
        const u32 sizes[] = {1000, 5000, 20000, 50000};
        if (bodies != 0)
        {
            SceneDrop(bodies);
        }
        else
        {
            for (u32 n : sizes)
            {
                SceneDrop(n);
            }
        }
    }
    if (all || CStrCompare(scene, "mesh") == 0)
    {
        SceneMesh();
    }
    if (all || CStrCompare(scene, "joints") == 0)
    {
        SceneJoints();
    }
    if (all || CStrCompare(scene, "rays") == 0)
    {
        SceneRays();
    }
    if (all || CStrCompare(scene, "characters") == 0)
    {
        SceneCharacters();
    }
    if (CStrCompare(scene, "large") == 0 && !SceneLarge(bodies != 0 ? bodies : 50000, long_steps))
    {
        return 1;
    }
    return 0;
}
