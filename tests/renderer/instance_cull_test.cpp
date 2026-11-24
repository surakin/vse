// Culling an instance set by chunk and the ray query over its chunks (Phase 28 Day 496): the chunked result equals a
// per-instance test as a set, for 200 random cameras; it is the same at 1 and 8 threads; the fast paths are taken; the
// records are exact 10,000 km from the origin; a ray finds the instance a brute-force pass finds.

#include <catch2/catch_test_macros.hpp>

#include "algorithm.h"
#include "containers/vector.h"
#include "jobs/scheduler.h"
#include "lang/limits.h"
#include "log/output.h"
#include "os/os.h"
#include "renderer/camera.h"
#include "renderer/instance_cull.h"
#include "renderer/instance_query.h"

namespace vse::renderer
{

namespace
{

namespace instance_cull_test
{

struct Gen
{
    u64 s;
    explicit Gen(u64 seed) : s(seed * 6364136223846793005ull + 1442695040888963407ull) {}
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

InstanceSetPtr Scatter(u32 count, u64 seed, f64 base, f32 half, f32 height)
{
    InstanceSetDesc d;
    InstanceSetPtr set = InstanceSet::Create(d);
    REQUIRE(set.Get() != nullptr);
    Gen g(seed);
    for (u32 i = 0; i < count; ++i)
    {
        InstanceTransform t;
        t.position = Vec3d(base + g.Range(-half, half), g.Range(0.0f, height), base + g.Range(-half, half));
        const f32 s = g.Range(0.5f, 2.0f);
        t.scale = Vec3(s, s, s);
        REQUIRE(set->Add(t, nullptr, nullptr) == InstanceStatus::Ok);
    }
    return set;
}

InstanceCullParams Params(const Camera& cam, const Vec3d& origin)
{
    InstanceCullParams p;
    p.frustum = Frustum::FromViewProjection(cam.ViewProjection());
    p.render_origin = origin;
    p.camera_position = cam.position;
    p.mesh_radius = 1.5f;
    return p;
}

// The reference: every instance on its own, with the same arithmetic as the cull.
Vector<u64> BruteForce(const InstanceSet& set, const InstanceCullParams& p)
{
    Vector<u64> keys;
    for (u32 c = 0; c < set.ChunkCount(); ++c)
    {
        const InstanceChunkInfo info = set.Chunk(c);
        const Vec3 rel = (info.origin - p.render_origin).Narrow();
        const Span<const InstanceRecord> records = set.Records(c);
        for (u32 i = 0; i < info.count; ++i)
        {
            const Vec3 pos = rel + records[i].position;
            const f32 radius = p.mesh_radius * Max(records[i].scale.x, Max(records[i].scale.y, records[i].scale.z));
            bool visible = p.frustum.TestSphere(BoundingSphere{pos, radius}) != Containment::Outside;
            const f32 dist = (pos - p.camera_position).Length();
            if (visible && p.max_distance > 0.0f && dist > p.max_distance)
            {
                visible = false;
            }
            if (visible && p.min_size_ratio > 0.0f && radius < p.min_size_ratio * dist)
            {
                visible = false;
            }
            if (visible)
            {
                keys.Push((static_cast<u64>(c) << 32) | i);
            }
        }
    }
    return keys;
}

Vector<u64> Expand(const InstanceCullResult& r)
{
    Vector<u64> keys;
    for (usize k = 0; k < r.runs.Size(); ++k)
    {
        for (u32 i = 0; i < r.runs[k].count; ++i)
        {
            keys.Push((static_cast<u64>(r.runs[k].chunk) << 32) | (r.runs[k].first + i));
        }
    }
    return keys;
}

bool Same(const Vector<u64>& a, const Vector<u64>& b)
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

Camera RandomCamera(Gen& g, f32 half)
{
    const Vec3 eye(g.Range(-half, half), g.Range(0.0f, 60.0f), g.Range(-half, half));
    const Vec3 target(g.Range(-half, half), g.Range(0.0f, 20.0f), g.Range(-half, half));
    Camera cam = MakeLookAtCamera(eye, target, Vec3(0.0f, 1.0f, 0.0f));
    cam.far_plane = g.Range(60.0f, 400.0f);
    return cam;
}

}  // namespace instance_cull_test

}  // namespace

using namespace instance_cull_test;

TEST_CASE("The chunked cull equals a per-instance test for 200 random cameras", "[instance_cull]")
{
    InstanceSetPtr set = Scatter(100000, 1, 0.0, 300.0f, 40.0f);
    Gen g(5);
    InstanceCullResult out;
    u32 accepted = 0;
    u32 skipped = 0;
    u32 partial = 0;
    for (int i = 0; i < 200; ++i)
    {
        InstanceCullParams p = Params(RandomCamera(g, 300.0f), Vec3d());
        if (i % 3 == 1)
        {
            p.max_distance = g.Range(40.0f, 200.0f);
        }
        if (i % 3 == 2)
        {
            p.min_size_ratio = InstanceSizeRatio(g.Range(1.0f, 12.0f), 1080.0f, 1.0f);
        }
        CullInstances(*set, p, nullptr, &out);
        REQUIRE(Same(Expand(out), BruteForce(*set, p)));
        REQUIRE(out.stats.instances_emitted == Expand(out).Size());
        REQUIRE(out.stats.chunks_tested ==
                out.stats.chunks_skipped + out.stats.chunks_accepted + out.stats.chunks_partial);
        accepted += out.stats.chunks_accepted;
        skipped += out.stats.chunks_skipped;
        partial += out.stats.chunks_partial;
    }
    // The fast paths are taken: chunks are rejected whole and accepted whole, and the rest are tested one by one.
    CHECK(accepted > 0);
    CHECK(skipped > 0);
    CHECK(partial > 0);
}

TEST_CASE("The visible list is the same at 1 and 8 threads", "[instance_cull][concurrency]")
{
    InstanceSetPtr set = Scatter(60000, 2, 0.0, 400.0f, 30.0f);
    jobs::Scheduler::Config config;
    config.participants = 8;
    config.pin = false;
    jobs::Scheduler scheduler(config);
    Gen g(9);
    InstanceCullResult serial;
    InstanceCullResult parallel;
    for (int i = 0; i < 30; ++i)
    {
        const InstanceCullParams p = Params(RandomCamera(g, 400.0f), Vec3d());
        CullInstances(*set, p, nullptr, &serial);
        CullInstances(*set, p, &scheduler, &parallel);
        REQUIRE(serial.runs.Size() == parallel.runs.Size());
        for (usize k = 0; k < serial.runs.Size(); ++k)
        {
            REQUIRE(serial.runs[k].chunk == parallel.runs[k].chunk);
            REQUIRE(serial.runs[k].first == parallel.runs[k].first);
            REQUIRE(serial.runs[k].count == parallel.runs[k].count);
        }
        REQUIRE(serial.stats.instances_tested == parallel.stats.instances_tested);
        REQUIRE(serial.stats.chunks_accepted == parallel.stats.chunks_accepted);
    }
}

TEST_CASE("A bad parameter finds nothing and an empty set is empty", "[instance_cull]")
{
    InstanceSetPtr set = Scatter(500, 3, 0.0, 20.0f, 5.0f);
    Camera cam = MakeLookAtCamera(Vec3(0.0f, 5.0f, 30.0f), Vec3(0.0f, 2.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    InstanceCullParams p = Params(cam, Vec3d());
    InstanceCullResult out;
    CullInstances(*set, p, nullptr, &out);
    CHECK(out.stats.instances_emitted > 0);
    p.mesh_radius = Limits<f32>::QuietNaN();
    CullInstances(*set, p, nullptr, &out);
    CHECK(out.runs.Size() == 0);
    p.mesh_radius = 1.0f;
    p.max_distance = -1.0f;
    CullInstances(*set, p, nullptr, &out);
    CHECK(out.runs.Size() == 0);
    set->Clear();
    p.max_distance = 0.0f;
    CullInstances(*set, p, nullptr, &out);
    CHECK(out.runs.Size() == 0);
    CHECK(out.stats.chunks_tested == 0);
}

TEST_CASE("A set 10,000 km away, culled from its own camera, writes the records an f64 reference gives",
          "[instance_cull]")
{
    const f64 base = 1.0e7;
    InstanceSetPtr set = Scatter(20000, 4, base, 200.0f, 30.0f);
    const Vec3d origin(base, 10.0, base + 250.0);
    // The camera is at the render origin; the view looks at the middle of the set.
    Camera cam = MakeLookAtCamera(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 5.0f, -250.0f), Vec3(0.0f, 1.0f, 0.0f));
    cam.far_plane = 600.0f;
    InstanceCullParams p = Params(cam, origin);
    InstanceCullResult out;
    CullInstances(*set, p, nullptr, &out);
    REQUIRE(out.stats.instances_emitted > 0);
    REQUIRE(Same(Expand(out), BruteForce(*set, p)));
    Vector<InstanceRecord> records;
    CompactInstances(*set, Span<const InstanceRun>(out.runs.Data(), out.runs.Size()), origin, &records);
    REQUIRE(records.Size() == out.stats.instances_emitted);
    usize at = 0;
    for (usize k = 0; k < out.runs.Size(); ++k)
    {
        const InstanceChunkInfo info = set->Chunk(out.runs[k].chunk);
        const Span<const InstanceRecord> source = set->Records(out.runs[k].chunk);
        for (u32 i = 0; i < out.runs[k].count; ++i, ++at)
        {
            const Vec3d world = info.origin + Vec3d(source[out.runs[k].first + i].position);
            const Vec3 want = (world - origin).Narrow();
            REQUIRE(records[at].position.x == want.x);
            REQUIRE(records[at].position.y == want.y);
            REQUIRE(records[at].position.z == want.z);
            // Near the camera the numbers are small: no precision was lost to the distance from the world origin.
            REQUIRE(Abs(records[at].position.x) < 700.0f);
        }
    }
}

TEST_CASE("A ray finds the instance a brute-force pass finds", "[instance_cull]")
{
    InstanceSetPtr set = Scatter(5000, 6, 0.0, 100.0f, 20.0f);
    InstanceQuery query;
    REQUIRE(query.Refresh(*set, 1.5f));
    REQUIRE(!query.Refresh(*set, 1.5f));
    Gen g(8);
    int hits = 0;
    for (int i = 0; i < 300; ++i)
    {
        const Vec3d from(g.Range(-120.0f, 120.0f), g.Range(-5.0f, 40.0f), g.Range(-120.0f, 120.0f));
        const Vec3d to(g.Range(-100.0f, 100.0f), g.Range(0.0f, 20.0f), g.Range(-100.0f, 100.0f));
        const Rayd ray = Rayd::Make(from, to - from);
        InstanceHit hit;
        const bool got = query.Raycast(*set, ray, 1.0, &hit);
        bool want = false;
        f64 best = 1.0;
        for (u32 c = 0; c < set->ChunkCount(); ++c)
        {
            const InstanceChunkInfo info = set->Chunk(c);
            for (u32 k = 0; k < info.count; ++k)
            {
                const InstanceRecord& r = set->Records(c)[k];
                const f64 reach = 1.5 * Max(r.scale.x, Max(r.scale.y, r.scale.z));
                const Vec3d centre = info.origin + Vec3d(r.position);
                const Aabbd box(centre - Vec3d(reach, reach, reach), centre + Vec3d(reach, reach, reach));
                f64 t = 0.0;
                if (Intersect(ray, box, best, &t) && (!want || t < best))
                {
                    want = true;
                    best = t;
                }
            }
        }
        REQUIRE(got == want);
        if (got)
        {
            ++hits;
            REQUIRE(hit.t == best);
            REQUIRE(set->Contains(hit.id));
        }
    }
    CHECK(hits > 10);
    // An edit makes the next refresh rebuild the tree.
    InstanceId id = set->IdAt(0, 0);
    InstanceTransform t;
    REQUIRE(set->Get(id, &t) == InstanceStatus::Ok);
    t.position.x += 1.0;
    REQUIRE(set->SetTransform(id, t) == InstanceStatus::Ok);
    REQUIRE(query.Refresh(*set, 1.5f));
}

TEST_CASE("Culling a million instances: the cost, recorded", "[instance_cull]")
{
    InstanceSetPtr set = Scatter(1000000, 7, 0.0, 2000.0f, 40.0f);
    jobs::Scheduler::Config config;
    config.participants = 4;
    config.pin = false;
    jobs::Scheduler scheduler(config);
    Camera cam = MakeLookAtCamera(Vec3(0.0f, 30.0f, 0.0f), Vec3(300.0f, 10.0f, 300.0f), Vec3(0.0f, 1.0f, 0.0f));
    cam.far_plane = 600.0f;
    const InstanceCullParams p = Params(cam, Vec3d());
    InstanceCullResult out;
    CullInstances(*set, p, nullptr, &out);  // warm
    const u64 t0 = os::MonotonicTimeNanos();
    for (int i = 0; i < 5; ++i)
    {
        CullInstances(*set, p, nullptr, &out);
    }
    const u64 t1 = os::MonotonicTimeNanos();
    for (int i = 0; i < 5; ++i)
    {
        CullInstances(*set, p, &scheduler, &out);
    }
    const u64 t2 = os::MonotonicTimeNanos();
    out::Print(
        "instance cull: 1,000,000 instances, %u visible in %u runs (%u of %u chunks skipped): %u us on one thread, %u us on 4\n",
        out.stats.instances_emitted, out.stats.runs, out.stats.chunks_skipped, set->ChunkCount(),
        static_cast<unsigned>((t1 - t0) / 5000), static_cast<unsigned>((t2 - t1) / 5000));
    CHECK(out.stats.instances_emitted > 0);
    CHECK(out.stats.chunks_skipped > out.stats.chunks_tested / 2);
}

}  // namespace vse::renderer
