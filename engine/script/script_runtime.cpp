#include "script/script_runtime.h"

#include "algorithm.h"
#include "containers/vector.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "log/assert.h"
#include "script/command_buffer.h"
#include "script/component_bindings.h"
#include "script/log_buffer.h"
#include "script/partition_context.h"
#include "script/script_world.h"
#include "script/settings_bindings.h"

namespace engine_placeholder::script
{

namespace
{

/// One behavior instance: what the runtime knows about it outside the language state.
struct Instance
{
    u64 key = 0;
    ecs::Entity entity;
    u32 serial = 0;
    u32 partition = 0;
    String<HeapAllocator> behavior{HeapAllocator::Default()};
    /// The entity still has its script component (cleared by a reconcile that does not find it).
    bool alive = true;
    /// `__behavior_create` succeeded.
    bool created = false;
    /// The enabled state the script was last told about (`on_enable` called and not yet `on_disable`).
    bool notified_enabled = false;
    bool started = false;
    /// An error disabled the instance: no callback runs, but it is still dropped.
    bool failed = false;
    bool logged = false;
};

struct FoundScript
{
    u64 key = 0;
    ecs::Entity entity;
};

/// What one partition keeps between ticks.
struct PartitionData
{
    CommandBuffer commands;
    Overlay overlay;
    Vector<u32> members;
    u64 resets_seen = 0;
    bool load_logged = false;
};

}  // namespace

struct ScriptRuntime::Impl
{
    explicit Impl(const ScriptRuntimeConfig& cfg) : config(cfg), services(cfg.services) {}

    ScriptRuntimeConfig config;
    UniquePtr<ScriptWorld> scripts;
    CommandKindTable kinds;
    MergeServices services;
    Vector<UniquePtr<PartitionData>> partitions;
    Vector<Instance> instances;
    Vector<FoundScript> found;
    ScriptRuntimeStats stats;
    u32 next_serial = 1;
    u64 tick = 0;
    f64 sim_time = 0.0;
    u32 fixed_steps = 0;
    f64 fixed_dt = 0.0;
    bool any_gone = false;

    void Reconcile(ecs::World* world, bool allow_new);
    void Compact();
    void RunPhase(ecs::World* world, f64 dt, bool destroys_only);
    void RunPartition(ecs::World* world, u32 p, f64 dt, bool destroys_only);
    bool Call(PartitionLease* lease, PartitionData* data, Instance* in, const char* callback, f64 dt);
    void Fail(Instance* in, const char* what, const Outcome& outcome);
    void CheckReset(u32 p, PartitionData* data);
    void RunLive(PartitionLease* lease, PartitionData* data, Instance* in, const ScriptComponent& comp, f64 dt);
    void RunDestroy(PartitionLease* lease, PartitionData* data, Instance* in, f64 dt, bool entity_exists);
};

void ScriptRuntime::Impl::Reconcile(ecs::World* world, bool allow_new)
{
    found.Clear();
    world->Query<ecs::Read<ScriptComponent>>().ForEach(
        [&](ecs::Entity e, const ScriptComponent&)
        {
            FoundScript f;
            f.key = e.ToU64();
            f.entity = e;
            found.Push(f);
        });
    Sort(Span<FoundScript>(found.Data(), found.Size()),
         [](const FoundScript& a, const FoundScript& b) { return a.key < b.key; });
    any_gone = false;
    bool same = found.Size() == instances.Size();
    for (usize k = 0; same && k < found.Size(); ++k)
    {  // the usual tick changes nothing: no rebuild
        const ScriptComponent* comp = static_cast<const ecs::World*>(world)->Get<ScriptComponent>(found[k].entity);
        same = found[k].key == instances[k].key && comp->behavior.View() == instances[k].behavior.View();
    }
    if (same)
    {
        return;
    }
    Vector<Instance> merged;
    usize i = 0;
    usize j = 0;
    const u32 partition_count = config.partitions;
    while (i < instances.Size() || j < found.Size())
    {
        if (j == found.Size() || (i < instances.Size() && instances[i].key < found[j].key))
        {
            instances[i].alive = false;
            any_gone = true;
            merged.Push(Move(instances[i]));
            ++i;
        }
        else if (i == instances.Size() || found[j].key < instances[i].key)
        {
            if (allow_new)
            {
                const ScriptComponent* comp = static_cast<const ecs::World*>(world)->Get<ScriptComponent>(
                    found[j].entity);
                Instance in;
                in.key = found[j].key;
                in.entity = found[j].entity;
                in.serial = next_serial++;
                in.partition = PartitionOf(in.key, partition_count);
                in.behavior = String<HeapAllocator>(HeapAllocator::Default(), comp->behavior.View());
                merged.Push(Move(in));
            }
            ++j;
        }
        else
        {
            const ScriptComponent* comp = static_cast<const ecs::World*>(world)->Get<ScriptComponent>(found[j].entity);
            if (comp->behavior.View() != instances[i].behavior.View())
            {
                // A new behavior replaces the instance: the old one is closed now, the new one is made next tick.
                instances[i].alive = false;
                any_gone = true;
            }
            merged.Push(Move(instances[i]));
            ++i;
            ++j;
        }
    }
    instances = Move(merged);
}

void ScriptRuntime::Impl::Compact()
{
    Vector<Instance> kept;
    for (usize i = 0; i < instances.Size(); ++i)
    {
        if (instances[i].alive)
        {
            kept.Push(Move(instances[i]));
        }
    }
    instances = Move(kept);
}

void ScriptRuntime::Impl::Fail(Instance* in, const char* what, const Outcome& outcome)
{
    ++stats.errors;
    if (!in->logged)
    {
        in->logged = true;
        char key[32];
        FormatTo(key, sizeof(key), "%llu", static_cast<unsigned long long>(in->key));
        String<HeapAllocator> text(HeapAllocator::Default());
        text.Append("behavior '");
        text.Append(in->behavior.View());
        text.Append("' on entity ");
        text.Append(StringView(key));
        text.Append(": ");
        text.Append(StringView(what));
        text.Append(" failed: ");
        text.Append(outcome.message.View());
        WriteScriptLog(ScriptLogLevel::Error, in->behavior.View(), 0, text.View());
    }
    switch (config.on_error)
    {
    case OnScriptError::Retry: break;
    case OnScriptError::AbortInDebug:
        ENGINE_CHECK_SLOW(false, "a behavior callback failed (script.on_error = abort_in_debug)");
        in->failed = true;
        ++stats.disabled_by_error;
        break;
    case OnScriptError::Disable:
    default:
        in->failed = true;
        ++stats.disabled_by_error;
        break;
    }
}

void ScriptRuntime::Impl::CheckReset(u32 p, PartitionData* data)
{
    const u64 resets = scripts->Counters(p).resets;
    if (resets == data->resets_seen)
    {
        return;
    }
    data->resets_seen = resets;
    for (usize k = 0; k < data->members.Size(); ++k)
    {
        Instance& in = instances[data->members[k]];
        if (in.created && !in.failed)
        {
            in.failed = true;
            ++stats.disabled_by_error;
            ++stats.errors;
            if (!in.logged)
            {
                in.logged = true;
                WriteScriptLog(ScriptLogLevel::Error, in.behavior.View(), 0,
                               "the partition's state was reset (out of memory); its instances are disabled");
            }
        }
    }
}

bool ScriptRuntime::Impl::Call(PartitionLease* lease, PartitionData* data, Instance* in, const char* callback, f64 dt)
{
    Value args[3] = {Value::FromInt(static_cast<i64>(in->serial)), Value::FromText(StringView(callback)),
                     Value::FromFloat(dt)};
    CallOutput out;
    const Outcome outcome = lease->Call("__behavior_call", Span<const Value>(args, 3), config.budget, &out);
    CheckReset(lease->Partition(), data);
    if (!outcome.IsOk())
    {
        if (!in->failed)
        {
            Fail(in, callback, outcome);
        }
        return false;
    }
    return !in->failed;
}

void ScriptRuntime::Impl::RunLive(PartitionLease* lease, PartitionData* data, Instance* in, const ScriptComponent& comp,
                                  f64 dt)
{
    if (in->failed)
    {
        return;
    }
    if (!in->created)
    {
        Value args[4] = {Value::FromInt(static_cast<i64>(in->serial)), Value::FromText(in->behavior.View()),
                         Value::FromEntity(in->entity), comp.params};
        CallOutput out;
        const Outcome outcome = lease->Call("__behavior_create", Span<const Value>(args, 4), config.budget, &out);
        CheckReset(lease->Partition(), data);
        if (!outcome.IsOk())
        {
            Fail(in, "__behavior_create", outcome);
            return;
        }
        in->created = true;
        ++stats.created;
        if (!Call(lease, data, in, "on_create", dt))
        {
            return;
        }
    }
    if (comp.enabled != in->notified_enabled)
    {
        in->notified_enabled = comp.enabled;
        if (!Call(lease, data, in, comp.enabled ? "on_enable" : "on_disable", dt))
        {
            return;
        }
    }
    if (!in->notified_enabled)
    {
        return;
    }
    if (!in->started)
    {
        in->started = true;
        ++stats.started;
        if (!Call(lease, data, in, "on_start", dt))
        {
            return;
        }
    }
    for (u32 s = 0; s < fixed_steps; ++s)
    {
        ++stats.fixed_updates;
        if (!Call(lease, data, in, "on_fixed_update", fixed_dt))
        {
            return;
        }
    }
    ++stats.updates;
    Call(lease, data, in, "on_update", dt);
}

void ScriptRuntime::Impl::RunDestroy(PartitionLease* lease, PartitionData* data, Instance* in, f64 dt,
                                     bool entity_exists)
{
    if (!in->created)
    {
        return;
    }
    if (!in->failed)
    {
        if (in->notified_enabled && entity_exists)
        {  // a dead entity gets no on_disable: its own components are gone, so the callback could not work
            in->notified_enabled = false;
            Call(lease, data, in, "on_disable", dt);
        }
        if (!in->failed)
        {
            Call(lease, data, in, "on_destroy", dt);
        }
    }
    Value args[1] = {Value::FromInt(static_cast<i64>(in->serial))};
    CallOutput out;
    lease->Call("__behavior_drop", Span<const Value>(args, 1), config.budget, &out);
    CheckReset(lease->Partition(), data);
    ++stats.destroyed;
}

void ScriptRuntime::Impl::RunPartition(ecs::World* world, u32 p, f64 dt, bool destroys_only)
{
    PartitionData& data = *partitions[p];
    if (data.members.Size() == 0)
    {
        return;
    }
    PartitionLease lease;
    Outcome error;
    if (scripts->Checkout(p, &lease, &error) != CheckoutStatus::Ok)
    {
        ++stats.skipped_partitions;
        if (!data.load_logged)
        {
            data.load_logged = true;
            String<HeapAllocator> text(HeapAllocator::Default(), "partition state unavailable: ");
            text.Append(error.message.View());
            WriteScriptLog(ScriptLogLevel::Error, "script runtime", 0, text.View());
        }
        return;
    }
    data.resets_seen = scripts->Counters(p).resets;
    PartitionContext context;
    context.world = world;
    context.commands = &data.commands;
    context.overlay = &data.overlay;
    context.world_seed = config.random_seed;
    context.tick = tick;
    context.dt = dt;
    context.sim_time = sim_time;
    context.components = config.components;
    context.scene = config.scene;
    context.partitions = config.partitions;
    ScopedPartitionContext scope(&context);
    for (usize k = 0; k < data.members.Size(); ++k)
    {
        Instance& in = instances[data.members[k]];
        context.current_entity_key = in.key;
        data.overlay.Clear();
        SeedPartitionRandom(&context);
        if (!in.alive)
        {
            RunDestroy(&lease, &data, &in, dt, static_cast<const ecs::World*>(world)->Exists(in.entity));
        }
        else if (!destroys_only)
        {
            const ScriptComponent* comp = static_cast<const ecs::World*>(world)->Get<ScriptComponent>(in.entity);
            if (comp != nullptr)
            {
                RunLive(&lease, &data, &in, *comp, dt);
            }
        }
    }
}

void ScriptRuntime::Impl::RunPhase(ecs::World* world, f64 dt, bool destroys_only)
{
    const u32 count = config.partitions;
    for (u32 p = 0; p < count; ++p)
    {
        partitions[p]->commands.Begin(p);
        partitions[p]->members.Clear();
    }
    for (usize i = 0; i < instances.Size(); ++i)
    {
        partitions[instances[i].partition]->members.Push(static_cast<u32>(i));
    }
    {
        ReadOnlyPhase phase(world);
        for (u32 p = 0; p < count; ++p)
        {
            RunPartition(world, p, dt, destroys_only);
        }
    }
    Vector<CommandBuffer*> order;
    for (u32 p = 0; p < count; ++p)
    {
        order.Push(&partitions[p]->commands);
    }
    const MergeReport report = Merge(world, Span<CommandBuffer* const>(order.Data(), order.Size()), kinds,
                                     config.conflicts, &services);
    stats.commands_applied += report.applied;
}

ScriptRuntime::ScriptRuntime(const ScriptRuntimeConfig& config)
{
    ENGINE_ASSERT(config.backend != nullptr && config.bindings != nullptr,
                  "ScriptRuntime: the config needs a backend and bindings");
    ENGINE_ASSERT(config.partitions >= 1 && config.partitions <= kMaxPartitions,
                  "ScriptRuntime: partitions must be 1 to kMaxPartitions");
    impl_ = MakeUnique<Impl>(config);
    Impl& m = *impl_;
    if (m.services.components == nullptr)
    {
        m.services.components = config.components;
    }
    const bool kinds_ok = RegisterEntityCommandKinds(&m.kinds) && RegisterComponentCommandKinds(&m.kinds) &&
                          RegisterSettingsCommandKind(&m.kinds);
    ENGINE_ASSERT(kinds_ok, "ScriptRuntime: a command kind id is taken");
    ScriptWorldConfig world_config;
    world_config.backend = config.backend;
    world_config.bindings = config.bindings;
    world_config.profile = InstallProfile::Partition(ContextEntityCheck());
    world_config.partitions = config.partitions;
    world_config.partition_memory = config.partition_memory;
    m.scripts = MakeUnique<ScriptWorld>(world_config);
    for (u32 p = 0; p < config.partitions; ++p)
    {
        m.partitions.Push(MakeUnique<PartitionData>());
    }
    const char* host = !config.host_source.Empty() ? nullptr : LuaBehaviorHostSource();
    const StringView text = host != nullptr ? StringView(host) : config.host_source;
    m.scripts->AddScript("behavior_host",
                         CopyShared(ByteSpan(reinterpret_cast<const byte*>(text.Data()), text.Length())));
}

ScriptRuntime::~ScriptRuntime() = default;
ScriptRuntime::ScriptRuntime(ScriptRuntime&& other) noexcept = default;
ScriptRuntime& ScriptRuntime::operator=(ScriptRuntime&& other) noexcept = default;

void ScriptRuntime::AddScript(StringView name, SharedBytesPtr source)
{
    impl_->scripts->AddScript(name, Move(source));
}

void ScriptRuntime::SetFixedSteps(u32 steps, f64 fixed_dt)
{
    impl_->fixed_steps = steps;
    impl_->fixed_dt = fixed_dt;
}

ScriptWorld& ScriptRuntime::Scripts()
{
    return *impl_->scripts;
}

const ScriptRuntimeStats& ScriptRuntime::Stats() const
{
    return impl_->stats;
}

MergeServices& ScriptRuntime::Services()
{
    return impl_->services;
}

void ScriptRuntime::Tick(ecs::World* world, f64 dt)
{
    Impl& m = *impl_;
    m.Reconcile(world, true);
    m.RunPhase(world, dt, false);
    m.Compact();
    // Entities the merge destroyed, or whose script component it removed, are closed in a second, short phase.
    m.Reconcile(world, false);
    if (m.any_gone)
    {
        m.RunPhase(world, dt, true);
        m.Compact();
    }
    ++m.tick;
    m.sim_time += dt;
    ++m.stats.ticks;
    m.stats.instances = m.instances.Size();
}

ecs::SystemDesc MakeScriptSystem()
{
    return ecs::MakeSystem("script", [](ecs::SystemContext& ctx)
                           { ctx.Resource<ScriptRuntime>().Tick(&ctx.ExclusiveWorld(), static_cast<f64>(ctx.Dt())); })
        .ReadsAllComponents()
        .WritesAllComponents()
        .WritesResource<ScriptRuntime>();
}

}  // namespace engine_placeholder::script
