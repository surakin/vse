// Saving and loading whole worlds (Phase 19 Day 260).

#include <catch2/catch_test_macros.hpp>

#include "ecs/schedule.h"
#include "ecs/transform.h"
#include "spatial/components.h"
#include "spatial/spatial_sync.h"
#include "spatial/spatial_world.h"
#include "storage/builtin_components.h"
#include "storage/component_record.h"
#include "storage/memory_store.h"
#include "storage/world_storage.h"
#include "world_compare.h"

namespace vse::storage
{
namespace
{

using ecs::Entity;
using ecs::World;

struct RtA
{
    i32 a = 0;
    f32 b = 0;
    u8 flag = 0;
};
struct RtB
{
    String<HeapAllocator> name{HeapAllocator::Default()};
    Array<u16, 4> codes;
};
struct RtC
{
    Vec3 v;
    Quaternion q;
};
struct RtD
{
    EntityRef target;
    u64 big = 0;
};
struct RtE
{
    bool on = false;
    f64 d = 0;
};
struct RtTag
{
};

Status RegisterRoundTripComponents(ComponentRegistry* r)
{
    ENGINE_TRY(RegisterBuiltinComponents(r));
    ENGINE_TRY(r->Register<RtA>(
        {.id = 2000, .name = "rt_a", .fields = {Field<&RtA::a>("a"), Field<&RtA::b>("b"), Field<&RtA::flag>("flag")}}));
    ENGINE_TRY(r->Register<RtB>(
        {.id = 2001, .name = "rt_b", .fields = {Field<&RtB::name>("name"), Field<&RtB::codes>("codes")}}));
    ENGINE_TRY(r->Register<RtC>({.id = 2002, .name = "rt_c", .fields = {Field<&RtC::v>("v"), Field<&RtC::q>("q")}}));
    ENGINE_TRY(r->Register<RtD>(
        {.id = 2003, .name = "rt_d", .fields = {Field<&RtD::target>("target"), Field<&RtD::big>("big")}}));
    ENGINE_TRY(r->Register<RtE>({.id = 2004, .name = "rt_e", .fields = {Field<&RtE::on>("on"), Field<&RtE::d>("d")}}));
    ENGINE_TRY(r->Register<RtTag>({.id = 2005, .name = "rt_tag"}));
    return Status::Success();
}

struct Rng
{
    u64 s;
    u32 Next()
    {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<u32>(s >> 33);
    }
};

// Entities with random subsets of the six test components (64 archetypes), a hierarchy of up to depth 5, references and
// strings.
void Populate(World& world, KeyIndex& index, usize count, u64 seed)
{
    Rng rng{seed};
    KeyGenerator gen = KeyGenerator::Seeded(seed);
    Vector<Entity> made;
    Vector<u32> depth;
    for (usize i = 0; i < count; ++i)
    {
        const u32 mask = rng.Next() % 64;
        const Entity e = world.Create(EntityKeyComponent{gen.Next()});
        REQUIRE(e.IsValid());
        if (mask & 1)
        {
            REQUIRE(world.Add(e, RtA{static_cast<i32>(rng.Next()) - 1000, static_cast<f32>(rng.Next() % 1000) / 7.0f,
                                     static_cast<u8>(rng.Next())}) == ecs::Status::Ok);
        }
        if (mask & 2)
        {
            RtB b;
            const u32 len = rng.Next() % 12;
            for (u32 k = 0; k < len; ++k)
            {
                b.name.Append(static_cast<char>('a' + rng.Next() % 26));
            }
            for (usize k = 0; k < 4; ++k)
            {
                b.codes[k] = static_cast<u16>(rng.Next());
            }
            REQUIRE(world.Add(e, Move(b)) == ecs::Status::Ok);
        }
        if (mask & 4)
        {
            RtC c;
            c.v = Vec3(static_cast<f32>(rng.Next() % 100) / 3.0f, -1.5f, 2.25f);
            REQUIRE(world.Add(e, c) == ecs::Status::Ok);
        }
        if (mask & 8)
        {
            RtD d;
            d.big = (static_cast<u64>(rng.Next()) << 32) | rng.Next();
            if (!made.Empty())
            {
                d.target.key = world.Peek<EntityKeyComponent>(made[rng.Next() % made.Size()])->key;
            }
            REQUIRE(world.Add(e, d) == ecs::Status::Ok);
        }
        if (mask & 16)
        {
            REQUIRE(world.Add(e, RtE{(rng.Next() & 1) != 0, -0.0}) == ecs::Status::Ok);
        }
        if (mask & 32)
        {
            REQUIRE(world.Add(e, RtTag{}) == ecs::Status::Ok);
        }
        u32 my_depth = 0;
        if (!made.Empty() && rng.Next() % 3 == 0)
        {
            const usize p = rng.Next() % made.Size();
            if (depth[p] < 5)
            {
                my_depth = depth[p] + 1;
                REQUIRE(world.Set(e, ecs::Parent{made[p]}) == ecs::Status::Ok);
                REQUIRE(world.Set(e, ecs::HierarchyDepth{my_depth}) == ecs::Status::Ok);
            }
        }
        made.Push(e);
        depth.Push(my_depth);
    }
    REQUIRE(index.Sync(world).Ok());
}

struct Rig
{
    ComponentRegistry registry;
    MemoryStore store;
    Rig() { REQUIRE(RegisterRoundTripComponents(&registry).Ok()); }
};

Vector<EntityKey> OrderOfKeys(World& world)
{
    Vector<EntityKey> keys;
    world.Query<ecs::Read<EntityKeyComponent>>().ForEach([&](const EntityKeyComponent& k) { keys.Push(k.key); });
    return keys;
}

TEST_CASE("World round trip: 10,000 entities in 64 archetypes load back bit-exact and in the same order",
          "[storage][world]")
{
    Rig rig;
    World source;
    KeyIndex source_index;
    Populate(source, source_index, 10000, 42);
    WorldStorage storage(rig.registry, rig.store);

    Report saved;
    REQUIRE(storage.Save(source, source_index, SaveOptions(), &saved).Ok());
    CHECK(saved.created == 10000);
    CHECK(saved.replaced == 0);
    CHECK(saved.bytes > 0);

    World first;
    KeyIndex first_index;
    Report loaded;
    REQUIRE(storage.Load(first, first_index, LoadOptions(), &loaded).Ok());
    CHECK(loaded.created == 10000);
    CHECK(loaded.bytes == saved.bytes);
    test::RequireSameWorlds(rig.registry, source, first);
    CHECK(first_index.Size() == 10000);
    CHECK(first.Validate().Ok());

    World second;
    KeyIndex second_index;
    REQUIRE(storage.Load(second, second_index, LoadOptions(), nullptr).Ok());
    const Vector<EntityKey> order_a = OrderOfKeys(first);
    const Vector<EntityKey> order_b = OrderOfKeys(second);
    REQUIRE(order_a.Size() == order_b.Size());
    bool same_order = true;
    for (usize i = 0; i < order_a.Size(); ++i)
    {
        same_order = same_order && order_a[i] == order_b[i];
    }
    CHECK(same_order);
    CHECK(first.ArchetypeCount() == second.ArchetypeCount());
    CHECK(first.ArchetypeCount() >= 50);

    // Saving what was loaded writes the same bytes again.
    MemoryStore again;
    WorldStorage storage_again(rig.registry, again);
    REQUIRE(storage_again.Save(first, first_index, SaveOptions(), nullptr).Ok());
    usize a = 0, b = 0;
    REQUIRE(rig.store.Count(&a).Ok());
    REQUIRE(again.Count(&b).Ok());
    CHECK(a == b);
    for (u64 probe = 0; probe < 50; ++probe)
    {
        const EntityKey key = order_a[probe * 100];
        EntityRecord r1, r2;
        REQUIRE(rig.store.Get(key, &r1).Ok());
        REQUIRE(again.Get(key, &r2).Ok());
        CHECK(r1.hash == r2.hash);
    }
}

TEST_CASE("World round trip: the store keeps the format, the manifest and the label", "[storage][world]")
{
    Rig rig;
    World world;
    KeyIndex index;
    Populate(world, index, 100, 3);
    WorldStorage storage(rig.registry, rig.store);
    SaveOptions options;
    options.label.Append("slot 1");
    REQUIRE(storage.Save(world, index, options, nullptr).Ok());
    String<HeapAllocator> value(HeapAllocator::Default());
    REQUIRE(rig.store.GetMetadata(metadata::kLastSaveLabel, &value).Ok());
    CHECK(value.View() == StringView("slot 1"));
    REQUIRE(rig.store.GetMetadata(metadata::kFormatVersion, &value).Ok());
    CHECK(value.View() == StringView("1"));

    SECTION("a registry that renamed a component refuses the store")
    {
        ComponentRegistry other;
        REQUIRE(RegisterBuiltinComponents(&other).Ok());
        REQUIRE(other.Register<RtA>({.id = 2000, .name = "renamed", .fields = {Field<&RtA::a>("a")}}).Ok());
        WorldStorage wrong(other, rig.store);
        World target;
        KeyIndex target_index;
        CHECK(wrong.Load(target, target_index, LoadOptions(), nullptr).Is(ErrorCode::Mismatch));
        CHECK(OrderOfKeys(target).Size() == 0);
    }
    SECTION("a store from a newer format is TooNew")
    {
        REQUIRE(rig.store.SetMetadata(metadata::kFormatVersion, "2").Ok());
        World target;
        KeyIndex target_index;
        CHECK(storage.Load(target, target_index, LoadOptions(), nullptr).Is(ErrorCode::TooNew));
    }
}

Vector<EntityKey> AllKeys(MemoryStore& store)
{
    Vector<EntityKey> keys;
    REQUIRE(ListKeys(store, KeyRange(), ~usize(0), &keys).Ok());
    return keys;
}

RtA* ARow(World& world, KeyIndex& index, EntityKey key)
{
    return world.Get<RtA>(index.Find(key));
}

TEST_CASE("World round trip: Create refuses a key in the world, Replace swaps, Merge keeps entity ids",
          "[storage][world]")
{
    Rig rig;
    World source;
    KeyIndex source_index;
    Populate(source, source_index, 300, 7);
    WorldStorage storage(rig.registry, rig.store);
    REQUIRE(storage.Save(source, source_index, SaveOptions(), nullptr).Ok());
    EntityKey with_a;
    source.Query<ecs::Read<EntityKeyComponent>, ecs::Read<RtA>>().ForEach([&](const EntityKeyComponent& k, const RtA&)
                                                                          { with_a = k.key; });
    REQUIRE_FALSE(with_a.IsNull());

    World target;
    KeyIndex target_index;
    REQUIRE(storage.Load(target, target_index, LoadOptions(), nullptr).Ok());

    SECTION("Create fails and changes nothing")
    {
        ARow(target, target_index, with_a)->a = 123456;
        Report report;
        CHECK(storage.Load(target, target_index, LoadOptions(), &report).Is(ErrorCode::Exists));
        CHECK(OrderOfKeys(target).Size() == 300);
        CHECK(ARow(target, target_index, with_a)->a == 123456);
        CHECK(report.created == 0);
    }
    SECTION("Replace brings back the stored state and drops entities the store lacks")
    {
        ARow(target, target_index, with_a)->a = 123456;
        const Entity extra = target.Create(EntityKeyComponent{KeyGenerator::Seeded(999).Next()});
        REQUIRE(extra.IsValid());
        REQUIRE(target_index.Sync(target).Ok());
        LoadOptions options;
        options.mode = LoadMode::Replace;
        Report report;
        REQUIRE(storage.Load(target, target_index, options, &report).Ok());
        CHECK(report.removed == 301);
        CHECK(report.created == 300);
        CHECK_FALSE(target.Exists(extra));
        test::RequireSameWorlds(rig.registry, source, target);
    }
    SECTION("Merge keeps the world's entity ids and entities that are not in the store")
    {
        const Entity before = target_index.Find(with_a);
        ARow(target, target_index, with_a)->a = 123456;
        const EntityKey extra_key = KeyGenerator::Seeded(999).Next();
        const Entity extra = target.Create(EntityKeyComponent{extra_key});
        REQUIRE(target_index.Sync(target).Ok());
        LoadOptions options;
        options.mode = LoadMode::Merge;
        Report report;
        REQUIRE(storage.Load(target, target_index, options, &report).Ok());
        CHECK(report.replaced == 300);
        CHECK(report.created == 0);
        CHECK(target_index.Find(with_a) == before);
        CHECK(ARow(target, target_index, with_a)->a == ARow(source, source_index, with_a)->a);
        CHECK(target.Exists(extra));
        CHECK(target_index.Find(extra_key) == extra);
        CHECK(OrderOfKeys(target).Size() == 301);
    }
}

TEST_CASE("World round trip: a store that fails at every write of a save is left unchanged", "[storage][world]")
{
    Rig rig;
    World world;
    KeyIndex index;
    Populate(world, index, 20, 5);
    WorldStorage storage(rig.registry, rig.store);
    bool succeeded = false;
    for (u32 fail = 1; fail <= 40 && !succeeded; ++fail)
    {
        rig.store.FailAfter(fail);
        const Status s = storage.Save(world, index, SaveOptions(), nullptr);
        rig.store.FailAfter(0);
        if (s.Ok())
        {
            succeeded = true;
            break;
        }
        CHECK(s.Is(ErrorCode::Io));
        usize count = 99;
        REQUIRE(rig.store.Count(&count).Ok());
        CHECK(count == 0);
        String<HeapAllocator> v(HeapAllocator::Default());
        CHECK(rig.store.GetMetadata(metadata::kFormatVersion, &v).Is(ErrorCode::NotFound));
    }
    CHECK(succeeded);
    CHECK(AllKeys(rig.store).Size() == 20);
}

TEST_CASE("World round trip: a damaged store leaves the target world unchanged", "[storage][world]")
{
    Rig rig;
    World source;
    KeyIndex source_index;
    Populate(source, source_index, 200, 11);
    WorldStorage storage(rig.registry, rig.store);
    REQUIRE(storage.Save(source, source_index, SaveOptions(), nullptr).Ok());
    const Vector<EntityKey> keys = AllKeys(rig.store);
    const EntityKey victim = keys[keys.Size() - 3];  // late in key order, so earlier records were fine

    World target;
    KeyIndex target_index;
    const EntityKey mine = KeyGenerator::Seeded(5).Next();
    const Entity existing = target.Create(EntityKeyComponent{mine});
    REQUIRE(target_index.Sync(target).Ok());
    LoadOptions options;
    options.mode = LoadMode::Replace;

    SECTION("a record whose bytes no longer match their hash")
    {
        REQUIRE(rig.store.CorruptRecord(victim));
        CHECK(storage.Load(target, target_index, options, nullptr).Is(ErrorCode::Corrupt));
    }
    SECTION("a record cut short, with a hash that matches the cut")
    {
        EntityRecord r;
        REQUIRE(rig.store.Get(victim, &r).Ok());
        REQUIRE(r.blob.Size() > 6);
        REQUIRE(rig.store.Put(EntityRecord::Make(victim, ByteSpan(r.blob.Data(), r.blob.Size() - 3))).Ok());
        CHECK(storage.Load(target, target_index, options, nullptr).Is(ErrorCode::Corrupt));
    }
    SECTION("a record with garbage and a matching hash")
    {
        const byte junk[] = {static_cast<byte>(255), static_cast<byte>(255), static_cast<byte>(255),
                             static_cast<byte>(255), static_cast<byte>(1)};
        REQUIRE(rig.store.Put(EntityRecord::Make(victim, ByteSpan(junk, 5))).Ok());
        const Status s = storage.Load(target, target_index, options, nullptr);
        CHECK((s.Is(ErrorCode::LimitExceeded) || s.Is(ErrorCode::Corrupt)));
    }
    CHECK(target.Exists(existing));
    CHECK(OrderOfKeys(target).Size() == 1);
    CHECK(target_index.Find(mine) == existing);
}

// A record of one component with the given id and bytes.
EntityRecord OneComponent(EntityKey key, u32 id, ByteSpan bytes)
{
    bytes::ByteWriter w;
    w.WriteU32(1);
    ComponentRecord rec;
    rec.id = id;
    rec.version = 1;
    rec.bytes = bytes;
    REQUIRE(WriteComponentRecord(rec, CodecLimits(), w).Ok());
    return EntityRecord::Make(key, w.Bytes());
}

ByteSpan KeyBytes(bytes::ByteWriter& w, EntityKey key)
{
    w.WriteU64(key.high);
    w.WriteU64(key.low);
    return w.Bytes();
}

TEST_CASE("World round trip: unknown components follow the policy, and KeepOpaque survives a save", "[storage][world]")
{
    ComponentRegistry full;
    REQUIRE(RegisterRoundTripComponents(&full).Ok());
    ComponentRegistry partial;  // lacks rt_e and rt_tag
    REQUIRE(RegisterBuiltinComponents(&partial).Ok());
    REQUIRE(partial
                .Register<RtA>({.id = 2000,
                                .name = "rt_a",
                                .fields = {Field<&RtA::a>("a"), Field<&RtA::b>("b"), Field<&RtA::flag>("flag")}})
                .Ok());
    REQUIRE(partial
                .Register<RtB>(
                    {.id = 2001, .name = "rt_b", .fields = {Field<&RtB::name>("name"), Field<&RtB::codes>("codes")}})
                .Ok());
    REQUIRE(
        partial.Register<RtC>({.id = 2002, .name = "rt_c", .fields = {Field<&RtC::v>("v"), Field<&RtC::q>("q")}}).Ok());
    REQUIRE(partial
                .Register<RtD>(
                    {.id = 2003, .name = "rt_d", .fields = {Field<&RtD::target>("target"), Field<&RtD::big>("big")}})
                .Ok());

    World source;
    KeyIndex source_index;
    Populate(source, source_index, 400, 21);
    MemoryStore store;
    WorldStorage writer(full, store);
    REQUIRE(writer.Save(source, source_index, SaveOptions(), nullptr).Ok());
    usize with_unknown = 0;
    source.Query<ecs::Read<EntityKeyComponent>, ecs::Read<RtE>>().ForEach([&](const EntityKeyComponent&, const RtE&)
                                                                          { ++with_unknown; });
    source.Query<ecs::Read<EntityKeyComponent>, ecs::With<RtTag>>().ForEach([&](const EntityKeyComponent&)
                                                                            { ++with_unknown; });
    REQUIRE(with_unknown > 0);

    WorldStorage reader(partial, store);
    World target;
    KeyIndex target_index;
    SECTION("Fail")
    {
        LoadOptions options;
        CHECK(reader.Load(target, target_index, options, nullptr).Is(ErrorCode::NotFound));
        CHECK(OrderOfKeys(target).Size() == 0);
    }
    SECTION("SkipAndReport")
    {
        LoadOptions options;
        options.unknown = UnknownPolicy::SkipAndReport;
        Report report;
        REQUIRE(reader.Load(target, target_index, options, &report).Ok());
        CHECK(report.skipped == with_unknown);
        CHECK(OrderOfKeys(target).Size() == 400);
    }
    SECTION("KeepOpaque writes the unknown records back unchanged")
    {
        LoadOptions options;
        options.unknown = UnknownPolicy::KeepOpaque;
        Report report;
        REQUIRE(reader.Load(target, target_index, options, &report).Ok());
        CHECK(report.unknown_kept == with_unknown);
        MemoryStore resaved;
        WorldStorage rewriter(partial, resaved);
        REQUIRE(rewriter.Save(target, target_index, SaveOptions(), nullptr).Ok());
        World round;
        KeyIndex round_index;
        WorldStorage full_reader(full, resaved);
        REQUIRE(full_reader.Load(round, round_index, LoadOptions(), nullptr).Ok());
        test::RequireSameWorlds(full, source, round);
    }
}

TEST_CASE("World round trip: references to a missing entity are an error or a counted null", "[storage][world]")
{
    Rig rig;
    World source;
    KeyIndex source_index;
    KeyGenerator gen = KeyGenerator::Seeded(1);
    const EntityKey parent_key = gen.Next();
    const EntityKey child_key = gen.Next();
    const Entity parent = source.Create(EntityKeyComponent{parent_key});
    const Entity child = source.Create(EntityKeyComponent{child_key});
    REQUIRE(ecs::SetParent(source, child, parent) == ecs::HierarchyStatus::Ok);
    REQUIRE(source_index.Sync(source).Ok());
    WorldStorage storage(rig.registry, rig.store);
    REQUIRE(storage.Save(source, source_index, SaveOptions(), nullptr).Ok());

    LoadOptions options;
    options.selection.kind = Selection::Kind::Keys;
    options.selection.keys.Push(child_key);  // the parent is not selected, and the target world lacks it
    World target;
    KeyIndex target_index;

    SECTION("Fail changes nothing")
    {
        Report report;
        CHECK(storage.Load(target, target_index, options, &report).Is(ErrorCode::MissingReference));
        CHECK(OrderOfKeys(target).Size() == 0);
    }
    SECTION("NullReference loads the child as a root and counts it")
    {
        options.missing = MissingPolicy::NullReference;
        Report report;
        REQUIRE(storage.Load(target, target_index, options, &report).Ok());
        CHECK(report.nulled == 1);
        CHECK(report.created == 1);
        const Entity loaded = target_index.Find(child_key);
        REQUIRE(loaded.IsValid());
        CHECK_FALSE(target.Has<ecs::Parent>(loaded));
        CHECK_FALSE(target.Has<ParentLink>(loaded));
    }
    SECTION("a key set that includes the parent is whole")
    {
        options.selection.keys.Push(parent_key);
        options.selection.keys.Push(KeyGenerator::Seeded(77).Next());  // nothing has this key: reported, not an error
        Report report;
        REQUIRE(storage.Load(target, target_index, options, &report).Ok());
        CHECK(report.missing_keys == 1);
        CHECK(target.Peek<ecs::Parent>(target_index.Find(child_key))->entity == target_index.Find(parent_key));
    }
}

TEST_CASE("World round trip: stored parent links in a cycle are rejected and counted", "[storage][world]")
{
    Rig rig;
    KeyGenerator gen = KeyGenerator::Seeded(2);
    const EntityKey a = gen.Next();
    const EntityKey b = gen.Next();
    bytes::ByteWriter wa, wb;
    REQUIRE(rig.store.Put(OneComponent(a, kParentLinkId, KeyBytes(wa, b))).Ok());
    REQUIRE(rig.store.Put(OneComponent(b, kParentLinkId, KeyBytes(wb, a))).Ok());
    WorldStorage storage(rig.registry, rig.store);
    World target;
    KeyIndex index;
    Report report;
    REQUIRE(storage.Load(target, index, LoadOptions(), &report).Ok());
    CHECK(report.created == 2);
    CHECK(report.parents_rejected == 2);
    CHECK_FALSE(target.Has<ecs::Parent>(index.Find(a)));
    CHECK_FALSE(target.Has<ecs::Parent>(index.Find(b)));
}

TEST_CASE("World round trip: cancelling stops a save and a load without a trace", "[storage][world]")
{
    Rig rig;
    World source;
    KeyIndex source_index;
    Populate(source, source_index, 500, 9);
    WorldStorage storage(rig.registry, rig.store);
    CancelToken token;
    token.Cancel();
    SaveOptions save;
    save.cancel = &token;
    CHECK(storage.Save(source, source_index, save, nullptr).Is(ErrorCode::Cancelled));
    usize count = 9;
    REQUIRE(rig.store.Count(&count).Ok());
    CHECK(count == 0);

    REQUIRE(storage.Save(source, source_index, SaveOptions(), nullptr).Ok());
    World target;
    KeyIndex target_index;
    LoadOptions load;
    load.cancel = &token;
    CHECK(storage.Load(target, target_index, load, nullptr).Is(ErrorCode::Cancelled));
    CHECK(OrderOfKeys(target).Size() == 0);
}

TEST_CASE("World round trip: a full save drops records of entities that are gone; a key set save does not",
          "[storage][world]")
{
    Rig rig;
    World world;
    KeyIndex index;
    Populate(world, index, 50, 13);
    WorldStorage storage(rig.registry, rig.store);
    REQUIRE(storage.Save(world, index, SaveOptions(), nullptr).Ok());
    const Vector<EntityKey> keys = AllKeys(rig.store);
    REQUIRE(world.Destroy(index.Find(keys[0])) == ecs::Status::Ok);
    REQUIRE(world.Destroy(index.Find(keys[1])) == ecs::Status::Ok);

    SaveOptions partial;
    partial.selection.kind = Selection::Kind::Keys;
    partial.selection.keys.Push(keys[2]);
    Report r1;
    REQUIRE(storage.Save(world, index, partial, &r1).Ok());
    CHECK(r1.replaced == 1);
    CHECK(r1.removed == 0);
    CHECK(AllKeys(rig.store).Size() == 50);

    Report r2;
    REQUIRE(storage.Save(world, index, SaveOptions(), &r2).Ok());
    CHECK(r2.removed == 2);
    CHECK(r2.replaced == 48);
    CHECK(AllKeys(rig.store).Size() == 48);
}

// A world with transforms, a hierarchy and spatial membership, run through the transform and spatial systems.
struct SystemRig
{
    World world;
    ecs::Schedule schedule;
    KeyIndex index;
    SystemRig()
    {
        world.SetResource(ecs::TransformReport{});
        world.SetResource(spatial::SpatialWorld());
        schedule.Add(ecs::MakeTransformSystem("transforms"));
        schedule.Add(spatial::MakeSpatialSyncSystem("spatial_sync").InStage(ecs::kPostUpdate));
        REQUIRE(schedule.Build().ok);
    }
    void Frame() { schedule.Run(world, 0.016f); }
};

TEST_CASE("World round trip: derived state is rebuilt by running the systems once after a load", "[storage][world]")
{
    Rig rig;
    SystemRig source;
    KeyGenerator gen = KeyGenerator::Seeded(4);
    Vector<Entity> made;
    for (u32 i = 0; i < 60; ++i)
    {
        ecs::LocalTransform t;
        t.position = Vec3(static_cast<f32>(i), 1.0f, static_cast<f32>(i % 7));
        spatial::SpatialBounds b;
        b.local = Aabb{Vec3(-0.5f, -0.5f, -0.5f), Vec3(0.5f, 0.5f, 0.5f)};
        spatial::SpatialMembership m;
        m.sets = 1u << spatial::kRenderSet;
        const Entity e = source.world.Create(EntityKeyComponent{gen.Next()}, t, ecs::WorldTransform{}, b, m);
        REQUIRE(e.IsValid());
        if (i >= 10)
        {
            REQUIRE(ecs::SetParent(source.world, e, made[i % 10]) == ecs::HierarchyStatus::Ok);
        }
        made.Push(e);
    }
    source.Frame();
    REQUIRE(source.index.Sync(source.world).Ok());
    WorldStorage storage(rig.registry, rig.store);
    REQUIRE(storage.Save(source.world, source.index, SaveOptions(), nullptr).Ok());

    SystemRig target;
    REQUIRE(storage.Load(target.world, target.index, LoadOptions(), nullptr).Ok());
    target.Frame();
    target.Frame();

    usize compared = 0;
    source.world.Query<ecs::Read<EntityKeyComponent>, ecs::Read<ecs::WorldTransform>, ecs::Read<spatial::WorldBounds>>()
        .ForEach(
            [&](const EntityKeyComponent& k, const ecs::WorldTransform& wt, const spatial::WorldBounds& wb)
            {
                const Entity other = target.index.Find(k.key);
                REQUIRE(other.IsValid());
                const ecs::WorldTransform* twt = target.world.Peek<ecs::WorldTransform>(other);
                const spatial::WorldBounds* twb = target.world.Peek<spatial::WorldBounds>(other);
                REQUIRE(twt != nullptr);
                REQUIRE(twb != nullptr);
                CHECK(MemCompare(&wt.matrix, &twt->matrix, sizeof(Mat4)) == 0);
                CHECK(MemCompare(&wb.box, &twb->box, sizeof(Aabb)) == 0);
                ++compared;
            });
    CHECK(compared == 60);
    const spatial::SpatialSetStats
        s1 = source.world.Resource<spatial::SpatialWorld>().Set(spatial::kRenderSet)->Stats();
    const spatial::SpatialSetStats
        s2 = target.world.Resource<spatial::SpatialWorld>().Set(spatial::kRenderSet)->Stats();
    CHECK(s1.static_items + s1.dynamic_items == 60);
    CHECK(s2.static_items + s2.dynamic_items == 60);
    CHECK(target.world.Validate().Ok());
}

}  // namespace
}  // namespace vse::storage
