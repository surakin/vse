#include "script/script_runtime.h"

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
    u32 next_sequence = 0;
    bool subscription_limit_logged = false;
    Vector<Group> groups;      // event name -> subscribed instance keys, sorted by name and key
    LogBuffer log;             // this phase's log lines, flushed in partition order after the jobs finish
    ScriptRuntimeStats stats;  // this phase's counts, summed after the jobs finish
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

}  // namespace

struct ScriptRuntime::Impl
{
    explicit Impl(const ScriptRuntimeConfig& cfg) : config(cfg), services(cfg.services), bus(cfg.event_chain_limit) {}

    ScriptRuntimeConfig config;
    UniquePtr<ScriptWorld> scripts;
    CommandKindTable kinds;
    MergeServices services;
    Vector<UniquePtr<PartitionData>> partitions;
    Vector<Instance> instances;
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

    void Reconcile(ecs::World* world, bool allow_new);
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
    void Resume(PartitionLease* lease, PartitionData* data, PartitionContext* context, const Waiter& w,
                Span<const Value> values);
    Instance* FindInstance(PartitionData* data, u64 key);
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

void ScriptRuntime::Impl::Fail(PartitionData* data, Instance* in, const char* what, const Outcome& outcome)
{
    ++data->stats.errors;
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
        data->log.Add(ScriptLogLevel::Error, in->behavior.View(), 0, text.View());
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
    for (usize k = 0; k < data->members.Size(); ++k)
    {
        Instance& in = instances[data->members[k]];
        if (in.created && !in.failed)
        {
            in.failed = true;
            ++data->stats.disabled_by_error;
            ++data->stats.errors;
            if (!in.logged)
            {
                in.logged = true;
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
    if (data->waiters.Size() + data->event_waiters.Size() >= config.coroutine_limit)
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
    w.wake_tick = tick + out.wait.frames;
    w.event = String<HeapAllocator>(HeapAllocator::Default(), out.wait.event.View());
    w.sequence = data->next_sequence++;
    if (w.kind == WaitRequest::Kind::Event)
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
    if (in == nullptr)
    {
        return;
    }
    context->current_entity_key = in->key;
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
    for (Vector<Waiter>* list : {&data->waiters, &data->event_waiters})
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
    context.dt = dt;
    context.sim_time = sim_time;
    context.components = config.components;
    context.scene = config.scene;
    context.partitions = config.partitions;
    context.log = &data.log;
    context.events = &data.outbox;
    data.outbox.chain = 0;
    data.outbox.weight = 0;
    ScopedPartitionContext scope(&context);
    if (!destroys_only)
    {
        WakeTimers(&lease, &data, &context, p);
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
        stats.disabled_by_error += d.disabled_by_error;
        stats.skipped_partitions += d.skipped_partitions;
        stats.events_delivered += d.events_delivered;
        stats.waits_resumed += d.waits_resumed;
        waiting += partitions[p]->waiters.Size() + partitions[p]->event_waiters.Size();
        stats.events_emitted += bus.Accept(&partitions[p]->outbox);
    }
    stats.coroutines_waiting = waiting;
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

void ScriptRuntime::Tick(ecs::World* world, f64 dt, jobs::Scheduler* scheduler)
{
    Impl& m = *impl_;
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
    ++m.tick;
    m.sim_time += dt;
    ++m.stats.ticks;
    m.stats.instances = m.instances.Size();
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
