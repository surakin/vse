#include "script/script_runtime.h"
#include "containers/hash.h"

#include "algorithm.h"
#include "jobs/parallel_for.h"
#include "jobs/scheduler.h"
#include "containers/vector.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "log/assert.h"
#include "script/command_buffer.h"
#include "script/component_bindings.h"
#include "script/event_bindings.h"
#include "script/log_buffer.h"
#include "script/partition_context.h"
#include "script/script_world.h"
#include "script/settings_bindings.h"
#include "script/task_pool.h"

namespace engine_placeholder::script
{

// The runtime's own types are in a named namespace, not an anonymous one: `ScriptRuntime::Impl` (a class with external
// linkage) holds them, and GCC refuses a field of internal linkage there (`-Wsubobject-linkage`, an error in a unity
// build).
namespace runtime_detail
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
    /// The last error's identity (callback and message) and how many times it happened in a row; 0 means none yet.
    u64 error_hash = 0;
    u64 error_count = 0;
    /// Persistent fields a load gave this entity: a map, applied (with `on_load`) once the instance exists.
    Value pending;
    bool has_pending = false;
};

/// The persistent fields of an entity that a load queued, until its instance takes them.
struct LoadedEntry
{
    u64 key = 0;
    Value fields;
};

/// A new error of one instance, kept until the phase ends and then logged, recorded and sent as an event in partition
/// order.
struct ErrorReport
{
    ScriptErrorRecord record;
    u64 hash = 0;
};

/// More of an error the instance had already reported in this phase.
struct ErrorRepeat
{
    u64 entity_key = 0;
    u64 hash = 0;
    u64 count = 0;
};

/// A console chunk waiting for the next tick, or its result.
struct ConsoleJob
{
    u64 id = 0;
    u32 partition = 0;
    String<HeapAllocator> chunk{HeapAllocator::Default()};
};

struct ConsoleDone
{
    u64 id = 0;
    ConsoleResult result;
};

struct FoundScript
{
    u64 key = 0;
    ecs::Entity entity;
};

/// A call that suspended in a wait and the condition that resumes it.
struct Waiter
{
    u64 key = 0;  // the instance's stable id
    u64 coroutine = 0;
    WaitRequest::Kind kind = WaitRequest::Kind::None;
    f64 wake_time = 0.0;  // Seconds: the simulation time to resume at
    u64 wake_tick = 0;    // Frames: the tick to resume at
    String<HeapAllocator> event{HeapAllocator::Default()};
    u32 sequence = 0;  // creation order, the last tie-break of the wake order
    u64 task = 0;      // Task: the handle it waits for
};

/// The instances of one partition subscribed to one event name.
struct Group
{
    String<HeapAllocator> name{HeapAllocator::Default()};
    Vector<u64> keys;
};

/// What one partition keeps between ticks.
struct PartitionData
{
    CommandBuffer commands;
    Overlay overlay;
    Vector<u32> members;
    EventOutbox outbox;
    Vector<Waiter> waiters;        // timers: Seconds and Frames
    Vector<Waiter> event_waiters;  // coroutines in wait_event, scanned only when an event is delivered
    Vector<Waiter> task_waiters;   // coroutines in wait_task
    TaskOutbox task_outbox;        // the tasks asked for in this tick
    Vector<u64> task_taken;        // the handles taken in this tick, erased at the sync point
    TaskPartition task_view;       // what the task bindings of this partition's job reach
    u32 task_held = 0;             // the finished tasks the table holds for this partition's instances
    u32 next_sequence = 0;
    bool subscription_limit_logged = false;
    Vector<Group> groups;         // event name -> subscribed instance keys, sorted by name and key
    Vector<ErrorReport> reports;  // this phase's new errors, in the order they happened
    Vector<ErrorRepeat> repeats;  // this phase's repeats of earlier errors
    LogBuffer log;                // this phase's log lines, flushed in partition order after the jobs finish
    ScriptRuntimeStats stats;     // this phase's counts, summed after the jobs finish
    u64 resets_seen = 0;
    bool load_logged = false;
};

/// The index of the first group whose name is not below `name` (the groups are sorted by name).
usize LowerBoundGroup(const Vector<Group>& groups, StringView name)
{
    usize lo = 0;
    usize hi = groups.Size();
    while (lo < hi)
    {
        const usize mid = (lo + hi) / 2;
        if (groups[mid].name.View() < name)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }
    return lo;
}

usize LowerBoundKey(const Vector<u64>& keys, u64 key)
{
    usize lo = 0;
    usize hi = keys.Size();
    while (lo < hi)
    {
        const usize mid = (lo + hi) / 2;
        if (keys[mid] < key)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }
    return lo;
}

void Subscribe(Vector<Group>* groups, StringView name, u64 key)
{
    usize g = LowerBoundGroup(*groups, name);
    if (g == groups->Size() || (*groups)[g].name.View() != name)
    {
        Group group;
        group.name = String<HeapAllocator>(HeapAllocator::Default(), name);
        groups->Insert(g, Move(group));
    }
    Vector<u64>& keys = (*groups)[g].keys;
    const usize at = LowerBoundKey(keys, key);
    if (at == keys.Size() || keys[at] != key)
    {
        keys.Insert(at, key);
    }
}

void Unsubscribe(Vector<Group>* groups, StringView name, u64 key)
{
    const usize g = LowerBoundGroup(*groups, name);
    if (g == groups->Size() || (*groups)[g].name.View() != name)
    {
        return;
    }
    Vector<u64>& keys = (*groups)[g].keys;
    const usize at = LowerBoundKey(keys, key);
    if (at < keys.Size() && keys[at] == key)
    {
        keys.Remove(at);
    }
    if (keys.Size() == 0)
    {
        groups->Remove(g);
    }
}

void UnsubscribeAll(Vector<Group>* groups, u64 key)
{
    for (usize g = groups->Size(); g > 0; --g)
    {
        Vector<u64>& keys = (*groups)[g - 1].keys;
        const usize at = LowerBoundKey(keys, key);
        if (at < keys.Size() && keys[at] == key)
        {
            keys.Remove(at);
        }
        if (keys.Size() == 0)
        {
            groups->Remove(g - 1);
        }
    }
}

}  // namespace runtime_detail

using namespace runtime_detail;

struct ScriptRuntime::Impl
{
    explicit Impl(const ScriptRuntimeConfig& cfg) : config(cfg), services(cfg.services), bus(cfg.event_chain_limit) {}

    ScriptRuntimeConfig config;
    UniquePtr<ScriptWorld> scripts;
    CommandKindTable kinds;
    MergeServices services;
    Vector<UniquePtr<PartitionData>> partitions;
    Vector<Instance> instances;
    Vector<LoadedEntry> loaded;  // sorted by key
    Vector<FoundScript> found;
    EventBus bus;
    Vector<EventRecord> delivery;
    ScriptRuntimeStats stats;
    u32 next_serial = 1;
    u64 tick = 0;
    f64 sim_time = 0.0;
    u32 fixed_steps = 0;
    f64 fixed_dt = 0.0;
    bool any_gone = false;
    TaskTable task_table;
    UniquePtr<TaskPool> task_pool;
    Vector<u64> removed_keys;                // the instances Compact dropped, until the tasks they owned are dropped
    const ecs::World* tick_world = nullptr;  // the world of the running tick (read-only in the jobs)
    Vector<ScriptErrorRecord> recent;        // the last errors reported, oldest first
    UniquePtr<ScriptDebugger> debugger;
    Mutex console_mutex;
    Vector<ConsoleJob> console_jobs;
    Vector<ConsoleDone> console_done;
    u64 next_console_id = 1;

    ~Impl()
    {
        if (debugger.Get() != nullptr)
        {
            scripts->SetDebugger(nullptr);
            debugger.Reset();
        }
    }
    void RunConsole(u32 partition, StringView chunk, ConsoleResult* out);
    void RunConsoleJobs();
    void CollectErrors(u32 count);

    void Reconcile(ecs::World* world, bool allow_new);
    void TakeLoaded(Instance* in);
    usize LoadedAt(u64 key) const;
    void Compact();
    void RunPhase(ecs::World* world, f64 dt, bool destroys_only, jobs::Scheduler* scheduler);
    void RunPartition(ecs::World* world, u32 p, f64 dt, bool destroys_only);
    bool Call(PartitionLease* lease, PartitionData* data, Instance* in, const char* callback, f64 dt,
              bool allow_wait = true);
    void Fail(PartitionData* data, Instance* in, const char* what, const Outcome& outcome);
    void CheckReset(u32 p, PartitionData* data);
    void RunLive(PartitionLease* lease, PartitionData* data, Instance* in, const ScriptComponent& comp, f64 dt);
    void DeliverEvents(PartitionLease* lease, PartitionData* data, PartitionContext* context, ecs::World* world, u32 p);
    void ApplyChanges(PartitionData* data);
    void Park(PartitionLease* lease, PartitionData* data, Instance* in, const CallOutput& out, bool allow_wait);
    void WakeTimers(PartitionLease* lease, PartitionData* data, PartitionContext* context, u32 p);
    void WakeTasks(PartitionLease* lease, PartitionData* data, PartitionContext* context);
    void RunTasks(jobs::Scheduler* scheduler);
    void Resume(PartitionLease* lease, PartitionData* data, PartitionContext* context, const Waiter& w,
                Span<const Value> values);
    Instance* FindInstance(PartitionData* data, u64 key);
    void RunDestroy(PartitionLease* lease, PartitionData* data, Instance* in, f64 dt, bool entity_exists);
};

usize ScriptRuntime::Impl::LoadedAt(u64 key) const
{
    usize lo = 0;
    usize hi = loaded.Size();
    while (lo < hi)
    {
        const usize mid = (lo + hi) / 2;
        if (loaded[mid].key < key)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }
    return lo;
}

void ScriptRuntime::Impl::TakeLoaded(Instance* in)
{
    if (loaded.Size() == 0)
    {
        return;
    }
    const usize at = LoadedAt(in->key);
    if (at < loaded.Size() && loaded[at].key == in->key)
    {
        in->pending = Move(loaded[at].fields);
        in->has_pending = true;
        loaded.Remove(at);
    }
}

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
                TakeLoaded(&in);
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
        else
        {
            removed_keys.Push(instances[i].key);
        }
    }
    instances = Move(kept);
}

namespace
{

u64 ErrorHash(StringView what, StringView message)
{
    Fnv1a64Builder h(kFnv1a64LegacyBasis);
    h.AddText(what).AddByte(0xFFu).AddText(message).AddByte(0xFFu);
    return h.Value() | 1u;  // never 0
}

/// "behavior 'x' on entity 12 ('Name'): on_update failed: message" (no trace, no count).
void AppendErrorHead(String<HeapAllocator>* text, const ScriptErrorRecord& r)
{
    char key[32];
    FormatTo(key, sizeof(key), "%llu", static_cast<unsigned long long>(r.entity_key));
    text->Append("behavior '");
    text->Append(r.behavior.View());
    text->Append("' on entity ");
    text->Append(StringView(key));
    if (r.entity_name.Length() != 0)
    {
        text->Append(" ('");
        text->Append(r.entity_name.View());
        text->Append("')");
    }
    text->Append(": ");
    text->Append(r.callback.View());
    text->Append(" failed: ");
    text->Append(r.message.View());
}

}  // namespace

void ScriptRuntime::Impl::Fail(PartitionData* data, Instance* in, const char* what, const Outcome& outcome)
{
    ++data->stats.errors;
    const u64 hash = ErrorHash(StringView(what), outcome.message.View());
    if (in->error_count != 0 && in->error_hash == hash)
    {
        ++in->error_count;
        if (data->repeats.Empty() || data->repeats[data->repeats.Size() - 1].entity_key != in->key ||
            data->repeats[data->repeats.Size() - 1].hash != hash)
        {
            ErrorRepeat repeat;
            repeat.entity_key = in->key;
            repeat.hash = hash;
            data->repeats.Push(repeat);
        }
        ++data->repeats[data->repeats.Size() - 1].count;
        // The log shows the repeat count at every power of ten, so a callback that fails every tick stays readable.
        u64 step = 10;
        while (step < in->error_count)
        {
            step *= 10;
        }
        if (step == in->error_count)
        {
            ScriptErrorRecord head;
            head.entity_key = in->key;
            head.behavior = String<HeapAllocator>(HeapAllocator::Default(), in->behavior.View());
            head.callback = String<HeapAllocator>(HeapAllocator::Default(), StringView(what));
            head.message = String<HeapAllocator>(HeapAllocator::Default(), outcome.message.View());
            String<HeapAllocator> text(HeapAllocator::Default());
            AppendErrorHead(&text, head);
            char count[48];
            FormatTo(count, sizeof(count), " (repeated %llu times)", static_cast<unsigned long long>(in->error_count));
            text.Append(StringView(count));
            data->log.Add(ScriptLogLevel::Error, in->behavior.View(), 0, text.View());
        }
    }
    else
    {
        in->error_hash = hash;
        in->error_count = 1;
        ++data->stats.distinct_errors;
        ErrorReport report;
        report.hash = hash;
        ScriptErrorRecord& r = report.record;
        r.tick = tick;
        r.partition = in->partition;
        r.entity_key = in->key;
        r.behavior = String<HeapAllocator>(HeapAllocator::Default(), in->behavior.View());
        r.callback = String<HeapAllocator>(HeapAllocator::Default(), StringView(what));
        r.message = String<HeapAllocator>(HeapAllocator::Default(), outcome.message.View());
        {
            // The language's traceback starts with the message again: keep the frames.
            const StringView trace = outcome.trace.View();
            const usize at = trace.Find(StringView("stack traceback:"));
            if (at < trace.Length())
            {
                r.trace = String<HeapAllocator>(HeapAllocator::Default(), trace.Substring(at));
            }
        }
        if (config.scene != nullptr && config.scene->name_of != nullptr && tick_world != nullptr)
        {
            char name[128];
            const usize length = config.scene->name_of(config.scene->user, *tick_world, in->entity, name, sizeof(name));
            if (length != 0 && length <= sizeof(name))
            {
                r.entity_name = String<HeapAllocator>(HeapAllocator::Default(), StringView(name, length));
            }
        }
        String<HeapAllocator> text(HeapAllocator::Default());
        AppendErrorHead(&text, r);
        data->log.Add(ScriptLogLevel::Error, in->behavior.View(), 0, text.View());
        if (r.trace.Length() != 0)
        {  // a line of its own: one log line is cut at kMaxScriptLogText bytes
            data->log.Add(ScriptLogLevel::Error, in->behavior.View(), 0, r.trace.View());
        }
        data->reports.Push(Move(report));
    }
    switch (config.on_error)
    {
    case OnScriptError::Retry: break;
    case OnScriptError::AbortInDebug:
        ENGINE_CHECK_SLOW(false, "a behavior callback failed (script.on_error = abort_in_debug)");
        in->failed = true;
        ++data->stats.disabled_by_error;
        break;
    case OnScriptError::Disable:
    default:
        in->failed = true;
        ++data->stats.disabled_by_error;
        break;
    }
}

void ScriptRuntime::Impl::CollectErrors(u32 count)
{
    constexpr usize kRecentLimit = 64;
    for (u32 p = 0; p < count; ++p)
    {
        PartitionData& d = *partitions[p];
        for (usize i = 0; i < d.reports.Size(); ++i)
        {
            ScriptErrorRecord& r = d.reports[i].record;
            Value payload = Value::MakeMap();
            payload.Set("behavior", Value::FromText(r.behavior.View()));
            payload.Set("entity", Value::FromEntity(ecs::Entity::FromU64(r.entity_key)));
            payload.Set("entity_name", Value::FromText(r.entity_name.View()));
            payload.Set("callback", Value::FromText(r.callback.View()));
            payload.Set("message", Value::FromText(r.message.View()));
            payload.Set("trace", Value::FromText(r.trace.View()));
            payload.Set("partition", Value::FromInt(r.partition));
            payload.Set("tick", Value::FromInt(static_cast<i64>(r.tick)));
            bus.Emit("on_script_error", Move(payload));
            if (recent.Size() == kRecentLimit)
            {
                recent.Remove(0);
            }
            recent.Push(Move(r));
        }
        for (usize i = 0; i < d.repeats.Size(); ++i)
        {
            const ErrorRepeat& repeat = d.repeats[i];
            for (usize k = recent.Size(); k > 0; --k)
            {
                ScriptErrorRecord& r = recent[k - 1];
                if (r.entity_key == repeat.entity_key && ErrorHash(r.callback.View(), r.message.View()) == repeat.hash)
                {
                    r.count += repeat.count;
                    break;
                }
            }
        }
        d.reports.Clear();
        d.repeats.Clear();
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
    data->waiters.Clear();  // the coroutines died with the state
    data->event_waiters.Clear();
    data->task_waiters.Clear();
    for (usize k = 0; k < data->members.Size(); ++k)
    {
        Instance& in = instances[data->members[k]];
        if (in.created && !in.failed)
        {
            in.failed = true;
            ++data->stats.disabled_by_error;
            ++data->stats.errors;
            if (in.error_count == 0)
            {
                in.error_count = 1;
                in.error_hash = 1;
                data->log.Add(ScriptLogLevel::Error, in.behavior.View(), 0,
                              "the partition's state was reset (out of memory); its instances are disabled");
            }
        }
    }
}

bool ScriptRuntime::Impl::Call(PartitionLease* lease, PartitionData* data, Instance* in, const char* callback, f64 dt,
                               bool allow_wait)
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
            Fail(data, in, callback, outcome);
        }
        return false;
    }
    Park(lease, data, in, out, allow_wait);
    return !in->failed;
}

Instance* ScriptRuntime::Impl::FindInstance(PartitionData* data, u64 key)
{
    usize lo = 0;
    usize hi = data->members.Size();
    while (lo < hi)
    {
        const usize mid = (lo + hi) / 2;
        if (instances[data->members[mid]].key < key)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }
    return lo < data->members.Size() && instances[data->members[lo]].key == key ? &instances[data->members[lo]]
                                                                                : nullptr;
}

// A call that suspended in a wait becomes a waiter; a call that may not wait (a destroy callback), or one over the
// partition's cap, is abandoned.
void ScriptRuntime::Impl::Park(PartitionLease* lease, PartitionData* data, Instance* in, const CallOutput& out,
                               bool allow_wait)
{
    if (!out.Suspended())
    {
        return;
    }
    State* state = lease->GetState();
    if (!allow_wait)
    {
        state->Abandon(out.coroutine);
        return;
    }
    if (data->waiters.Size() + data->event_waiters.Size() + data->task_waiters.Size() >= config.coroutine_limit)
    {
        state->Abandon(out.coroutine);
        Fail(data, in, "a wait", Outcome(Status::RuntimeError, "too many waiting coroutines in this partition"));
        return;
    }
    Waiter w;
    w.key = in->key;
    w.coroutine = out.coroutine;
    w.kind = out.wait.kind;
    w.wake_time = sim_time + out.wait.seconds;
    // A task waiter is due the tick after the one that asked: the task runs at the end of this tick. A coroutine
    // resumed from a timed wait at the start of a tick can ask and wait in the same tick, before the task has run.
    w.wake_tick = tick + (out.wait.kind == WaitRequest::Kind::Task ? 1 : out.wait.frames);
    w.event = String<HeapAllocator>(HeapAllocator::Default(), out.wait.event.View());
    w.sequence = data->next_sequence++;
    w.task = out.wait.task;
    if (w.kind == WaitRequest::Kind::Task)
    {
        data->task_waiters.Push(Move(w));
    }
    else if (w.kind == WaitRequest::Kind::Event)
    {
        data->event_waiters.Push(Move(w));
    }
    else
    {
        data->waiters.Push(Move(w));
    }
}

void ScriptRuntime::Impl::Resume(PartitionLease* lease, PartitionData* data, PartitionContext* context, const Waiter& w,
                                 Span<const Value> values)
{
    Instance* in = FindInstance(data, w.key);
    if (in == nullptr || in->failed || !in->alive)
    {
        return;
    }
    context->current_entity_key = in->key;
    // The stream of a resumed wait: the entity, the tick and the wait's own id, so it does not depend on what ran
    // before.
    context->random_state = RandomSeedFor(config.random_seed, in->key,
                                          tick + ((static_cast<u64>(w.coroutine) + 1) << 40));
    data->overlay.Clear();
    CallOutput out;
    const Outcome outcome = lease->Resume(w.coroutine, values, config.budget, &out);
    CheckReset(lease->Partition(), data);
    ++data->stats.waits_resumed;
    if (!outcome.IsOk())
    {
        if (!in->failed)
        {
            Fail(data, in, "a resumed wait", outcome);
        }
        return;
    }
    Park(lease, data, in, out, true);
}

// Resumes the waiters whose time has come, in (wake time, entity id, creation order); the waits of a disabled instance
// move later by the time that passed, so they resume with the time they had left.
void ScriptRuntime::Impl::WakeTimers(PartitionLease* lease, PartitionData* data, PartitionContext* context, u32 p)
{
    (void)p;
    if (data->waiters.Size() == 0)
    {
        return;
    }
    Vector<Waiter> due;
    Vector<Waiter> keep;
    for (usize i = 0; i < data->waiters.Size(); ++i)
    {
        Waiter& w = data->waiters[i];
        Instance* in = FindInstance(data, w.key);
        if (in == nullptr || !in->alive || in->failed)
        {
            lease->GetState()->Abandon(w.coroutine);
            continue;
        }
        if (!in->notified_enabled)
        {
            w.wake_time += context->dt;
            w.wake_tick += 1;
            keep.Push(Move(w));
            continue;
        }
        const bool ready = w.kind == WaitRequest::Kind::Seconds ? w.wake_time <= sim_time : w.wake_tick <= tick;
        if (ready)
        {
            due.Push(Move(w));
        }
        else
        {
            keep.Push(Move(w));
        }
    }
    data->waiters = Move(keep);
    Sort(Span<Waiter>(due.Data(), due.Size()),
         [](const Waiter& a, const Waiter& b)
         {
             const f64 ta = a.kind == WaitRequest::Kind::Seconds ? a.wake_time : 0.0;
             const f64 tb = b.kind == WaitRequest::Kind::Seconds ? b.wake_time : 0.0;
             if (ta != tb)
             {
                 return ta < tb;
             }
             if (a.key != b.key)
             {
                 return a.key < b.key;
             }
             return a.sequence < b.sequence;
         });
    for (usize i = 0; i < due.Size(); ++i)
    {
        Resume(lease, data, context, due[i], Span<const Value>());
    }
}

// Resumes the coroutines waiting for a task, in (entity id, creation order). A task finishes at the end of the tick it
// was asked for, so every handle a waiter holds is ready now; an unknown or foreign handle resumes with an error.
void ScriptRuntime::Impl::WakeTasks(PartitionLease* lease, PartitionData* data, PartitionContext* context)
{
    if (data->task_waiters.Size() == 0)
    {
        return;
    }
    Vector<Waiter> due;
    Vector<Waiter> keep;
    for (usize i = 0; i < data->task_waiters.Size(); ++i)
    {
        Waiter& w = data->task_waiters[i];
        Instance* in = FindInstance(data, w.key);
        if (in == nullptr || !in->alive || in->failed)
        {
            lease->GetState()->Abandon(w.coroutine);
        }
        else if (!in->notified_enabled || w.wake_tick > tick)
        {
            keep.Push(Move(w));
        }
        else
        {
            due.Push(Move(w));
        }
    }
    data->task_waiters = Move(keep);
    Sort(Span<Waiter>(due.Data(), due.Size()),
         [](const Waiter& a, const Waiter& b) { return a.key != b.key ? a.key < b.key : a.sequence < b.sequence; });
    for (usize i = 0; i < due.Size(); ++i)
    {
        Value out;
        const TakeStatus status = TakeTask(&data->task_view, due[i].key, due[i].task, &out);
        Value values[2];
        if (status == TakeStatus::Ready)
        {
            values[0] = Move(out);
        }
        else if (status == TakeStatus::Failed)
        {
            values[1] = Move(out);
        }
        else
        {
            values[1] = Value::FromText(
                status == TakeStatus::NotOwner  ? StringView("that task belongs to another script")
                : status == TakeStatus::Pending ? StringView("the task has not finished")
                                                : StringView("no such task (unknown, already taken, or dropped)"));
        }
        Resume(lease, data, context, due[i], Span<const Value>(values, 2));
    }
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
            Fail(data, in, "__behavior_create", outcome);
            return;
        }
        in->created = true;
        ++data->stats.created;
        if (!Call(lease, data, in, "on_create", dt))
        {
            return;
        }
        if (in->has_pending)
        {
            // A load gave this entity saved fields: they replace what `on_create` set, then `on_load` rebuilds the
            // rest.
            Value saved = Move(in->pending);
            in->pending = Value();
            in->has_pending = false;
            Value pairs = Value::MakeArray();
            for (usize i = 0; i < saved.Size(); ++i)
            {
                Value pair = Value::MakeArray();
                pair.Push(Value::FromText(saved.KeyAt(i)));
                pair.Push(Value(saved.At(i)));
                pairs.Push(Move(pair));
            }
            Value load_args[2] = {Value::FromInt(static_cast<i64>(in->serial)), Move(pairs)};
            CallOutput load_out;
            const Outcome loaded_outcome = lease->Call("__behavior_load", Span<const Value>(load_args, 2),
                                                       config.budget, &load_out);
            CheckReset(lease->Partition(), data);
            if (!loaded_outcome.IsOk())
            {
                Fail(data, in, "__behavior_load", loaded_outcome);
                return;
            }
            if (!Call(lease, data, in, "on_load", dt))
            {
                return;
            }
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
        ++data->stats.started;
        if (!Call(lease, data, in, "on_start", dt))
        {
            return;
        }
    }
    for (u32 s = 0; s < fixed_steps; ++s)
    {
        ++data->stats.fixed_updates;
        if (!Call(lease, data, in, "on_fixed_update", fixed_dt))
        {
            return;
        }
    }
    ++data->stats.updates;
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
            Call(lease, data, in, "on_disable", dt, false);
        }
        if (!in->failed)
        {
            Call(lease, data, in, "on_destroy", dt, false);
        }
    }
    {  // a callback of the closing instance may have subscribed: those changes must not outlive it
        Vector<SubscriptionChange> kept;
        for (usize i = 0; i < data->outbox.changes.Size(); ++i)
        {
            if (data->outbox.changes[i].key != in->key)
            {
                kept.Push(Move(data->outbox.changes[i]));
            }
        }
        data->outbox.changes = Move(kept);
    }
    UnsubscribeAll(&data->groups, in->key);
    for (Vector<Waiter>* list : {&data->waiters, &data->event_waiters, &data->task_waiters})
    {  // the instance's waiting coroutines are dropped without running another step
        Vector<Waiter> kept;
        for (usize i = 0; i < list->Size(); ++i)
        {
            if ((*list)[i].key == in->key)
            {
                lease->GetState()->Abandon((*list)[i].coroutine);
            }
            else
            {
                kept.Push(Move((*list)[i]));
            }
        }
        *list = Move(kept);
    }
    Value args[1] = {Value::FromInt(static_cast<i64>(in->serial))};
    CallOutput out;
    lease->Call("__behavior_drop", Span<const Value>(args, 1), config.budget, &out);
    CheckReset(lease->Partition(), data);
    ++data->stats.destroyed;
}

void ScriptRuntime::Impl::ApplyChanges(PartitionData* data)
{
    usize subscriptions = 0;
    for (usize g = 0; g < data->groups.Size(); ++g)
    {
        subscriptions += data->groups[g].keys.Size();
    }
    for (usize i = 0; i < data->outbox.changes.Size(); ++i)
    {
        const SubscriptionChange& change = data->outbox.changes[i];
        if (change.add)
        {
            if (subscriptions >= kMaxSubscriptionsPerPartition)
            {
                if (!data->subscription_limit_logged)
                {
                    data->subscription_limit_logged = true;
                    data->log.Add(ScriptLogLevel::Error, "script runtime", 0,
                                  "a partition reached its event subscription limit; further ones are refused");
                }
                continue;
            }
            Subscribe(&data->groups, change.name.View(), change.key);
            ++subscriptions;
        }
        else
        {
            Unsubscribe(&data->groups, change.name.View(), change.key);
        }
    }
    data->outbox.changes.Clear();
}

void ScriptRuntime::Impl::DeliverEvents(PartitionLease* lease, PartitionData* data, PartitionContext* context,
                                        ecs::World* world, u32 p)
{
    (void)world;
    for (usize e = 0; e < delivery.Size(); ++e)
    {
        const EventRecord& event = delivery[e];
        if (event.target_key != 0 && PartitionOf(event.target_key, config.partitions) != p)
        {
            continue;
        }
        const usize g = LowerBoundGroup(data->groups, event.name.View());
        const bool has_group = g < data->groups.Size() && data->groups[g].name.View() == event.name.View();
        usize first = 0;
        usize last = 0;
        if (has_group)
        {
            const Vector<u64>& keys = data->groups[g].keys;
            last = keys.Size();
            if (event.target_key != 0)
            {
                first = LowerBoundKey(keys, event.target_key);
                last = first < keys.Size() && keys[first] == event.target_key ? first + 1 : first;
            }
        }
        for (usize k = first; k < last; ++k)
        {
            // The instance of this key: the partition's members are in key order.
            usize lo = 0;
            usize hi = data->members.Size();
            while (lo < hi)
            {
                const usize mid = (lo + hi) / 2;
                if (instances[data->members[mid]].key < data->groups[g].keys[k])
                {
                    lo = mid + 1;
                }
                else
                {
                    hi = mid;
                }
            }
            if (lo == data->members.Size() || instances[data->members[lo]].key != data->groups[g].keys[k])
            {
                continue;
            }
            Instance& in = instances[data->members[lo]];
            if (!in.alive || !in.created || in.failed || !in.notified_enabled)
            {
                continue;
            }
            context->current_entity_key = in.key;
            data->overlay.Clear();
            context->random_state = RandomSeedFor(config.random_seed, in.key, tick + ((static_cast<u64>(e) + 1) << 40));
            data->outbox.chain = event.chain + 1;
            Value args[4] = {
                Value::FromInt(static_cast<i64>(in.serial)), Value::FromText(event.name.View()), event.payload,
                event.sender_key != 0 ? Value::FromEntity(ecs::Entity::FromU64(event.sender_key)) : Value()};
            CallOutput out;
            const Outcome outcome = lease->Call("__behavior_event", Span<const Value>(args, 4), config.budget, &out);
            CheckReset(p, data);
            ++data->stats.events_delivered;
            if (!outcome.IsOk() && !in.failed)
            {
                Fail(data, &in, "an event handler", outcome);
            }
            else if (outcome.IsOk())
            {
                Park(lease, data, &in, out, true);
            }
        }
        // Coroutines waiting for this event: the ones for this entity, or every one for a broadcast.
        if (data->event_waiters.Size() != 0)
        {
            Vector<Waiter> woken;
            Vector<Waiter> keep;
            for (usize i = 0; i < data->event_waiters.Size(); ++i)
            {
                Waiter& w = data->event_waiters[i];
                const bool match = w.kind == WaitRequest::Kind::Event && w.event.View() == event.name.View() &&
                                   (event.target_key == 0 || event.target_key == w.key);
                Instance* target = match ? FindInstance(data, w.key) : nullptr;
                if (match && target != nullptr && target->notified_enabled && !target->failed)
                {
                    woken.Push(Move(w));
                }
                else
                {
                    keep.Push(Move(w));
                }
            }
            data->event_waiters = Move(keep);
            for (usize i = 0; i < woken.Size(); ++i)
            {
                Value values[2] = {event.payload, event.sender_key != 0
                                                      ? Value::FromEntity(ecs::Entity::FromU64(event.sender_key))
                                                      : Value()};
                data->outbox.chain = event.chain + 1;
                Resume(lease, data, context, woken[i], Span<const Value>(values, 2));
            }
        }
    }
    data->outbox.chain = 0;
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
        ++data.stats.skipped_partitions;
        if (!data.load_logged)
        {
            data.load_logged = true;
            String<HeapAllocator> text(HeapAllocator::Default(), "partition state unavailable: ");
            text.Append(error.message.View());
            data.log.Add(ScriptLogLevel::Error, "script runtime", 0, text.View());
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
    context.frame = tick;
    context.dt = dt;
    context.sim_time = sim_time;
    context.components = config.components;
    context.scene = config.scene;
    context.partitions = config.partitions;
    for (usize i = 0; i < kMaxServiceExtensions; ++i)
    {
        context.extensions[i] = services.extensions[i];
    }
    context.log = &data.log;
    context.events = &data.outbox;
    data.task_view.outbox = &data.task_outbox;
    data.task_view.table = &task_table;
    data.task_view.taken = &data.task_taken;
    data.task_view.partition = p;
    data.task_view.held = data.task_held;
    data.task_view.queue_limit = config.task_queue_limit;
    data.task_view.arg_bytes_limit = config.task_arg_bytes;
    data.task_view.tick = tick;
    context.tasks = &data.task_view;
    data.outbox.chain = 0;
    data.outbox.weight = 0;
    ScopedPartitionContext scope(&context);
    if (!destroys_only)
    {
        WakeTimers(&lease, &data, &context, p);
        WakeTasks(&lease, &data, &context);
        DeliverEvents(&lease, &data, &context, world, p);
    }
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
    ApplyChanges(&data);
}

void ScriptRuntime::Impl::RunPhase(ecs::World* world, f64 dt, bool destroys_only, jobs::Scheduler* scheduler)
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
    for (u32 p = 0; p < count; ++p)
    {
        partitions[p]->stats = ScriptRuntimeStats();
    }
    {
        ReadOnlyPhase phase(world);
        struct Job
        {
            Impl* self;
            ecs::World* world;
            f64 dt;
            bool destroys_only;
        } job{this, world, dt, destroys_only};
        auto run = [](void* user, usize begin, usize end)
        {
            Job* j = static_cast<Job*>(user);
            for (usize p = begin; p < end; ++p)
            {
                j->self->RunPartition(j->world, static_cast<u32>(p), j->dt, j->destroys_only);
            }
        };
        if (scheduler != nullptr && scheduler->ParticipantCount() > 1 && !jobs::Scheduler::InsideJob() &&
            jobs::Scheduler::Current() == scheduler)
        {  // one job per partition: the work-stealing scheduler balances uneven partitions
            jobs::ParallelForBlocking(*scheduler, count, 1, run, &job);
        }
        else
        {
            run(&job, 0, count);
        }
    }
    usize waiting = 0;
    for (u32 p = 0; p < count; ++p)
    {
        const ScriptRuntimeStats& d = partitions[p]->stats;
        stats.created += d.created;
        stats.started += d.started;
        stats.fixed_updates += d.fixed_updates;
        stats.updates += d.updates;
        stats.destroyed += d.destroyed;
        stats.errors += d.errors;
        stats.distinct_errors += d.distinct_errors;
        stats.disabled_by_error += d.disabled_by_error;
        stats.skipped_partitions += d.skipped_partitions;
        stats.events_delivered += d.events_delivered;
        stats.waits_resumed += d.waits_resumed;
        waiting += partitions[p]->waiters.Size() + partitions[p]->event_waiters.Size() +
                   partitions[p]->task_waiters.Size();
        stats.events_emitted += bus.Accept(&partitions[p]->outbox);
    }
    stats.coroutines_waiting = waiting;
    CollectErrors(count);
    {
        Vector<LogBuffer*> logs;
        for (u32 p = 0; p < count; ++p)
        {
            logs.Push(&partitions[p]->log);
        }
        FlushLogBuffers(Span<LogBuffer* const>(logs.Data(), logs.Size()));
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

// The sync point of the tasks: results taken in this tick are dropped, so are the tasks of instances that went away;
// the requests of the tick run on the pool and their results wait for the owners to take them.
void ScriptRuntime::Impl::RunTasks(jobs::Scheduler* scheduler)
{
    for (u32 p = 0; p < config.partitions; ++p)
    {
        PartitionData& d = *partitions[p];
        for (usize i = 0; i < d.task_taken.Size(); ++i)
        {
            task_table.Erase(d.task_taken[i]);
            if (d.task_held > 0)
            {
                --d.task_held;
            }
        }
        d.task_taken.Clear();
    }
    if (removed_keys.Size() != 0)
    {
        Vector<u32> removed;
        // Two Compact calls in one tick leave two sorted runs; the search needs one.
        Sort(Span<u64>(removed_keys.Data(), removed_keys.Size()), [](u64 a, u64 b) { return a < b; });
        task_table.EraseOwners(Span<const u64>(removed_keys.Data(), removed_keys.Size()), &removed);
        for (u32 p = 0; p < removed.Size() && p < config.partitions; ++p)
        {
            PartitionData& d = *partitions[p];
            d.task_held = d.task_held > removed[p] ? d.task_held - removed[p] : 0;
        }
        removed_keys.Clear();
    }
    Vector<TaskRequest> batch;
    for (u32 p = 0; p < config.partitions; ++p)
    {
        TaskOutbox& box = partitions[p]->task_outbox;
        for (usize i = 0; i < box.requests.Size(); ++i)
        {
            usize lo = 0;
            usize hi = instances.Size();
            const u64 key = box.requests[i].owner_key;
            while (lo < hi)
            {
                const usize mid = lo + (hi - lo) / 2;
                if (instances[mid].key < key)
                {
                    lo = mid + 1;
                }
                else
                {
                    hi = mid;
                }
            }
            if (lo < instances.Size() && instances[lo].key == key && instances[lo].alive)
            {  // a task never runs for an owner that is gone
                batch.Push(Move(box.requests[i]));
            }
        }
        box.requests.Clear();
        box.bytes = 0;
    }
    if (batch.Size() != 0)
    {
        if (task_pool.Get() == nullptr)
        {
            TaskPoolConfig pc;
            pc.backend = config.backend;
            pc.bindings = config.bindings;
            u32 states = config.task_states;
            if (states == 0)
            {
                states = scheduler != nullptr ? static_cast<u32>(scheduler->ParticipantCount()) : 1u;
            }
            pc.states = states < 1 ? 1 : (states > kMaxPartitions ? kMaxPartitions : states);
            pc.state_memory = config.task_state_memory;
            pc.budget = config.task_budget;
            task_pool = MakeUnique<TaskPool>(pc);
        }
        Vector<TaskOutcome> outcomes;
        stats.task_chunks += task_pool->Execute(Span<const TaskRequest>(batch.Data(), batch.Size()), &outcomes,
                                                scheduler);
        for (usize i = 0; i < batch.Size(); ++i)
        {
            const u32 owner_partition = static_cast<u32>((batch[i].id >> kTaskIdShift) - 1);
            TaskRecord* record = task_table.Insert(batch[i].id);
            record->owner_key = batch[i].owner_key;
            record->partition = owner_partition;
            record->failed = outcomes[i].failed;
            record->result = Move(outcomes[i].value);
            record->error = Move(outcomes[i].error);
            ++partitions[owner_partition]->task_held;
            ++stats.tasks_run;
            stats.tasks_failed += record->failed ? 1 : 0;
        }
    }
    stats.tasks_held = task_table.Size();
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
    world_config.profiling = config.profile;
    m.scripts = MakeUnique<ScriptWorld>(world_config);
    for (u32 p = 0; p < config.partitions; ++p)
    {
        m.partitions.Push(MakeUnique<PartitionData>());
        m.partitions[p]->log.SetLimit(config.log_limit);
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

EventBus& ScriptRuntime::Events()
{
    return impl_->bus;
}

MergeServices& ScriptRuntime::Services()
{
    return impl_->services;
}

CommandKindTable& ScriptRuntime::Kinds()
{
    return impl_->kinds;
}

bool ScriptRuntime::HasHandler(u64 entity_key, StringView name) const
{
    const Impl& m = *impl_;
    if (entity_key == 0 || m.partitions.Size() == 0)
    {
        return false;
    }
    const u32 p = PartitionOf(entity_key, m.config.partitions);
    if (p >= m.partitions.Size() || m.partitions[p].Get() == nullptr)
    {
        return false;
    }
    const PartitionData& data = *m.partitions[p];
    const usize g = LowerBoundGroup(data.groups, name);
    if (g < data.groups.Size() && data.groups[g].name.View() == name)
    {
        const Vector<u64>& keys = data.groups[g].keys;
        const usize at = LowerBoundKey(keys, entity_key);
        if (at < keys.Size() && keys[at] == entity_key)
        {
            return true;
        }
    }
    // A coroutine parked in `wait_event(name)` is a listener too.
    for (usize i = 0; i < data.event_waiters.Size(); ++i)
    {
        const Waiter& w = data.event_waiters[i];
        if (w.kind == WaitRequest::Kind::Event && w.key == entity_key && w.event.View() == name)
        {
            return true;
        }
    }
    return false;
}

const Vector<ScriptErrorRecord>& ScriptRuntime::RecentErrors() const
{
    return impl_->recent;
}

ScriptDebugger* ScriptRuntime::AttachDebugger(DebugTransport* transport)
{
    Impl& m = *impl_;
    ENGINE_ASSERT(transport != nullptr, "AttachDebugger: no transport");
    if (!m.scripts->SetDebugger(nullptr))
    {
        return nullptr;
    }
    m.debugger.Reset();
    UniquePtr<ScriptDebugger> fresh = MakeUnique<ScriptDebugger>(transport, m.config.partitions,
                                                                 m.config.debug_pause_all);
    if (!m.scripts->SetDebugger(fresh.Get()))
    {
        return nullptr;
    }
    m.debugger = Move(fresh);
    return m.debugger.Get();
}

void ScriptRuntime::DetachDebugger()
{
    Impl& m = *impl_;
    if (m.debugger.Get() != nullptr)
    {
        m.debugger->Detach();  // stopped partitions run on before the states let go of their sessions
        if (m.scripts->SetDebugger(nullptr))
        {
            m.debugger.Reset();
        }
    }
}

void ScriptRuntime::Impl::RunConsole(u32 partition, StringView chunk, ConsoleResult* out)
{
    *out = ConsoleResult();
    CallOutput results;
    Outcome outcome;
    if (partition == kConsoleScratch)
    {
        UniquePtr<State> scratch = config.backend->CreateState(config.partition_memory);
        outcome = scratch.Get() != nullptr ? scratch->Eval(StringView("console"), chunk, config.budget, &results)
                                           : Outcome(Status::OutOfMemory, "the scratch state could not be made");
    }
    else
    {
        PartitionLease lease;
        Outcome error;
        const CheckoutStatus got = scripts->TryCheckout(partition, &lease, &error);
        if (got == CheckoutStatus::Ok)
        {
            outcome = lease.Eval(StringView("console"), chunk, config.budget, &results);
        }
        else if (got == CheckoutStatus::Busy)
        {
            outcome = Outcome(Status::Invalid, "the partition is busy: run the console between ticks");
        }
        else if (got == CheckoutStatus::Invalid)
        {
            outcome = Outcome(Status::Invalid, "the partition number is out of range");
        }
        else
        {
            outcome = Outcome(error.status, error.message.View());
        }
    }
    out->ok = outcome.IsOk();
    out->status = outcome.status;
    out->message = String<HeapAllocator>(HeapAllocator::Default(), outcome.message.View());
    out->trace = String<HeapAllocator>(HeapAllocator::Default(), outcome.trace.View());
    if (out->ok)
    {
        out->values = Move(results.results);
    }
}

void ScriptRuntime::Impl::RunConsoleJobs()
{
    Vector<ConsoleJob> jobs_now;
    {
        LockGuard<Mutex> lock(console_mutex);
        jobs_now = Move(console_jobs);
        console_jobs = Vector<ConsoleJob>();
    }
    for (usize i = 0; i < jobs_now.Size(); ++i)
    {
        ConsoleDone done;
        done.id = jobs_now[i].id;
        RunConsole(jobs_now[i].partition, jobs_now[i].chunk.View(), &done.result);
        LockGuard<Mutex> lock(console_mutex);
        console_done.Push(Move(done));
    }
}

void ScriptRuntime::ConsoleRun(u32 partition, StringView chunk, ConsoleResult* result)
{
    ENGINE_ASSERT(result != nullptr, "ConsoleRun: no result");
    impl_->RunConsole(partition, chunk, result);
}

u64 ScriptRuntime::ConsoleSubmit(u32 partition, StringView chunk)
{
    Impl& m = *impl_;
    ConsoleJob job;
    job.partition = partition;
    job.chunk = String<HeapAllocator>(HeapAllocator::Default(), chunk);
    LockGuard<Mutex> lock(m.console_mutex);
    job.id = m.next_console_id++;
    const u64 id = job.id;
    m.console_jobs.Push(Move(job));
    return id;
}

bool ScriptRuntime::ConsoleTake(u64 id, ConsoleResult* result)
{
    ENGINE_ASSERT(result != nullptr, "ConsoleTake: no result");
    Impl& m = *impl_;
    LockGuard<Mutex> lock(m.console_mutex);
    for (usize i = 0; i < m.console_done.Size(); ++i)
    {
        if (m.console_done[i].id == id)
        {
            *result = Move(m.console_done[i].result);
            m.console_done.Remove(i);
            return true;
        }
    }
    return false;
}

void ScriptRuntime::SetProfiling(bool on)
{
    impl_->scripts->SetProfiling(on);
}

Outcome ScriptRuntime::JitTotals(JitStats* totals)
{
    return impl_->scripts->JitTotals(totals);
}

Outcome ScriptRuntime::ProfileReport(Vector<ProfileEntry>* out, usize top)
{
    return impl_->scripts->ProfileReport(out, top);
}

namespace
{

// What CapturePersistent learned about one instance before it writes the components.
struct Captured
{
    bool have = false;  // fields is a map to write (possibly empty)
    bool failed = false;
    bool pending = false;
    Value fields;
};

u64 HashStep(u64 hash, u64 word)
{
    return Fnv1a64Builder(hash).AddU64Bytes(word).Value();
}

// Builds the map of a `{ name, value }` list; false when the list is not that.
bool MapFromPairs(const Value& list, Value* map)
{
    *map = Value::MakeMap();
    if (list.Kind() != ValueKind::Array && !(list.Kind() == ValueKind::Map && list.Size() == 0))
    {
        return list.IsNil();
    }
    if (list.Kind() == ValueKind::Map)
    {
        return true;
    }
    for (usize i = 0; i < list.Size(); ++i)
    {
        const Value& pair = list.At(i);
        if (pair.Kind() != ValueKind::Array || pair.Size() != 2 || pair.At(0).Kind() != ValueKind::Text ||
            !map->Set(pair.At(0).Text(), Value(pair.At(1))))
        {
            return false;
        }
    }
    return true;
}

}  // namespace

void ScriptRuntime::CapturePersistent(ecs::World* world, const storage::KeyIndex& index, PersistReport* report)
{
    Impl& m = *impl_;
    PersistReport local;
    PersistReport& rep = report != nullptr ? *report : local;
    rep = PersistReport();
    Vector<Captured> captured;
    captured.Resize(m.instances.Size());
    for (u32 p = 0; p < m.partitions.Size(); ++p)
    {
        if (m.partitions[p].Get() == nullptr)
        {
            continue;
        }
        PartitionData& data = *m.partitions[p];
        rep.coroutines_not_saved += data.waiters.Size() + data.event_waiters.Size() + data.task_waiters.Size();
        if (data.members.Size() == 0)
        {
            continue;
        }
        PartitionLease lease;
        Outcome error;
        const CheckoutStatus got = m.scripts->TryCheckout(p, &lease, &error);
        for (usize k = 0; k < data.members.Size(); ++k)
        {
            const usize at = data.members[k];
            const Instance& in = m.instances[at];
            if (!in.alive)
            {
                continue;
            }
            Captured& c = captured[at];
            if (!in.created)
            {
                if (in.has_pending)
                {
                    c.have = true;
                    c.pending = true;
                    c.fields = in.pending;
                }
                continue;
            }
            if (in.failed || got != CheckoutStatus::Ok)
            {
                c.failed = true;
                continue;
            }
            Value args[1] = {Value::FromInt(static_cast<i64>(in.serial))};
            CallOutput out;
            Outcome outcome = lease.Call("__behavior_save", Span<const Value>(args, 1), m.config.budget, &out);
            if (outcome.IsOk() && out.results.Size() == 1 && MapFromPairs(out.results[0], &c.fields))
            {
                c.have = true;
                continue;
            }
            // Something inside a field cannot cross: ask for the fields one at a time and leave out the ones that fail.
            CallOutput names;
            outcome = lease.Call("__behavior_save_names", Span<const Value>(args, 1), m.config.budget, &names);
            if (!outcome.IsOk() || names.results.Size() != 1)
            {
                c.failed = true;
                continue;
            }
            c.fields = Value::MakeMap();
            c.have = true;
            const Value& list = names.results[0];
            for (usize i = 0; list.Kind() == ValueKind::Array && i < list.Size(); ++i)
            {
                if (list.At(i).Kind() != ValueKind::Text)
                {
                    continue;
                }
                Value field_args[2] = {Value::FromInt(static_cast<i64>(in.serial)), list.At(i)};
                CallOutput field;
                outcome = lease.Call("__behavior_save_field", Span<const Value>(field_args, 2), m.config.budget,
                                     &field);
                if (outcome.IsOk() && field.results.Size() == 1)
                {
                    if (!field.results[0].IsNil())
                    {
                        c.fields.Set(list.At(i).Text(), Value(field.results[0]));
                    }
                }
                else
                {
                    ++rep.fields_skipped;
                }
            }
        }
    }
    u64 hash = kFnv1a64Basis;
    const ecs::World& view = *world;
    auto write = [&](ecs::Entity entity, const Value& fields) -> bool
    {
        if (!view.Exists(entity))
        {
            return false;
        }
        if (fields.Size() == 0)
        {
            if (view.Get<ScriptPersist>(entity) != nullptr)
            {
                world->Remove<ScriptPersist>(entity);
            }
            return true;
        }
        bytes::ByteWriter writer;
        EncodeValueRecord(fields, &writer, &index);
        ScriptPersist record;
        for (usize i = 0; i < writer.Size(); ++i)
        {
            record.blob.Push(static_cast<u8>(writer.Bytes()[i]));
        }
        world->Set(entity, Move(record));
        hash = HashStep(HashStep(hash, entity.ToU64()), fields.Hash());
        rep.fields_saved += fields.Size();
        return true;
    };
    for (usize i = 0; i < m.instances.Size(); ++i)
    {
        const Captured& c = captured[i];
        if (c.failed)
        {
            ++rep.instances_failed;
        }
        else if (c.have)
        {
            if (write(m.instances[i].entity, c.fields))
            {  // an instance whose entity the host destroyed since the last tick is not counted
                if (c.pending)
                {
                    ++rep.instances_pending;
                }
                else
                {
                    ++rep.instances_saved;
                }
            }
        }
    }
    for (usize i = 0; i < m.loaded.Size(); ++i)
    {  // a load that no instance has taken yet is written back, so a second save does not lose it
        if (write(ecs::Entity::FromU64(m.loaded[i].key), m.loaded[i].fields))
        {
            ++rep.instances_pending;
        }
    }
    rep.state_hash = hash;
    rep.tick = m.tick;
    rep.sim_time = m.sim_time;
}

void ScriptRuntime::ResolveLoaded(ecs::World* world, const storage::KeyIndex& index, LoadReport* report)
{
    Impl& m = *impl_;
    LoadReport local;
    LoadReport& rep = report != nullptr ? *report : local;
    rep = LoadReport();
    struct Found
    {
        ecs::Entity entity;
        Vector<u8> blob;
    };
    Vector<Found> found;
    world->Query<ecs::Read<ScriptPersist>>().ForEach(
        [&](ecs::Entity e, const ScriptPersist& persist)
        {
            Found f;
            f.entity = e;
            for (usize b = 0; b < persist.blob.Size(); ++b)
            {
                f.blob.Push(persist.blob[b]);
            }
            found.Push(Move(f));
        });
    Sort(Span<Found>(found.Data(), found.Size()),
         [](const Found& a, const Found& b) { return a.entity.ToU64() < b.entity.ToU64(); });
    for (usize i = 0; i < found.Size(); ++i)
    {
        world->Remove<ScriptPersist>(found[i].entity);
        bytes::ByteReader reader(ByteSpan(reinterpret_cast<const byte*>(found[i].blob.Data()), found[i].blob.Size()));
        Value fields;
        if (DecodeValueRecord(&reader, &fields, &index, ValueLimits()) != ValueRecordStatus::Ok || !reader.AtEnd() ||
            fields.Kind() != ValueKind::Map)
        {
            ++rep.corrupt;
            continue;
        }
        const u64 key = found[i].entity.ToU64();
        const usize at = m.LoadedAt(key);
        if (at < m.loaded.Size() && m.loaded[at].key == key)
        {
            m.loaded[at].fields = Move(fields);
        }
        else
        {
            LoadedEntry entry;
            entry.key = key;
            entry.fields = Move(fields);
            m.loaded.Insert(at, Move(entry));
        }
        ++rep.queued;
    }
}

void ScriptRuntime::Clock(u64* tick, f64* sim_time) const
{
    *tick = impl_->tick;
    *sim_time = impl_->sim_time;
}

void ScriptRuntime::RestoreClock(u64 tick, f64 sim_time)
{
    impl_->tick = tick;
    impl_->sim_time = sim_time;
}

void ScriptRuntime::Tick(ecs::World* world, f64 dt, jobs::Scheduler* scheduler)
{
    Impl& m = *impl_;
    m.tick_world = world;
    m.RunConsoleJobs();
    m.bus.Take(&m.delivery);  // the events of the last tick, to native subscribers now and to scripts in the jobs
    m.Reconcile(world, true);
    m.RunPhase(world, dt, false, scheduler);
    m.Compact();
    // Entities the merge destroyed, or whose script component it removed, are closed in a second, short phase.
    m.Reconcile(world, false);
    if (m.any_gone)
    {
        m.RunPhase(world, dt, true, scheduler);
        m.Compact();
    }
    m.RunTasks(scheduler);
    ++m.tick;
    m.sim_time += dt;
    ++m.stats.ticks;
    m.stats.instances = m.instances.Size();
    m.tick_world = nullptr;
}

ecs::SystemDesc MakeScriptSystem()
{
    return ecs::MakeSystem("script",
                           [](ecs::SystemContext& ctx)
                           {
                               ctx.Resource<ScriptRuntime>().Tick(&ctx.ExclusiveWorld(), static_cast<f64>(ctx.Dt()),
                                                                  ctx.JobScheduler());
                           })
        .ReadsAllComponents()
        .WritesAllComponents()
        .WritesResource<ScriptRuntime>();
}

}  // namespace engine_placeholder::script
