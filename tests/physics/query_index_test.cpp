// The query index (query and hierarchy indices series, Day 1): every query through the index equals the same query
// testing every collider, bit for bit, over random scenes that change between queries.
#include <catch2/catch_test_macros.hpp>

#include "bytes/byte_writer.h"
#include "cstr/mem_ops.h"
#include "lang/scalar.h"
#include "physics/compound.h"
#include "physics/query.h"
#include "physics/query_index.h"
#include "physics/triangle_mesh.h"
#include "test_hulls.h"

using namespace vse;
using namespace vse::physics;

namespace
{

struct IndexRng
{
    u64 state = 0x9E3779B97F4A7C15ull;
    u32 Next()
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<u32>(state >> 33);
    }
    f32 Unit() { return static_cast<f32>(Next() & 0xFFFFFF) / 16777216.0f; }
    f32 Range(f32 lo, f32 hi) { return lo + (hi - lo) * Unit(); }
    Vec3 Point(f32 r) { return Vec3(Range(-r, r), Range(-r, r), Range(-r, r)); }
    Quaternion Turn()
    {
        const Quaternion q(Range(-1, 1), Range(-1, 1), Range(-1, 1), Range(0.1f, 1));
        return q.Normalized();
    }
};

bool SameBits(f32 a, f32 b)
{
    return MemCompare(&a, &b, sizeof(f32)) == 0;
}

bool SameHit(const PhysicsHit& a, const PhysicsHit& b)
{
    return a.collider == b.collider && a.body == b.body && SameBits(a.point.x, b.point.x) &&
           SameBits(a.point.y, b.point.y) && SameBits(a.point.z, b.point.z) && SameBits(a.normal.x, b.normal.x) &&
           SameBits(a.normal.y, b.normal.y) && SameBits(a.normal.z, b.normal.z) && SameBits(a.distance, b.distance) &&
           a.triangle == b.triangle;
}

// The libraries' shapes, added once per world.
struct Library
{
    u32 hull = 0;
    Vec3 hull_extent;
    u32 mesh = 0;
    Vec3 mesh_extent;
    u32 compound = 0;
    Vec3 compound_extent;
};

Library AddLibrary(PhysicsWorld& world)
{
    Library lib;
    const ConvexHullConstPtr hull = testing::BoxHull(Vec3(0.6f, 0.4f, 0.5f));
    REQUIRE(hull);
    lib.hull = world.Hulls().Add(hull);
    lib.hull_extent = hull->Extent();

    Vector<Vec3> v;
    Vector<u32> idx;
    // A bumpy floor of 4 x 4 quads.
    for (u32 z = 0; z <= 4; ++z)
    {
        for (u32 x = 0; x <= 4; ++x)
        {
            v.Push(Vec3(-4.0f + 2.0f * static_cast<f32>(x), ((x + z) % 2 == 0) ? 0.0f : 0.5f,
                        -4.0f + 2.0f * static_cast<f32>(z)));
        }
    }
    for (u32 z = 0; z < 4; ++z)
    {
        for (u32 x = 0; x < 4; ++x)
        {
            const u32 a = z * 5 + x;
            for (const u32 i : {a, a + 5, a + 1, a + 1, a + 5, a + 6})
            {
                idx.Push(i);
            }
        }
    }
    TriangleMeshSpec spec;
    spec.positions = Span<const Vec3>(v.Data(), v.Size());
    spec.indices = Span<const u32>(idx.Data(), idx.Size());
    Validation r = Validation::Ok;
    const TriangleMeshConstPtr mesh = TriangleMesh::Create(spec, &r);
    REQUIRE(r == Validation::Ok);
    lib.mesh = world.Meshes().Add(mesh);
    lib.mesh_extent = mesh->Extent();

    Vector<CompoundChild> children;
    CompoundChild top;
    top.shape = Shape::MakeBox(Vec3(0.6f, 0.05f, 0.4f));
    top.offset = Vec3(0, 0.5f, 0);
    top.material.density = 100.0f;
    children.Push(top);
    CompoundChild leg;
    leg.shape = Shape::MakeCapsule(0.2f, 0.05f);
    leg.offset = Vec3(0.4f, 0.1f, 0.2f);
    leg.material.density = 100.0f;
    children.Push(leg);
    const CompoundShapeConstPtr table = CompoundShape::Create(
        Span<const CompoundChild>(children.Data(), children.Size()), nullptr, &r);
    REQUIRE(r == Validation::Ok);
    lib.compound = world.Compounds().Add(table);
    lib.compound_extent = table->Extent();
    return lib;
}

Shape RandomShape(IndexRng& rng, const Library& lib, bool may_mesh)
{
    switch (rng.Next() % (may_mesh ? 6 : 5))
    {
    case 0: return Shape::MakeSphere(rng.Range(0.2f, 1.0f));
    case 1: return Shape::MakeBox(Vec3(rng.Range(0.1f, 1.0f), rng.Range(0.1f, 1.0f), rng.Range(0.1f, 1.0f)));
    case 2: return Shape::MakeCapsule(rng.Range(0.1f, 0.8f), rng.Range(0.1f, 0.5f));
    case 3: return Shape::MakeHull(lib.hull, lib.hull_extent);
    case 4: return Shape::MakeCompound(lib.compound, lib.compound_extent);
    default: return Shape::MakeMesh(lib.mesh, lib.mesh_extent);
    }
}

BodyId AddRandomBody(PhysicsWorld& world, IndexRng& rng, const Library& lib)
{
    BodyDesc desc;
    const u32 kind = rng.Next() % 3;
    desc.settings.type = kind == 0 ? BodyType::Static : (kind == 1 ? BodyType::Dynamic : BodyType::Kinematic);
    desc.position = rng.Point(12.0f);
    desc.orientation = rng.Turn();
    if (desc.settings.type == BodyType::Kinematic)
    {
        desc.velocity.linear = rng.Point(2.0f);
    }
    const BodyId id = world.CreateBody(desc);
    REQUIRE(id.IsValid());
    Collider c;
    c.shape = RandomShape(rng, lib, desc.settings.type == BodyType::Static);
    c.offset = rng.Point(0.3f);
    c.rotation = rng.Turn();
    c.layer = static_cast<u8>(rng.Next() % 4);
    c.is_trigger = rng.Next() % 8 == 0;
    REQUIRE(world.AddCollider(id, c) == BodyStatus::Ok);
    return id;
}

PhysicsQueryFilter RandomFilter(IndexRng& rng)
{
    PhysicsQueryFilter f;
    if (rng.Next() % 3 == 0)
    {
        f.layer_mask = rng.Next() | 1u;
    }
    f.include_triggers = rng.Next() % 2 == 0;
    f.include_static = rng.Next() % 5 != 0;
    f.include_kinematic = rng.Next() % 5 != 0;
    return f;
}

// Runs a query with the index and without it; `run` fills `out` from the world and returns a description.
template <class Run> void BothWays(PhysicsWorld& world, Run&& run)
{
    world.SetQueryIndexEnabled(true);
    run(true);
    world.SetQueryIndexEnabled(false);
    run(false);
    world.SetQueryIndexEnabled(true);
}

// Whether the index holds every collider's current exact box: the condition the queries' superset rests on.
bool IndexMatchesWorld(const PhysicsWorld& world)
{
    const QueryIndex* index = world.QueryIndexOf();
    if (index == nullptr)
    {
        return false;
    }
    for (u32 slot = 0; slot < world.Stats().collider_slots; ++slot)
    {
        const ColliderId id = world.ColliderAtSlot(slot);
        const ColliderRecord* record = id.IsValid() ? world.FindCollider(id) : nullptr;
        const Body* body = record != nullptr ? world.Find(record->owner) : nullptr;
        if (body != nullptr && !index->Holds(slot, id.generation, ColliderBounds(*body, record->value)))
        {
            return false;
        }
    }
    return true;
}

struct Tally
{
    u32 queries = 0;
    u32 hits = 0;
    u32 mismatches = 0;
    u32 stale = 0;  // comparisons that found the index behind the world
};

// Every kind of query at random places, compared between the two paths.
void CompareQueries(PhysicsWorld& world, IndexRng& rng, u32 count, Tally* tally)
{
    tally->stale += IndexMatchesWorld(world) ? 0 : 1;
    for (u32 n = 0; n < count; ++n)
    {
        const PhysicsQueryFilter filter = RandomFilter(rng);
        const Vec3 at = rng.Point(15.0f);
        Vec3 dir = rng.Point(1.0f);
        if (dir.LengthSquared() < 1e-4f)
        {
            dir = Vec3(0, -1, 0);
        }
        const f32 length = rng.Range(1.0f, 40.0f);
        switch (n % 5)
        {
        case 0:
        {
            PhysicsHit a;
            PhysicsHit b;
            bool fa = false;
            bool fb = false;
            BothWays(world, [&](bool indexed)
                     { (indexed ? fa : fb) = RayCast(world, at, dir, length, filter, indexed ? &a : &b); });
            tally->mismatches += (fa != fb || (fa && !SameHit(a, b))) ? 1 : 0;
            tally->hits += fa ? 1 : 0;
            break;
        }
        case 1:
        {
            Vector<PhysicsHit> a;
            Vector<PhysicsHit> b;
            BothWays(world, [&](bool indexed) { RayCastAll(world, at, dir, length, filter, indexed ? &a : &b); });
            bool same = a.Size() == b.Size();
            for (usize i = 0; same && i < a.Size(); ++i)
            {
                same = SameHit(a[i], b[i]);
            }
            tally->mismatches += same ? 0 : 1;
            tally->hits += a.Empty() ? 0 : 1;
            break;
        }
        case 2:
        {
            const Shape shape = rng.Next() % 2 == 0 ? Shape::MakeSphere(rng.Range(0.2f, 1.0f))
                                                    : Shape::MakeCapsule(rng.Range(0.2f, 0.8f), rng.Range(0.1f, 0.4f));
            const Quaternion turn = rng.Turn();
            const f32 cast = rng.Range(0.5f, 8.0f);
            PhysicsHit a;
            PhysicsHit b;
            bool fa = false;
            bool fb = false;
            BothWays(
                world, [&](bool indexed)
                { (indexed ? fa : fb) = ShapeCast(world, shape, at, turn, dir, cast, filter, indexed ? &a : &b); });
            tally->mismatches += (fa != fb || (fa && !SameHit(a, b))) ? 1 : 0;
            tally->hits += fa ? 1 : 0;
            break;
        }
        case 3:
        {
            const Shape shape = rng.Next() % 2 == 0
                                    ? Shape::MakeSphere(rng.Range(0.5f, 3.0f))
                                    : Shape::MakeBox(Vec3(rng.Range(0.5f, 3.0f), rng.Range(0.5f, 3.0f), 1.0f));
            const Quaternion turn = rng.Turn();
            Vector<ColliderId> a;
            Vector<ColliderId> b;
            BothWays(world, [&](bool indexed) { Overlap(world, shape, at, turn, filter, indexed ? &a : &b); });
            bool same = a.Size() == b.Size();
            for (usize i = 0; same && i < a.Size(); ++i)
            {
                same = a[i] == b[i];
            }
            tally->mismatches += same ? 0 : 1;
            tally->hits += a.Empty() ? 0 : 1;
            break;
        }
        default:
        {
            const f32 reach = rng.Range(0.5f, 6.0f);
            PhysicsHit a;
            PhysicsHit b;
            bool fa = false;
            bool fb = false;
            BothWays(world, [&](bool indexed)
                     { (indexed ? fa : fb) = ClosestPoint(world, at, reach, filter, indexed ? &a : &b); });
            tally->mismatches += (fa != fb || (fa && !SameHit(a, b))) ? 1 : 0;
            tally->hits += fa ? 1 : 0;
            break;
        }
        }
        ++tally->queries;
    }
}

// One random change of the world: a new body, a removal, a teleport, a new shape, a change of class, or steps.
void ChangeWorld(PhysicsWorld& world, IndexRng& rng, const Library& lib, Vector<BodyId>* bodies)
{
    const u32 what = rng.Next() % 7;
    if (what == 0 || bodies->Empty())
    {
        bodies->Push(AddRandomBody(world, rng, lib));
        return;
    }
    const usize pick = rng.Next() % bodies->Size();
    const BodyId id = (*bodies)[pick];
    const Body* body = world.Find(id);
    REQUIRE(body != nullptr);
    switch (what)
    {
    case 1:
        REQUIRE(world.DestroyBody(id) == BodyStatus::Ok);
        (*bodies)[pick] = (*bodies)[bodies->Size() - 1];
        bodies->Pop();
        break;
    case 2: REQUIRE(world.SetTransform(id, rng.Point(12.0f), rng.Turn()) == BodyStatus::Ok); break;
    case 3:
    {
        Collider c = world.FindCollider(body->first_collider)->value;
        c.shape = RandomShape(rng, lib, body->settings.type == BodyType::Static);
        c.offset = rng.Point(0.3f);
        REQUIRE(world.SetCollider(id, c) == BodyStatus::Ok);
        break;
    }
    case 4:
    {
        RigidBody settings = body->settings;
        const bool mesh = world.FindCollider(body->first_collider)->value.shape.kind == ShapeKind::Mesh;
        settings.type = (settings.type == BodyType::Static && !mesh) ? BodyType::Dynamic : BodyType::Static;
        REQUIRE(world.SetSettings(id, settings) == BodyStatus::Ok);
        break;
    }
    default:
        for (u32 s = rng.Next() % 4 + 1; s > 0; --s)
        {
            world.Step();
        }
        break;
    }
}

}  // namespace

TEST_CASE("Query index: every query equals the full scan over random changing scenes", "[physics][query][query_index]")
{
    IndexRng rng;
    Tally tally;
    for (u32 scene = 0; scene < 6; ++scene)
    {
        PhysicsWorld world;
        const Library lib = AddLibrary(world);
        Vector<BodyId> bodies;
        const u32 start = 20 + 30 * scene;
        for (u32 i = 0; i < start; ++i)
        {
            bodies.Push(AddRandomBody(world, rng, lib));
        }
        CompareQueries(world, rng, 50, &tally);
        for (u32 round = 0; round < 60; ++round)
        {
            ChangeWorld(world, rng, lib, &bodies);
            CompareQueries(world, rng, 10, &tally);
        }
        REQUIRE(world.QueryIndexOf() != nullptr);
        CHECK(world.QueryIndexOf()->Complete());
    }
    INFO("queries " << tally.queries << ", with a hit " << tally.hits);
    CHECK(tally.mismatches == 0);
    CHECK(tally.stale == 0);
    CHECK(tally.hits > tally.queries / 10);  // the scenes are not empty space
}

TEST_CASE("Query index: queries agree after a restore and from the pre-step hook", "[physics][query][query_index]")
{
    IndexRng rng;
    PhysicsWorld world;
    const Library lib = AddLibrary(world);
    Vector<BodyId> bodies;
    for (u32 i = 0; i < 80; ++i)
    {
        bodies.Push(AddRandomBody(world, rng, lib));
    }
    for (u32 i = 0; i < 20; ++i)
    {
        world.Step();
    }
    bytes::ByteWriter writer;
    world.Snapshot(&writer);
    PhysicsWorld restored;
    AddLibrary(restored);
    REQUIRE(restored.Restore(writer.Bytes()) == SnapshotStatus::Ok);
    Tally tally;
    CompareQueries(restored, rng, 200, &tally);
    CHECK(tally.mismatches == 0);
    CHECK(tally.stale == 0);

    // From the pre-step hook, the world is the one the step starts from.
    struct HookState
    {
        IndexRng* rng;
        Tally tally;
    } hook{&rng, Tally()};
    world.SetPreStepHook(
        [](void* user, PhysicsWorld* w, f32)
        { CompareQueries(*w, *static_cast<HookState*>(user)->rng, 20, &static_cast<HookState*>(user)->tally); }, &hook);
    for (u32 i = 0; i < 10; ++i)
    {
        world.Step();
    }
    CHECK(hook.tally.queries == 200);
    CHECK(hook.tally.mismatches == 0);
    CHECK(hook.tally.stale == 0);
}

TEST_CASE("Query index: a step moves only the boxes that left their fat box", "[physics][query][query_index]")
{
    PhysicsWorld world;
    BodyDesc floor_desc;
    floor_desc.settings.type = BodyType::Static;
    const BodyId floor = world.CreateBody(floor_desc);
    Collider floor_collider;
    floor_collider.shape = Shape::MakeBox(Vec3(50, 0.5f, 50));
    REQUIRE(world.AddCollider(floor, floor_collider) == BodyStatus::Ok);
    for (u32 i = 0; i < 50; ++i)
    {
        BodyDesc desc;
        desc.settings.type = BodyType::Dynamic;
        desc.position = Vec3(-20.0f + static_cast<f32>(i % 10) * 4.0f, 0.5f + 0.01f,
                             -10.0f + static_cast<f32>(i / 10) * 4.0f);
        const BodyId id = world.CreateBody(desc);
        Collider c;
        c.shape = Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f));
        REQUIRE(world.AddCollider(id, c) == BodyStatus::Ok);
    }
    REQUIRE(world.QueryIndexOf() != nullptr);
    CHECK(world.QueryIndexOf()->Stats().indexed == 51);
    for (u32 i = 0; i < 120; ++i)
    {
        world.Step();
    }
    const u64 settled = world.QueryIndexOf()->Stats().reinsertions;
    for (u32 i = 0; i < 60; ++i)
    {
        world.Step();
    }
    // Resting boxes move inside their fat boxes (by a rounding, if at all): no reinsertion once they rest.
    CHECK(world.QueryIndexOf()->Stats().reinsertions - settled <= 5);
    PhysicsHit hit;
    REQUIRE(RayCast(world, Vec3(-20.0f, 10.0f, -10.0f), Vec3(0, -1, 0), 20.0f, PhysicsQueryFilter(), &hit));
    CHECK(hit.distance > 8.0f);
    CHECK(hit.distance < 9.5f);
}

TEST_CASE("Query index: a sleeping pile woken by an impact is followed", "[physics][query][query_index]")
{
    IndexRng rng;
    PhysicsWorld world;
    BodyDesc floor_desc;
    floor_desc.settings.type = BodyType::Static;
    const BodyId floor = world.CreateBody(floor_desc);
    Collider floor_collider;
    floor_collider.shape = Shape::MakeBox(Vec3(30, 0.5f, 30));
    REQUIRE(world.AddCollider(floor, floor_collider) == BodyStatus::Ok);
    for (u32 i = 0; i < 60; ++i)
    {
        BodyDesc desc;
        desc.settings.type = BodyType::Dynamic;
        desc.position = Vec3(static_cast<f32>(i % 4) * 1.05f, 1.0f + static_cast<f32>(i / 4) * 1.02f, 0.0f);
        const BodyId id = world.CreateBody(desc);
        Collider c;
        c.shape = Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f));
        REQUIRE(world.AddCollider(id, c) == BodyStatus::Ok);
    }
    Tally tally;
    for (u32 i = 0; i < 600; ++i)
    {
        world.Step();
        if (i % 50 == 0)
        {
            CompareQueries(world, rng, 20, &tally);
        }
    }
    u32 asleep = 0;
    for (u32 slot = 0; slot < world.Stats().body_slots; ++slot)
    {
        const Body* b = world.Find(world.BodyAtSlot(slot));
        asleep += (b != nullptr && b->sleeping) ? 1 : 0;
    }
    CHECK(asleep > 0);  // the pile went to sleep, so the skip of sleeping bodies is exercised
    // A heavy ball into the pile wakes it; queries after every step agree while it falls apart and settles again.
    BodyDesc ball_desc;
    ball_desc.settings.type = BodyType::Dynamic;
    ball_desc.position = Vec3(1.5f, 25.0f, 0.0f);
    ball_desc.velocity.linear = Vec3(0.0f, -20.0f, 0.0f);
    const BodyId ball = world.CreateBody(ball_desc);
    Collider ball_collider;
    ball_collider.shape = Shape::MakeSphere(1.0f);
    ball_collider.material.density = 5000.0f;
    REQUIRE(world.AddCollider(ball, ball_collider) == BodyStatus::Ok);
    for (u32 i = 0; i < 400; ++i)
    {
        world.Step();
        CompareQueries(world, rng, 5, &tally);
    }
    CHECK(tally.mismatches == 0);
    CHECK(tally.stale == 0);
}
