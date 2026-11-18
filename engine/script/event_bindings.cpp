#include "script/event_bindings.h"

#include "script/event_bus.h"
#include "script/partition_context.h"

namespace engine_placeholder::script
{

namespace
{

EventOutbox* Outbox()
{
    PartitionContext* context = CurrentPartitionContext();
    if (context == nullptr || context->events == nullptr)
    {
        RaiseScriptError("events are not available to this script");
        return nullptr;
    }
    return context->events;
}

bool CheckName(StringView name)
{
    if (name.Length() == 0 || name.Length() > kMaxEventNameBytes)
    {
        RaiseScriptError("an event name is 1 to 64 bytes");
        return false;
    }
    return true;
}

void Emit(StringView name, Value payload, u64 target_key)
{
    EventOutbox* outbox = Outbox();
    if (outbox == nullptr || !CheckName(name))
    {
        return;
    }
    if (outbox->events.Size() >= kMaxEventsPerPartition)
    {
        RaiseScriptError("this partition emitted too many events this tick");
        return;
    }
    const u64 weight = EventPayloadWeight(payload);
    if (outbox->weight + weight > kMaxEventPayloadWeight)
    {
        RaiseScriptError("this partition emitted too much event data this tick");
        return;
    }
    outbox->weight += weight;
    EventRecord record;
    record.name = String<HeapAllocator>(HeapAllocator::Default(), name);
    record.payload = Move(payload);
    record.sender_key = CurrentPartitionContext()->current_entity_key;
    record.target_key = target_key;
    record.chain = outbox->chain;
    outbox->events.Push(Move(record));
}

void EventEmit(StringView name, Value payload)
{
    Emit(name, Move(payload), 0);
}

void EventEmitTo(ecs::Entity target, StringView name, Value payload)
{
    Emit(name, Move(payload), target.ToU64());
}

void Change(StringView name, bool add)
{
    EventOutbox* outbox = Outbox();
    if (outbox == nullptr || !CheckName(name))
    {
        return;
    }
    if (outbox->changes.Size() >= kMaxSubscriptionChangesPerPartition)
    {
        RaiseScriptError("this partition changed too many event subscriptions this tick");
        return;
    }
    SubscriptionChange change;
    change.key = CurrentPartitionContext()->current_entity_key;
    change.name = String<HeapAllocator>(HeapAllocator::Default(), name);
    change.add = add;
    outbox->changes.Push(Move(change));
}

void EventSubscribe(StringView name)
{
    Change(name, true);
}

void EventUnsubscribe(StringView name)
{
    Change(name, false);
}

}  // namespace

bool RegisterEventBindings(BindingRegistry* registry)
{
    bool ok = true;
    ok &= registry->Add<&EventEmit>(
        "event", "emit", ThreadRule::WorldWrite, "name, payload",
        "Emits an event to every subscriber of `name`; handlers run at the start of the next "
        "tick, never inside this call. The payload is copied.");
    ok &= registry->Add<&EventEmitTo>(
        "event", "emit_to", ThreadRule::WorldWrite, "entity, name, payload",
        "Emits an event only the given entity's script receives, at the start of the next "
        "tick.");
    ok &= registry->Add<&EventSubscribe>("event", "subscribe", ThreadRule::WorldWrite, "name",
                                         "Tells the engine the running instance wants events called `name` (the host "
                                         "behind `event.on`; scripts call `event.on`).");
    ok &= registry->Add<&EventUnsubscribe>("event", "unsubscribe", ThreadRule::WorldWrite, "name",
                                           "Withdraws the running instance's interest in `name` (the host behind "
                                           "`event.off`).");
    return ok;
}

}  // namespace engine_placeholder::script
