// The instance hierarchy (Phase 28 Day 499): the builder, the packed depth-first array, world transforms against a
// separate reference, bounds, edits and the refusals.

#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "jobs/scheduler.h"
#include "lang/limits.h"
#include "lang/scalar.h"
#include "math/transform.h"
#include "renderer/instance_tree.h"

namespace vse::renderer
{

namespace
{

struct TrGen
{
    u64 state;
    explicit TrGen(u64 seed) : state(seed * 2654435761u + 1) {}
    u32 Next()
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<u32>(state >> 33);
    }
    f32 Unit() { return static_cast<f32>(Next() & 0xFFFFFF) / 16777216.0f; }
    f32 Range(f32 lo, f32 hi) { return lo + (hi - lo) * Unit(); }
};

TreeNodeDesc TrDesc(TrGen& g, u32 meshes)
{
    TreeNodeDesc d;
    d.position = Vec3d(g.Range(-3.0f, 3.0f), g.Range(0.0f, 4.0f), g.Range(-3.0f, 3.0f));
    const f32 a = g.Range(0.0f, 6.0f);
    d.rotation = Quaternion(0.0f, Sin(a * 0.5f), 0.0f, Cos(a * 0.5f));
    d.scale = Vec3(g.Range(0.5f, 1.2f), g.Range(0.5f, 1.2f), g.Range(0.5f, 1.2f));
    d.mesh = g.Next() % meshes;
    d.material_slot = g.Next() % 4;
    return d;
}

Vector<TreeMeshInfo> TrMeshes()
{
    Vector<TreeMeshInfo> m;
    for (u32 i = 0; i < 3; ++i)
    {
        TreeMeshInfo info;
        info.bounds = Aabb::FromMinMax(Vec3(-0.5f, 0.0f, -0.5f) * static_cast<f32>(i + 1),
                                       Vec3(0.5f, 1.0f, 0.5f) * static_cast<f32>(i + 1));
        m.Push(info);
    }
    return m;
}

// A plain description of a random forest: the parent of each id (parents come first) and its local transform.
struct TrModel
{
    Vector<u32> parent;
    Vector<TreeNodeDesc> desc;
    Vector<u32> depth;
};

// roots * 1 + a branching tree 5 levels deep: level sizes multiply by `fanout`.
InstanceTreePtr TrBuild(TrModel* model, u32 roots, u32 fanout, u32 levels, u64 seed, jobs::Scheduler* scheduler)
{
    TrGen g(seed);
    Vector<TreeMeshInfo> meshes = TrMeshes();
    InstanceTreeBuilder builder(Span<const TreeMeshInfo>(meshes.Data(), meshes.Size()));
    Vector<u32> level;
    for (u32 r = 0; r < roots; ++r)
    {
        TreeNodeDesc d = TrDesc(g, 3);
        d.position = Vec3d(static_cast<f64>(r) * 20.0, 0.0, 1.0e7);
        TreeNodeId id;
        REQUIRE(builder.AddRoot(d, &id) == TreeStatus::Ok);
        model->parent.Push(kTreeNone);
        model->desc.Push(d);
        model->depth.Push(0);
        level.Push(id.value);
    }
    for (u32 l = 1; l < levels; ++l)
    {
        Vector<u32> next;
        for (usize k = 0; k < level.Size(); ++k)
        {
            for (u32 c = 0; c < fanout; ++c)
            {
                const TreeNodeDesc d = TrDesc(g, 3);
                TreeNodeId id;
                TreeNodeId parent;
                parent.value = level[k];
                REQUIRE(builder.AddChild(parent, d, &id) == TreeStatus::Ok);
                model->parent.Push(level[k]);
                model->desc.Push(d);
                model->depth.Push(l);
                next.Push(id.value);
            }
        }
        level.Swap(next);
    }
    InstanceTreePtr tree = builder.Finish(scheduler);
    REQUIRE(tree.Get() != nullptr);
    return tree;
}

Transform TrLocal(const TreeNodeDesc& d, bool root)
{
    Transform t;
    t.position = root ? Vec3(0.0f, 0.0f, 0.0f) : d.position.Narrow();
    t.rotation = d.rotation;
    t.scale = d.scale;
    return t;
}

bool TrSame(const Transform& a, const Transform& b)
{
    return a.position.x == b.position.x && a.position.y == b.position.y && a.position.z == b.position.z &&
           a.rotation.x == b.rotation.x && a.rotation.y == b.rotation.y && a.rotation.z == b.rotation.z &&
           a.rotation.w == b.rotation.w && a.scale.x == b.scale.x && a.scale.y == b.scale.y && a.scale.z == b.scale.z;
}

bool TrSameBox(const Aabb& a, const Aabb& b)
{
    if (a.IsEmpty() || b.IsEmpty())
    {
        return a.IsEmpty() == b.IsEmpty();
    }
    return a.min.x == b.min.x && a.min.y == b.min.y && a.min.z == b.min.z && a.max.x == b.max.x && a.max.y == b.max.y &&
           a.max.z == b.max.z;
}

// Worlds and bounds of every id, by ids in order (a parent id is smaller than its children's), independent of the
// array order the tree keeps.
void TrReference(const TrModel& m, const InstanceTree& tree, Vector<Transform>* world, Vector<Aabb>* own,
                 Vector<Aabb>* sub)
{
    const usize n = m.parent.Size();
    world->Resize(n);
    own->Resize(n);
    sub->Resize(n);
    for (usize i = 0; i < n; ++i)
    {
        const Transform local = TrLocal(m.desc[i], m.parent[i] == kTreeNone);
        (*world)[i] = m.parent[i] == kTreeNone ? local : Compose((*world)[m.parent[i]], local);
        (*own)[i] = TransformAabb(tree.Mesh(m.desc[i].mesh).bounds, (*world)[i].ToMatrix());
        (*sub)[i] = (*own)[i];
    }
    for (usize i = n; i > 0; --i)
    {
        if (m.parent[i - 1] != kTreeNone)
        {
            (*sub)[m.parent[i - 1]] = Union((*sub)[m.parent[i - 1]], (*sub)[i - 1]);
        }
    }
}

void TrCheckAgainst(const InstanceTree& tree, const TrModel& m)
{
    Vector<Transform> world;
    Vector<Aabb> own;
    Vector<Aabb> sub;
    TrReference(m, tree, &world, &own, &sub);
    REQUIRE(tree.NodeCount() == m.parent.Size());
    u32 wrong_world = 0;
    u32 wrong_own = 0;
    u32 wrong_sub = 0;
    for (u32 i = 0; i < tree.NodeCount(); ++i)
    {
        const InstanceTreeNode& n = tree.Node(i);
        wrong_world += TrSame(n.world, world[n.id]) ? 0u : 1u;
        wrong_own += TrSameBox(n.bounds, own[n.id]) ? 0u : 1u;
        wrong_sub += TrSameBox(n.subtree_bounds, sub[n.id]) ? 0u : 1u;
    }
    CHECK(wrong_world == 0);
    CHECK(wrong_own == 0);
    CHECK(wrong_sub == 0);
}

}  // namespace

TEST_CASE("Instance tree: the builder lays nodes out depth first", "[instance_tree]")
{
    Vector<TreeMeshInfo> meshes = TrMeshes();
    InstanceTreeBuilder builder(Span<const TreeMeshInfo>(meshes.Data(), meshes.Size()));
    TrGen g(1);
    TreeNodeId a;
    TreeNodeId b;
    TreeNodeId c;
    TreeNodeId d;
    TreeNodeId e;
    REQUIRE(builder.AddRoot(TrDesc(g, 3), &a) == TreeStatus::Ok);
    REQUIRE(builder.AddChild(a, TrDesc(g, 3), &b) == TreeStatus::Ok);
    REQUIRE(builder.AddChild(a, TrDesc(g, 3), &c) == TreeStatus::Ok);
    REQUIRE(builder.AddChild(b, TrDesc(g, 3), &d) == TreeStatus::Ok);  // added after c, still under b
    REQUIRE(builder.AddRoot(TrDesc(g, 3), &e) == TreeStatus::Ok);
    InstanceTreePtr tree = builder.Finish(nullptr);
    REQUIRE(tree.Get() != nullptr);
    REQUIRE(tree->NodeCount() == 5);
    CHECK(tree->Validate());
    CHECK(tree->RootCount() == 2);
    // a, b, d, c, e
    CHECK(tree->IdAt(0) == a);
    CHECK(tree->IdAt(1) == b);
    CHECK(tree->IdAt(2) == d);
    CHECK(tree->IdAt(3) == c);
    CHECK(tree->IdAt(4) == e);
    CHECK(tree->Node(0).subtree_size == 4);
    CHECK(tree->Node(1).subtree_size == 2);
    CHECK(tree->SubtreeEnd(0) == 4);
    CHECK(tree->Parent(2) == 1);
    CHECK(tree->Parent(3) == 0);
    CHECK(tree->Parent(0) == kTreeNone);
    CHECK(tree->Find(d) == 2);
    CHECK(tree->RootOf(2) == 0);
}

TEST_CASE("Instance tree: the builder refuses what it cannot hold", "[instance_tree]")
{
    Vector<TreeMeshInfo> meshes = TrMeshes();
    TrGen g(2);
    InstanceTreeBuilder builder(Span<const TreeMeshInfo>(meshes.Data(), meshes.Size()));
    TreeNodeId root;
    REQUIRE(builder.AddRoot(TrDesc(g, 3), &root) == TreeStatus::Ok);
    TreeNodeId none;
    CHECK(builder.AddChild(none, TrDesc(g, 3), nullptr) == TreeStatus::UnknownParent);
    TreeNodeId ahead;
    ahead.value = 7;  // not added yet: a cycle cannot be asked for
    CHECK(builder.AddChild(ahead, TrDesc(g, 3), nullptr) == TreeStatus::UnknownParent);
    TreeNodeDesc bad = TrDesc(g, 3);
    bad.scale = Vec3(0.0f, 1.0f, 1.0f);
    CHECK(builder.AddChild(root, bad, nullptr) == TreeStatus::InvalidValue);
    bad = TrDesc(g, 3);
    bad.mesh = 3;
    CHECK(builder.AddChild(root, bad, nullptr) == TreeStatus::InvalidValue);
    bad = TrDesc(g, 3);
    bad.material_slot = kMaxTreeMaterialSlots;
    CHECK(builder.AddChild(root, bad, nullptr) == TreeStatus::InvalidValue);
    bad = TrDesc(g, 3);
    bad.position = Vec3d(1.0e13, 0.0, 0.0);
    CHECK(builder.AddChild(root, bad, nullptr) == TreeStatus::InvalidValue);
    // A chain deeper than the limit.
    TreeNodeId at = root;
    u32 added = 0;
    TreeStatus last = TreeStatus::Ok;
    for (u32 i = 0; i < kMaxTreeDepth + 3; ++i)
    {
        TreeNodeId next;
        last = builder.AddChild(at, TrDesc(g, 3), &next);
        if (last != TreeStatus::Ok)
        {
            break;
        }
        at = next;
        ++added;
    }
    CHECK(last == TreeStatus::TooDeep);
    CHECK(added == kMaxTreeDepth);
    // A bad mesh list refuses every node.
    Vector<TreeMeshInfo> too_many;
    for (u32 i = 0; i < kMaxTreeMeshes + 1; ++i)
    {
        too_many.Push(meshes[0]);
    }
    InstanceTreeBuilder broken(Span<const TreeMeshInfo>(too_many.Data(), too_many.Size()));
    CHECK(broken.AddRoot(TrDesc(g, 1), nullptr) == TreeStatus::InvalidValue);
    CHECK(broken.Finish(nullptr).Get() == nullptr);
    InstanceTreeBuilder empty(Span<const TreeMeshInfo>(meshes.Data(), meshes.Size()));
    CHECK(empty.Finish(nullptr).Get() == nullptr);
}

TEST_CASE("Instance tree: a tree of 100,000 nodes equals the reference, and a trunk moves its subtree",
          "[instance_tree]")
{
    TrModel model;
    // 20 roots, fanout 9, 5 levels: 20 * (1 + 9 + 81 + 729 + 6561) = 146,200 nodes; use 8 for 100,000-ish.
    InstanceTreePtr tree = TrBuild(&model, 20, 8, 5, 3, nullptr);
    REQUIRE(tree->NodeCount() == 20u * (1 + 8 + 64 + 512 + 4096));
    CHECK(tree->NodeCount() > 90000);
    CHECK(tree->Validate());
    TrCheckAgainst(*tree, model);

    // Move one inner node: only its subtree's worlds change, the rest stay bit for bit.
    Vector<Transform> before;
    before.Resize(model.parent.Size());
    for (u32 i = 0; i < tree->NodeCount(); ++i)
    {
        before[tree->Node(i).id] = tree->Node(i).world;
    }
    TrGen g(5);
    u32 victim_id = 0;
    for (;;)
    {
        victim_id = g.Next() % static_cast<u32>(model.parent.Size());
        if (model.depth[victim_id] == 1)
        {
            break;
        }
    }
    TreeNodeId victim;
    victim.value = victim_id;
    TreeNodeDesc moved = TrDesc(g, 3);
    moved.mesh = model.desc[victim_id].mesh;  // SetLocal moves a node, it does not change its mesh
    model.desc[victim_id] = moved;
    REQUIRE(tree->SetLocal(victim, moved.position, moved.rotation, moved.scale) == TreeStatus::Ok);
    CHECK(tree->NeedsUpdate());
    const u32 touched = tree->UpdateWorldTransforms(nullptr);
    CHECK_FALSE(tree->NeedsUpdate());
    const u32 at = tree->Find(victim);
    const u32 size = tree->Node(at).subtree_size;
    CHECK(size == 1 + 8 + 64 + 512);
    CHECK(touched == size);
    u32 changed_outside = 0;
    u32 changed_inside = 0;
    for (u32 i = 0; i < tree->NodeCount(); ++i)
    {
        const bool same = TrSame(before[tree->Node(i).id], tree->Node(i).world);
        if (i >= at && i < at + size)
        {
            changed_inside += same ? 0u : 1u;
        }
        else
        {
            changed_outside += same ? 0u : 1u;
        }
    }
    CHECK(changed_outside == 0);
    CHECK(changed_inside > size / 2);  // all of them move; a few may coincide only by luck
    TrCheckAgainst(*tree, model);
}

TEST_CASE("Instance tree: random edits keep worlds and bounds equal to a recomputation", "[instance_tree]")
{
    TrModel model;
    InstanceTreePtr tree = TrBuild(&model, 10, 4, 5, 11, nullptr);
    TrGen g(12);
    for (u32 round = 0; round < 30; ++round)
    {
        const u32 edits = 1 + g.Next() % 6;
        for (u32 e = 0; e < edits; ++e)
        {
            const u32 id = g.Next() % static_cast<u32>(model.parent.Size());
            TreeNodeDesc d = TrDesc(g, 3);
            d.mesh = model.desc[id].mesh;
            if (model.parent[id] == kTreeNone)
            {
                d.position = model.desc[id].position;
            }
            model.desc[id] = d;
            TreeNodeId node;
            node.value = id;
            REQUIRE(tree->SetLocal(node, d.position, d.rotation, d.scale) == TreeStatus::Ok);
        }
        tree->UpdateWorldTransforms(nullptr);
        TrCheckAgainst(*tree, model);
    }
    CHECK(tree->Validate());
}

TEST_CASE("Instance tree: the update is the same on 1 and 8 threads", "[instance_tree][concurrency]")
{
    jobs::Scheduler::Config config;
    config.participants = 8;
    config.pin = false;
    jobs::Scheduler scheduler(config);
    TrModel m1;
    TrModel m2;
    InstanceTreePtr a = TrBuild(&m1, 40, 4, 5, 21, nullptr);
    InstanceTreePtr b = TrBuild(&m2, 40, 4, 5, 21, &scheduler);
    REQUIRE(a->NodeCount() == b->NodeCount());
    TrGen g(22);
    for (u32 round = 0; round < 20; ++round)
    {
        for (u32 e = 0; e < 40; ++e)
        {
            const u32 id = g.Next() % static_cast<u32>(m1.parent.Size());
            const TreeNodeDesc d = TrDesc(g, 3);
            TreeNodeId node;
            node.value = id;
            REQUIRE(a->SetLocal(node, d.position, d.rotation, d.scale) == TreeStatus::Ok);
            REQUIRE(b->SetLocal(node, d.position, d.rotation, d.scale) == TreeStatus::Ok);
        }
        a->UpdateWorldTransforms(nullptr);
        b->UpdateWorldTransforms(&scheduler);
        u32 different = 0;
        for (u32 i = 0; i < a->NodeCount(); ++i)
        {
            different += TrSame(a->Node(i).world, b->Node(i).world) &&
                                 TrSameBox(a->Node(i).subtree_bounds, b->Node(i).subtree_bounds)
                             ? 0u
                             : 1u;
        }
        CHECK(different == 0);
    }
}

TEST_CASE("Instance tree: attach and detach keep the array valid", "[instance_tree]")
{
    TrModel model;
    InstanceTreePtr tree = TrBuild(&model, 3, 3, 4, 31, nullptr);
    TrGen g(32);
    for (u32 round = 0; round < 40; ++round)
    {
        TreeNodeId node;
        node.value = g.Next() % static_cast<u32>(model.parent.Size());
        if (g.Next() % 2 == 0)
        {
            const u32 index = tree->Find(node);
            const Vec3d world_before = tree->WorldPosition(index);
            const Quaternion rot_before = tree->Node(index).world.rotation;
            REQUIRE(tree->Detach(node) == TreeStatus::Ok);
            const u32 now = tree->Find(node);
            CHECK(tree->Parent(now) == kTreeNone);
            const Vec3d world_after = tree->WorldPosition(now);
            CHECK(Abs(world_after.x - world_before.x) < 1.0e-3);
            CHECK(Abs(world_after.y - world_before.y) < 1.0e-3);
            CHECK(Abs(world_after.z - world_before.z) < 1.0e-3);
            CHECK(tree->Node(now).world.rotation.w == rot_before.w);
        }
        else
        {
            TreeNodeId parent;
            parent.value = g.Next() % static_cast<u32>(model.parent.Size());
            const u32 index = tree->Find(node);
            const u32 pindex = tree->Find(parent);
            const bool inside = pindex >= index && pindex < tree->SubtreeEnd(index);
            const TreeStatus status = tree->Attach(node, parent);
            if (inside)
            {
                CHECK(status == TreeStatus::Cycle);
            }
            else if (status == TreeStatus::Ok)
            {
                CHECK(tree->Parent(tree->Find(node)) == tree->Find(parent));
            }
            else
            {
                CHECK(status == TreeStatus::TooDeep);
            }
        }
        REQUIRE(tree->Validate());
        CHECK_FALSE(tree->NeedsUpdate());
    }
    // Worlds still equal a recomputation from the tree's own locals.
    u32 wrong = 0;
    for (u32 i = 0; i < tree->NodeCount(); ++i)
    {
        const InstanceTreeNode& n = tree->Node(i);
        const Transform expect = n.parent == kTreeNone ? n.local : Compose(tree->Node(n.parent).world, n.local);
        wrong += TrSame(expect, n.world) ? 0u : 1u;
    }
    CHECK(wrong == 0);
}

TEST_CASE("Instance tree: a root 10,000 km away keeps its offsets exact", "[instance_tree]")
{
    Vector<TreeMeshInfo> meshes = TrMeshes();
    InstanceTreeBuilder builder(Span<const TreeMeshInfo>(meshes.Data(), meshes.Size()));
    TreeNodeDesc root;
    root.position = Vec3d(1.0e7 + 0.125, 0.0, -1.0e7);
    root.mesh = 0;
    TreeNodeId rid;
    REQUIRE(builder.AddRoot(root, &rid) == TreeStatus::Ok);
    TreeNodeDesc child;
    child.position = Vec3d(0.0, 2.5, 0.0);
    child.mesh = 1;
    TreeNodeId cid;
    REQUIRE(builder.AddChild(rid, child, &cid) == TreeStatus::Ok);
    InstanceTreePtr tree = builder.Finish(nullptr);
    REQUIRE(tree.Get() != nullptr);
    const Vec3d p = tree->WorldPosition(tree->Find(cid));
    CHECK(p.x == 1.0e7 + 0.125);
    CHECK(p.y == 2.5);
    CHECK(p.z == -1.0e7);
    // The child's box is an offset box: as precise as at the origin.
    const Aabb box = tree->Node(tree->Find(cid)).bounds;
    CHECK(box.min.y == 2.5f);
    CHECK(box.max.y == 2.5f + 2.0f);
}

TEST_CASE("Instance tree: edits are refused by status, not by abort", "[instance_tree]")
{
    TrModel model;
    InstanceTreePtr tree = TrBuild(&model, 1, 2, 3, 41, nullptr);
    TreeNodeId none;
    CHECK(tree->SetLocal(none, Vec3d(), Quaternion::Identity(), Vec3(1.0f, 1.0f, 1.0f)) == TreeStatus::UnknownNode);
    TreeNodeId node;
    node.value = 1;
    CHECK(tree->SetLocal(node, Vec3d(), Quaternion::Identity(), Vec3(1.0f, -1.0f, 1.0f)) == TreeStatus::InvalidValue);
    CHECK(tree->SetLocal(node, Vec3d(Limits<f64>::QuietNaN(), 0.0, 0.0), Quaternion::Identity(),
                         Vec3(1.0f, 1.0f, 1.0f)) == TreeStatus::InvalidValue);
    CHECK(tree->Detach(none) == TreeStatus::UnknownNode);
    CHECK(tree->Attach(node, node) == TreeStatus::Cycle);
    CHECK_FALSE(tree->NeedsUpdate());
}

TEST_CASE("Instance tree: attaching a root keeps its offset from the new parent", "[instance_tree]")
{
    Vector<TreeMeshInfo> meshes = TrMeshes();
    InstanceTreeBuilder builder(Span<const TreeMeshInfo>(meshes.Data(), meshes.Size()));
    TreeNodeDesc first;
    first.position = Vec3d(100.0, 0.0, 0.0);
    TreeNodeDesc second;
    second.position = Vec3d(0.0, 50.0, 0.0);
    TreeNodeId a;
    TreeNodeId b;
    REQUIRE(builder.AddRoot(first, &a) == TreeStatus::Ok);
    REQUIRE(builder.AddRoot(second, &b) == TreeStatus::Ok);
    InstanceTreePtr tree = builder.Finish(nullptr);
    REQUIRE(tree->Attach(a, b) == TreeStatus::Ok);
    REQUIRE(tree->Validate());
    const Vec3d origin = tree->Node(tree->Find(b)).origin;
    const Vec3 offset = tree->Node(tree->Find(a)).local.position;
    // the first root's 100 m east of the second, not on top of it
    CHECK(origin.y == 50.0);
    CHECK(offset.x == 100.0f);
    CHECK(tree->Node(tree->Find(a)).world.position.x == 100.0f);
}

TEST_CASE("Instance tree: a stand-in beyond the cull distance is refused", "[instance_tree]")
{
    Vector<TreeMeshInfo> meshes = TrMeshes();
    InstanceTreeBuilder builder(Span<const TreeMeshInfo>(meshes.Data(), meshes.Size()));
    TreeNodeDesc d;
    d.has_lod = true;
    d.lod.standin_mesh = 1;
    d.lod.standin_distance = 500.0f;
    d.lod.cull_distance = 100.0f;
    TreeNodeId id;
    CHECK(builder.AddRoot(d, &id) == TreeStatus::InvalidValue);
    d.lod.standin_distance = 50.0f;
    CHECK(builder.AddRoot(d, &id) == TreeStatus::Ok);
}

}  // namespace vse::renderer
