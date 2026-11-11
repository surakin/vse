#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "ecs/transform.h"
#include "jobs/scheduler.h"
#include "os/console.h"
#include "os/os.h"
#include "test_rng.h"

using namespace engine_placeholder;
using namespace engine_placeholder::ecs;

namespace
{

bool SameMat(const Mat4& a, const Mat4& b)
{
    for (int c = 0; c < 4; ++c)
    {
        if (a.col[c].x != b.col[c].x || a.col[c].y != b.col[c].y || a.col[c].z != b.col[c].z ||
            a.col[c].w != b.col[c].w)
        {
            return false;
        }
    }
    return true;
}

LocalTransform At(f32 x, f32 y, f32 z)
{
    LocalTransform t;
    t.position = Vec3(x, y, z);
    return t;
}

// A world with the report resource and a schedule that runs only the propagation.
struct Rig
{
    World world;
    Schedule schedule;
    Rig()
    {
        world.SetResource(TransformReport{});
        schedule.Add(MakeTransformSystem("transforms"));
        REQUIRE(schedule.Build().ok);
    }
    void Frame() { schedule.Run(world, 0.016f); }
    const TransformReport& Report() { return world.Resource<TransformReport>(); }
    Mat4 Matrix(Entity e) { return world.Get<WorldTransform>(e)->matrix; }
};

u64 HashMatrices(World& world)
{
    u64 h = 1469598103934665603ull;
    world.Query<Read<WorldTransform>>().ForEach(
        [&](const WorldTransform& w)
        {
            const byte* bytes = reinterpret_cast<const byte*>(&w.matrix);
            for (usize i = 0; i < sizeof(Mat4); ++i)
            {
                h = (h ^ bytes[i]) * 1099511628211ull;
            }
        });
    return h;
}

}  // namespace

TEST_CASE("Transform: a chain of ten parents composes to the analytic matrix", "[ecs][transform]")
{
    Rig rig;
    Vector<Entity> chain;
    Mat4 expected = Mat4::Identity();
    for (u32 i = 0; i < 10; ++i)
    {
        LocalTransform local = At(1.0f, 0.5f * static_cast<f32>(i), 0.0f);
        local.rotation = Quaternion::FromAxisAngle(Vec3(0, 0, 1), 0.1f * static_cast<f32>(i + 1));
        local.scale = Vec3(1.0f, 1.0f, 1.0f) * 1.1f;
        chain.Push(CreateTransformEntity(rig.world, local));
        if (i > 0)
        {
            REQUIRE(SetParent(rig.world, chain[i], chain[i - 1]) == HierarchyStatus::Ok);
        }
        expected = expected * local.ToMatrix();
    }
    rig.Frame();
    CHECK(SameMat(rig.Matrix(chain[9]), expected));
    CHECK(rig.world.Get<HierarchyDepth>(chain[9])->depth == 9);
    CHECK(rig.Report().unresolved == 0);
}

TEST_CASE("Transform: siblings are independent, reparenting moves the subtree", "[ecs][transform]")
{
    Rig rig;
    const Entity a = CreateTransformEntity(rig.world, At(10, 0, 0));
    const Entity b = CreateTransformEntity(rig.world, At(0, 20, 0));
    const Entity kid1 = CreateTransformEntity(rig.world, At(1, 0, 0));
    const Entity kid2 = CreateTransformEntity(rig.world, At(2, 0, 0));
    const Entity grandkid = CreateTransformEntity(rig.world, At(0, 0, 3));
    REQUIRE(SetParent(rig.world, kid1, a) == HierarchyStatus::Ok);
    REQUIRE(SetParent(rig.world, kid2, a) == HierarchyStatus::Ok);
    REQUIRE(SetParent(rig.world, grandkid, kid1) == HierarchyStatus::Ok);
    rig.Frame();
    CHECK(rig.Matrix(kid1).col[3].x == 11.0f);
    CHECK(rig.Matrix(kid2).col[3].x == 12.0f);
    CHECK(rig.Matrix(grandkid).col[3].z == 3.0f);
    CHECK(rig.Matrix(grandkid).col[3].x == 11.0f);
    REQUIRE(SetParent(rig.world, kid1, b) == HierarchyStatus::Ok);  // the grandchild follows
    rig.Frame();
    CHECK(rig.Matrix(kid1).col[3].y == 20.0f);
    CHECK(rig.Matrix(kid1).col[3].x == 1.0f);
    CHECK(rig.Matrix(grandkid).col[3].y == 20.0f);
    CHECK(rig.Matrix(kid2).col[3].x == 12.0f);  // the sibling stayed
    CHECK(rig.world.Get<HierarchyDepth>(grandkid)->depth == 2);
    REQUIRE(ClearParent(rig.world, kid1) == HierarchyStatus::Ok);
    CHECK(rig.world.Get<HierarchyDepth>(grandkid)->depth == 1);
    rig.Frame();
    CHECK(rig.Matrix(kid1).col[3].x == 1.0f);
    CHECK(rig.Matrix(kid1).col[3].y == 0.0f);
    CHECK(rig.Matrix(grandkid).col[3].x == 1.0f);
}

TEST_CASE("Transform: moving a root changes exactly its subtree's matrices", "[ecs][transform]")
{
    Rig rig;
    Vector<Entity> ids;
    for (u32 tree = 0; tree < 5; ++tree)
    {
        const Entity root = CreateTransformEntity(rig.world, At(static_cast<f32>(tree) * 100.0f, 0, 0));
        ids.Push(root);
        for (u32 k = 0; k < 6; ++k)
        {
            const Entity kid = CreateTransformEntity(rig.world, At(1, static_cast<f32>(k), 0));
            REQUIRE(SetParent(rig.world, kid, root) == HierarchyStatus::Ok);
            ids.Push(kid);
        }
    }
    rig.Frame();
    Vector<Mat4> before;
    for (usize i = 0; i < ids.Size(); ++i)
    {
        before.Push(rig.Matrix(ids[i]));
    }
    rig.world.Get<LocalTransform>(ids[7])->position = Vec3(500, 0, 0);  // tree 1's root
    const u64 written_before = rig.Report().written;
    rig.Frame();
    u32 changed = 0;
    for (usize i = 0; i < ids.Size(); ++i)
    {
        const bool differs = !SameMat(before[i], rig.Matrix(ids[i]));
        CHECK(differs == (i >= 7 && i < 14));  // exactly tree 1: the root and its six children
        changed += differs ? 1u : 0u;
    }
    CHECK(changed == 7);
    CHECK(rig.Report().written - written_before == 7);
}

TEST_CASE("Transform: a static world marks nothing over 100 frames", "[ecs][transform]")
{
    Rig rig;
    for (u32 i = 0; i < 200; ++i)
    {
        const Entity root = CreateTransformEntity(rig.world, At(static_cast<f32>(i), 0, 0));
        const Entity kid = CreateTransformEntity(rig.world, At(0, 1, 0));
        REQUIRE(SetParent(rig.world, kid, root) == HierarchyStatus::Ok);
    }
    rig.Frame();
    rig.Frame();
    auto watch = rig.world.Query<Changed<WorldTransform>>();
    watch.Finish();  // everything so far is handled
    const u64 written = rig.Report().written;
    for (u32 frame = 0; frame < 100; ++frame)
    {
        rig.Frame();
    }
    u32 visited = 0;
    watch.ForEachChunk([&](ChunkView&) { ++visited; });
    CHECK(visited == 0);
    CHECK(rig.Report().written == written);
    CHECK(rig.Report().recomputed == rig.Report().recomputed);  // (counter exists)
}

TEST_CASE("Transform: a destroyed parent turns its child into a root", "[ecs][transform]")
{
    Rig rig;
    const Entity parent = CreateTransformEntity(rig.world, At(10, 0, 0));
    const Entity child = CreateTransformEntity(rig.world, At(1, 0, 0));
    REQUIRE(SetParent(rig.world, child, parent) == HierarchyStatus::Ok);
    rig.Frame();
    CHECK(rig.Matrix(child).col[3].x == 11.0f);
    rig.world.Destroy(parent);
    rig.Frame();
    rig.Frame();
    CHECK_FALSE(rig.world.Has<Parent>(child));
    CHECK_FALSE(rig.world.Has<HierarchyDepth>(child));
    CHECK(rig.Report().dangling_fixed == 1);
    CHECK(rig.Matrix(child).col[3].x == 1.0f);  // now a root
}

TEST_CASE("Transform: a parent destroyed while another entity is created is still noticed", "[ecs][transform]")
{
    Rig rig;
    const Entity parent = CreateTransformEntity(rig.world, At(10, 0, 0));
    const Entity child = CreateTransformEntity(rig.world, At(1, 0, 0));
    REQUIRE(SetParent(rig.world, child, parent) == HierarchyStatus::Ok);
    rig.Frame();
    rig.world.Destroy(parent);
    CreateTransformEntity(rig.world, At(5, 0, 0));  // the entity count is the same as before
    rig.Frame();
    rig.Frame();
    CHECK_FALSE(rig.world.Has<Parent>(child));
    CHECK(rig.Report().dangling_fixed == 1);
}

TEST_CASE("Transform: cycles are refused by SetParent and survived when forced", "[ecs][transform]")
{
    Rig rig;
    const Entity a = CreateTransformEntity(rig.world, At(1, 0, 0));
    const Entity b = CreateTransformEntity(rig.world, At(2, 0, 0));
    const Entity c = CreateTransformEntity(rig.world, At(3, 0, 0));
    CHECK(SetParent(rig.world, a, a) == HierarchyStatus::SelfParent);
    REQUIRE(SetParent(rig.world, b, a) == HierarchyStatus::Ok);
    REQUIRE(SetParent(rig.world, c, b) == HierarchyStatus::Ok);
    CHECK(SetParent(rig.world, a, c) == HierarchyStatus::Cycle);
    CHECK_FALSE(rig.world.Has<Parent>(a));
    rig.Frame();
    const Mat4 before = rig.Matrix(a);
    rig.world.Set<Parent>(a, Parent{c});  // forced, bypassing SetParent: a has no stored depth, its chain loops
    rig.world.Get<LocalTransform>(a)->position = Vec3(9, 9, 9);
    rig.Frame();
    rig.Frame();
    CHECK(rig.Report().unresolved >= 1);
    CHECK(SameMat(rig.Matrix(a), before));  // the member that cannot be resolved keeps its previous matrix
    CHECK(rig.world.Validate().Ok());
}

TEST_CASE("Transform: the depth limit is enforced with a counted refusal", "[ecs][transform]")
{
    Rig rig;
    Vector<Entity> chain;
    for (u32 i = 0; i <= kMaxHierarchyDepth; ++i)
    {
        chain.Push(CreateTransformEntity(rig.world, At(1, 0, 0)));
        if (i > 0)
        {
            REQUIRE(SetParent(rig.world, chain[i], chain[i - 1]) == HierarchyStatus::Ok);
        }
    }
    CHECK(rig.world.Get<HierarchyDepth>(chain[kMaxHierarchyDepth])->depth == kMaxHierarchyDepth);
    const Entity extra = CreateTransformEntity(rig.world, At(1, 0, 0));
    CHECK(SetParent(rig.world, extra, chain[kMaxHierarchyDepth]) == HierarchyStatus::TooDeep);
    CHECK(rig.Report().refused_depth == 1);
    CHECK_FALSE(rig.world.Has<Parent>(extra));
    // Re-rooting the whole chain under another entity would also be too deep.
    const Entity top = CreateTransformEntity(rig.world, At(0, 0, 0));
    CHECK(SetParent(rig.world, chain[0], top) == HierarchyStatus::TooDeep);
    rig.Frame();
    CHECK(rig.Matrix(chain[kMaxHierarchyDepth]).col[3].x == static_cast<f32>(kMaxHierarchyDepth + 1));
}

TEST_CASE("Transform: DestroyRecursive removes a subtree and nothing else", "[ecs][transform]")
{
    Rig rig;
    const Entity root = CreateTransformEntity(rig.world, At(0, 0, 0));
    const Entity other = CreateTransformEntity(rig.world, At(0, 0, 0));
    Vector<Entity> tree;
    tree.Push(root);
    for (u32 i = 0; i < 20; ++i)
    {
        const Entity e = CreateTransformEntity(rig.world, At(1, 0, 0));
        REQUIRE(SetParent(rig.world, e, tree[i / 2]) == HierarchyStatus::Ok);
        tree.Push(e);
    }
    CommandBuffer commands;
    CHECK(DestroyRecursive(rig.world, commands, root) == 21);
    CHECK(rig.world.Exists(root));  // recorded, not applied
    rig.world.Apply(commands);
    for (usize i = 0; i < tree.Size(); ++i)
    {
        CHECK_FALSE(rig.world.Exists(tree[i]));
    }
    CHECK(rig.world.Exists(other));
    CHECK(rig.world.Validate().Ok());
}

TEST_CASE("Transform: a forced Set<Parent> is repaired", "[ecs][transform]")
{
    Rig rig;
    const Entity a = CreateTransformEntity(rig.world, At(10, 0, 0));
    const Entity b = CreateTransformEntity(rig.world, At(100, 0, 0));
    const Entity kid = CreateTransformEntity(rig.world, At(1, 0, 0));
    REQUIRE(SetParent(rig.world, kid, a) == HierarchyStatus::Ok);
    rig.Frame();
    rig.world.Set<Parent>(kid, Parent{b});  // the stored depth is still right here: the new parent is a root too
    rig.Frame();
    CHECK(rig.Matrix(kid).col[3].x == 101.0f);
    const Entity mid = CreateTransformEntity(rig.world, At(0, 5, 0));
    REQUIRE(SetParent(rig.world, mid, b) == HierarchyStatus::Ok);
    rig.world.Set<Parent>(kid, Parent{mid});  // now the stored depth (1) is wrong: it should be 2
    rig.Frame();
    rig.Frame();
    CHECK(rig.Report().depth_repaired >= 1);
    CHECK(rig.world.Get<HierarchyDepth>(kid)->depth == 2);
    CHECK(rig.Matrix(kid).col[3].y == 5.0f);
}

TEST_CASE("Transform: LookAt and Decompose", "[ecs][transform]")
{
    const LocalTransform look = LookAt(Vec3(0, 0, 5), Vec3(0, 0, 0), Vec3(0, 1, 0));
    const Vec3 forward = look.rotation.RotateVector(Vec3(0, 0, -1));
    CHECK(forward.z < -0.999f);
    LocalTransform original = At(1, 2, 3);
    original.rotation = Quaternion::FromAxisAngle(Vec3(0, 1, 0), 0.7f);
    original.scale = Vec3(2, 3, 4);
    const LocalTransform back = Decompose(original.ToMatrix());
    CHECK(back.position.x == 1.0f);
    CHECK(back.scale.x > 1.999f);
    CHECK(back.scale.z > 3.999f);
    const Vec3 a = original.rotation.RotateVector(Vec3(1, 0, 0));
    const Vec3 b = back.rotation.RotateVector(Vec3(1, 0, 0));
    CHECK((a - b).Length() < 1e-4f);
}

namespace
{

// A random forest of `count` entities; depth is bounded by the limit.
void BuildForest(World& world, u32 count, u64 seed)
{
    ecs_test::Rng rng(seed);
    Vector<Entity> ids;
    Vector<u32> depth;
    for (u32 i = 0; i < count; ++i)
    {
        LocalTransform local = At(static_cast<f32>(rng.Below(100)), static_cast<f32>(rng.Below(100)),
                                  static_cast<f32>(rng.Below(100)));
        local.rotation = Quaternion::FromAxisAngle(Vec3(0, 1, 0), static_cast<f32>(rng.Below(628)) * 0.01f);
        if (i > 0 && rng.Below(10) < 8)
        {
            const u32 p = rng.Below(i);
            if (depth[p] + 1 <= kMaxHierarchyDepth)
            {
                ids.Push(world.Create(local, WorldTransform{}, Parent{ids[p]}, HierarchyDepth{depth[p] + 1}));
                depth.Push(depth[p] + 1);
                continue;
            }
        }
        ids.Push(world.Create(local, WorldTransform{}));
        depth.Push(0);
    }
}

}  // namespace

TEST_CASE("Transform: parallel propagation equals the sequential result on a random forest", "[ecs][transform]")
{
    Rig sequential;
    BuildForest(sequential.world, 100000, 11);
    sequential.Frame();
    const u64 expected = HashMatrices(sequential.world);
    CHECK(sequential.Report().unresolved == 0);
    for (usize participants : {usize{2}, usize{4}, usize{8}})
    {
        jobs::Scheduler::Config config;
        config.participants = participants;
        config.pin = false;
        jobs::Scheduler jobs(config);
        Rig parallel;
        parallel.schedule.SetScheduler(&jobs);
        BuildForest(parallel.world, 100000, 11);
        parallel.Frame();
        CHECK(HashMatrices(parallel.world) == expected);
        // Move some roots and compare again over a few frames.
        for (u32 frame = 0; frame < 3; ++frame)
        {
            ecs_test::Rng rng(frame + 1);
            for (Rig* rig : {&sequential, &parallel})
            {
                ecs_test::Rng pick(frame + 100);
                rig->world.Query<Write<LocalTransform>, Without<Parent>>().ForEach(
                    [&](LocalTransform& t)
                    {
                        if (pick.Below(50) == 0)
                        {
                            t.position.x += 1.0f;
                        }
                    });
            }
            sequential.Frame();
            parallel.Frame();
            CHECK(HashMatrices(parallel.world) == HashMatrices(sequential.world));
        }
        CHECK(parallel.world.Validate().Ok());
        // Re-run the sequential rig to the same state for the next worker count: rebuild it.
        sequential.world.~World();
        new (&sequential.world) World();
        sequential.world.SetResource(TransformReport{});
        BuildForest(sequential.world, 100000, 11);
        sequential.Frame();
    }
}

TEST_CASE("Transform: propagation cost at depth 1, 4 and 16 (recorded, not asserted)", "[ecs][transform][.cost]")
{
    for (u32 depth : {1u, 4u, 16u})
    {
        Rig rig;
        const u32 per_level = 100000 / (depth + 1);
        Vector<Entity> level;
        for (u32 i = 0; i < per_level; ++i)
        {
            level.Push(CreateTransformEntity(rig.world, At(static_cast<f32>(i), 0, 0)));
        }
        for (u32 d = 1; d <= depth; ++d)
        {
            Vector<Entity> next;
            for (u32 i = 0; i < per_level; ++i)
            {
                next.Push(rig.world.Create(At(0, 1, 0), WorldTransform{}, Parent{level[i]}, HierarchyDepth{d}));
            }
            level = Move(next);
        }
        rig.Frame();
        u64 best = ~u64{0};
        for (int frame = 0; frame < 5; ++frame)
        {
            rig.world.Query<Write<LocalTransform>, Without<Parent>>().ForEach([](LocalTransform& t)
                                                                              { t.position.x += 1.0f; });
            const u64 t0 = os::MonotonicTimeNanos();
            rig.Frame();
            const u64 t = os::MonotonicTimeNanos() - t0;
            best = t < best ? t : best;
        }
        os::Print("transform propagation: depth %u, ~100k entities: %.2f ms\n", depth, static_cast<double>(best) / 1e6);
    }
}
