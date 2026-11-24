#include "script/event_bus.h"

#include "lang/utility.h"

namespace vse::script
{

u64 EventPayloadWeight(const Value& payload)
{
    u64 weight = 1;
    switch (payload.Kind())
    {
    case ValueKind::Text: weight += payload.Text().Length() / 16; break;
    case ValueKind::Bytes: weight += payload.Bytes().Size() / 16; break;
    case ValueKind::Array:
        for (usize i = 0; i < payload.Size(); ++i)
        {
            weight += EventPayloadWeight(payload.At(i));  // the depth of a value is bounded by kMaxValueDepth
        }
        break;
    case ValueKind::Map:
        for (usize i = 0; i < payload.Size(); ++i)
        {
            weight += 1 + payload.KeyAt(i).Length() / 16 + EventPayloadWeight(payload.At(i));
        }
        break;
    default: break;
    }
    return weight;
}

bool EventBus::Emit(StringView name, Value payload, u64 target_key)
{
    if (name.Length() == 0 || name.Length() > kMaxEventNameBytes)
    {
        ++dropped_;
        return false;
    }
    EventRecord record;
    record.name = String<HeapAllocator>(HeapAllocator::Default(), name);
    record.payload = Move(payload);
    record.target_key = target_key;
    pending_.Push(Move(record));
    return true;
}

u32 EventBus::Subscribe(StringView name, NativeEventHandler handler, void* user)
{
    NativeSub sub;
    sub.id = next_id_++;
    sub.name = String<HeapAllocator>(HeapAllocator::Default(), name);
    sub.handler = handler;
    sub.user = user;
    const u32 id = sub.id;
    natives_.Push(Move(sub));
    return id;
}

bool EventBus::Unsubscribe(u32 id)
{
    for (usize i = 0; i < natives_.Size(); ++i)
    {
        if (natives_[i].id == id && natives_[i].handler != nullptr)
        {
            if (delivering_)
            {
                natives_[i].handler = nullptr;  // Take skips it and drops it when the delivery ends
                return true;
            }
            RemoveCleared(i);
            return true;
        }
    }
    return false;
}

void EventBus::RemoveCleared(usize also)
{
    Vector<NativeSub> kept;
    for (usize k = 0; k < natives_.Size(); ++k)
    {
        if (k != also && natives_[k].handler != nullptr)
        {
            kept.Push(Move(natives_[k]));
        }
    }
    natives_ = Move(kept);
}

usize EventBus::Accept(EventOutbox* outbox)
{
    usize accepted = 0;
    for (usize i = 0; i < outbox->events.Size(); ++i)
    {
        if (outbox->events[i].chain > chain_limit_)
        {
            ++dropped_;
            continue;
        }
        pending_.Push(Move(outbox->events[i]));
        ++accepted;
    }
    outbox->events.Clear();
    return accepted;
}

void EventBus::Take(Vector<EventRecord>* out)
{
    *out = Move(pending_);
    pending_ = Vector<EventRecord>();
    if (natives_.Size() == 0)
    {
        return;
    }
    delivering_ = true;
    for (usize i = 0; i < out->Size(); ++i)
    {
        const EventRecord& event = (*out)[i];
        // The count is read once per event: a subscription added by a handler starts with the next event.
        const usize count = natives_.Size();
        for (usize k = 0; k < count; ++k)
        {
            // Read by index on every step: a handler may add subscriptions (the vector may move).
            const NativeSub& sub = natives_[k];
            if (sub.handler != nullptr && (sub.name.Length() == 0 || sub.name.View() == event.name.View()))
            {
                const NativeEventHandler handler = sub.handler;
                void* const user = sub.user;
                handler(user, event);
            }
        }
    }
    delivering_ = false;
    RemoveCleared(natives_.Size());
}

}  // namespace vse::script
