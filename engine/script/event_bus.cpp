#include "script/event_bus.h"

#include "lang/utility.h"

namespace engine_placeholder::script
{

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
        if (natives_[i].id == id)
        {
            Vector<NativeSub> kept;
            for (usize k = 0; k < natives_.Size(); ++k)
            {
                if (k != i)
                {
                    kept.Push(Move(natives_[k]));
                }
            }
            natives_ = Move(kept);
            return true;
        }
    }
    return false;
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
    for (usize i = 0; i < out->Size(); ++i)
    {
        const EventRecord& event = (*out)[i];
        for (usize k = 0; k < natives_.Size(); ++k)
        {
            if (natives_[k].name.Length() == 0 || natives_[k].name.View() == event.name.View())
            {
                natives_[k].handler(natives_[k].user, event);
            }
        }
    }
}

}  // namespace engine_placeholder::script
