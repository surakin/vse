// The entity and transform bindings (Phase 27 Day 455): spawn, destroy, parent, names, transforms with the overlay, and
// the definition-of-done run: a script moves entities for 120 ticks and the world is the same on 1 and 8 threads.
#include <catch2/catch_test_macros.hpp>

#include "algorithm.h"
#include "cstr/cstr.h"
#include "ecs/query.h"
#include "ecs/transform.h"
#include "script/backend_registry.h"
#include "script/bindings.h"
#include "script/entity_bindings.h"
#include "script/luajit/luajit_backend.h"
#include "script/merge.h"
#include "script/merge_services.h"
#include "script/partition_context.h"
#include "script/script_world.h"
#include "sync/atomic.h"
#include "sync/thread.h"

using namespace vse;
using namespace vse::script;

namespace
{

struct EntityName
{
    char text[32];
};

usize NameOfImpl(void*, const ecs::World& world, ecs::Entity e, char* out, usize capacity)
{
    const EntityName* name = world.Get<EntityName>(e);
    if (name == nullptr)
    {
        return 0;
    }
    const usize length = CStrLength(name->text);
    const usize n = length < capacity ? length : capacity;
    MemCopy(out, name->text, n);
    return n;
}

ecs::Entity FindImpl(void*, const ecs::World& world, StringView wanted)
{
    ecs::Entity found;
    // A read-only query: the world is in its parallel phase (no structural change) while scripts run.
    const_cast<ecs::World&>(world).Query<ecs::Read<EntityName>>().ForEach(
        [&](ecs::Entity e, const EntityName& n)
        {
            if (!found.IsValid() && StringView(n.text, CStrLength(n.text)) == wanted)
            {
                found = e;
            }
        });
    return found;
}

bool HasPrefabImpl(void*, StringView prefab)
{
    return prefab == StringView("crate");
}

struct Crate
{
    int marker;
};

bool SpawnPrefabImpl(void*, StringView prefab, ecs::World* world, ecs::Entity e)
{
    if (!(prefab == StringView("crate")))
    {
        return false;
    }
    return world->Add<Crate>(e, Crate{7}) == ecs::Status::Ok;
}

struct EntityRig
{
    BindingRegistry bindings;
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    UniquePtr<State> state = backend->CreateState(0);
    UniquePtr<Installation> installed;
    ecs::World world;
    CommandKindTable kinds;
    CommandBuffer buffer;
    Overlay overlay;
    PartitionContext context;
    SceneServices scene;
    PrefabSource prefab_source;
    MergeServices services;
    ecs::Entity self;

    EntityRig()
    {
        REQUIRE(RegisterStandardBindings(&bindings));
        Outcome error;
        installed = bindings.Install(state.Get(), InstallProfile::Partition(ContextEntityCheck()), &error);
        REQUIRE(installed.Get() != nullptr);
        REQUIRE(RegisterEntityCommandKinds(&kinds));
        buffer.Begin(0);
        scene.name_of = &NameOfImpl;
        scene.find = &FindImpl;
        scene.has_prefab = &HasPrefabImpl;
        prefab_source.spawn = &SpawnPrefabImpl;
        services.prefabs = &prefab_source;
        self = ecs::CreateTransformEntity(world, ecs::LocalTransform());
        context.world = &world;
        context.commands = &buffer;
        context.overlay = &overlay;
        context.scene = &scene;
        context.current_entity_key = self.ToU64();
    }

    Outcome Run(const char* body, CallOutput* out, const Value* arg = nullptr, usize count = 1)
    {
        String<HeapAllocator> source(HeapAllocator::Default());
        source.Append("function run(a, b) ");
        source.Append(StringView(body));
        source.Append(" end");
        REQUIRE(state->Load("entity_test", source.View()).IsOk());
        ScopedPartitionContext scope(&context);
        return state->Call("run", Span<const Value>(arg, arg != nullptr ? count : 0), Budget(), out);
    }

    MergeReport Merge()
    {
        CommandBuffer* buffers[] = {&buffer};
        return script::Merge(&world, Span<CommandBuffer* const>(buffers, 1), kinds, ConflictPolicy::LastWins,
                             &services);
    }
};

bool Near3(Vec3 a, f32 x, f32 y, f32 z)
{
    return Abs(a.x - x) < 1.0e-5f && Abs(a.y - y) < 1.0e-5f && Abs(a.z - z) < 1.0e-5f;
}

}  // namespace

TEST_CASE("Entity bindings: a spawned entity can be used at once and exists after the merge", "[script][entity]")
{
    EntityRig rig;
    CallOutput out;
    REQUIRE(rig.Run("local e = entity.spawn(nil) transform.set_position(e, vec3.new(1, 2, 3)) "
                    "transform.translate(e, vec3.new(1, 0, 0)) return e, transform.position(e), entity.valid(e)",
                    &out)
                .IsOk());
    REQUIRE(out.results.Size() == 3);
    CHECK(out.results[0].AsEntity().IsPlaceholder());
    CHECK(Near3(out.results[1].AsVec3(), 2, 2, 3));  // the script's own writes, before the merge
    CHECK(out.results[2].AsBool());
    const usize before = rig.world.Query<ecs::Read<ecs::LocalTransform>>().Count();
    const MergeReport report = rig.Merge();
    CHECK(report.applied == 3);
    CHECK(report.dangling == 0);
    CHECK(rig.world.Query<ecs::Read<ecs::LocalTransform>>().Count() == before + 1);
    Vec3 found;
    rig.world.Query<ecs::Read<ecs::LocalTransform>>().ForEach(
        [&](ecs::Entity e, const ecs::LocalTransform& t)
        {
            if (e != rig.self)
            {
                found = t.position.Narrow();
            }
        });
    CHECK(Near3(found, 2, 2, 3));
}

TEST_CASE("Entity bindings: a position far from the origin is read and written without narrowing",
          "[script][entity][far]")
{
    // 1e11 + 0.25 is exact in double precision and not representable in single precision (spacing 8192 there); 1e12 is
    // the world limit a script position may not pass.
    EntityRig rig;
    CallOutput out;
    REQUIRE(rig.Run("local e = entity.spawn(nil) transform.set_position(e, vec3.new(1e11 + 0.25, 2, 3)) "
                    "transform.translate(e, vec3.new(0.5, 0, 0)) "
                    "local p = transform.position(e) "
                    "return e, p, vec3.distance(p, vec3.new(1e11, 2, 3))",
                    &out)
                .IsOk());
    REQUIRE(out.results.Size() == 3);
    CHECK(out.results[1].AsVec3d().x == 1.0e11 + 0.75);
    CHECK(out.results[1].AsVec3d().y == 2.0);
    CHECK(out.results[2].AsFloat() == 0.75);
    rig.Merge();
    Vec3d found;
    rig.world.Query<ecs::Read<ecs::LocalTransform>>().ForEach(
        [&](ecs::Entity e, const ecs::LocalTransform& t)
        {
            if (e != rig.self)
            {
                found = t.position;
            }
        });
    CHECK(found.x == 1.0e11 + 0.75);
}

TEST_CASE("Entity bindings: a position past the world limit is refused and the entity stays", "[script][entity][far]")
{
    EntityRig rig;
    CallOutput out;
    const Outcome refused = rig.Run("local e = entity.spawn(nil) transform.set_position(e, vec3.new(1e12 + 1, 2, 3))",
                                    &out);
    CHECK_FALSE(refused.IsOk());
    const Outcome edge = rig.Run("local e = entity.spawn(nil) transform.set_position(e, vec3.new(1e12, 2, 3)) return e",
                                 &out);
    CHECK(edge.IsOk());
}

TEST_CASE("Entity bindings: valid, self and dead handles", "[script][entity]")
{
    EntityRig rig;
    CallOutput out;
    const ecs::Entity gone = ecs::CreateTransformEntity(rig.world, ecs::LocalTransform());
    rig.world.Destroy(gone);
    const Value dead = Value::FromEntity(gone);
    REQUIRE(rig.Run("return entity.valid(a), entity.valid(nil), entity.valid(5), entity.self()", &out, &dead).IsOk());
    REQUIRE(out.results.Size() == 4);
    CHECK_FALSE(out.results[0].AsBool());
    CHECK_FALSE(out.results[1].AsBool());
    CHECK_FALSE(out.results[2].AsBool());
    CHECK(out.results[3].AsEntity() == rig.self);
    // A call with a dead handle is refused before the binding runs.
    CHECK_FALSE(rig.Run("return transform.position(a)", &out, &dead).IsOk());
}

TEST_CASE("Entity bindings: transforms read, write and see the script's own writes", "[script][entity]")
{
    EntityRig rig;
    CallOutput out;
    const Value me = Value::FromEntity(rig.self);
    const Outcome ran = rig.Run(
        "local p = transform.position(a) transform.set_position(a, vec3.new(5, 0, 0)) "
        "transform.set_scale(a, vec3.new(2, 2, 2)) "
        "transform.set_rotation(a, quat.from_axis_angle(vec3.new(0, 0, 1), 1.5707963) ) "
        "local t = transform.get(a) return p, t.position, t.scale, t.rotation, transform.world(a)",
        &out, &me);
    INFO(ran.message.CStr());
    REQUIRE(ran.IsOk());
    REQUIRE(out.results.Size() == 5);
    CHECK(Near3(out.results[0].AsVec3(), 0, 0, 0));
    CHECK(Near3(out.results[1].AsVec3(), 5, 0, 0));
    CHECK(Near3(out.results[2].AsVec3(), 2, 2, 2));
    const Quaternion q = out.results[3].AsQuat();
    CHECK(Abs(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w - 1.0f) < 1.0e-5f);  // normalized
    CHECK(out.results[4].Size() == 16);
    // The world still holds the old value until the merge.
    CHECK(Near3(rig.world.Get<ecs::LocalTransform>(rig.self)->position.Narrow(), 0, 0, 0));
    CHECK(rig.Merge().applied == 3);
    CHECK(Near3(rig.world.Get<ecs::LocalTransform>(rig.self)->position.Narrow(), 5, 0, 0));
    CHECK(Near3(rig.world.Get<ecs::LocalTransform>(rig.self)->scale, 2, 2, 2));
}

TEST_CASE("Entity bindings: bad transform values and entities without a transform are errors", "[script][entity]")
{
    EntityRig rig;
    CallOutput out;
    const Value me = Value::FromEntity(rig.self);
    CHECK_FALSE(rig.Run("transform.set_position(a, vec3.new(1 / 0, 0, 0))", &out, &me).IsOk());
    const Value pair[2] = {me, Value::FromQuat(Quaternion(0.0f, 0.0f, 0.0f, 0.0f))};
    CHECK_FALSE(rig.Run("transform.set_rotation(a, b)", &out, pair, 2).IsOk());
    const Value big[2] = {me, Value::FromQuat(Quaternion(0.0f, 0.0f, 3.0f, 4.0f))};
    REQUIRE(rig.Run("transform.set_rotation(a, b) return transform.rotation(a)", &out, big, 2).IsOk());
    CHECK(Abs(out.results[0].AsQuat().w - 0.8f) < 1.0e-5f);  // normalized: (0, 0, 0.6, 0.8)
    rig.buffer.Begin(0);
    CHECK_FALSE(rig.Run("transform.set_scale(a, vec3.new(0 / 0, 0, 0))", &out, &me).IsOk());
    const ecs::Entity bare = rig.world.Create();
    const Value bare_value = Value::FromEntity(bare);
    const Outcome o = rig.Run("return transform.position(a)", &out, &bare_value);
    CHECK_FALSE(o.IsOk());
    CHECK(o.message.View().Contains("no transform"));
    CHECK(rig.buffer.Size() == 0);
}

TEST_CASE("Entity bindings: destroy takes the children, set_parent builds and clears the hierarchy", "[script][entity]")
{
    EntityRig rig;
    const ecs::Entity root = ecs::CreateTransformEntity(rig.world, ecs::LocalTransform());
    const ecs::Entity child = ecs::CreateTransformEntity(rig.world, ecs::LocalTransform());
    const ecs::Entity other = ecs::CreateTransformEntity(rig.world, ecs::LocalTransform());
    CallOutput out;
    Value args[1];
    args[0] = Value::FromEntity(root);
    // Parent child under root, in a script that also spawns and parents a fresh entity.
    REQUIRE(rig.Run(("local fresh = entity.spawn(nil) entity.set_parent(fresh, a) return fresh"), &out, args).IsOk());
    REQUIRE(rig.Merge().applied == 2);
    REQUIRE(ecs::SetParent(rig.world, child, root) == ecs::HierarchyStatus::Ok);
    CHECK(rig.world.Get<ecs::Parent>(child) != nullptr);
    rig.buffer.Begin(0);
    REQUIRE(rig.Run("entity.destroy(a)", &out, args).IsOk());
    rig.Merge();
    CHECK_FALSE(rig.world.Exists(root));
    CHECK_FALSE(rig.world.Exists(child));
    CHECK(rig.world.Exists(other));
    // set_parent(nil) clears; a cycle is refused at the merge and counted.
    const ecs::Entity a = ecs::CreateTransformEntity(rig.world, ecs::LocalTransform());
    const ecs::Entity b = ecs::CreateTransformEntity(rig.world, ecs::LocalTransform());
    REQUIRE(ecs::SetParent(rig.world, b, a) == ecs::HierarchyStatus::Ok);
    const Value pair[2] = {Value::FromEntity(a), Value::FromEntity(b)};
    rig.buffer.Begin(0);
    REQUIRE(rig.Run("entity.set_parent(a, b)", &out, pair, 2).IsOk());  // a under its own child
    MergeReport report = rig.Merge();
    CHECK(report.skipped == 1);
    CHECK(rig.world.Get<ecs::Parent>(a) == nullptr);
    rig.buffer.Begin(0);
    REQUIRE(rig.Run("entity.set_parent(b, nil)", &out, pair, 2).IsOk());
    report = rig.Merge();
    CHECK(report.applied == 1);
    CHECK(rig.world.Get<ecs::Parent>(b) == nullptr);
}

TEST_CASE("Entity bindings: names, find and prefabs come from the scene's services", "[script][entity]")
{
    EntityRig rig;
    rig.world.Add<EntityName>(rig.self, EntityName{"hero"});
    CallOutput out;
    REQUIRE(
        rig.Run(
               "local h = entity.find('hero') return entity.name(h), entity.find('nobody'), entity.name(entity.self())",
               &out)
            .IsOk());
    CHECK(out.results[0].Text() == StringView("hero"));
    CHECK(out.results[1].IsNil());
    CHECK(out.results[2].Text() == StringView("hero"));
    // A prefab spawns with its components; an unknown one is an error, and so is a prefab with no scene.
    REQUIRE(rig.Run("entity.spawn('crate')", &out).IsOk());
    CHECK(rig.Merge().applied == 1);
    CHECK(rig.world.Query<ecs::Read<Crate>>().Count() == 1);
    CHECK_FALSE(rig.Run("entity.spawn('barrel')", &out).IsOk());
    CHECK_FALSE(rig.Run("entity.spawn(5)", &out).IsOk());
    rig.context.scene = nullptr;
    CHECK_FALSE(rig.Run("entity.spawn('crate')", &out).IsOk());
    REQUIRE(rig.Run("return entity.find('hero'), entity.name(entity.self())", &out).IsOk());
    CHECK(out.results[0].IsNil());
}

// ---- the definition of done: scripts spawn and move entities, the same world for any thread count.

namespace
{

const char* const kMover = "function step(i, tick)\n"
                           "  local e = entity.self()\n"
                           "  local dt = time.dt()\n"
                           "  local p = transform.position(e)\n"
                           "  local path = vec3.new(math.sin(i * 0.1) , 1, math.cos(tick * 0.05)) \n"
                           "  transform.set_position(e, p + path * dt)\n"
                           "  transform.set_rotation(e, quat.from_axis_angle(vec3.new(0, 1, 0), tick * 0.01))\n"
                           "  if tick % 30 == 0 and i % 7 == 0 then\n"
                           "    local c = entity.spawn(nil)\n"
                           "    entity.set_parent(c, e)\n"
                           "    transform.set_position(c, vec3.new(0, 0.5, 0))\n"
                           "  end\n"
                           "  if tick == 100 and i % 11 == 0 then entity.destroy(e) end\n"
                           "end\n";

struct MoveSim
{
    ecs::World world;
    BindingRegistry bindings;
    CommandKindTable kinds;
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    UniquePtr<ScriptWorld> scripts;
    Vector<UniquePtr<CommandBuffer>> buffers;
    u32 partitions = 64;
    u64 tick = 0;

    explicit MoveSim(u32 entities)
    {
        REQUIRE(RegisterStandardBindings(&bindings));
        REQUIRE(RegisterEntityCommandKinds(&kinds));
        ScriptWorldConfig config;
        config.backend = backend.Get();
        config.bindings = &bindings;
        config.profile = InstallProfile::Partition(ContextEntityCheck());
        config.partitions = partitions;
        scripts = MakeUnique<ScriptWorld>(config);
        scripts->AddScript("mover", CopyShared(ByteSpan(reinterpret_cast<const byte*>(kMover), CStrLength(kMover))));
        for (u32 p = 0; p < partitions; ++p)
        {
            buffers.Push(MakeUnique<CommandBuffer>());
        }
        for (u32 i = 0; i < entities; ++i)
        {
            ecs::LocalTransform local;
            local.position = Vec3(static_cast<f32>(i % 17), 0.0f, static_cast<f32>(i % 13));
            ecs::CreateTransformEntity(world, local);
        }
    }

    void Tick(u32 threads)
    {
        Vector<u64> keys;
        world.Query<ecs::Read<ecs::LocalTransform>>().ForEach([&](ecs::Entity e, const ecs::LocalTransform&)
                                                              { keys.Push(e.ToU64()); });
        Sort(Span<u64>(keys.Data(), keys.Size()), [](u64 a, u64 b) { return a < b; });
        Vector<Vector<u32>> lists;
        for (u32 p = 0; p < partitions; ++p)
        {
            lists.Push(Vector<u32>());
        }
        for (u32 i = 0; i < keys.Size(); ++i)
        {
            lists[PartitionOf(keys[i], partitions)].Push(i);
        }
        Atomic<u32> next(0);
        Atomic<u32> failures(0);
        {
            ReadOnlyPhase phase(&world);
            Vector<UniquePtr<Thread>> pool;
            for (u32 t = 0; t < threads; ++t)
            {
                pool.Push(MakeUnique<Thread>(
                    [&]()
                    {
                        for (;;)
                        {
                            const u32 p = next.FetchAdd(1, MemoryOrder::AcqRel);
                            if (p >= partitions)
                            {
                                return;
                            }
                            PartitionLease lease;
                            if (scripts->Checkout(p, &lease, nullptr) != CheckoutStatus::Ok)
                            {
                                failures.FetchAdd(1, MemoryOrder::AcqRel);
                                continue;
                            }
                            CommandBuffer& buffer = *buffers[p];
                            buffer.Begin(p);
                            Overlay overlay;
                            PartitionContext context;
                            context.world = &world;
                            context.commands = &buffer;
                            context.overlay = &overlay;
                            context.dt = 1.0 / 60.0;
                            context.tick = tick;
                            ScopedPartitionContext scope(&context);
                            for (u32 k = 0; k < lists[p].Size(); ++k)
                            {
                                const u32 index = lists[p][k];
                                context.current_entity_key = keys[index];
                                overlay.Clear();
                                Value args[2] = {Value::FromInt(index), Value::FromInt(static_cast<i64>(tick))};
                                CallOutput out;
                                Budget budget;
                                budget.instructions = 5000000;
                                if (!lease.Call("step", Span<const Value>(args, 2), budget, &out).IsOk())
                                {
                                    failures.FetchAdd(1, MemoryOrder::AcqRel);
                                }
                            }
                        }
                    }));
            }
            for (usize t = 0; t < pool.Size(); ++t)
            {
                pool[t]->Join();
            }
        }
        REQUIRE(failures.Load(MemoryOrder::Acquire) == 0);
        Vector<CommandBuffer*> order;
        for (u32 p = 0; p < partitions; ++p)
        {
            order.Push(buffers[p].Get());
        }
        Merge(&world, Span<CommandBuffer* const>(order.Data(), order.Size()), kinds, ConflictPolicy::LastWins);
        ++tick;
    }

    u64 Hash()
    {
        u64 sum = 0;
        world.Query<ecs::Read<ecs::LocalTransform>>().ForEach(
            [&](ecs::Entity e, const ecs::LocalTransform& t)
            {
                const f32 v[] = {static_cast<f32>(t.position.x),
                                 static_cast<f32>(t.position.y),
                                 static_cast<f32>(t.position.z),
                                 t.rotation.x,
                                 t.rotation.y,
                                 t.rotation.z,
                                 t.rotation.w};
                u64 h = e.ToU64() * 0x9E3779B97F4A7C15ull;
                for (f32 f : v)
                {
                    u32 bits;
                    MemCopy(&bits, &f, sizeof(bits));
                    h = (h ^ bits) * 0x100000001B3ull;
                }
                sum += h;
            });
        return sum;
    }

    usize Count() { return world.Query<ecs::Read<ecs::LocalTransform>>().Count(); }
};

}  // namespace

TEST_CASE("Entity bindings: scripts move, spawn and destroy entities for 120 ticks, the same on 1 and 8 threads",
          "[script][entity][world]")
{
    u64 hash_one = 0;
    usize count_one = 0;
    {
        MoveSim sim(300);
        for (u32 t = 0; t < 120; ++t)
        {
            sim.Tick(1);
        }
        hash_one = sim.Hash();
        count_one = sim.Count();
    }
    MoveSim sim(300);
    for (u32 t = 0; t < 120; ++t)
    {
        sim.Tick(8);
    }
    CHECK(sim.Hash() == hash_one);
    CHECK(sim.Count() == count_one);
    CHECK(count_one != 300);  // entities were spawned and destroyed
}
