// Culling and levels of detail of the instance hierarchy (Phase 28 Day 500): the subtree cull against a per-node
// reference, the level table, hysteresis, stand-ins, grouping, thread independence and the work done.

#include <catch2/catch_test_macros.hpp>

#include "algorithm.h"
#include "containers/vector.h"
#include "jobs/scheduler.h"
#include "lang/scalar.h"
#include "log/output.h"
#include "os/os.h"
#include "renderer/camera.h"
#include "renderer/instance_tree_cull.h"
#include "soak.h"

namespace vse::renderer
{

namespace
{

namespace instance_tree_cull_test
{

struct TcGen
{
    u64 s;
    explicit TcGen(u64 seed) : s(seed * 6364136223846793005ull + 1442695040888963407ull) {}
    u32 Next()
    {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return static_cast<u32>((s * 2685821657736338717ull) >> 32);
    }
    f32 Unit() { return static_cast<f32>(Next() >> 8) / static_cast<f32>(1u << 24); }
    f32 Range(f32 lo, f32 hi) { return lo + (hi - lo) * Unit(); }
};

Vector<TreeMeshInfo> TcMeshes()
{
    Vector<TreeMeshInfo> m;
    for (u32 i = 0; i < 6; ++i)
    {
        TreeMeshInfo info;
        info.bounds = Aabb::FromMinMax(Vec3(-0.5f, 0.0f, -0.5f), Vec3(0.5f, 2.0f, 0.5f));
        m.Push(info);
    }
    return m;
}

TreeLod TcRandomLod(TcGen& g)
{
    TreeLod lod;
    lod.level_count = 1 + g.Next() % 3;
    f32 d = g.Range(15.0f, 50.0f);
    for (u32 k = 1; k < lod.level_count; ++k)
    {
        lod.mesh[k] = 1 + g.Next() % 5;
        lod.switch_distance[k - 1] = d;
        d += g.Range(20.0f, 60.0f);
    }
    if (g.Next() % 3 == 0)
    {
        lod.cull_distance = d + g.Range(10.0f, 80.0f);
        lod.cull_children_beyond = g.Next() % 2 == 0;
    }
    if (g.Next() % 4 == 0)
    {
        lod.standin_mesh = 5;
        lod.standin_distance = g.Range(60.0f, 250.0f);
    }
    return lod;
}

// A forest on a field `half` metres wide: `roots` roots with `fanout` children per node over `levels` levels; a share
// of the nodes carry a level-of-detail entry when `lods` is set.
InstanceTreePtr TcForest(u32 roots, u32 fanout, u32 levels, f32 half, u64 seed, bool lods, f64 base,
                         jobs::Scheduler* scheduler)
{
    TcGen g(seed);
    Vector<TreeMeshInfo> meshes = TcMeshes();
    InstanceTreeBuilder builder(Span<const TreeMeshInfo>(meshes.Data(), meshes.Size()));
    Vector<u32> level;
    auto make = [&](bool root)
    {
        TreeNodeDesc d;
        d.position = root ? Vec3d(base + g.Range(-half, half), 0.0, base + g.Range(-half, half))
                          : Vec3d(g.Range(-2.0f, 2.0f), g.Range(1.0f, 3.0f), g.Range(-2.0f, 2.0f));
        const f32 a = g.Range(0.0f, 6.0f);
        d.rotation = Quaternion(0.0f, Sin(a * 0.5f), 0.0f, Cos(a * 0.5f));
        const f32 s = root ? 1.0f : g.Range(0.5f, 0.9f);
        d.scale = Vec3(s, s, s);
        d.mesh = g.Next() % 5 == 0 ? kTreeNoMesh : g.Next() % 5;
        d.material_slot = g.Next() % 3;
        if (lods && g.Next() % 3 == 0)
        {
            d.has_lod = true;
            d.lod = TcRandomLod(g);
        }
        return d;
    };
    for (u32 r = 0; r < roots; ++r)
    {
        TreeNodeId id;
        REQUIRE(builder.AddRoot(make(true), &id) == TreeStatus::Ok);
        level.Push(id.value);
    }
    for (u32 l = 1; l < levels; ++l)
    {
        Vector<u32> next;
        for (usize k = 0; k < level.Size(); ++k)
        {
            for (u32 c = 0; c < fanout; ++c)
            {
                TreeNodeId id;
                TreeNodeId parent;
                parent.value = level[k];
                REQUIRE(builder.AddChild(parent, make(false), &id) == TreeStatus::Ok);
                next.Push(id.value);
            }
        }
        level.Swap(next);
    }
    InstanceTreePtr tree = builder.Finish(scheduler);
    REQUIRE(tree.Get() != nullptr);
    return tree;
}

TreeCullParams TcParams(const Camera& cam, const Vec3d& origin)
{
    TreeCullParams p;
    p.frustum = Frustum::FromViewProjection(cam.ViewProjection());
    p.render_origin = origin;
    p.camera_position = cam.position;
    return p;
}

Camera TcRandomCamera(TcGen& g, f32 half)
{
    const Vec3 eye(g.Range(-half, half), g.Range(0.0f, 60.0f), g.Range(-half, half));
    const Vec3 target(g.Range(-half, half), g.Range(0.0f, 20.0f), g.Range(-half, half));
    Camera cam = MakeLookAtCamera(eye, target, Vec3(0.0f, 1.0f, 0.0f));
    cam.far_plane = g.Range(100.0f, 500.0f);
    return cam;
}

Aabb TcGrown(const Aabb& box, const Vec3& rel)
{
    const Vec3 lo = box.min + rel;
    const Vec3 hi = box.max + rel;
    const Vec3 centre = (lo + hi) * 0.5f;
    const Vec3 half = (hi - lo) * 0.5f;
    const f32 eps = 1.0e-4f * (1.0f + Abs(centre.x) + Abs(centre.y) + Abs(centre.z) + half.x + half.y + half.z);
    Aabb out;
    out.min = lo - Vec3(eps, eps, eps);
    out.max = hi + Vec3(eps, eps, eps);
    return out;
}

// The reference: every node on its own, descending from the root with no box jump and no whole-subtree emit.
void TcReference(const InstanceTree& tree, const TreeCullParams& p, u32 i, const Vec3& rel_root,
                 Vector<TreeVisibleNode>* out)
{
    const InstanceTreeNode& node = tree.Node(i);
    u32 mesh = node.mesh;
    u32 level = 0;
    if (node.lod != kTreeNone)
    {
        const TreeLod& lod = tree.Lod(node.lod);
        const f32 d = (rel_root + node.world.position - p.camera_position).Length();
        const u32 chosen = TreeLevelForDistance(lod, d);
        const bool standin = lod.standin_mesh != kTreeNoMesh && d > lod.standin_distance;
        if (standin)
        {
            if (!node.subtree_bounds.IsEmpty() &&
                p.frustum.TestAabb(TcGrown(node.subtree_bounds, rel_root)) != Containment::Outside)
            {
                TreeVisibleNode v;
                v.node = i;
                v.mesh = lod.standin_mesh;
                v.material_slot = node.material_slot;
                v.level = kTreeStandinLevel;
                out->Push(v);
            }
            return;
        }
        if (chosen >= lod.level_count)
        {
            if (lod.cull_children_beyond)
            {
                return;
            }
            mesh = kTreeNoMesh;
        }
        else
        {
            level = chosen;
            mesh = level == 0 ? node.mesh : lod.mesh[level];
        }
    }
    if (mesh != kTreeNoMesh && !node.bounds.IsEmpty() &&
        p.frustum.TestAabb(TcGrown(node.bounds, rel_root)) != Containment::Outside)
    {
        TreeVisibleNode v;
        v.node = i;
        v.mesh = mesh;
        v.material_slot = node.material_slot;
        v.level = level;
        out->Push(v);
    }
    const u32 end = i + node.subtree_size;
    for (u32 c = i + 1; c < end; c += tree.Node(c).subtree_size)
    {
        TcReference(tree, p, c, rel_root, out);
    }
}

Vector<u64> TcKeys(const Vector<TreeVisibleNode>& nodes)
{
    Vector<u64> keys;
    for (usize k = 0; k < nodes.Size(); ++k)
    {
        keys.Push((static_cast<u64>(nodes[k].node) << 32) | (static_cast<u64>(nodes[k].mesh) << 8) | nodes[k].level);
    }
    Sort(Span<u64>(keys.Data(), keys.Size()));
    return keys;
}

bool TcSameKeys(const Vector<u64>& a, const Vector<u64>& b)
{
    if (a.Size() != b.Size())
    {
        return false;
    }
    for (usize i = 0; i < a.Size(); ++i)
    {
        if (a[i] != b[i])
        {
            return false;
        }
    }
    return true;
}

Vector<TreeVisibleNode> TcRunReference(const InstanceTree& tree, const TreeCullParams& p)
{
    Vector<TreeVisibleNode> out;
    for (u32 r = 0; r < tree.RootCount(); ++r)
    {
        const u32 root = tree.RootIndex(r);
        TcReference(tree, p, root, (tree.Node(root).origin - p.render_origin).Narrow(), &out);
    }
    return out;
}

}  // namespace instance_tree_cull_test

}  // namespace

using namespace instance_tree_cull_test;

TEST_CASE("Instance tree cull: the subtree cull equals a per-node reference for 100 random cameras",
          "[instance_tree_cull]")
{
    InstanceTreePtr tree = TcForest(120, 3, 4, 300.0f, 1, true, 0.0, nullptr);
    TcGen g(7);
    TreeCullResult out;
    u32 accepted = 0;
    u32 skipped = 0;
    u32 standins = 0;
    for (u32 i = 0; i < 100; ++i)
    {
        const TreeCullParams p = TcParams(TcRandomCamera(g, 300.0f), Vec3d());
        CullInstanceTree(*tree, p, nullptr, nullptr, &out);
        REQUIRE(TcSameKeys(TcKeys(out.nodes), TcKeys(TcRunReference(*tree, p))));
        REQUIRE(out.stats.nodes_emitted == out.nodes.Size());
        accepted += out.stats.subtrees_accepted;
        skipped += out.stats.subtrees_skipped;
        standins += out.stats.standins;
    }
    CHECK(accepted > 0);
    CHECK(skipped > 0);
    CHECK(standins > 0);
}

TEST_CASE("Instance tree cull: a far forest culls like one at the origin", "[instance_tree_cull]")
{
    InstanceTreePtr near_tree = TcForest(60, 3, 3, 200.0f, 2, false, 0.0, nullptr);
    InstanceTreePtr far_tree = TcForest(60, 3, 3, 200.0f, 2, false, 1.0e7, nullptr);
    TcGen g(8);
    TreeCullResult a;
    TreeCullResult b;
    for (u32 i = 0; i < 30; ++i)
    {
        const Camera cam = TcRandomCamera(g, 200.0f);
        CullInstanceTree(*near_tree, TcParams(cam, Vec3d()), nullptr, nullptr, &a);
        CullInstanceTree(*far_tree, TcParams(cam, Vec3d(1.0e7, 0.0, 1.0e7)), nullptr, nullptr, &b);
        // The offsets are f32 either way, so the counts agree up to the rounding of a root's position.
        CHECK(Abs(static_cast<i64>(a.nodes.Size()) - static_cast<i64>(b.nodes.Size())) <= 4);
    }
}

TEST_CASE("Instance tree cull: the level follows the distance table", "[instance_tree_cull]")
{
    TreeLod lod;
    lod.level_count = 3;
    lod.mesh[1] = 1;
    lod.mesh[2] = 2;
    lod.switch_distance[0] = 10.0f;
    lod.switch_distance[1] = 30.0f;
    lod.cull_distance = 80.0f;
    struct Row
    {
        f32 distance;
        u32 level;
    };
    const Row table[] = {{0.0f, 0},  {9.9f, 0},  {10.0f, 0}, {10.1f, 1}, {29.9f, 1},  {30.0f, 1},
                         {30.1f, 2}, {79.9f, 2}, {80.0f, 2}, {80.1f, 3}, {1000.0f, 3}};
    for (const Row& r : table)
    {
        INFO(r.distance);
        CHECK(TreeLevelForDistance(lod, r.distance) == r.level);
    }
    lod.cull_distance = 0.0f;  // never culled
    CHECK(TreeLevelForDistance(lod, 1.0e6f) == 2);
}

namespace
{

// One node, one level switch at 10 m, seen by a camera that looks at it from `distance` metres.
u32 TcLevelAt(const InstanceTree& tree, f32 distance, TreeLodState* state)
{
    Camera cam = MakeLookAtCamera(Vec3(distance, 1.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    cam.far_plane = 500.0f;
    TreeCullResult out;
    CullInstanceTree(tree, TcParams(cam, Vec3d()), state, nullptr, &out);
    REQUIRE(out.nodes.Size() == 1);
    return out.nodes[0].level;
}

InstanceTreePtr TcSingleLodNode()
{
    Vector<TreeMeshInfo> meshes = TcMeshes();
    InstanceTreeBuilder builder(Span<const TreeMeshInfo>(meshes.Data(), meshes.Size()));
    TreeNodeDesc d;
    d.mesh = 0;
    d.has_lod = true;
    d.lod.level_count = 2;
    d.lod.mesh[1] = 1;
    d.lod.switch_distance[0] = 10.0f;
    d.lod.hysteresis = 1.0f;
    REQUIRE(builder.AddRoot(d, nullptr) == TreeStatus::Ok);
    InstanceTreePtr tree = builder.Finish(nullptr);
    REQUIRE(tree.Get() != nullptr);
    return tree;
}

}  // namespace

TEST_CASE("Instance tree cull: hysteresis changes the level once at a boundary", "[instance_tree_cull]")
{
    InstanceTreePtr tree = TcSingleLodNode();
    // Without a state the level flips at every crossing of 10 m.
    u32 flips_plain = 0;
    u32 last = TcLevelAt(*tree, 9.7f, nullptr);
    const f32 wobble[] = {10.3f, 9.7f, 10.3f, 9.7f, 10.3f};
    for (f32 d : wobble)
    {
        const u32 now = TcLevelAt(*tree, d, nullptr);
        flips_plain += now != last ? 1u : 0u;
        last = now;
    }
    CHECK(flips_plain == 5);
    // With a state, the wobble inside the margin changes nothing; leaving it changes the level once.
    TreeLodState state;
    CHECK(TcLevelAt(*tree, 9.7f, &state) == 0);
    for (f32 d : wobble)
    {
        CHECK(TcLevelAt(*tree, d, &state) == 0);
    }
    CHECK(TcLevelAt(*tree, 11.5f, &state) == 1);
    CHECK(TcLevelAt(*tree, 10.5f, &state) == 1);  // back inside the margin: stays
    CHECK(TcLevelAt(*tree, 9.5f, &state) == 1);   // still inside (10 - 1 = 9)
    CHECK(TcLevelAt(*tree, 8.5f, &state) == 0);   // beyond the margin: back
}

TEST_CASE("Instance tree cull: a stand-in replaces exactly its subtree", "[instance_tree_cull]")
{
    Vector<TreeMeshInfo> meshes = TcMeshes();
    InstanceTreeBuilder builder(Span<const TreeMeshInfo>(meshes.Data(), meshes.Size()));
    TreeNodeDesc trunk;
    trunk.mesh = 0;
    trunk.has_lod = true;
    trunk.lod.standin_mesh = 5;
    trunk.lod.standin_distance = 50.0f;
    TreeNodeId root;
    REQUIRE(builder.AddRoot(trunk, &root) == TreeStatus::Ok);
    for (u32 i = 0; i < 10; ++i)
    {
        TreeNodeDesc branch;
        branch.mesh = 1;
        branch.position = Vec3d(0.0, 1.0 + 0.1 * static_cast<f64>(i), 0.0);
        REQUIRE(builder.AddChild(root, branch, nullptr) == TreeStatus::Ok);
    }
    InstanceTreePtr tree = builder.Finish(nullptr);
    REQUIRE(tree.Get() != nullptr);
    TreeCullResult out;
    auto look = [&](f32 distance)
    {
        Camera cam = MakeLookAtCamera(Vec3(distance, 1.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
        cam.far_plane = 500.0f;
        CullInstanceTree(*tree, TcParams(cam, Vec3d()), nullptr, nullptr, &out);
    };
    look(20.0f);
    CHECK(out.nodes.Size() == 11);
    CHECK(out.stats.standins == 0);
    look(80.0f);
    REQUIRE(out.nodes.Size() == 1);  // one stand-in, no node of the subtree drawn twice
    CHECK(out.nodes[0].level == kTreeStandinLevel);
    CHECK(out.nodes[0].mesh == 5);
    CHECK(out.nodes[0].node == 0);
    CHECK(out.stats.standins == 1);
}

TEST_CASE("Instance tree cull: the output is grouped into runs and is the same on 1 and 8 threads",
          "[instance_tree_cull][concurrency]")
{
    jobs::Scheduler::Config config;
    config.participants = 8;
    config.pin = false;
    jobs::Scheduler scheduler(config);
    InstanceTreePtr tree = TcForest(300, 3, 4, 400.0f, 5, true, 0.0, &scheduler);
    TcGen g(9);
    TreeCullResult serial;
    TreeCullResult parallel;
    for (u32 i = 0; i < 20; ++i)
    {
        const TreeCullParams p = TcParams(TcRandomCamera(g, 400.0f), Vec3d());
        CullInstanceTree(*tree, p, nullptr, nullptr, &serial);
        CullInstanceTree(*tree, p, nullptr, &scheduler, &parallel);
        REQUIRE(serial.nodes.Size() == parallel.nodes.Size());
        u32 different = 0;
        for (usize k = 0; k < serial.nodes.Size(); ++k)
        {
            different += serial.nodes[k].node != parallel.nodes[k].node ? 1u : 0u;
        }
        CHECK(different == 0);
        CHECK(serial.runs.Size() == parallel.runs.Size());
        // The runs partition the nodes and each holds one (mesh, slot, level), in (mesh, level, slot) order.
        u32 covered = 0;
        for (usize r = 0; r < serial.runs.Size(); ++r)
        {
            const TreeDrawRun& run = serial.runs[r];
            REQUIRE(run.first == covered);
            REQUIRE(run.count > 0);
            for (u32 k = 0; k < run.count; ++k)
            {
                const TreeVisibleNode& v = serial.nodes[run.first + k];
                REQUIRE((v.mesh == run.mesh && v.material_slot == run.material_slot && v.level == run.level));
            }
            if (r > 0)
            {
                const TreeDrawRun& before = serial.runs[r - 1];
                const bool ordered = before.mesh != run.mesh     ? before.mesh < run.mesh
                                     : before.level != run.level ? before.level < run.level
                                                                 : before.material_slot < run.material_slot;
                REQUIRE(ordered);
            }
            covered += run.count;
        }
        CHECK(covered == serial.nodes.Size());
    }
}

TEST_CASE("Instance tree cull: the work follows the visible nodes, not the tree", "[instance_tree_cull]")
{
    // A forest of roots over 4 km, each with 781 nodes (5 levels of 5); the camera sees a small part.
    const u32 roots = test_support::SoakCount(130, 1300);
    InstanceTreePtr tree = TcForest(roots, 5, 5, 2000.0f, 11, false, 0.0, nullptr);
    const Vec3d at = tree->Node(tree->RootIndex(0)).origin;
    const Vec3 target(static_cast<f32>(at.x), 3.0f, static_cast<f32>(at.z));
    Camera cam = MakeLookAtCamera(target + Vec3(-25.0f, 8.0f, -25.0f), target, Vec3(0.0f, 1.0f, 0.0f));
    cam.far_plane = 120.0f;
    const TreeCullParams p = TcParams(cam, Vec3d());
    TreeCullResult out;
    const u64 t0 = os::MonotonicTimeNanos();
    CullInstanceTree(*tree, p, nullptr, nullptr, &out);
    const u64 t1 = os::MonotonicTimeNanos();
    REQUIRE(out.stats.nodes_emitted > 0);
    CHECK(out.stats.nodes_looked_at < tree->NodeCount() / 3);
    CHECK(out.stats.nodes_looked_at < roots + 4 * out.stats.nodes_emitted + 64);
    out::Print("tree cull: %u nodes, %u roots, looked at %u, emitted %u, %u subtrees skipped, %.3f ms\n",
               static_cast<unsigned>(tree->NodeCount()), static_cast<unsigned>(roots),
               static_cast<unsigned>(out.stats.nodes_looked_at), static_cast<unsigned>(out.stats.nodes_emitted),
               static_cast<unsigned>(out.stats.subtrees_skipped), static_cast<double>(t1 - t0) * 1.0e-6);
}

TEST_CASE("Instance tree cull: compacted records are relative to the render origin", "[instance_tree_cull]")
{
    Vector<TreeMeshInfo> meshes = TcMeshes();
    InstanceTreeBuilder builder(Span<const TreeMeshInfo>(meshes.Data(), meshes.Size()));
    TreeNodeDesc root;
    root.position = Vec3d(1.0e7 + 0.25, 3.0, -1.0e7);
    root.mesh = 0;
    TreeNodeId rid;
    REQUIRE(builder.AddRoot(root, &rid) == TreeStatus::Ok);
    TreeNodeDesc child;
    child.position = Vec3d(1.0, 2.0, 0.0);
    child.mesh = 1;
    child.extra.custom = 77;
    REQUIRE(builder.AddChild(rid, child, nullptr) == TreeStatus::Ok);
    InstanceTreePtr tree = builder.Finish(nullptr);
    REQUIRE(tree.Get() != nullptr);
    const Vec3d origin(1.0e7, 0.0, -1.0e7);
    Vector<TreeVisibleNode> nodes;
    TreeVisibleNode v;
    v.node = 1;
    nodes.Push(v);
    Vector<InstanceRecord> records;
    Vector<InstanceExtra> extras;
    CompactTreeNodes(*tree, Span<const TreeVisibleNode>(nodes.Data(), nodes.Size()), origin, &records, &extras);
    REQUIRE(records.Size() == 1);
    CHECK(records[0].position.x == 1.25f);
    CHECK(records[0].position.y == 5.0f);
    CHECK(records[0].position.z == 0.0f);
    CHECK(extras[0].custom == 77);
}

}  // namespace vse::renderer
