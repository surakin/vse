// The component and query bindings (Phase 27 Day 456): reflection by name over the storage registry, validation errors,
// script-defined components saved through storage, queries that give the same entities on any thread count.
#include <catch2/catch_test_macros.hpp>

#include "containers/array.h"
#include "cstr/cstr.h"
#include "ecs/query.h"
#include "ecs/transform.h"
#include "os/os.h"
#include "script/backend_registry.h"
#include "script/bindings.h"
#include "script/component_bindings.h"
#include "script/component_catalog.h"
#include "script/entity_bindings.h"
#include "script/luajit/luajit_backend.h"
#include "script/merge.h"
#include "script/merge_services.h"
#include "script/partition_context.h"
#include "script/script_world.h"
#include "storage/builtin_components.h"
#include "storage/identity.h"
#include "storage/memory_store.h"
#include "storage/world_storage.h"
#include "sync/atomic.h"
#include "sync/thread.h"

using namespace vse;
using namespace vse::script;

namespace
{

enum class GadgetMode : u8
{
    Off,
    On,
    Auto,
};

struct Gadget
{
    f32 power = 1.0f;
    i32 count = 0;
    bool armed = false;
    Vec3 where = Vec3(0, 0, 0);
    Quaternion spin = Quaternion(0, 0, 0, 1);
    GadgetMode mode = GadgetMode::Off;
    String<HeapAllocator> label{HeapAllocator::Default()};
    u64 asset = 0;
    Array<i32, 3> slots = {};
};

struct Marker
{
};

constexpr u32 kGadgetId = 2000;
constexpr u32 kMarkerId = 2001;

storage::Status RegisterComponentTestTypes(storage::ComponentRegistry* registry)
{
    using storage::Field;
    using storage::FieldOptions;
    storage::Status s = storage::RegisterBuiltinComponents(registry);
    if (s.Failed())
    {
        return s;
    }
    s = registry->Register<Gadget>(
        {.id = kGadgetId,
         .name = "gadget",
         .fields = {Field<&Gadget::power>("power"), Field<&Gadget::count>("count"), Field<&Gadget::armed>("armed"),
                    Field<&Gadget::where>("where"), Field<&Gadget::spin>("spin"),
                    Field<&Gadget::mode>("mode", FieldOptions::Enum(0, 2)), Field<&Gadget::label>("label"),
                    Field<&Gadget::asset>("asset", FieldOptions::Reference(0)), Field<&Gadget::slots>("slots")}});
    if (s.Failed())
    {
        return s;
    }
    return registry->Register<Marker>({.id = kMarkerId, .name = "marker"});
}

struct ComponentRig
{
    storage::ComponentRegistry registry;
    ComponentCatalog catalog;
    BindingRegistry bindings;
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    UniquePtr<State> state = backend->CreateState(0);
    UniquePtr<Installation> installed;
    ecs::World world;
    CommandKindTable kinds;
    CommandBuffer buffer;
    Overlay overlay;
    PartitionContext context;
    MergeServices services;
    ecs::Entity self;

    ComponentRig() : catalog(&registry)
    {
        REQUIRE(RegisterComponentTestTypes(&registry).Ok());
        REQUIRE(RegisterStandardBindings(&bindings));
        Outcome error;
        installed = bindings.Install(state.Get(), InstallProfile::Partition(ContextEntityCheck()), &error);
        REQUIRE(installed.Get() != nullptr);
        REQUIRE(RegisterEntityCommandKinds(&kinds));
        REQUIRE(RegisterComponentCommandKinds(&kinds));
        buffer.Begin(0);
        services.components = &catalog;
        Gadget g;
        g.power = 2.5f;
        g.count = 3;
        g.label.Append("hi");
        g.asset = 77;
        g.slots[0] = 1;
        g.slots[1] = 2;
        g.slots[2] = 3;
        self = ecs::CreateTransformEntity(world, ecs::LocalTransform());
        REQUIRE(world.Add(self, Move(g)) == ecs::Status::Ok);
        context.world = &world;
        context.commands = &buffer;
        context.overlay = &overlay;
        context.components = &catalog;
        context.current_entity_key = self.ToU64();
    }

    Outcome Run(const char* body, CallOutput* out, const Value* arg = nullptr, usize count = 1)
    {
        String<HeapAllocator> source(HeapAllocator::Default());
        source.Append("function run(a, b) ");
        source.Append(StringView(body));
        source.Append(" end");
        REQUIRE(state->Load("component_test", source.View()).IsOk());
        overlay.Clear();
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

bool Mentions(const Outcome& outcome, const char* text)
{
    return outcome.message.View().Contains(StringView(text));
}

}  // namespace

TEST_CASE("Component bindings: get, set, add and remove by name; the script sees its own writes", "[script][component]")
{
    ComponentRig rig;
    CallOutput out;
    const Value me = Value::FromEntity(rig.self);
    const Outcome first = rig.Run(
        "local g = component.get(a, 'gadget') "
        "return g.power, g.count, g.label, g.asset, g.slots[2], g.mode, g.armed, component.has(a, 'gadget'), "
        "component.has(a, 'marker'), component.exists('gadget'), component.exists('nope')",
        &out, &me);
    INFO(first.message.CStr());
    REQUIRE(first.IsOk());
    REQUIRE(out.results.Size() == 11);
    CHECK(out.results[0].ToDouble() == 2.5);
    CHECK(out.results[1].AsInt() == 3);
    CHECK(out.results[2].Text() == StringView("hi"));
    CHECK(out.results[3].AsInt() == 77);
    CHECK(out.results[4].AsInt() == 2);
    CHECK(out.results[5].AsInt() == 0);
    CHECK_FALSE(out.results[6].AsBool());
    CHECK(out.results[7].AsBool());
    CHECK_FALSE(out.results[8].AsBool());
    CHECK(out.results[9].AsBool());
    CHECK_FALSE(out.results[10].AsBool());

    const Outcome second = rig.Run(
        "component.set(a, 'gadget', {power = 5.5, count = 9, armed = true, where = vec3.new(1, 2, 3), mode = 2, "
        "label = 'new', slots = {7, 8, 9}}) "
        "component.add(a, 'marker', nil) "
        "local g = component.get(a, 'gadget') "
        "return g.power, g.count, g.armed, g.where, g.mode, g.label, g.slots[3], g.asset, component.has(a, 'marker')",
        &out, &me);
    INFO(second.message.CStr());
    REQUIRE(second.IsOk());
    REQUIRE(out.results.Size() == 9);
    CHECK(out.results[0].ToDouble() == 5.5);
    CHECK(out.results[1].AsInt() == 9);
    CHECK(out.results[2].AsBool());
    CHECK(out.results[3].AsVec3().z == 3.0f);
    CHECK(out.results[4].AsInt() == 2);
    CHECK(out.results[5].Text() == StringView("new"));
    CHECK(out.results[6].AsInt() == 9);
    CHECK(out.results[7].AsInt() == 77);  // untouched fields keep their value
    CHECK(out.results[8].AsBool());
    // The world holds the old values until the merge.
    CHECK(rig.world.Get<Gadget>(rig.self)->count == 3);
    CHECK_FALSE(rig.world.Has<Marker>(rig.self));
    const MergeReport report = rig.Merge();
    CHECK(report.applied == 2);
    CHECK(report.skipped == 0);
    const Gadget* g = rig.world.Get<Gadget>(rig.self);
    CHECK(g->power == 5.5f);
    CHECK(g->count == 9);
    CHECK(g->armed);
    CHECK(g->where.y == 2.0f);
    CHECK(g->mode == GadgetMode::Auto);
    CHECK(StringView(g->label.View()) == StringView("new"));
    CHECK(g->asset == 77);
    CHECK(g->slots[2] == 9);
    CHECK(rig.world.Has<Marker>(rig.self));

    rig.buffer.Begin(0);
    const Outcome third = rig.Run(
        "local had = component.remove(a, 'gadget') return had, component.has(a, 'gadget'), component.get(a, 'gadget'), "
        "component.remove(a, 'gadget')",
        &out, &me);
    INFO(third.message.CStr());
    REQUIRE(third.IsOk());
    CHECK(out.results[0].AsBool());
    CHECK_FALSE(out.results[1].AsBool());
    CHECK(out.results[2].IsNil());
    CHECK_FALSE(out.results[3].AsBool());
    CHECK(rig.Merge().applied == 1);
    CHECK_FALSE(rig.world.Has<Gadget>(rig.self));
}

TEST_CASE("Component bindings: a wrong value is an error that names the component and the field", "[script][component]")
{
    ComponentRig rig;
    CallOutput out;
    const Value me = Value::FromEntity(rig.self);
    struct Case
    {
        const char* body;
        const char* mentions_a;
        const char* mentions_b;
    };
    const Case cases[] = {
        {"component.set(a, 'gadget', {count = 'many'})", "gadget", "count"},
        {"component.set(a, 'gadget', {count = 1.5})", "gadget", "count"},
        {"component.set(a, 'gadget', {count = 3000000000})", "gadget", "count"},
        {"component.set(a, 'gadget', {mode = 9})", "gadget", "mode"},
        {"component.set(a, 'gadget', {armed = 1})", "gadget", "armed"},
        {"component.set(a, 'gadget', {power = 0 / 0})", "gadget", "power"},
        {"component.set(a, 'gadget', {power = 1e40})", "gadget", "power"},
        {"component.set(a, 'gadget', {where = 3})", "gadget", "where"},
        {"component.set(a, 'gadget', {slots = {1, 2}})", "gadget", "slots"},
        {"component.set(a, 'gadget', {label = 5})", "gadget", "label"},
        {"component.set(a, 'gadget', {powr = 1})", "gadget", "powr"},
        {"component.set(a, 'gadget', 5)", "gadget", "table"},
        {"component.get(a, 'gizmo')", "gizmo", "no component"},
        {"component.add(a, 'gadget', nil)", "gadget", "already"},
        {"component.set(a, 'marker', {})", "marker", "no `marker`"},
        {"component.set(a, 'local_transform', {})", "local_transform", "engine"},
        {"component.remove(a, 'local_transform')", "local_transform", "engine"},
        {"component.get(a, 'world_transform')", "world_transform", "not available"},
    };
    for (const Case& c : cases)
    {
        INFO(c.body);
        const Outcome ran = rig.Run(c.body, &out, &me);
        CHECK_FALSE(ran.IsOk());
        CHECK(Mentions(ran, c.mentions_a));
        CHECK(Mentions(ran, c.mentions_b));
    }
    // Nothing was recorded by the failed calls.
    CHECK(rig.buffer.Size() == 0);
    // The engine's component can be read.
    REQUIRE(rig.Run("return component.get(a, 'local_transform').scale", &out, &me).IsOk());
    CHECK(out.results[0].AsVec3().x == 1.0f);
}

TEST_CASE("Component bindings: the merge never applies a command that names an engine component", "[script][component]")
{
    ComponentRig rig;
    // A forged command: the stored id of `local_transform` (1) and a record of the right size.
    u8 payload[4 + 40] = {1, 0, 0, 0};
    REQUIRE(rig.buffer.Record(kCommandComponentSet, rig.self.ToU64(), rig.self, payload, sizeof(payload)));
    const MergeReport report = rig.Merge();
    CHECK(report.applied == 0);
    CHECK(report.skipped == 1);
    CHECK(rig.world.Get<ecs::LocalTransform>(rig.self)->scale.x == 1.0f);
}

TEST_CASE("Component bindings: a script defines a component, every state may repeat it, a different one is refused",
          "[script][component][define]")
{
    ComponentRig rig;
    CallOutput out;
    const Value me = Value::FromEntity(rig.self);
    REQUIRE(rig.Run("component.define('health', {hp = 'f32', max = 'f32', dead = 'bool', at = 'vec3'}) "
                    "return component.exists('health')",
                    &out)
                .IsOk());
    CHECK(out.results[0].AsBool());
    // The same definition with the fields in another order is the same component.
    CHECK(rig.Run("component.define('health', {at = 'vec3', dead = 'bool', max = 'f32', hp = 'f32'})", &out).IsOk());
    CHECK(rig.catalog.DefinedCount() == 1);
    const Outcome other = rig.Run("component.define('health', {hp = 'f64'})", &out);
    CHECK_FALSE(other.IsOk());
    CHECK(Mentions(other, "health"));
    CHECK_FALSE(rig.Run("component.define('gadget', {a = 'f32'})", &out).IsOk());  // an engine component's name
    CHECK_FALSE(rig.Run("component.define('bad name', {})", &out).IsOk());
    CHECK_FALSE(rig.Run("component.define('c', {x = 'string'})", &out).IsOk());
    CHECK_FALSE(rig.Run("component.define('c', 5)", &out).IsOk());

    const Outcome used = rig.Run("component.add(a, 'health', {hp = 10, max = 12.5, at = vec3.new(1, 2, 3)}) "
                                 "component.set(a, 'health', {hp = 9}) "
                                 "local h = component.get(a, 'health') return h.hp, h.max, h.dead, h.at",
                                 &out, &me);
    INFO(used.message.CStr());
    REQUIRE(used.IsOk());
    CHECK(out.results[0].ToDouble() == 9.0);
    CHECK(out.results[1].ToDouble() == 12.5);
    CHECK_FALSE(out.results[2].AsBool());
    CHECK(out.results[3].AsVec3().y == 2.0f);
    CHECK(rig.Merge().applied == 2);
    const storage::RegisteredComponent* health = rig.registry.FindByName("health");
    REQUIRE(health != nullptr);
    CHECK(health->Id() >= 0x80000000u);
    CHECK(health->IsStored());
    bytes::ByteWriter encoded;
    REQUIRE(health->Encode(rig.world.CellOf(rig.self, health->EcsId()), encoded, storage::CodecLimits()).Ok());
    CHECK(encoded.Size() == 4 + 4 + 1 + 12);  // fields in name order: at, dead, hp, max

    // After the first tick nothing new is defined, and the same definition still is accepted.
    rig.catalog.Freeze();
    CHECK(rig.Run("component.define('health', {hp = 'f32', max = 'f32', dead = 'bool', at = 'vec3'})", &out).IsOk());
    const Outcome late = rig.Run("component.define('late', {x = 'f32'})", &out);
    CHECK_FALSE(late.IsOk());
    CHECK(Mentions(late, "before the first tick"));
}

TEST_CASE("Component bindings: a catalog defines at most 32 components, and a component outlives its catalog",
          "[script][component][define]")
{
    storage::ComponentRegistry registry;
    ecs::ComponentId ecs_id = ecs::kInvalidComponent;
    {
        ComponentCatalog catalog(&registry);
        String<HeapAllocator> error(HeapAllocator::Default());
        Value fields = Value::MakeMap();
        fields.Set("x", Value::FromText("f32"));
        for (u32 i = 0; i < kMaxScriptComponents; ++i)
        {
            String<HeapAllocator> name(HeapAllocator::Default(), "capped_");
            name.Append(static_cast<char>('a' + i / 26));
            name.Append(static_cast<char>('a' + i % 26));
            REQUIRE(catalog.Define(name.View(), fields, &error));
        }
        CHECK_FALSE(catalog.Define("one_too_many", fields, &error));
        CHECK(error.View().Contains(StringView("at most 32")));
        CHECK(catalog.Define("capped_ad", fields, &error));  // the same definition again is not a new one
        ecs_id = registry.FindByName("capped_ah")->EcsId();
    }
    // The catalog is gone; the registries copied the names they point at (a use after free under ASan before).
    const storage::RegisteredComponent* kept = registry.FindByName("capped_ah");
    REQUIRE(kept != nullptr);
    CHECK(StringView(kept->Name()) == StringView("capped_ah"));
    CHECK(StringView(kept->Fields()[0].name) == StringView("x"));
    CHECK(StringView(ecs::ComponentRegistry::Global().Info(ecs_id).name) == StringView("capped_ah"));
}

TEST_CASE("Component bindings: a script-defined component is saved and loaded through storage",
          "[script][component][define]")
{
    ComponentRig rig;
    CallOutput out;
    REQUIRE(
        rig.Run("component.define('stats', {level = 'i32', xp = 'i64', speed = 'f32', tint = 'vec4'})", &out).IsOk());
    storage::KeyIndex index;
    storage::KeyGenerator generator = storage::KeyGenerator::Seeded(11);
    ecs::Entity saved;
    {
        ecs::World& w = rig.world;
        saved = w.Create(storage::EntityKeyComponent{generator.Next()});
        REQUIRE(saved.IsValid());
    }
    rig.context.current_entity_key = saved.ToU64();
    const Value target = Value::FromEntity(saved);
    REQUIRE(
        rig.Run("component.add(a, 'stats', {level = 7, xp = 5000000000, speed = 1.25, tint = vec4.new(1, 0, 0.5, 1)})",
                &out, &target)
            .IsOk());
    REQUIRE(rig.Merge().applied == 1);
    storage::MemoryStore store;
    storage::WorldStorage storage(rig.registry, store);
    storage::Report save_report;
    REQUIRE(storage.Save(rig.world, index, storage::SaveOptions(), &save_report).Ok());

    ecs::World loaded;
    storage::KeyIndex loaded_index;
    storage::Report load_report;
    REQUIRE(storage.Load(loaded, loaded_index, storage::LoadOptions(), &load_report).Ok());
    const storage::RegisteredComponent* stats = rig.registry.FindByName("stats");
    REQUIRE(stats != nullptr);
    usize found = 0;
    for (u32 a = 0; a < loaded.ArchetypeCount(); ++a)
    {
        const ecs::Archetype& archetype = loaded.ArchetypeAt(a);
        if (!archetype.Has(stats->EcsId()))
        {
            continue;
        }
        const Span<ecs::Entity> entities = archetype.Entities(archetype.ChunkAt(0));
        for (usize i = 0; i < entities.Size(); ++i)
        {
            bytes::ByteWriter a_bytes;
            bytes::ByteWriter b_bytes;
            REQUIRE(stats->Encode(loaded.CellOf(entities[i], stats->EcsId()), a_bytes, storage::CodecLimits()).Ok());
            REQUIRE(stats->Encode(rig.world.CellOf(saved, stats->EcsId()), b_bytes, storage::CodecLimits()).Ok());
            CHECK(a_bytes.Size() == b_bytes.Size());
            CHECK(MemCompare(a_bytes.Bytes().Data(), b_bytes.Bytes().Data(), a_bytes.Size()) == 0);
            ++found;
        }
    }
    CHECK(found == 1);
}

namespace
{

// A world of entities that have a Gadget (every third also a Marker), for the query tests.
void FillQueryWorld(ecs::World* world, u32 count)
{
    for (u32 i = 0; i < count; ++i)
    {
        const ecs::Entity e = ecs::CreateTransformEntity(*world, ecs::LocalTransform());
        if (i % 2 == 0)
        {
            Gadget g;
            g.count = static_cast<i32>(i);
            REQUIRE(world->Add(e, Move(g)) == ecs::Status::Ok);
        }
        if (i % 3 == 0)
        {
            REQUIRE(world->Add(e, Marker()) == ecs::Status::Ok);
        }
    }
}

}  // namespace

TEST_CASE("Component bindings: queries return entities in stable id order; each keeps the partition's own",
          "[script][component][query]")
{
    ComponentRig rig;
    FillQueryWorld(&rig.world, 600);
    rig.context.partitions = 4;
    CallOutput out;
    REQUIRE(
        rig.Run("return query.all({'gadget'}), query.all({'gadget', 'marker'}), query.all({'marker'})", &out).IsOk());
    REQUIRE(out.results.Size() == 3);
    const usize gadgets = 301;  // 300 of the 600 and the rig's own
    CHECK(out.results[0].Size() == gadgets);
    CHECK(out.results[1].Size() == 100);
    CHECK(out.results[2].Size() == 200);
    for (usize i = 1; i < out.results[0].Size(); ++i)
    {
        CHECK(out.results[0].At(i - 1).AsEntity().ToU64() < out.results[0].At(i).AsEntity().ToU64());
    }
    // `each` over the four partitions covers `all`, each entity in exactly one.
    usize total = 0;
    Vector<u64> seen;
    for (u32 p = 0; p < 4; ++p)
    {
        rig.buffer.Begin(p);
        CallOutput part;
        REQUIRE(rig.Run("return query.each({'gadget'})", &part).IsOk());
        total += part.results[0].Size();
        for (usize i = 0; i < part.results[0].Size(); ++i)
        {
            const u64 id = part.results[0].At(i).AsEntity().ToU64();
            CHECK(PartitionOf(id, 4) == p);
            seen.Push(id);
        }
    }
    CHECK(total == gadgets);
    // Pages walk the same list.
    Vector<u64> paged;
    Value after;
    for (;;)
    {
        CallOutput page;
        const Value args[2] = {Value::FromInt(64), after};
        REQUIRE(rig.Run("return query.page({'gadget'}, b, a)", &page, args, 2).IsOk());
        if (page.results[0].Size() == 0)
        {
            break;
        }
        for (usize i = 0; i < page.results[0].Size(); ++i)
        {
            paged.Push(page.results[0].At(i).AsEntity().ToU64());
        }
        after = Value::FromEntity(page.results[0].At(page.results[0].Size() - 1).AsEntity());
    }
    CHECK(paged.Size() == gadgets);
    for (usize i = 0; i < paged.Size(); ++i)
    {
        CHECK(paged[i] == out.results[0].At(i).AsEntity().ToU64());
    }
    CHECK_FALSE(rig.Run("return query.all({})", &out).IsOk());
    CHECK_FALSE(rig.Run("return query.all({'nope'})", &out).IsOk());
    CHECK_FALSE(rig.Run("return query.all('gadget')", &out).IsOk());
    CHECK_FALSE(rig.Run("return query.page({'gadget'}, nil, 0)", &out).IsOk());
}

namespace
{

const char* const kDrain = "function step(tick)\n"
                           "  for _, e in ipairs(query.each({'gadget'})) do\n"
                           "    local g = component.get(e, 'gadget')\n"
                           "    component.set(e, 'gadget', {count = g.count + 1, power = g.power * 0.5 + tick})\n"
                           "    if g.count % 5 == 0 and not component.has(e, 'marker') then\n"
                           "      component.add(e, 'marker', nil)\n"
                           "    elseif g.count % 5 == 1 then\n"
                           "      component.remove(e, 'marker')\n"
                           "    end\n"
                           "  end\n"
                           "end\n";

struct DrainSim
{
    ecs::World world;
    BindingRegistry bindings;
    CommandKindTable kinds;
    storage::ComponentRegistry registry;
    ComponentCatalog catalog;
    MergeServices services;
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    UniquePtr<ScriptWorld> scripts;
    Vector<UniquePtr<CommandBuffer>> buffers;
    u32 partitions = 16;
    u64 tick = 0;

    DrainSim() : catalog(&registry)
    {
        REQUIRE(RegisterComponentTestTypes(&registry).Ok());
        REQUIRE(RegisterStandardBindings(&bindings));
        REQUIRE(RegisterEntityCommandKinds(&kinds));
        REQUIRE(RegisterComponentCommandKinds(&kinds));
        services.components = &catalog;
        ScriptWorldConfig config;
        config.backend = backend.Get();
        config.bindings = &bindings;
        config.profile = InstallProfile::Partition(ContextEntityCheck());
        config.partitions = partitions;
        scripts = MakeUnique<ScriptWorld>(config);
        scripts->AddScript("drain", CopyShared(ByteSpan(reinterpret_cast<const byte*>(kDrain), CStrLength(kDrain))));
        for (u32 p = 0; p < partitions; ++p)
        {
            buffers.Push(MakeUnique<CommandBuffer>());
        }
        FillQueryWorld(&world, 400);
    }

    void Tick(u32 threads)
    {
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
                            context.components = &catalog;
                            context.partitions = partitions;
                            context.current_entity_key = p;
                            context.tick = tick;
                            ScopedPartitionContext scope(&context);
                            Value args[1] = {Value::FromInt(static_cast<i64>(tick))};
                            CallOutput out;
                            Budget budget;
                            budget.instructions = 50000000;
                            if (!lease.Call("step", Span<const Value>(args, 1), budget, &out).IsOk())
                            {
                                failures.FetchAdd(1, MemoryOrder::AcqRel);
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
        Merge(&world, Span<CommandBuffer* const>(order.Data(), order.Size()), kinds, ConflictPolicy::LastWins,
              &services);
        ++tick;
    }

    u64 Hash()
    {
        u64 sum = 0;
        world.Query<ecs::Read<Gadget>>().ForEach(
            [&](ecs::Entity e, const Gadget& g)
            {
                u32 bits;
                MemCopy(&bits, &g.power, sizeof(bits));
                u64 h = e.ToU64() * 0x9E3779B97F4A7C15ull;
                h = (h ^ bits) * 0x100000001B3ull;
                h = (h ^ static_cast<u64>(g.count)) * 0x100000001B3ull;
                h = (h ^ (world.Has<Marker>(e) ? 1u : 0u)) * 0x100000001B3ull;
                sum += h;
            });
        return sum;
    }
};

}  // namespace

TEST_CASE("Component bindings: scripts change components through queries for 40 ticks, the same on 1 and 8 threads",
          "[script][component][query][world]")
{
    u64 hash_one = 0;
    {
        DrainSim sim;
        for (u32 t = 0; t < 40; ++t)
        {
            sim.Tick(1);
        }
        hash_one = sim.Hash();
    }
    DrainSim sim;
    for (u32 t = 0; t < 40; ++t)
    {
        sim.Tick(8);
    }
    CHECK(sim.Hash() == hash_one);
    CHECK(hash_one != 0);
    // Every gadget ran 40 times: the counts moved by exactly 40.
    sim.world.Query<ecs::Read<Gadget>>().ForEach([&](ecs::Entity, const Gadget& g) { CHECK(g.count >= 40); });
}

TEST_CASE("Component bindings: the cost of a partition query and of a whole-world query over 100,000 entities",
          "[script][component][query][cost]")
{
    ComponentRig rig;
    for (u32 i = 0; i < 100000; ++i)
    {
        rig.world.Create(Marker());
    }
    rig.context.partitions = 16;
    CallOutput out;
    const u64 each_start = os::MonotonicTimeNanos();
    REQUIRE(rig.Run("return query.each({'marker'})", &out).IsOk());
    const u64 each_ns = os::MonotonicTimeNanos() - each_start;
    CHECK(out.results[0].Size() > 5000);
    CHECK(out.results[0].Size() < 7500);
    const u64 page_start = os::MonotonicTimeNanos();
    const Outcome paged = rig.Run("return query.page({'marker'}, nil, 60000)", &out);
    INFO(paged.message.CStr());
    REQUIRE(paged.IsOk());
    const u64 page_ns = os::MonotonicTimeNanos() - page_start;
    CHECK(out.results[0].Size() == 60000);
    const u64 all_start = os::MonotonicTimeNanos();
    const Outcome all = rig.Run("return query.all({'marker'})", &out);
    const u64 all_ns = os::MonotonicTimeNanos() - all_start;
    CHECK_FALSE(all.IsOk());  // 100,001 matches: over the 60000 a call returns
    WARN("cost over 100,001 entities: query.each of one of 16 partitions "
         << each_ns / 1000 << " us, query.page of 60000 " << page_ns / 1000 << " us, query.all (refused, over 60000) "
         << all_ns / 1000 << " us");
}
