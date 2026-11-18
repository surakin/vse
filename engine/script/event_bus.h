// The event bus (Phase 27 Day 459): named events with `Value` payloads that scripts and native systems exchange. An
// emit never runs a handler inside the emitter's call: events wait for the sync point and are delivered at the start of
// the next tick, in an order that does not depend on threads (docs/guide/scripting.md, "Events").
#pragma once

#include "containers/vector.h"
#include "mem/heap_allocator.h"
#include "script/value.h"
#include "string/string.h"
#include "string_view.h"
#include "types.h"

namespace engine_placeholder::script
{

/// The longest event name.
inline constexpr usize kMaxEventNameBytes = 64;

/// The most events one partition may emit in one tick (a script that exceeds it gets an error).
inline constexpr usize kMaxEventsPerPartition = usize{1} << 16;

/// One event waiting for delivery or being delivered.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns its name and payload.
struct EventRecord
{
    /// The event's name.
    String<HeapAllocator> name{HeapAllocator::Default()};
    /// The payload (nil when none).
    Value payload;
    /// The stable id (`Entity::ToU64`) of the emitting entity; 0 for a native emitter.
    u64 sender_key = 0;
    /// The stable id of the only entity that receives the event; 0 delivers to every subscriber of the name.
    u64 target_key = 0;
    /// How many handler hops led to this event: 0 for an event emitted by a lifecycle callback or native code, one more
    /// than the event whose handler emitted it otherwise.
    u32 chain = 0;
};

/// A change to one instance's subscriptions, made by a script during a callback and applied at the end of the
/// partition's work.
///
/// - Thread-safety: thread-compatible plain data.
struct SubscriptionChange
{
    /// The subscribing entity's stable id.
    u64 key = 0;
    /// The event name.
    String<HeapAllocator> name{HeapAllocator::Default()};
    /// True to subscribe, false to unsubscribe.
    bool add = true;
};

/// What one partition's scripts emitted and changed this tick; the partition's job is the only writer, and the sync
/// point moves the events into the bus in partition order.
///
/// - Thread-safety: thread-compatible; one job owns it.
/// - Ownership: owns its entries.
struct EventOutbox
{
    /// The events in emission order.
    Vector<EventRecord> events;
    /// The subscription changes in the order made.
    Vector<SubscriptionChange> changes;
    /// The chain depth stamped on events emitted now (the runtime sets it around each callback).
    u32 chain = 0;
};

/// A native subscriber: called on the thread that runs the script tick, before the tick's script callbacks, once per
/// matching event in delivery order.
using NativeEventHandler = void (*)(void* user, const EventRecord& event);

/// The pending events of a script runtime, and the native emitters and subscribers.
///
/// Delivery order: events are delivered in the order they entered the bus. A native `Emit` enters at the call; the
/// events of the script partitions enter at the sync point in `(partition, emitting entity id, sequence)` order. An
/// event emitted by a handler enters the bus at the sync point after it, so it is delivered one tick later; an event
/// whose `chain` exceeds the chain limit is dropped and counted.
///
/// - Thread-safety: thread-compatible: call it from the thread that runs the script tick.
/// - Ownership: owns the pending events and the native subscriptions.
class EventBus
{
public:
    /// Makes a bus.
    ///
    /// - Param chain_limit: the most handler hops of a chain (setting `script.event_chain_limit`).
    explicit EventBus(u32 chain_limit = 8) : chain_limit_(chain_limit) {}

    /// Sets the chain limit.
    ///
    /// - Fails: never.
    void SetChainLimit(u32 chain_limit) { chain_limit_ = chain_limit; }
    /// The chain limit.
    ///
    /// - Fails: never.
    u32 ChainLimit() const { return chain_limit_; }

    /// Emits an event from native code; it is delivered at the start of the next script tick.
    ///
    /// - Param name: 1 to [[script::kMaxEventNameBytes]] bytes; copied.
    /// - Param payload: copied.
    /// - Param target_key: the only receiving entity's stable id, or 0 for every subscriber.
    /// - Returns: false (and nothing is queued) for a bad name.
    /// - Fails: returns false, as described.
    bool Emit(StringView name, Value payload = Value(), u64 target_key = 0);

    /// Subscribes native code to an event name.
    ///
    /// - Param name: the event name; an empty name receives every event.
    /// - Param handler: not null.
    /// - Param user: passed to `handler`; borrowed.
    /// - Returns: an id for [[script::EventBus::Unsubscribe]], never 0.
    /// - Ownership: `user` must outlive the subscription.
    u32 Subscribe(StringView name, NativeEventHandler handler, void* user);

    /// Removes a native subscription.
    ///
    /// - Returns: whether the id was subscribed.
    /// - Fails: returns false, as described.
    bool Unsubscribe(u32 id);

    /// How many events wait for the next tick.
    ///
    /// - Fails: never.
    usize Pending() const { return pending_.Size(); }

    /// How many events were dropped for the chain limit or a bad name since the bus was made.
    ///
    /// - Fails: never.
    u64 Dropped() const { return dropped_; }

    /// Moves a partition's emitted events into the bus (the sync point calls it for each partition in order); events
    /// over the chain limit are dropped.
    ///
    /// - Param outbox: its events are moved out and its list emptied.
    /// - Returns: how many events were accepted.
    /// - Ownership: borrows `outbox` for the call.
    usize Accept(EventOutbox* outbox);

    /// Takes the waiting events for delivery and calls the native subscribers for them, in order.
    ///
    /// - Param out: receives the events (its old contents are replaced).
    /// - Ownership: borrows `out` for the call.
    void Take(Vector<EventRecord>* out);

private:
    struct NativeSub
    {
        u32 id = 0;
        String<HeapAllocator> name{HeapAllocator::Default()};
        NativeEventHandler handler = nullptr;
        void* user = nullptr;
    };

    u32 chain_limit_;
    u32 next_id_ = 1;
    u64 dropped_ = 0;
    Vector<EventRecord> pending_;
    Vector<NativeSub> natives_;
};

}  // namespace engine_placeholder::script
