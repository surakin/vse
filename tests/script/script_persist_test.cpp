// Saving and loading scripted state (Phase 27 Day 469): `persist(...)` marks the fields of `self` a save keeps, the
// runtime writes them into `script_persist` components, the storage module saves them with the `script` components, and
// a load gives them back to new instances (`on_load` before `on_start`). Also: the value record's hostile-input rules,
// and a session that stops, saves, loads on another thread count and goes on to the same hash.
#include <catch2/catch_test_macros.hpp>

#include "cstr/cstr.h"
#include "cstr/mem_ops.h"
#include "ecs/transform.h"
#include "jobs/scheduler.h"
#include "script/backend_registry.h"
#include "script/bindings.h"
#include "script/component_catalog.h"
#include "script/luajit/luajit_backend.h"
#include "script/script_persist.h"
#include "script/script_runtime.h"
#include "script/value_codec.h"
#include "storage/builtin_components.h"
#include "storage/identity.h"
#include "storage/memory_store.h"
#include "storage/world_storage.h"

using namespace vse;
using namespace vse::script;

namespace persist_test
{

const char* const kScripts = R"LUA(
behavior('counter', {
  on_create = function(self)
    self.hp = self.params.start
    self.ticks = 0
    self.bag = { gold = 1, name = 'x' }
    self.helper = function() return 1 end
    persist('hp', 'ticks', 'bag', 'target')
  end,
  on_load = function(self)
    self.helper = function() return 2 end   -- not saved: a script rebuilds it here
    self.loaded = (self.loaded or 0) + 1
  end,
  on_update = function(self, dt)
    self.ticks = self.ticks + 1
    self.hp = self.hp + random.int(0, 9)
    self.bag.gold = self.bag.gold + 1
    local p = transform.position(self.entity)
    transform.set_position(self.entity, vec3.new(p.x + self.hp % 3, p.y, p.z))
  end,
})

behavior('order', {
  on_create = function(self) persist('a') self.a = 1 self.stage = 'create' end,
  on_load = function(self) self.stage = self.stage .. '>load(a=' .. tostring(self.a) .. ')' end,
  on_start = function(self) self.stage = self.stage .. '>start' self.trace = self.stage persist('trace') end,
})

behavior('messy', {
  on_create = function(self)
    self.good = 5
    self.mixed = { keep = 1, drop = function() end, nested = { ok = true, bad = print } }
    self.bad = setmetatable({ f = function() end }, {})
    persist('good', 'mixed', 'bad', 'missing')
  end,
})

behavior('badpersist', {
  on_create = function(self) persist(42) end,
})

behavior('nilpersist', {
  on_create = function(self) local gone = nil persist('a', gone, 'b') end,
})
)LUA";

SharedBytesPtr Source()
{
    return CopyShared(ByteSpan(reinterpret_cast<const byte*>(kScripts), CStrLength(kScripts)));
}

jobs::Scheduler::Config ConfigFor(usize participants)
{
    jobs::Scheduler::Config config;
    config.participants = participants;
    config.pin = false;
    return config;
}

struct Rig
{
    storage::ComponentRegistry registry;
    ComponentCatalog catalog;
    BindingRegistry bindings;
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    ecs::World world;
    storage::KeyIndex index;
    UniquePtr<ScriptRuntime> runtime;

    explicit Rig(u32 partitions = 16) : catalog(&registry)
    {
        REQUIRE(storage::RegisterBuiltinComponents(&registry).Ok());
        REQUIRE(RegisterScriptComponents(&registry));
        REQUIRE(RegisterStandardBindings(&bindings));
        ScriptRuntimeConfig config;
        config.backend = backend.Get();
        config.bindings = &bindings;
        config.partitions = partitions;
        config.components = &catalog;
        config.random_seed = 7;
        runtime = MakeUnique<ScriptRuntime>(config);
        runtime->AddScript("persist_scripts", Source());
    }

    ecs::Entity Add(u64 number, const char* behavior, Value params = Value())
    {
        const ecs::Entity e = ecs::CreateTransformEntity(world, ecs::LocalTransform());
        REQUIRE(world.Add(e, storage::EntityKeyComponent{storage::EntityKey{0, number}}) == ecs::Status::Ok);
        REQUIRE(world.Add(e, ScriptComponent(StringView(behavior, CStrLength(behavior)), Move(params))) ==
                ecs::Status::Ok);
        return e;
    }

    void Populate(u32 count)
    {
        for (u32 i = 0; i < count; ++i)
        {
            Value params = Value::MakeMap();
            params.Set("start", Value::FromInt(static_cast<i64>(i % 50)));
            Add(i + 1, "counter", Move(params));
        }
    }

    // The ECS state: the position of every entity, mixed so that the order does not matter.
    u64 Hash()
    {
        u64 sum = 0;
        world.Query<ecs::Read<ecs::LocalTransform>, ecs::Read<storage::EntityKeyComponent>>().ForEach(
            [&](ecs::Entity, const ecs::LocalTransform& t, const storage::EntityKeyComponent& k)
            {
                u32 bits[3];
                MemCopy(&bits[0], &t.position.x, 4);
                MemCopy(&bits[1], &t.position.y, 4);
                MemCopy(&bits[2], &t.position.z, 4);
                u64 h = (k.key.low + 1) * 0x9E3779B97F4A7C15ull;
                h = (h ^ bits[0]) * 0x100000001B3ull;
                h = (h ^ bits[1]) * 0x100000001B3ull;
                h = (h ^ bits[2]) * 0x100000001B3ull;
                sum += h;
            });
        return sum;
    }

    PersistReport Capture()
    {
        REQUIRE(index.Sync(world).Ok());
        PersistReport report;
        runtime->CapturePersistent(&world, index, &report);
        return report;
    }

    void Tick(usize threads, u32 count)
    {
        jobs::Scheduler scheduler(ConfigFor(threads));
        for (u32 i = 0; i < count; ++i)
        {
            runtime->Tick(&world, 0.1, &scheduler);
        }
    }
};

Value Number(i64 n)
{
    return Value::FromInt(n);
}

// A value that holds one of everything.
Value Everything()
{
    Value map = Value::MakeMap();
    map.Set("nil", Value());
    map.Set("flag", Value::FromBool(true));
    map.Set("int", Number(-5000000000ll));
    map.Set("float", Value::FromFloat(0.1));
    map.Set("text", Value::FromText("h\xc3\xa9llo"));
    map.Set("v2", Value::FromVec2(Vec2{1.0f, 2.0f}));
    map.Set("v3", Value::FromVec3(Vec3{1.0f, 2.0f, 3.0f}));
    map.Set("v4", Value::FromVec4(Vec4{1.0f, 2.0f, 3.0f, 4.0f}));
    Quaternion q;
    q.x = 0.0f;
    q.y = 0.0f;
    q.z = 0.0f;
    q.w = 1.0f;
    map.Set("quat", Value::FromQuat(q));
    const u8 raw[3] = {0, 255, 7};
    map.Set("bytes", Value::FromBytes(Span<const u8>(raw, 3)));
    Value array = Value::MakeArray();
    array.Push(Number(1));
    array.Push(Value::FromText("two"));
    Value inner = Value::MakeMap();
    inner.Set("deep", Value::FromBool(false));
    array.Push(Move(inner));
    map.Set("array", Move(array));
    return map;
}

Vector<u8> Bytes(const bytes::ByteWriter& w)
{
    Vector<u8> out;
    for (usize i = 0; i < w.Size(); ++i)
    {
        out.Push(static_cast<u8>(w.Bytes()[i]));
    }
    return out;
}

ValueRecordStatus Decode(const Vector<u8>& data, usize length, Value* out, const storage::KeyIndex* index = nullptr,
                         const ValueLimits& limits = ValueLimits())
{
    bytes::ByteReader reader(ByteSpan(reinterpret_cast<const byte*>(data.Data()), length));
    return DecodeValueRecord(&reader, out, index, limits);
}

}  // namespace persist_test

using namespace persist_test;

TEST_CASE("Value record: every kind round trips, and the bytes are the same every time", "[script][persist][codec]")
{
    const Value original = Everything();
    bytes::ByteWriter a;
    bytes::ByteWriter b;
    EncodeValueRecord(original, &a, nullptr);
    EncodeValueRecord(original, &b, nullptr);
    CHECK(a.Size() == b.Size());
    CHECK(MemCompare(a.Bytes().Data(), b.Bytes().Data(), a.Size()) == 0);
    const Vector<u8> data = Bytes(a);
    Value back;
    REQUIRE(Decode(data, data.Size(), &back) == ValueRecordStatus::Ok);
    CHECK(back.Equals(original));
    CHECK(back.Hash() == original.Hash());
}

TEST_CASE("Value record: every cut of a record is refused, and bytes that lie are refused", "[script][persist][codec]")
{
    bytes::ByteWriter w;
    EncodeValueRecord(Everything(), &w, nullptr);
    const Vector<u8> data = Bytes(w);
    for (usize cut = 0; cut < data.Size(); ++cut)
    {
        Value out = Number(1);
        INFO("cut at " << cut);
        CHECK(Decode(data, cut, &out) != ValueRecordStatus::Ok);
        CHECK(out.Kind() == ValueKind::Int);  // unchanged on failure
    }
    Vector<u8> bad = Bytes(w);
    SECTION("an unknown version")
    {
        bad[0] = 99;
        Value out;
        CHECK(Decode(bad, bad.Size(), &out) == ValueRecordStatus::Corrupt);
    }
    SECTION("an unknown kind")
    {
        bad[2] = 200;
        Value out;
        CHECK(Decode(bad, bad.Size(), &out) == ValueRecordStatus::Corrupt);
    }
    SECTION("a count far above the bytes that follow")
    {
        // version, mode, array tag, a count of 60,000 and nothing after it
        const u8 lie[] = {1, 0, 12, 0xE0, 0xD4, 0x03};
        Vector<u8> v;
        for (u8 byte_value : lie)
        {
            v.Push(byte_value);
        }
        Value out;
        CHECK(Decode(v, v.Size(), &out) == ValueRecordStatus::Truncated);
    }
    SECTION("nesting past the depth limit")
    {
        Vector<u8> v;
        v.Push(1);
        v.Push(0);
        for (u32 i = 0; i < 40; ++i)
        {
            v.Push(12);  // an array
            v.Push(1);   // of one item
        }
        v.Push(0);  // nil
        Value out;
        CHECK(Decode(v, v.Size(), &out) == ValueRecordStatus::OverLimit);
    }
    SECTION("text above the limit")
    {
        ValueLimits limits;
        limits.max_text_bytes = 2;
        Value out;
        CHECK(Decode(data, data.Size(), &out, nullptr, limits) == ValueRecordStatus::OverLimit);
    }
}

TEST_CASE("Value record: entities are written as keys and found again in another world", "[script][persist][codec]")
{
    ecs::World one;
    storage::KeyIndex one_index;
    const ecs::Entity a = one.Create(storage::EntityKeyComponent{storage::EntityKey{0, 11}});
    const ecs::Entity stranger = one.Create();  // has no key
    REQUIRE(one_index.Sync(one).Ok());
    Value map = Value::MakeMap();
    map.Set("friend", Value::FromEntity(a));
    map.Set("nobody", Value::FromEntity(stranger));
    bytes::ByteWriter w;
    EncodeValueRecord(map, &w, &one_index);
    const Vector<u8> data = Bytes(w);

    ecs::World two;
    storage::KeyIndex two_index;
    two.Create();  // shift the ids
    two.Create();
    const ecs::Entity b = two.Create(storage::EntityKeyComponent{storage::EntityKey{0, 11}});
    REQUIRE(two_index.Sync(two).Ok());
    Value back;
    REQUIRE(Decode(data, data.Size(), &back, &two_index) == ValueRecordStatus::Ok);
    REQUIRE(back.Find("friend") != nullptr);
    CHECK(back.Find("friend")->AsEntity() == b);
    CHECK(!back.Find("nobody")->AsEntity().IsValid());
    Value without;
    REQUIRE(Decode(data, data.Size(), &without, nullptr) == ValueRecordStatus::Ok);
    CHECK(!without.Find("friend")->AsEntity().IsValid());
}

namespace persist_test
{

// The persistent record of the entity with key number `number`, decoded.
Value RecordOf(Rig& rig, u64 number)
{
    const ecs::Entity e = rig.index.Find(storage::EntityKey{0, number});
    const ScriptPersist* record = rig.world.Get<ScriptPersist>(e);
    Value out;
    if (record != nullptr)
    {
        bytes::ByteReader reader(ByteSpan(reinterpret_cast<const byte*>(record->blob.Data()), record->blob.Size()));
        REQUIRE(DecodeValueRecord(&reader, &out, &rig.index, ValueLimits()) == ValueRecordStatus::Ok);
    }
    return out;
}

// Saves `source` (captured) through a memory store and loads it into `target`, like a game's save and load.
void SaveAndLoad(Rig& source, Rig& target, LoadReport* load = nullptr)
{
    const PersistReport saved = source.Capture();
    storage::MemoryStore store;
    storage::WorldStorage storage(source.registry, store);
    REQUIRE(storage.Save(source.world, source.index, storage::SaveOptions(), nullptr).Ok());
    REQUIRE(storage.Load(target.world, target.index, storage::LoadOptions(), nullptr).Ok());
    target.runtime->ResolveLoaded(&target.world, target.index, load);
    target.runtime->RestoreClock(saved.tick, saved.sim_time);
}

}  // namespace persist_test

TEST_CASE("Persist: marked fields are captured, the rest are not, and on_load runs before on_start",
          "[script][persist]")
{
    Rig rig;
    rig.Add(1, "order");
    rig.Tick(1, 1);
    const PersistReport report = rig.Capture();
    CHECK(report.instances_saved == 1);
    Value first = RecordOf(rig, 1);
    REQUIRE(first.Kind() == ValueKind::Map);
    REQUIRE(first.Find("a") != nullptr);
    CHECK(first.Find("a")->AsInt() == 1);
    CHECK(first.Find("stage") == nullptr);  // not marked

    Rig second;
    LoadReport load;
    SaveAndLoad(rig, second, &load);
    CHECK(load.queued == 1);
    CHECK(load.corrupt == 0);
    second.Tick(1, 1);
    CHECK(second.runtime->Stats().errors == 0);
    second.Capture();
    const Value after = RecordOf(second, 1);
    REQUIRE(after.Find("trace") != nullptr);
    CHECK(after.Find("trace")->Text() == StringView("create>load(a=1)>start"));
}

TEST_CASE("Persist: a field that cannot be saved is left out and the others are kept", "[script][persist]")
{
    Rig rig;
    rig.Add(1, "messy");
    rig.Tick(1, 1);
    const PersistReport report = rig.Capture();
    CHECK(report.instances_saved == 1);
    CHECK(report.fields_skipped == 1);  // `bad`: a table with a metatable that holds a function
    const Value record = RecordOf(rig, 1);
    REQUIRE(record.Kind() == ValueKind::Map);
    REQUIRE(record.Find("good") != nullptr);
    CHECK(record.Find("good")->AsInt() == 5);
    REQUIRE(record.Find("mixed") != nullptr);
    CHECK(record.Find("mixed")->Find("keep") != nullptr);
    CHECK(record.Find("mixed")->Find("drop") == nullptr);
    CHECK(record.Find("mixed")->Find("nested")->Find("bad") == nullptr);
    CHECK(record.Find("mixed")->Find("nested")->Find("ok") != nullptr);
    CHECK(record.Find("bad") == nullptr);
    CHECK(record.Find("missing") == nullptr);  // nil is not saved
}

TEST_CASE("Persist: persist() outside a callback or with a bad name is an error", "[script][persist]")
{
    Rig rig;
    rig.Add(1, "badpersist");
    rig.Tick(1, 1);
    CHECK(rig.runtime->Stats().errors >= 1);
    ConsoleResult result;
    rig.runtime->ConsoleRun(0, "persist('x')", &result);
    CHECK(!result.ok);
    CHECK(result.message.View().Contains("only a behavior callback"));
}

TEST_CASE("Persist: 10,000 scripted entities save and load with their fields", "[script][persist][large]")
{
    Rig rig(64);
    rig.Populate(10000);
    rig.Tick(4, 3);
    const u64 before = rig.Hash();
    Rig loaded(64);
    LoadReport load;
    SaveAndLoad(rig, loaded, &load);
    CHECK(load.queued == 10000);
    CHECK(load.corrupt == 0);
    CHECK(loaded.Hash() == before);  // transforms are stored too
    loaded.Tick(4, 1);
    CHECK(loaded.runtime->Stats().errors == 0);
    CHECK(loaded.runtime->Stats().created == 10000);
    // Every entity got its own fields back: `hp` grew from the saved value, not from `params.start`.
    const PersistReport again = loaded.Capture();
    CHECK(again.instances_saved == 10000);
    for (u64 number : {u64{1}, u64{77}, u64{5000}, u64{10000}})
    {
        const Value a = RecordOf(rig, number);
        const Value b = RecordOf(loaded, number);
        REQUIRE(b.Find("ticks") != nullptr);
        CHECK(a.Find("ticks")->AsInt() == 3);
        CHECK(b.Find("ticks")->AsInt() == 4);
        CHECK(b.Find("hp")->AsInt() >= a.Find("hp")->AsInt());
        CHECK(b.Find("bag")->Find("gold")->AsInt() == 5);
    }
}

TEST_CASE("Persist: a session saved at tick 3 and continued on another thread count ends on the same state",
          "[script][persist][replay]")
{
    // Control: nine ticks without stopping.
    Rig control(32);
    control.Populate(500);
    control.Tick(2, 9);
    const u64 control_hash = control.Hash();
    const PersistReport control_state = control.Capture();

    const usize thread_counts[] = {1, 2, 3, 7, 16};
    for (usize threads : thread_counts)
    {
        INFO("threads after the load " << threads);
        Rig first(32);
        first.Populate(500);
        first.Tick(16, 3);
        Rig second(32);
        SaveAndLoad(first, second);
        second.Tick(threads, 6);
        CHECK(second.Hash() == control_hash);
        const PersistReport state = second.Capture();
        CHECK(state.state_hash == control_state.state_hash);
        CHECK(state.fields_saved == control_state.fields_saved);
        CHECK(state.tick == control_state.tick);
    }
}

TEST_CASE("Persist: damaged script records are refused by value", "[script][persist][corrupt]")
{
    Rig rig;
    const storage::RegisteredComponent* script = rig.registry.FindByName("script");
    const storage::RegisteredComponent* persist = rig.registry.FindByName("script_persist");
    REQUIRE(script != nullptr);
    REQUIRE(persist != nullptr);

    ecs::World world;
    const ecs::Entity e = world.Create();
    Value params = Value::MakeMap();
    params.Set("start", Number(3));
    REQUIRE(world.Add(e, ScriptComponent("counter", Move(params), false)) == ecs::Status::Ok);
    bytes::ByteWriter good;
    REQUIRE(script->Encode(world.CellOf(e, script->EcsId()), good, storage::CodecLimits()).Ok());
    const Vector<u8> record = Bytes(good);

    // Decode into a scratch component; every prefix is refused as corrupt.
    auto decode = [&](const Vector<u8>& data, usize length, u32 version = 1)
    {
        ScriptComponent out;
        return script->DecodeRecord(version, ByteSpan(reinterpret_cast<const byte*>(data.Data()), length), &out,
                                    storage::CodecLimits());
    };
    CHECK(decode(record, record.Size()).Ok());
    for (usize cut = 0; cut < record.Size(); ++cut)
    {
        INFO("cut at " << cut);
        CHECK(decode(record, cut).Is(storage::ErrorCode::Corrupt));
    }
    CHECK(decode(record, record.Size(), 2).Is(storage::ErrorCode::TooNew));
    CHECK(decode(record, record.Size(), 0).Is(storage::ErrorCode::TooOld));
    Vector<u8> huge = Bytes(good);
    huge[0] = 0xFF;  // a behavior name length of 255 + more: the name is cut off by the record
    huge[1] = 0x7F;
    CHECK(!decode(huge, huge.Size()).Ok());
    Vector<u8> trailing = Bytes(good);
    trailing.Push(1);
    CHECK(decode(trailing, trailing.Size()).Is(storage::ErrorCode::Corrupt));

    // A persistent record must be a map of values.
    bytes::ByteWriter not_a_map;
    EncodeValueRecord(Number(5), &not_a_map, nullptr);
    ScriptPersist bad;
    for (usize i = 0; i < not_a_map.Size(); ++i)
    {
        bad.blob.Push(static_cast<u8>(not_a_map.Bytes()[i]));
    }
    ecs::World other;
    const ecs::Entity f = other.Create();
    REQUIRE(other.Add(f, Move(bad)) == ecs::Status::Ok);
    bytes::ByteWriter written;
    REQUIRE(persist->Encode(other.CellOf(f, persist->EcsId()), written, storage::CodecLimits()).Ok());
    const Vector<u8> persist_bytes = Bytes(written);
    ScriptPersist out;
    CHECK(persist
              ->DecodeRecord(1, ByteSpan(reinterpret_cast<const byte*>(persist_bytes.Data()), persist_bytes.Size()),
                             &out, storage::CodecLimits())
              .Is(storage::ErrorCode::Corrupt));
}

TEST_CASE("Persist: a record that does not decode starts the instance without it", "[script][persist][corrupt]")
{
    Rig rig;
    const ecs::Entity e = rig.Add(1, "counter", Value());
    ScriptPersist junk;
    for (u8 b : {u8{1}, u8{0}, u8{99}})
    {
        junk.blob.Push(b);  // a record with an unknown kind
    }
    REQUIRE(rig.world.Add(e, Move(junk)) == ecs::Status::Ok);
    REQUIRE(rig.index.Sync(rig.world).Ok());
    LoadReport load;
    rig.runtime->ResolveLoaded(&rig.world, rig.index, &load);
    CHECK(load.corrupt == 1);
    CHECK(load.queued == 0);
    CHECK(rig.world.Get<ScriptPersist>(e) == nullptr);  // taken either way
}

TEST_CASE("Persist: a load not yet taken by an instance is written back by the next capture", "[script][persist]")
{
    Rig source;
    Value params = Value::MakeMap();
    params.Set("start", Number(10));
    source.Add(1, "counter", Move(params));
    source.Tick(1, 2);
    Rig loaded;
    SaveAndLoad(source, loaded);
    // No tick yet: the fields wait in the runtime. A second save keeps them.
    const PersistReport report = loaded.Capture();
    CHECK(report.instances_pending == 1);
    const Value record = RecordOf(loaded, 1);
    REQUIRE(record.Find("ticks") != nullptr);
    CHECK(record.Find("ticks")->AsInt() == 2);
}

TEST_CASE("Persist: an entity the host destroyed since the last tick is not counted as saved", "[script][persist]")
{
    Rig rig;
    rig.Populate(3);
    rig.Tick(1, 1);
    REQUIRE(rig.index.Sync(rig.world).Ok());
    const ecs::Entity gone = rig.index.Find(storage::EntityKey{0, 2});
    REQUIRE(rig.world.Destroy(gone) == ecs::Status::Ok);
    REQUIRE(rig.index.Sync(rig.world).Ok());
    PersistReport report;
    rig.runtime->CapturePersistent(&rig.world, rig.index, &report);
    CHECK(report.instances_saved == 2);
    CHECK(report.instances_failed == 0);
}

TEST_CASE("Persist: a nil among the names is an error, not a silent end of the list", "[script][persist]")
{
    Rig rig;
    rig.Add(1, "nilpersist");
    rig.Tick(1, 1);
    CHECK(rig.runtime->Stats().errors >= 1);
}
