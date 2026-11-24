// Snapshot, restore and replay (Phase 25 Day 406): a run restored from a snapshot continues bit for bit, a replayed
// input log reproduces a run, and corrupt bytes are refused.
#include <catch2/catch_test_macros.hpp>

#include "lang/scalar.h"
#include "os/os.h"
#include "physics/input_log.h"
#include "physics/snapshot.h"
#include "storage/memory_store.h"
#include "test_hulls.h"

using namespace vse;
using namespace vse::physics;

namespace
{

u64 SnHash(const PhysicsWorld& world)
{
    bool valid = false;
    const u64 h = world.StateHash(&valid);
    REQUIRE(valid);
    return h;
}

BodyId SnBody(PhysicsWorld& world, BodyType type, const Vec3& position, const Shape& shape, f32 friction = 0.6f)
{
    BodyDesc desc;
    desc.settings.type = type;
    desc.position = position;
    const BodyId id = world.CreateBody(desc);
    REQUIRE(id.IsValid());
    Collider c;
    c.shape = shape;
    c.material.friction = friction;
    REQUIRE(world.AddCollider(id, c) == BodyStatus::Ok);
    return id;
}

// A floor, a stack of boxes, a ball dropped on them and a hinge chain: contacts, sleeping and joints all in one world.
void BuildScene(PhysicsWorld& world)
{
    StepConfig config = world.GetStepConfig();
    config.sleep_time = 0.2f;
    REQUIRE(world.SetStepConfig(config) == BodyStatus::Ok);
    SnBody(world, BodyType::Static, Vec3(0, -0.5f, 0), Shape::MakeBox(Vec3(30, 0.5f, 30)));
    for (u32 i = 0; i < 5; ++i)
    {
        SnBody(world, BodyType::Dynamic, Vec3(0, 0.5f + 1.0f * static_cast<f32>(i), 0),
               Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f)));
    }
    SnBody(world, BodyType::Dynamic, Vec3(0.2f, 9.0f, 0), Shape::MakeSphere(0.4f));
    SnBody(world, BodyType::Dynamic, Vec3(6, 0.4f, 0), Shape::MakeCapsule(0.4f, 0.3f));
    const BodyId anchor = SnBody(world, BodyType::Static, Vec3(-5, 8, 0), Shape::MakeSphere(0.1f));
    BodyId previous = anchor;
    for (u32 i = 0; i < 4; ++i)
    {
        const BodyId link = SnBody(world, BodyType::Dynamic, Vec3(-5 + 0.6f * static_cast<f32>(i + 1), 8, 0),
                                   Shape::MakeBox(Vec3(0.3f, 0.1f, 0.1f)));
        REQUIRE(world
                    .CreateJoint(world.MakeHingeJoint(previous, link, Vec3(-5 + 0.6f * static_cast<f32>(i), 8, 0),
                                                      Vec3(0, 0, 1)))
                    .IsValid());
        previous = link;
    }
}

Vector<byte> Bytes(const PhysicsWorld& world)
{
    bytes::ByteWriter writer;
    world.Snapshot(&writer);
    Vector<byte> out;
    for (usize i = 0; i < writer.Size(); ++i)
    {
        out.Push(writer.Bytes()[i]);
    }
    return out;
}

ByteSpan View(const Vector<byte>& v)
{
    return ByteSpan(v.Data(), v.Size());
}

}  // namespace

TEST_CASE("Snapshot: a restore at a dozen points of a run continues bit for bit", "[physics][snapshot]")
{
    PhysicsWorld original;
    BuildScene(original);
    for (u32 point = 0; point < 12; ++point)
    {
        for (u32 i = 0; i < 17; ++i)
        {
            original.Step();
        }
        const Vector<byte> bytes = Bytes(original);
        PhysicsWorld restored;
        REQUIRE(restored.Restore(View(bytes)) == SnapshotStatus::Ok);
        CHECK(SnHash(restored) == SnHash(original));
        // A snapshot of the restored world is the same bytes (nothing is lost or invented).
        const Vector<byte> again = Bytes(restored);
        REQUIRE(again.Size() == bytes.Size());
        bool same = true;
        for (usize k = 0; k < bytes.Size(); ++k)
        {
            same = same && again[k] == bytes[k];
        }
        CHECK(same);
        // Both continue in step, with the warm-start caches and the sleeping state restored.
        PhysicsWorld fork;
        REQUIRE(fork.Restore(View(bytes)) == SnapshotStatus::Ok);
        PhysicsWorld copy_of_original;
        REQUIRE(copy_of_original.Restore(View(bytes)) == SnapshotStatus::Ok);
        for (u32 i = 0; i < 12; ++i)
        {
            fork.Step();
            copy_of_original.Step();
            REQUIRE(SnHash(fork) == SnHash(copy_of_original));
        }
    }
    // The original, never restored, reaches the same state as a restored copy that ran the same steps.
    PhysicsWorld reference;
    BuildScene(reference);
    for (u32 i = 0; i < 12 * 17; ++i)
    {
        reference.Step();
    }
    CHECK(SnHash(reference) == SnHash(original));
}

TEST_CASE("Snapshot: a restored world continues exactly as the original (hash after every step)", "[physics][snapshot]")
{
    PhysicsWorld a;
    BuildScene(a);
    for (u32 i = 0; i < 100; ++i)
    {
        a.Step();
    }
    const Vector<byte> bytes = Bytes(a);
    PhysicsWorld b;
    REQUIRE(b.Restore(View(bytes)) == SnapshotStatus::Ok);
    for (u32 i = 0; i < 200; ++i)
    {
        a.Step();
        b.Step();
        REQUIRE(SnHash(a) == SnHash(b));
    }
    // Ids made after the restore are the same too.
    const BodyId x = SnBody(a, BodyType::Dynamic, Vec3(3, 3, 3), Shape::MakeSphere(0.3f));
    const BodyId y = SnBody(b, BodyType::Dynamic, Vec3(3, 3, 3), Shape::MakeSphere(0.3f));
    CHECK(x == y);
}

TEST_CASE("Snapshot: restoring into a world with other content replaces it entirely", "[physics][snapshot]")
{
    PhysicsWorld a;
    BuildScene(a);
    for (u32 i = 0; i < 30; ++i)
    {
        a.Step();
    }
    PhysicsWorld other;
    for (u32 i = 0; i < 7; ++i)
    {
        SnBody(other, BodyType::Dynamic, Vec3(static_cast<f32>(i), 1, 0), Shape::MakeSphere(0.5f));
    }
    other.Step();
    REQUIRE(other.Restore(View(Bytes(a))) == SnapshotStatus::Ok);
    CHECK(other.Stats().bodies == a.Stats().bodies);
    CHECK(other.JointCount() == a.JointCount());
    CHECK(SnHash(other) == SnHash(a));
    CHECK(other.GetStepStats().steps == a.GetStepStats().steps);
}

TEST_CASE("Snapshot: a replayed input log reproduces a run of 600 steps", "[physics][snapshot][replay]")
{
    PhysicsWorld original;
    BuildScene(original);
    InputLog log;
    InputRecorder recorder(&original, &log);
    Vector<byte> bytes;
    u64 state = 12345;
    const auto rnd = [&]
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<f32>((state >> 40) & 0xffffff) / 16777216.0f;
    };
    Vector<BodyId> live;
    for (u32 slot = 0; slot < original.Stats().body_slots; ++slot)
    {
        const BodyId id = original.BodyAtSlot(slot);
        if (id.IsValid() && original.Find(id)->settings.type == BodyType::Dynamic)
        {
            live.Push(id);
        }
    }
    for (u32 step = 0; step < 600; ++step)
    {
        if (step == 100)
        {
            bytes = Bytes(original);
        }
        if (step % 9 == 0)
        {
            const BodyId target = live[static_cast<usize>(rnd() * static_cast<f32>(live.Size())) % live.Size()];
            if (step % 27 == 0)
            {
                recorder.ApplyImpulse(target, Vec3(rnd() * 4 - 2, rnd() * 6, rnd() * 4 - 2),
                                      original.WorldCenter(target));
            }
            else if (step % 18 == 0)
            {
                recorder.ApplyForce(target, Vec3(rnd() * 200 - 100, 100, 0), original.WorldCenter(target));
            }
            else
            {
                Velocity v;
                v.linear = Vec3(rnd() - 0.5f, 3, rnd() - 0.5f);
                recorder.SetVelocity(target, v);
            }
        }
        if (step == 150)
        {
            BodyDesc desc;
            desc.position = Vec3(1, 12, 0);
            Collider c;
            c.shape = Shape::MakeSphere(0.3f);
            live.Push(recorder.CreateBody(desc, c));
        }
        if (step == 300)
        {
            recorder.SetTransform(live[2], Vec3(2, 6, 1), Quaternion::Identity());
        }
        if (step == 400)
        {
            const BodyId gone = live[live.Size() - 1];
            live.Pop();
            recorder.DestroyBody(gone);
        }
        original.Step();
    }
    const u64 final_hash = SnHash(original);
    REQUIRE(log.Entries().Size() > 40);

    PhysicsWorld replayed;
    REQUIRE(replayed.Restore(View(bytes)) == SnapshotStatus::Ok);
    REQUIRE(replayed.GetStepStats().steps == 100);
    usize cursor = FirstEntryAt(log, 100);
    for (u32 step = 100; step < 600; ++step)
    {
        ReplayStep(&replayed, log, &cursor);
    }
    CHECK(SnHash(replayed) == final_hash);
    CHECK(cursor == log.Entries().Size());

    // The log itself travels: saved, loaded and replayed, it gives the same run.
    bytes::ByteWriter writer;
    log.Save(&writer);
    InputLog loaded;
    REQUIRE(loaded.Load(writer.Bytes()) == SnapshotStatus::Ok);
    CHECK(loaded.Entries().Size() == log.Entries().Size());
    PhysicsWorld again;
    REQUIRE(again.Restore(View(bytes)) == SnapshotStatus::Ok);
    usize cursor2 = FirstEntryAt(loaded, 100);
    for (u32 step = 100; step < 600; ++step)
    {
        ReplayStep(&again, loaded, &cursor2);
    }
    CHECK(SnHash(again) == final_hash);
}

TEST_CASE("Snapshot: a version, a magic and a truncation are told apart", "[physics][snapshot]")
{
    PhysicsWorld a;
    SnBody(a, BodyType::Dynamic, Vec3(0, 1, 0), Shape::MakeSphere(0.5f));
    a.Step();
    const Vector<byte> bytes = Bytes(a);
    PhysicsWorld target;
    SnBody(target, BodyType::Dynamic, Vec3(9, 9, 9), Shape::MakeSphere(0.5f));
    const u64 before = SnHash(target);
    Vector<byte> bad = Bytes(a);
    bad[0] = static_cast<byte>(bad[0] ^ 0x55);
    CHECK(target.Restore(View(bad)) == SnapshotStatus::BadMagic);
    bad = Bytes(a);
    bad[4] = 99;
    CHECK(target.Restore(View(bad)) == SnapshotStatus::UnknownVersion);
    CHECK(target.Restore(ByteSpan()) == SnapshotStatus::Truncated);
    // Every proper prefix is refused, and the world is untouched each time.
    for (usize length = 0; length < bytes.Size(); ++length)
    {
        const SnapshotStatus status = target.Restore(ByteSpan(bytes.Data(), length));
        REQUIRE(status != SnapshotStatus::Ok);
        REQUIRE(SnHash(target) == before);
    }
    // Trailing bytes are refused too.
    Vector<byte> longer = Bytes(a);
    longer.Push(static_cast<byte>(0));
    CHECK(target.Restore(View(longer)) == SnapshotStatus::Corrupt);
    CHECK(target.Restore(View(bytes)) == SnapshotStatus::Ok);
}

TEST_CASE("Snapshot: 10000 mutated snapshots are refused or restore to a valid world, never a crash",
          "[physics][snapshot]")
{
    PhysicsWorld a;
    BuildScene(a);
    for (u32 i = 0; i < 40; ++i)
    {
        a.Step();
    }
    const Vector<byte> bytes = Bytes(a);
    PhysicsWorld victim;
    SnBody(victim, BodyType::Dynamic, Vec3(9, 9, 9), Shape::MakeSphere(0.5f));
    const u64 before = SnHash(victim);
    u64 state = 12345;
    const auto next = [&]
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<u32>(state >> 33);
    };
    u32 refused = 0;
    u32 accepted = 0;
    for (u32 n = 0; n < 10000; ++n)
    {
        Vector<byte> mutant = Bytes(a);
        const u32 edits = 1 + next() % 3;
        for (u32 e = 0; e < edits; ++e)
        {
            const usize at = next() % mutant.Size();
            mutant[at] = static_cast<byte>(next() & 0xff);
        }
        const SnapshotStatus status = victim.Restore(View(mutant));
        if (status == SnapshotStatus::Ok)
        {
            // A mutant that is still a valid state (a changed float, say) must also step without a crash.
            ++accepted;
            victim.Step();
            victim.Step();
            REQUIRE(victim.Restore(View(bytes)) == SnapshotStatus::Ok);  // back to a known state
            REQUIRE(SnHash(victim) == SnHash(a));
            continue;
        }
        ++refused;
        // A refusal leaves the world as it was: the known state `a` or the first world.
        REQUIRE((SnHash(victim) == before || SnHash(victim) == SnHash(a)));
    }
    CHECK(refused > 1000);
    CHECK(accepted + refused == 10000);
}

TEST_CASE("Snapshot: a refusal leaves the world as it was", "[physics][snapshot]")
{
    PhysicsWorld a;
    BuildScene(a);
    for (u32 i = 0; i < 20; ++i)
    {
        a.Step();
    }
    PhysicsWorld victim;
    BuildScene(victim);
    victim.Step();
    const u64 before = SnHash(victim);
    const Vector<byte> bytes = Bytes(a);
    u64 state = 12345;
    for (u32 n = 0; n < 300; ++n)
    {
        Vector<byte> mutant = Bytes(a);
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        mutant[static_cast<usize>(state >> 33) % mutant.Size()] = static_cast<byte>(state & 0xff);
        if (victim.Restore(View(mutant)) != SnapshotStatus::Ok)
        {
            REQUIRE(SnHash(victim) == before);
        }
        else
        {
            REQUIRE(victim.Restore(View(Bytes(a))) == SnapshotStatus::Ok);
            REQUIRE(victim.Restore(View(bytes)) == SnapshotStatus::Ok);
            // Put the victim back to a world of its own for the next round.
            PhysicsWorld fresh;
            BuildScene(fresh);
            fresh.Step();
            REQUIRE(victim.Restore(View(Bytes(fresh))) == SnapshotStatus::Ok);
            REQUIRE(SnHash(victim) == before);
        }
    }
}

TEST_CASE("Snapshot: a shape that names a missing hull is refused", "[physics][snapshot]")
{
    PhysicsWorld a;
    const u32 hull = a.Hulls().Add(physics::testing::BoxHull(Vec3(1, 1, 1)));
    BodyDesc desc;
    desc.position = Vec3(0, 3, 0);
    const BodyId id = a.CreateBody(desc);
    Collider c;
    c.shape = Shape::MakeHull(hull, Vec3(1, 1, 1));
    REQUIRE(a.AddCollider(id, c) == BodyStatus::Ok);
    a.Step();
    const Vector<byte> bytes = Bytes(a);
    PhysicsWorld without;
    CHECK(without.Restore(View(bytes)) == SnapshotStatus::LibraryMismatch);
    PhysicsWorld with;
    with.Hulls().Add(physics::testing::BoxHull(Vec3(1, 1, 1)));
    CHECK(with.Restore(View(bytes)) == SnapshotStatus::Ok);
    CHECK(SnHash(with) == SnHash(a));
}

TEST_CASE("Snapshot: a snapshot travels through a storage record store", "[physics][snapshot][storage]")
{
    PhysicsWorld a;
    BuildScene(a);
    for (u32 i = 0; i < 25; ++i)
    {
        a.Step();
    }
    storage::MemoryStore store;
    storage::EntityKey key;
    key.high = 1;
    key.low = 2;
    CHECK(SaveSnapshot(a, &store, storage::EntityKey()).Failed());
    REQUIRE(SaveSnapshot(a, &store, key).Ok());
    PhysicsWorld b;
    SnapshotStatus detail = SnapshotStatus::Corrupt;
    REQUIRE(LoadSnapshot(&b, store, key, &detail).Ok());
    CHECK(detail == SnapshotStatus::Ok);
    CHECK(SnHash(a) == SnHash(b));
    storage::EntityKey missing;
    missing.high = 9;
    CHECK(LoadSnapshot(&b, store, missing, &detail).Is(storage::ErrorCode::NotFound));
    // A record that is not a snapshot is refused with the reason.
    const byte junk[8] = {};
    REQUIRE(store.Put(storage::EntityRecord::Make(missing, ByteSpan(junk, 8))).Ok());
    CHECK(LoadSnapshot(&b, store, missing, &detail).Is(storage::ErrorCode::Corrupt));
    CHECK(detail == SnapshotStatus::BadMagic);
    CHECK(SnHash(a) == SnHash(b));
}

TEST_CASE("InputLog: a hostile log is refused", "[physics][snapshot][replay]")
{
    InputLog log;
    InputEntry e;
    e.step = 5;
    e.kind = InputKind::ApplyImpulse;
    e.a = Vec3(1, 2, 3);
    CHECK(log.Add(e));
    e.step = 4;
    CHECK(!log.Add(e));  // steps never go back
    bytes::ByteWriter writer;
    log.Save(&writer);
    InputLog loaded;
    CHECK(loaded.Load(writer.Bytes()) == SnapshotStatus::Ok);
    for (usize length = 0; length < writer.Size(); ++length)
    {
        CHECK(loaded.Load(ByteSpan(writer.Bytes().Data(), length)) != SnapshotStatus::Ok);
    }
    u64 state = 12345;
    for (u32 n = 0; n < 2000; ++n)
    {
        Vector<byte> mutant;
        for (usize i = 0; i < writer.Size(); ++i)
        {
            mutant.Push(writer.Bytes()[i]);
        }
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        mutant[static_cast<usize>(state >> 33) % mutant.Size()] = static_cast<byte>(state & 0xff);
        static_cast<void>(loaded.Load(View(mutant)));  // no crash; Ok or refused
    }
}

TEST_CASE("Snapshot: size and time for 2000 bodies (recorded)", "[physics][snapshot][bench]")
{
    PhysicsWorld a;
    SnBody(a, BodyType::Static, Vec3(0, -0.5f, 0), Shape::MakeBox(Vec3(100, 0.5f, 100)));
    for (u32 i = 0; i < 2000; ++i)
    {
        SnBody(a, BodyType::Dynamic,
               Vec3(static_cast<f32>(i % 40) * 1.2f - 24, 0.5f + static_cast<f32>(i / 40) * 1.1f, 0),
               Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f)));
    }
    for (u32 i = 0; i < 10; ++i)
    {
        a.Step();
    }
    const u64 t0 = os::MonotonicTimeNanos();
    const Vector<byte> bytes = Bytes(a);
    const u64 t1 = os::MonotonicTimeNanos();
    PhysicsWorld b;
    REQUIRE(b.Restore(View(bytes)) == SnapshotStatus::Ok);
    const u64 t2 = os::MonotonicTimeNanos();
    CHECK(SnHash(a) == SnHash(b));
    WARN("2000 bodies: " << bytes.Size() << " bytes, snapshot " << (t1 - t0) / 1000 << " us, restore "
                         << (t2 - t1) / 1000 << " us");
}

TEST_CASE("Snapshot: numbers that would leave the world in one step are refused at the door", "[physics][snapshot]")
{
    RigidBody body;
    body.mass = 1.0e-30f;
    CHECK(body.Validate() == Validation::OutOfRange);
    body.mass = 1.0e20f;
    CHECK(body.Validate() == Validation::OutOfRange);
    body.mass = 5.0f;
    CHECK(body.Validate() == Validation::Ok);

    PhysicsMaterial material;
    material.density = 1.0e-30f;
    CHECK(material.Validate() == Validation::OutOfRange);
    material.density = 1000.0f;
    CHECK(material.Validate() == Validation::Ok);

    Collider collider;
    collider.shape = Shape::MakeSphere(0.5f);
    collider.offset = Vec3(0, 0, 2.0e6f);
    CHECK(collider.Validate() == Validation::OutOfRange);
    collider.offset = Vec3(0, 0, 2.0f);
    CHECK(collider.Validate() == Validation::Ok);

    PhysicsWorld world;
    const BodyId a = SnBody(world, BodyType::Dynamic, Vec3(0, 5, 0), Shape::MakeSphere(0.5f));
    const BodyId b = SnBody(world, BodyType::Dynamic, Vec3(1, 5, 0), Shape::MakeSphere(0.5f));
    JointDesc joint = world.MakeBallJoint(a, b, Vec3(0.5f, 5, 0));
    CHECK(joint.Validate() == Validation::Ok);
    joint.anchor_b = Vec3(0, 0, 8.0e6f);
    CHECK(joint.Validate() == Validation::OutOfRange);
    joint = world.MakeBallJoint(a, b, Vec3(0.5f, 5, 0));
    joint.hertz = 1.0e-40f;
    CHECK(joint.Validate() == Validation::OutOfRange);
}

TEST_CASE("Snapshot: a world with a very light but valid body restores", "[physics][snapshot]")
{
    PhysicsWorld world;
    SnBody(world, BodyType::Static, Vec3(0, -0.5f, 0), Shape::MakeBox(Vec3(10, 0.5f, 10)));
    // A half-millimetre sphere of ordinary density: 5e-7 kg, lighter than anything a game uses, still a valid body.
    SnBody(world, BodyType::Dynamic, Vec3(0, 1, 0), Shape::MakeSphere(0.0005f));
    for (u32 i = 0; i < 30; ++i)
    {
        world.Step();
    }
    const Vector<byte> bytes = Bytes(world);
    PhysicsWorld other;
    REQUIRE(other.Restore(View(bytes)) == SnapshotStatus::Ok);
    CHECK(SnHash(other) == SnHash(world));
}

TEST_CASE("Snapshot: a replayed log of extreme inputs leaves the world finite", "[physics][snapshot]")
{
    PhysicsWorld world;
    SnBody(world, BodyType::Static, Vec3(0, -0.5f, 0), Shape::MakeBox(Vec3(10, 0.5f, 10)));
    const BodyId light = SnBody(world, BodyType::Dynamic, Vec3(0, 1, 0), Shape::MakeSphere(0.5f));
    InputLog log;
    InputEntry entry;
    entry.kind = InputKind::ApplyImpulse;
    entry.body = light;
    entry.a = Vec3(1.0e9f, -1.0e9f, 1.0e9f);
    entry.b = Vec3(0.4f, 0, 0);
    REQUIRE(log.Add(entry));
    entry.kind = InputKind::SetVelocity;
    entry.step = 2;
    entry.a = Vec3(1.0e9f, 0, 0);
    entry.b = Vec3(0, 1.0e9f, 0);
    REQUIRE(log.Add(entry));
    usize cursor = 0;
    for (u32 i = 0; i < 20; ++i)
    {
        ReplayStep(&world, log, &cursor);
    }
    const Body* body = world.Find(light);
    REQUIRE(body != nullptr);
    CHECK(Abs(body->position.x) < 1.0e8f);
    CHECK(Abs(body->position.y) < 1.0e8f);
    CHECK(Abs(body->position.z) < 1.0e8f);
}

TEST_CASE("Snapshot: a restore keeps the program's pre-step hook installed", "[physics][snapshot]")
{
    // The hook (the characters' moves) is the program's, not world state: a restore that dropped it stopped every
    // character of a restored world (C API script physics, 2026-11-20).
    PhysicsWorld world;
    BuildScene(world);
    u32 calls = 0;
    world.SetPreStepHook([](void* user, PhysicsWorld*, f32) { ++*static_cast<u32*>(user); }, &calls);
    world.Step();
    const Vector<byte> bytes = Bytes(world);
    REQUIRE(world.Restore(View(bytes)) == SnapshotStatus::Ok);
    world.Step();
    CHECK(calls == 2);
    world.SetPreStepHook(nullptr, nullptr);
}

TEST_CASE("Snapshot: a body's joints are found again after a restore, and stale entries do no harm",
          "[physics][snapshot]")
{
    PhysicsWorld world;
    BuildScene(world);
    const u32 joints = world.JointCount();
    REQUIRE(joints == 4);
    const Vector<byte> bytes = Bytes(world);
    PhysicsWorld restored;
    REQUIRE(restored.Restore(View(bytes)) == SnapshotStatus::Ok);
    REQUIRE(restored.JointCount() == joints);
    // A body of the chain is destroyed: the two joints that name it go (the index was made again by the restore).
    BodyId middle;
    // The chain's second link is the body of the third joint's `body_a`: find it through a joint.
    for (u32 slot = 0; slot < 8; ++slot)
    {
        const JointRecord* joint = restored.FindJoint(JointId{slot, 1});
        if (joint != nullptr && joint->desc.body_a.IsValid() && slot == 1)
        {
            middle = joint->desc.body_b;
        }
    }
    REQUIRE(middle.IsValid());
    REQUIRE(restored.DestroyBody(middle) == BodyStatus::Ok);
    CHECK(restored.JointCount() == joints - 2);
    // Stale entries: a joint made and destroyed many times between two bodies leaves slots in their lists that name
    // no joint now; destroying a body takes only the live one.
    PhysicsWorld churn;
    const BodyId a = SnBody(churn, BodyType::Dynamic, Vec3(0, 0, 0), Shape::MakeSphere(0.5f));
    const BodyId b = SnBody(churn, BodyType::Dynamic, Vec3(2, 0, 0), Shape::MakeSphere(0.5f));
    for (u32 i = 0; i < 100; ++i)
    {
        const JointId id = churn.CreateJoint(churn.MakeBallJoint(a, b, Vec3(1, 0, 0)));
        REQUIRE(id.IsValid());
        if (i < 99)
        {
            REQUIRE(churn.DestroyJoint(id) == BodyStatus::Ok);
        }
    }
    CHECK(churn.JointCount() == 1);
    REQUIRE(churn.DestroyBody(a) == BodyStatus::Ok);
    CHECK(churn.JointCount() == 0);
}

TEST_CASE("Snapshot: a hull library that holds another hull under the same id is refused", "[physics][snapshot]")
{
    PhysicsWorld a;
    const u32 hull = a.Hulls().Add(physics::testing::BoxHull(Vec3(1, 1, 1)));
    BodyDesc desc;
    desc.position = Vec3(0, 3, 0);
    const BodyId id = a.CreateBody(desc);
    Collider c;
    c.shape = Shape::MakeHull(hull, Vec3(1, 1, 1));
    REQUIRE(a.AddCollider(id, c) == BodyStatus::Ok);
    a.Step();
    const Vector<byte> bytes = Bytes(a);
    // A smaller hull fits the shape's bound, so only its content tells it apart.
    PhysicsWorld other;
    other.Hulls().Add(physics::testing::BoxHull(Vec3(0.5f, 0.5f, 0.5f)));
    CHECK(other.Restore(View(bytes)) == SnapshotStatus::LibraryMismatch);
    PhysicsWorld same;
    same.Hulls().Add(physics::testing::BoxHull(Vec3(1, 1, 1)));
    CHECK(same.Restore(View(bytes)) == SnapshotStatus::Ok);
}

TEST_CASE("Snapshot: touching pairs out of order are refused", "[physics][snapshot]")
{
    PhysicsWorld world;
    // Two trigger volumes around one ball: two touching trigger pairs, the last records before the library section.
    for (u32 i = 0; i < 2; ++i)
    {
        BodyDesc zone;
        zone.settings.type = BodyType::Static;
        zone.position = Vec3(0.1f * static_cast<f32>(i), 0, 0);
        const BodyId z = world.CreateBody(zone);
        Collider zc;
        zc.shape = Shape::MakeBox(Vec3(3, 3, 3));
        zc.is_trigger = true;
        REQUIRE(world.AddCollider(z, zc) == BodyStatus::Ok);
    }
    BodyDesc ball;
    ball.settings.gravity_scale = 0.0f;
    const BodyId b = world.CreateBody(ball);
    Collider bc;
    bc.shape = Shape::MakeSphere(0.5f);
    REQUIRE(world.AddCollider(b, bc) == BodyStatus::Ok);
    world.Step();
    REQUIRE(world.Events().triggers.Size() == 2);
    Vector<byte> bytes = Bytes(world);
    PhysicsWorld good;
    REQUIRE(good.Restore(View(bytes)) == SnapshotStatus::Ok);
    // The tail is: two trigger records of 48 bytes, then the library count (0, one byte). Swap the two records.
    REQUIRE(bytes[bytes.Size() - 1] == byte{0});
    const usize second = bytes.Size() - 1 - 48;
    const usize first = second - 48;
    for (usize k = 0; k < 48; ++k)
    {
        const byte t = bytes[first + k];
        bytes[first + k] = bytes[second + k];
        bytes[second + k] = t;
    }
    PhysicsWorld swapped;
    CHECK(swapped.Restore(View(bytes)) == SnapshotStatus::Corrupt);
}
