// Queries (Phase 25 Day 391): rays with analytic hits, filters, shape casts, overlaps and closest points.
#include <catch2/catch_test_macros.hpp>

#include "jobs/parallel_for.h"
#include "jobs/scheduler.h"
#include "lang/scalar.h"
#include "physics/collide.h"
#include "physics/query.h"
#include "physics/triangle_mesh.h"
#include "test_hulls.h"

using namespace engine_placeholder;
using namespace engine_placeholder::physics;

namespace
{

bool QNear(f32 a, f32 b, f32 tolerance = 1e-3f)
{
    return Abs(a - b) <= tolerance;
}

BodyId QPlace(PhysicsWorld& world, const Shape& shape, const Vec3& position,
              const Quaternion& rotation = Quaternion::Identity(), BodyType type = BodyType::Static, u8 layer = 0,
              bool trigger = false)
{
    BodyDesc desc;
    desc.settings.type = type;
    desc.position = position;
    desc.orientation = rotation;
    const BodyId id = world.CreateBody(desc);
    Collider c;
    c.shape = shape;
    c.layer = layer;
    c.is_trigger = trigger;
    REQUIRE(world.AddCollider(id, c) == BodyStatus::Ok);
    return id;
}

bool QRay(const PhysicsWorld& world, const Vec3& o, const Vec3& d, PhysicsHit* hit,
          const PhysicsQueryFilter& filter = PhysicsQueryFilter())
{
    return RayCast(world, o, d, 100.0f, filter, hit);
}

}  // namespace

TEST_CASE("Query: rays hit each shape where the geometry says", "[physics][query]")
{
    PhysicsWorld world;
    PhysicsHit h;
    QPlace(world, Shape::MakeSphere(1.0f), Vec3(0, 0, 5));
    REQUIRE(QRay(world, Vec3(0, 0, 0), Vec3(0, 0, 1), &h));
    CHECK(QNear(h.distance, 4.0f));
    CHECK(QNear(h.normal.z, -1.0f));
    CHECK(QNear(h.point.z, 4.0f));
    CHECK(h.triangle == kNoTriangle);
    CHECK(QRay(world, Vec3(0, 0.999f, 0), Vec3(0, 0, 1), &h));   // grazing, inside the radius
    CHECK(!QRay(world, Vec3(0, 1.001f, 0), Vec3(0, 0, 1), &h));  // grazing, outside it
    CHECK(!QRay(world, Vec3(0, 0, 0), Vec3(0, 0, -1), &h));      // away
    // Inside: distance zero, the normal against the ray.
    REQUIRE(QRay(world, Vec3(0, 0, 5), Vec3(1, 0, 0), &h));
    CHECK(h.distance == 0.0f);
    CHECK(QNear(h.normal.x, -1.0f));

    PhysicsWorld boxes;
    QPlace(boxes, Shape::MakeBox(Vec3(1, 2, 3)), Vec3(0, 0, 10));
    REQUIRE(QRay(boxes, Vec3(0.5f, 0.5f, 0), Vec3(0, 0, 1), &h));
    CHECK(QNear(h.distance, 7.0f));
    CHECK(QNear(h.normal.z, -1.0f));
    REQUIRE(QRay(boxes, Vec3(-5, 0, 10), Vec3(1, 0, 0), &h));
    CHECK(QNear(h.distance, 4.0f));
    CHECK(QNear(h.normal.x, -1.0f));
    // A box turned 90 degrees about z: its half extents swap on x and y.
    PhysicsWorld turned;
    QPlace(turned, Shape::MakeBox(Vec3(1, 2, 3)), Vec3(0, 0, 10), Quaternion::FromAxisAngle(Vec3(0, 0, 1), 1.5707963f));
    REQUIRE(QRay(turned, Vec3(-5, 0, 10), Vec3(1, 0, 0), &h));
    CHECK(QNear(h.distance, 3.0f));

    PhysicsWorld capsules;
    QPlace(capsules, Shape::MakeCapsule(1.0f, 0.5f), Vec3(0, 0, 5));
    REQUIRE(QRay(capsules, Vec3(0, 0, 0), Vec3(0, 0, 1), &h));
    CHECK(QNear(h.distance, 4.5f));
    REQUIRE(QRay(capsules, Vec3(0, 1.25f, 0), Vec3(0, 0, 1), &h));  // the upper cap
    CHECK(QNear(h.distance, 5.0f - Sqrt(0.25f - 0.0625f)));
    CHECK(!QRay(capsules, Vec3(0, 1.6f, 0), Vec3(0, 0, 1), &h));
    REQUIRE(QRay(capsules, Vec3(0, 5, 5), Vec3(0, -1, 0), &h));  // down onto the cap
    CHECK(QNear(h.distance, 5.0f - 1.5f));

    PhysicsWorld hulls;
    const u32 id = hulls.Hulls().Add(physics::testing::BoxHull(Vec3(1, 1, 1)));
    QPlace(hulls, Shape::MakeHull(id, Vec3(1, 1, 1)), Vec3(0, 0, 5));
    REQUIRE(QRay(hulls, Vec3(0.2f, 0.2f, 0), Vec3(0, 0, 1), &h));
    CHECK(QNear(h.distance, 4.0f));
    CHECK(QNear(h.normal.z, -1.0f));
}

TEST_CASE("Query: a ray hits a mesh and names the triangle", "[physics][query]")
{
    PhysicsWorld world;
    Vector<Vec3> v;
    v.Push(Vec3(10, 0, -5));
    v.Push(Vec3(10, 4, -5));
    v.Push(Vec3(10, 4, 5));
    v.Push(Vec3(10, 0, 5));
    Vector<u32> idx;
    for (const u32 i : {0u, 1u, 2u, 0u, 2u, 3u})
    {
        idx.Push(i);
    }
    TriangleMeshSpec spec;
    spec.positions = Span<const Vec3>(v.Data(), v.Size());
    spec.indices = Span<const u32>(idx.Data(), idx.Size());
    Validation r = Validation::Ok;
    const TriangleMeshConstPtr mesh = TriangleMesh::Create(spec, &r);
    REQUIRE(r == Validation::Ok);
    QPlace(world, Shape::MakeMesh(world.Meshes().Add(mesh), mesh->Extent()), Vec3(0, 0, 0));
    PhysicsHit h;
    REQUIRE(QRay(world, Vec3(0, 2, 1), Vec3(1, 0, 0), &h));
    CHECK(QNear(h.distance, 10.0f));
    CHECK(h.triangle < 2);
    CHECK(QNear(h.normal.x, -1.0f));
    REQUIRE(QRay(world, Vec3(20, 2, 1), Vec3(-1, 0, 0), &h));  // from the other side
    CHECK(QNear(h.normal.x, 1.0f));
    CHECK(!QRay(world, Vec3(0, 6, 1), Vec3(1, 0, 0), &h));
}

TEST_CASE("Query: RayCastAll is sorted and the filters apply", "[physics][query]")
{
    PhysicsWorld world;
    const BodyId far_box = QPlace(world, Shape::MakeBox(Vec3(1, 1, 1)), Vec3(0, 0, 20), Quaternion::Identity(),
                                  BodyType::Static, 2);
    const BodyId near_box = QPlace(world, Shape::MakeBox(Vec3(1, 1, 1)), Vec3(0, 0, 10), Quaternion::Identity(),
                                   BodyType::Dynamic, 1);
    const BodyId trigger = QPlace(world, Shape::MakeBox(Vec3(1, 1, 1)), Vec3(0, 0, 5), Quaternion::Identity(),
                                  BodyType::Static, 0, true);
    Vector<PhysicsHit> hits;
    PhysicsQueryFilter filter;
    CHECK(RayCastAll(world, Vec3(0, 0, 0), Vec3(0, 0, 1), 100.0f, filter, &hits) == 2);  // no trigger
    CHECK(hits[0].body == near_box);
    CHECK(hits[1].body == far_box);
    CHECK(hits[0].distance < hits[1].distance);
    filter.include_triggers = true;
    CHECK(RayCastAll(world, Vec3(0, 0, 0), Vec3(0, 0, 1), 100.0f, filter, &hits) == 3);
    CHECK(hits[0].body == trigger);
    filter.include_triggers = false;
    filter.include_dynamic = false;
    PhysicsHit h;
    REQUIRE(RayCast(world, Vec3(0, 0, 0), Vec3(0, 0, 1), 100.0f, filter, &h));
    CHECK(h.body == far_box);
    filter.include_dynamic = true;
    filter.include_static = false;
    REQUIRE(RayCast(world, Vec3(0, 0, 0), Vec3(0, 0, 1), 100.0f, filter, &h));
    CHECK(h.body == near_box);
    filter.include_static = true;
    filter.layer_mask = ~(1u << 1);  // not layer 1
    REQUIRE(RayCast(world, Vec3(0, 0, 0), Vec3(0, 0, 1), 100.0f, filter, &h));
    CHECK(h.body == far_box);
    filter.layer_mask = 0xFFFFFFFFu;
    const BodyId ignored[] = {near_box};
    filter.ignore = Span<const BodyId>(ignored, 1);
    REQUIRE(RayCast(world, Vec3(0, 0, 0), Vec3(0, 0, 1), 100.0f, filter, &h));
    CHECK(h.body == far_box);
    CHECK(!RayCast(world, Vec3(0, 0, 0), Vec3(0, 0, 1), 3.0f, filter, &h));    // max distance
    CHECK(!RayCast(world, Vec3(0, 0, 0), Vec3(0, 0, 0), 100.0f, filter, &h));  // no direction
}

TEST_CASE("Query: a ray agrees with a marched probe over random shapes", "[physics][query]")
{
    PhysicsWorld world;
    u64 state = 99;
    const auto rnd = [&]
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<f32>((state >> 40) & 0xffffff) / 16777216.0f;
    };
    for (u32 i = 0; i < 8; ++i)
    {
        const Vec3 at(-4.0f + 8.0f * rnd(), -4.0f + 8.0f * rnd(), -4.0f + 8.0f * rnd());
        const Quaternion q = Quaternion::FromAxisAngle(Vec3(0, 1, 0), 6.0f * rnd());
        switch (i % 3)
        {
        case 0: QPlace(world, Shape::MakeSphere(0.5f + rnd()), at, q); break;
        case 1: QPlace(world, Shape::MakeBox(Vec3(0.5f + rnd(), 0.5f + rnd(), 0.5f + rnd())), at, q); break;
        default: QPlace(world, Shape::MakeCapsule(0.5f + rnd(), 0.4f + 0.3f * rnd()), at, q); break;
        }
    }
    const PhysicsQueryFilter filter;
    for (u32 n = 0; n < 150; ++n)
    {
        const Vec3 o(-8.0f + 16.0f * rnd(), -8.0f + 16.0f * rnd(), -8.0f + 16.0f * rnd());
        const Vec3 d(rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f);
        if (d.LengthSquared() < 1e-4f)
        {
            continue;
        }
        const Vec3 dir = d * (1.0f / Sqrt(d.LengthSquared()));
        PhysicsHit h;
        const bool hit = RayCast(world, o, dir, 12.0f, filter, &h);
        // March a small probe along the ray: where it first touches something, the ray must have hit by then.
        f32 marched = -1.0f;
        for (f32 t = 0.0f; t < 12.0f && marched < 0.0f; t += 0.01f)
        {
            Vector<ColliderId> found;
            if (Overlap(world, Shape::MakeSphere(0.002f), o + dir * t, Quaternion::Identity(), filter, &found) > 0)
            {
                marched = t;
            }
        }
        if (marched >= 0.0f)
        {
            REQUIRE(hit);
            // A ray that grazes the surface passes a probe's reach far before it meets the surface: only a ray that
            // arrives at an angle is compared.
            if (Abs(h.normal.Dot(dir)) > 0.2f)
            {
                CHECK(h.distance <= marched + 0.01f);
            }
        }
        if (hit)
        {
            // The hit point is on the surface.
            PhysicsHit near;
            REQUIRE(ClosestPoint(world, h.point, 0.5f, filter, &near));
            CHECK(near.distance < 2e-3f);
            CHECK(QNear(h.point.x, o.x + dir.x * h.distance, 1e-4f));
        }
    }
}

TEST_CASE("Query: a sphere cast equals the ray against the grown shape", "[physics][query]")
{
    PhysicsWorld world;
    QPlace(world, Shape::MakeSphere(1.0f), Vec3(0, 0, 10));
    PhysicsHit h;
    REQUIRE(ShapeCast(world, Shape::MakeSphere(0.3f), Vec3(0, 0, 0), Quaternion::Identity(), Vec3(0, 0, 1), 20.0f,
                      PhysicsQueryFilter(), &h));
    CHECK(QNear(h.distance, 8.7f, 2e-3f));
    CHECK(QNear(h.normal.z, -1.0f, 1e-2f));
    PhysicsWorld grown;
    QPlace(grown, Shape::MakeSphere(1.3f), Vec3(0, 0, 10));
    PhysicsHit ray;
    REQUIRE(QRay(grown, Vec3(0, 0, 0), Vec3(0, 0, 1), &ray));
    CHECK(QNear(h.distance, ray.distance, 2e-3f));
    // A box target: the face is 0.3 closer.
    PhysicsWorld box;
    QPlace(box, Shape::MakeBox(Vec3(1, 1, 1)), Vec3(0, 0, 10));
    REQUIRE(ShapeCast(box, Shape::MakeSphere(0.3f), Vec3(0, 0, 0), Quaternion::Identity(), Vec3(0, 0, 1), 20.0f,
                      PhysicsQueryFilter(), &h));
    CHECK(QNear(h.distance, 8.7f, 2e-3f));
    // A capsule and a box swept sideways.
    REQUIRE(ShapeCast(box, Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f)), Vec3(-10, 0, 10), Quaternion::Identity(),
                      Vec3(1, 0, 0), 20.0f, PhysicsQueryFilter(), &h));
    CHECK(QNear(h.distance, 8.5f, 2e-3f));
    // Starting in contact: distance zero. Out of reach and refused inputs: no hit.
    REQUIRE(ShapeCast(box, Shape::MakeSphere(0.3f), Vec3(0, 0, 10), Quaternion::Identity(), Vec3(0, 0, 1), 5.0f,
                      PhysicsQueryFilter(), &h));
    CHECK(h.distance == 0.0f);
    CHECK(!ShapeCast(box, Shape::MakeSphere(0.3f), Vec3(0, 0, 0), Quaternion::Identity(), Vec3(0, 0, 1), 5.0f,
                     PhysicsQueryFilter(), &h));
    CHECK(!ShapeCast(box, Shape::MakeSphere(-1.0f), Vec3(0, 0, 0), Quaternion::Identity(), Vec3(0, 0, 1), 5.0f,
                     PhysicsQueryFilter(), &h));
}

TEST_CASE("Query: overlaps are sorted and equal a brute-force pass", "[physics][query]")
{
    PhysicsWorld world;
    u64 state = 7;
    const auto rnd = [&]
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<f32>((state >> 40) & 0xffffff) / 16777216.0f;
    };
    for (u32 i = 0; i < 40; ++i)
    {
        const Vec3 at(-10.0f + 20.0f * rnd(), -10.0f + 20.0f * rnd(), -10.0f + 20.0f * rnd());
        if (i % 2 == 0)
        {
            QPlace(world, Shape::MakeSphere(0.5f + rnd()), at);
        }
        else
        {
            QPlace(world, Shape::MakeBox(Vec3(0.5f, 0.5f + rnd(), 0.5f)), at, Quaternion::Identity(),
                   BodyType::Dynamic);
        }
    }
    const PhysicsQueryFilter filter;
    for (u32 n = 0; n < 60; ++n)
    {
        const Vec3 at(-10.0f + 20.0f * rnd(), -10.0f + 20.0f * rnd(), -10.0f + 20.0f * rnd());
        const Shape shape = n % 2 == 0 ? Shape::MakeSphere(1.0f + 2.0f * rnd()) : Shape::MakeBox(Vec3(2, 1, 2));
        Vector<ColliderId> got;
        Overlap(world, shape, at, Quaternion::Identity(), filter, &got);
        for (usize i = 1; i < got.Size(); ++i)
        {
            CHECK(got[i - 1].ToKey() < got[i].ToKey());
        }
        Vector<ColliderId> expected;
        const u32 slots = world.Stats().collider_slots;
        for (u32 s = 0; s < slots; ++s)
        {
            const ColliderId id = world.ColliderAtSlot(s);
            const ColliderRecord* record = world.FindCollider(id);
            const Body* body = world.Find(record->owner);
            Pose a;
            a.position = at;
            Pose b;
            b.position = body->position;
            b.rotation = body->orientation;
            ContactManifold m;
            if (Collide(shape, a, record->value.shape, b, 0.0f, &m, &world.Hulls(), &world.Meshes(),
                        &world.Compounds()) == CollideResult::Contact)
            {
                expected.Push(id);
            }
        }
        // `expected` is in slot order; the result is in key order. The same set.
        CHECK(got.Size() == expected.Size());
        for (usize i = 0; i < expected.Size(); ++i)
        {
            bool in = false;
            for (usize j = 0; j < got.Size(); ++j)
            {
                in = in || got[j] == expected[i];
            }
            CHECK(in);
        }
    }
}

TEST_CASE("Query: the closest point on a surface", "[physics][query]")
{
    PhysicsWorld world;
    QPlace(world, Shape::MakeSphere(1.0f), Vec3(0, 0, 5));
    QPlace(world, Shape::MakeBox(Vec3(1, 1, 1)), Vec3(10, 0, 0));
    PhysicsHit h;
    REQUIRE(ClosestPoint(world, Vec3(0, 0, 0), 20.0f, PhysicsQueryFilter(), &h));
    CHECK(QNear(h.distance, 4.0f));
    CHECK(QNear(h.point.z, 4.0f));
    CHECK(QNear(h.normal.z, -1.0f, 1e-2f));
    REQUIRE(ClosestPoint(world, Vec3(7, 0.2f, 0.1f), 20.0f, PhysicsQueryFilter(), &h));
    CHECK(QNear(h.distance, 2.0f));
    CHECK(QNear(h.point.x, 9.0f));
    REQUIRE(ClosestPoint(world, Vec3(0, 0, 5), 20.0f, PhysicsQueryFilter(), &h));  // inside
    CHECK(h.distance == 0.0f);
    CHECK(!ClosestPoint(world, Vec3(0, 0, 0), 2.0f, PhysicsQueryFilter(), &h));  // too far
}

TEST_CASE("Query: queries run from jobs and agree with the same queries run in order", "[physics][query]")
{
    PhysicsWorld world;
    for (u32 i = 0; i < 30; ++i)
    {
        QPlace(world, Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f)),
               Vec3(static_cast<f32>(i % 6) * 3.0f, static_cast<f32>(i / 6) * 3.0f, 10.0f));
    }
    constexpr u32 kRays = 512;
    Vector<f32> serial;
    Vector<f32> parallel;
    serial.Resize(kRays);
    parallel.Resize(kRays);
    const auto cast = [&](Vector<f32>* out, usize begin, usize end)
    {
        for (usize i = begin; i < end; ++i)
        {
            PhysicsHit h;
            const Vec3 origin(static_cast<f32>(i % 18) * 1.0f, static_cast<f32>(i / 18) * 0.5f, 0.0f);
            (*out)[i] = RayCast(world, origin, Vec3(0, 0, 1), 100.0f, PhysicsQueryFilter(), &h) ? h.distance : -1.0f;
        }
    };
    cast(&serial, 0, kRays);
    jobs::Scheduler::Config config;
    config.participants = 4;
    config.pin = false;
    jobs::Scheduler scheduler(config);
    auto run = [&](usize begin, usize end) { cast(&parallel, begin, end); };
    jobs::ParallelForBlocking(scheduler, kRays, 16, run);
    for (u32 i = 0; i < kRays; ++i)
    {
        CHECK(serial[i] == parallel[i]);
    }
}

TEST_CASE("ShapeCast: a distance beyond the world is refused and a long one is sampled with a bounded count",
          "[physics][query]")
{
    PhysicsWorld world;
    BodyDesc desc;
    desc.settings.type = BodyType::Static;
    desc.position = Vec3(0, 0, 5);
    const BodyId wall = world.CreateBody(desc);
    Collider c;
    c.shape = Shape::MakeBox(Vec3(5, 5, 0.5f));
    REQUIRE(world.AddCollider(wall, c) == BodyStatus::Ok);
    PhysicsHit hit;
    const Shape ball = Shape::MakeSphere(0.5f);
    CHECK(!ShapeCast(world, ball, Vec3(0, 0, 0), Quaternion::Identity(), Vec3(0, 0, 1), 3.0e38f, PhysicsQueryFilter(),
                     &hit));
    CHECK(!ShapeCast(world, ball, Vec3(0, 0, 0), Quaternion::Identity(), Vec3(0, 0, 1), 1.0e12f, PhysicsQueryFilter(),
                     &hit));
    CHECK(ShapeCast(world, ball, Vec3(0, 0, 0), Quaternion::Identity(), Vec3(0, 0, 1), 10.0f, PhysicsQueryFilter(),
                    &hit));
}
