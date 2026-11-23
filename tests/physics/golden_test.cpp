// Golden state hashes (Phase 25 Day 407, the terrain scene Phase 28 Day 492): seven reference scenes run for 600 steps,
// the hash of the state taken every 100th step, compared with the values committed here. The same values must come out
// of every Linux configuration (compiler, unity, optimisation, sanitizer) and every thread count, and, on Days 410-411,
// out of Windows and macOS.
//
// To regenerate after a deliberate change of the step: run `physics_tests "[golden-print]"` and paste the table it
// prints.
#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "log/output.h"
#include "jobs/scheduler.h"
#include "lang/scalar.h"
#include "os/os.h"
#include "os/console.h"
#include "physics/character.h"
#include "physics/height_field.h"
#include "physics/world.h"
#include "test_hulls.h"

using namespace vse;
using namespace vse::physics;

namespace
{

constexpr u32 kSteps = 600;
constexpr u32 kEvery = 100;
constexpr u32 kHashes = kSteps / kEvery;

enum class GoldenScene : u32
{
    Stack,
    Pyramid,
    Pile,
    Bridge,
    Walker,
    Bullet,
    Terrain,
    Count
};

const char* SceneName(GoldenScene scene)
{
    switch (scene)
    {
    case GoldenScene::Stack: return "Stack";
    case GoldenScene::Pyramid: return "Pyramid";
    case GoldenScene::Pile: return "Pile";
    case GoldenScene::Bridge: return "Bridge";
    case GoldenScene::Walker: return "Walker";
    case GoldenScene::Bullet: return "Bullet";
    case GoldenScene::Terrain: return "Terrain";
    case GoldenScene::Count: break;
    }
    return "?";
}

BodyId Add(PhysicsWorld& world, BodyType type, const Shape& shape, const Vec3& at, f32 friction = 0.6f)
{
    BodyDesc desc;
    desc.settings.type = type;
    desc.position = at;
    const BodyId id = world.CreateBody(desc);
    Collider c;
    c.shape = shape;
    c.material.friction = friction;
    REQUIRE(world.AddCollider(id, c) == BodyStatus::Ok);
    return id;
}

void Ground(PhysicsWorld& world, f32 half = 30.0f)
{
    Add(world, BodyType::Static, Shape::MakeBox(Vec3(half, 0.5f, half)), Vec3(0, -0.5f, 0));
}

void BuildStack(PhysicsWorld& world)
{
    Ground(world);
    for (u32 i = 0; i < 10; ++i)
    {
        Add(world, BodyType::Dynamic, Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f)),
            Vec3(0.01f * static_cast<f32>(i % 3), 0.5f + static_cast<f32>(i), 0));
    }
}

void BuildPyramid(PhysicsWorld& world)
{
    Ground(world);
    for (u32 row = 0; row < 5; ++row)
    {
        for (u32 i = 0; i < 5 - row; ++i)
        {
            Add(world, BodyType::Dynamic, Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f)),
                Vec3(-2.0f + static_cast<f32>(row) * 0.5f + static_cast<f32>(i), 0.5f + static_cast<f32>(row), 0));
        }
    }
}

TriangleMeshConstPtr FloorMesh()
{
    const u32 n = 16;
    const f32 cell = 1.0f;
    Vector<Vec3> v;
    for (u32 j = 0; j <= n; ++j)
    {
        for (u32 i = 0; i <= n; ++i)
        {
            const f32 x = static_cast<f32>(i) * cell - 8.0f;
            const f32 z = static_cast<f32>(j) * cell - 8.0f;
            v.Push(Vec3(x, 0.15f * ((i + j) % 2 == 0 ? 1.0f : 0.0f), z));
        }
    }
    Vector<u32> idx;
    for (u32 j = 0; j < n; ++j)
    {
        for (u32 i = 0; i < n; ++i)
        {
            const u32 a = j * (n + 1) + i;
            const u32 b = a + 1;
            const u32 d = a + (n + 1);
            const u32 c = d + 1;
            idx.Push(a);
            idx.Push(d);
            idx.Push(c);
            idx.Push(a);
            idx.Push(c);
            idx.Push(b);
        }
    }
    TriangleMeshSpec spec;
    spec.positions = Span<const Vec3>(v.Data(), v.Size());
    spec.indices = Span<const u32>(idx.Data(), idx.Size());
    Validation r = Validation::Ok;
    TriangleMeshConstPtr mesh = TriangleMesh::Create(spec, &r);
    REQUIRE(r == Validation::Ok);
    return mesh;
}

struct Rng
{
    u64 state = 0x2545f4914f6cdd1dull;
    f32 Next()
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<f32>((state >> 40) & 0xffffff) / 16777216.0f;
    }
};

void BuildPile(PhysicsWorld& world)
{
    const TriangleMeshConstPtr mesh = FloorMesh();
    const u32 mesh_id = world.Meshes().Add(mesh);
    Add(world, BodyType::Static, Shape::MakeMesh(mesh_id, mesh->Extent()), Vec3(0, 0, 0));
    Add(world, BodyType::Static, Shape::MakeBox(Vec3(0.5f, 4, 8)), Vec3(8.5f, 2, 0));
    Add(world, BodyType::Static, Shape::MakeBox(Vec3(0.5f, 4, 8)), Vec3(-8.5f, 2, 0));
    Add(world, BodyType::Static, Shape::MakeBox(Vec3(8, 4, 0.5f)), Vec3(0, 2, 8.5f));
    Add(world, BodyType::Static, Shape::MakeBox(Vec3(8, 4, 0.5f)), Vec3(0, 2, -8.5f));
    const ConvexHullConstPtr hull = physics::testing::BoxHull(Vec3(0.3f, 0.2f, 0.4f));
    const u32 hull_id = world.Hulls().Add(hull);
    Rng rng;
    for (u32 i = 0; i < 300; ++i)
    {
        // The draws are made one by one: the order in which a call's arguments are evaluated is the compiler's choice.
        const f32 rx = rng.Next();
        const f32 ry = rng.Next();
        const f32 rz = rng.Next();
        const Vec3 at(-6.0f + 12.0f * rx, 1.0f + 0.6f * static_cast<f32>(i % 25) + ry, -6.0f + 12.0f * rz);
        switch (i % 4)
        {
        case 0: Add(world, BodyType::Dynamic, Shape::MakeSphere(0.25f + 0.15f * rng.Next()), at); break;
        case 1: Add(world, BodyType::Dynamic, Shape::MakeBox(Vec3(0.3f, 0.2f + 0.2f * rng.Next(), 0.3f)), at); break;
        case 2: Add(world, BodyType::Dynamic, Shape::MakeCapsule(0.25f, 0.2f), at); break;
        default: Add(world, BodyType::Dynamic, Shape::MakeHull(hull_id, hull->Extent()), at); break;
        }
    }
}

void BuildBridge(PhysicsWorld& world)
{
    const BodyId left = Add(world, BodyType::Static, Shape::MakeBox(Vec3(0.5f, 0.5f, 1)), Vec3(-0.5f, 5, 0));
    BodyId previous = left;
    const u32 planks = 12;
    for (u32 i = 0; i < planks; ++i)
    {
        const BodyId plank = Add(world, BodyType::Dynamic, Shape::MakeBox(Vec3(0.45f, 0.08f, 0.9f)),
                                 Vec3(0.5f + static_cast<f32>(i), 5, 0));
        world.CreateJoint(world.MakeHingeJoint(previous, plank, Vec3(static_cast<f32>(i), 5, 0), Vec3(0, 0, 1)));
        previous = plank;
    }
    const BodyId right = Add(world, BodyType::Static, Shape::MakeBox(Vec3(0.5f, 0.5f, 1)),
                             Vec3(static_cast<f32>(planks) + 0.5f, 5, 0));
    world.CreateJoint(world.MakeHingeJoint(previous, right, Vec3(static_cast<f32>(planks), 5, 0), Vec3(0, 0, 1)));
    for (u32 i = 0; i < 6; ++i)
    {
        Add(world, BodyType::Dynamic, Shape::MakeBox(Vec3(0.3f, 0.3f, 0.3f)),
            Vec3(2.0f + static_cast<f32>(i), 6.0f + static_cast<f32>(i) * 0.7f, 0.1f * static_cast<f32>(i)));
    }
    Ground(world);
}

void BuildBullet(PhysicsWorld& world)
{
    Ground(world);
    Add(world, BodyType::Static, Shape::MakeBox(Vec3(0.05f, 3, 3)), Vec3(10, 3, 0));
    const BodyId bullet = Add(world, BodyType::Dynamic, Shape::MakeSphere(0.05f), Vec3(0, 2, 0));
    Velocity v;
    v.linear = Vec3(250, 0, 0);
    REQUIRE(world.SetVelocity(bullet, v) == BodyStatus::Ok);
    for (u32 i = 0; i < 4; ++i)
    {
        Add(world, BodyType::Dynamic, Shape::MakeBox(Vec3(0.3f, 0.3f, 0.3f)),
            Vec3(11.0f, 0.3f + 0.6f * static_cast<f32>(i), 0));
    }
}

// A hundred bodies dropped onto a bumpy height field (Phase 28 Day 492): heights from an integer hash, so the field is
// the same on every machine.
void BuildTerrain(PhysicsWorld& world)
{
    Vector<u16> samples;
    for (u32 z = 0; z < 33; ++z)
    {
        for (u32 x = 0; x < 33; ++x)
        {
            u32 h = (x / 2) * 73856093u ^ (z / 2) * 19349663u;
            h ^= h >> 13;
            h *= 0x5bd1e995u;
            h ^= h >> 15;
            samples.Push(static_cast<u16>(h % 40000u));
        }
    }
    Validation v = Validation::Ok;
    const HeightFieldConstPtr field = HeightField::CreateFromSamples(Span<const u16>(samples.Data(), samples.Size()),
                                                                     33, 33, 0.0f, 2.0f, 1.0f, &v);
    REQUIRE(field);
    Add(world, BodyType::Static, Shape::MakeHeightField(world.HeightFields().Add(field), field->Extent()),
        Vec3(0, 0, 0));
    Rng rng;
    for (u32 i = 0; i < 100; ++i)
    {
        const f32 rx = rng.Next();
        const f32 ry = rng.Next();
        const f32 rz = rng.Next();
        const Vec3 at(-13.0f + 26.0f * rx, 3.0f + 0.5f * static_cast<f32>(i % 20) + ry, -13.0f + 26.0f * rz);
        switch (i % 3)
        {
        case 0: Add(world, BodyType::Dynamic, Shape::MakeSphere(0.25f + 0.1f * rng.Next()), at); break;
        case 1: Add(world, BodyType::Dynamic, Shape::MakeBox(Vec3(0.3f, 0.2f + 0.1f * rng.Next(), 0.3f)), at); break;
        default: Add(world, BodyType::Dynamic, Shape::MakeCapsule(0.25f, 0.2f), at); break;
        }
    }
}

// Runs a scene and returns the hashes after steps 100, 200, ... 600.
bool Run(GoldenScene scene, usize participants, u64 (&hashes)[kHashes])
{
    PhysicsWorld world;
    StepConfig config = world.GetStepConfig();
    config.threads = participants > 1 ? static_cast<u32>(participants) : 1;
    REQUIRE(world.SetStepConfig(config) == BodyStatus::Ok);
    CharacterController walker;
    switch (scene)
    {
    case GoldenScene::Stack: BuildStack(world); break;
    case GoldenScene::Pyramid: BuildPyramid(world); break;
    case GoldenScene::Pile: BuildPile(world); break;
    case GoldenScene::Bridge: BuildBridge(world); break;
    case GoldenScene::Bullet: BuildBullet(world); break;
    case GoldenScene::Terrain: BuildTerrain(world); break;
    case GoldenScene::Walker:
        Ground(world);
        for (u32 i = 0; i < 4; ++i)
        {
            Add(world, BodyType::Static, Shape::MakeBox(Vec3(2, 0.1f * static_cast<f32>(i + 1), 2)),
                Vec3(4.0f + 4.0f * static_cast<f32>(i), 0.1f * static_cast<f32>(i + 1), 0));
        }
        Add(world, BodyType::Dynamic, Shape::MakeBox(Vec3(0.4f, 0.4f, 0.4f)), Vec3(3, 0.4f, 0.5f));
        REQUIRE(walker.Create(&world, CharacterConfig(), Vec3(-5, 1, 0)) == BodyStatus::Ok);
        break;
    case GoldenScene::Count: break;
    }
    jobs::Scheduler::Config sc;
    sc.participants = participants > 0 ? participants : 1;
    sc.pin = false;
    jobs::Scheduler scheduler(sc);
    if (participants > 0)
    {
        world.SetScheduler(&scheduler);
    }
    const f32 dt = static_cast<f32>(1.0 / static_cast<f64>(world.GetStepConfig().step_hz));
    bool valid = true;
    for (u32 step = 1; step <= kSteps; ++step)
    {
        if (scene == GoldenScene::Walker)
        {
            const f32 turn = static_cast<f32>(step / 150 % 2);
            walker.Move(&world, Vec3(3.0f - 6.0f * turn, 0, 0), dt);
        }
        world.Step();
        if (step % kEvery == 0)
        {
            bool ok = false;
            hashes[step / kEvery - 1] = world.StateHash(&ok);
            valid = valid && ok;
        }
    }
    return valid;
}

// The committed hashes.
struct Golden
{
    GoldenScene scene;
    u64 hashes[kHashes];
};

// GOLDEN-BEGIN
const Golden kGolden[] = {
    {GoldenScene::Stack,
     {0x5a13826aa5cc69f6ull, 0x5a13826aa5cc69f6ull, 0x5a13826aa5cc69f6ull, 0x5a13826aa5cc69f6ull, 0x5a13826aa5cc69f6ull,
      0x5a13826aa5cc69f6ull}},
    {GoldenScene::Pyramid,
     {0xa9d6ade5b87a6b0cull, 0xa9d6ade5b87a6b0cull, 0xa9d6ade5b87a6b0cull, 0xa9d6ade5b87a6b0cull, 0xa9d6ade5b87a6b0cull,
      0xa9d6ade5b87a6b0cull}},
    {GoldenScene::Pile,
     {0xe1837ed81df83595ull, 0x66fa83cb3c0d8cb5ull, 0x5404eec42f3e233dull, 0x443b77561917e38cull, 0x25d6028568d59dabull,
      0x3fad476321a17d38ull}},
    {GoldenScene::Bridge,
     {0xa0deeb96b290b7c5ull, 0x693eb77f883fe172ull, 0x4ffe87f40836c713ull, 0xa25190a78dbab985ull, 0xcafe3473336a6a76ull,
      0xddd5d5c688dab35full}},
    {GoldenScene::Walker,
     {0x5eb85818c24ef09dull, 0xf48727bd8d3d38acull, 0x965e16cf3b3af495ull, 0xcf71e0c42ceb2293ull, 0x81edbb577817f7efull,
      0x23cfcfdcf79ab543ull}},
    {GoldenScene::Bullet,
     {0xf32ac67e71d9d8d6ull, 0x71c057fd725e0876ull, 0x6fd9f285fb2f4438ull, 0x5d66f0fe40230e0full, 0xd39ed26d1454a5eeull,
      0x7afa3b741a404e8dull}},
    {GoldenScene::Terrain,
     {0x4df3f9a705311ba0ull, 0xbc80819d9471c5c7ull, 0xd47138999adb17ecull, 0x9b0d169b45148be8ull, 0x1ef91c1e5cd662b0ull,
      0x447cc3f9738ad539ull}},
};
// GOLDEN-END

}  // namespace

TEST_CASE("Golden: print the table (run by hand)", "[.][golden-print]")
{
    for (u32 s = 0; s < static_cast<u32>(GoldenScene::Count); ++s)
    {
        u64 h[kHashes] = {};
        REQUIRE(Run(static_cast<GoldenScene>(s), 0, h));
        out::Print("    {GoldenScene::%s, {", SceneName(static_cast<GoldenScene>(s)));
        for (u32 i = 0; i < kHashes; ++i)
        {
            out::Print("0x%016llxull%s", static_cast<unsigned long long>(h[i]), i + 1 < kHashes ? ", " : "");
        }
        out::Print("}},\n");
    }
}

TEST_CASE("Golden: every scene reproduces its committed hashes for every thread count", "[physics][golden]")
{
    const usize all_counts[] = {0, 1, 2, 8, 32};
    const usize few_counts[] = {0, 8};  // the pile is the slow scene
    REQUIRE(sizeof(kGolden) / sizeof(kGolden[0]) == static_cast<usize>(GoldenScene::Count));
    for (const Golden& golden : kGolden)
    {
        const bool pile = golden.scene == GoldenScene::Pile;
        for (usize k = 0; k < (pile ? 2u : 5u); ++k)
        {
            const usize n = pile ? few_counts[k] : all_counts[k];
            u64 h[kHashes] = {};
            REQUIRE(Run(golden.scene, n, h));
            for (u32 i = 0; i < kHashes; ++i)
            {
                INFO("scene " << SceneName(golden.scene) << " threads " << n << " at step " << (i + 1) * kEvery);
                CHECK(h[i] == golden.hashes[i]);
            }
        }
    }
}

// The cost per body (run by hand in a release tree): steps per second of N resting boxes, awake and asleep.
TEST_CASE("Golden: cost per body (run by hand)", "[.][golden-cost]")
{
    const u32 counts[] = {1000, 5000, 20000};
    for (const u32 n : counts)
    {
        for (u32 awake = 0; awake < 2; ++awake)
        {
            PhysicsWorld world;
            StepConfig config = world.GetStepConfig();
            config.allow_sleep = awake == 0;
            REQUIRE(world.SetStepConfig(config) == BodyStatus::Ok);
            u32 side = 1;
            while (side * side < n)
            {
                ++side;
            }
            const f32 half = 0.6f * static_cast<f32>(side) + 2.0f;
            Add(world, BodyType::Static, Shape::MakeBox(Vec3(half, 0.5f, half)), Vec3(0, -0.5f, 0));
            for (u32 i = 0; i < n; ++i)
            {
                Add(world, BodyType::Dynamic, Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f)),
                    Vec3(1.2f * static_cast<f32>(i % side) - 0.6f * static_cast<f32>(side), 0.52f,
                         1.2f * static_cast<f32>(i / side) - 0.6f * static_cast<f32>(side)));
            }
            for (u32 i = 0; i < 150; ++i)
            {
                world.Step();
            }
            const u64 begin = os::MonotonicTimeNanos();
            for (u32 i = 0; i < 60; ++i)
            {
                world.Step();
            }
            const f64 seconds = static_cast<f64>(os::MonotonicTimeNanos() - begin) * 1e-9;
            out::Print("cost: %u bodies %s: %.1f steps/s, %.2f us per body-step\n", n, awake != 0 ? "awake " : "asleep",
                       60.0 / seconds, seconds / 60.0 / static_cast<f64>(n) * 1e6);
        }
    }
}
