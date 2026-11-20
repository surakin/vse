// The script debugger (Phase 27 Day 465): breakpoints, stepping and inspection of scripts that run on the partitions,
// driven through a plain command and event interface (docs/guide/scripting.md, "Debugging").
#pragma once

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "mem/heap_allocator.h"
#include "script/backend.h"
#include "script/value.h"
#include "string/string.h"
#include "string_view.h"
#include "sync/atomic.h"
#include "sync/mutex.h"
#include "types.h"

namespace vse::script
{

/// The two directions of a debugger's connection: commands come in as [[script::Value]] maps, events go out as maps. A
/// network adapter for an external debugger (not in this phase) implements this interface; the in-process one below is
/// what the tests and a UI console use.
///
/// Commands (`cmd`): `set_breakpoints` (`breakpoints`: an array of `{script, line}`; `script` is the chunk name),
/// `pause` (stop at the next line of any partition), `continue`, `step_over`, `step_in`, `step_out` (the `partition`
/// that is stopped; the lowest stopped one when absent), `stack` and `locals` (`partition`, and `frame` for `locals`),
/// `detach` (drop every breakpoint and let everything run).
/// Events (`event`): `stopped` (`partition`, `script`, `line`, `depth`, `reason`: `breakpoint`, `step` or `pause`),
/// `continued`, `breakpoints` (`count`), `stack` (`frames`: `{function, script, line}`), `locals` (`variables`) and
/// `error` (`message`).
///
/// - Thread-safety: thread-safe: the handler may be called from any thread, and `PostEvent` from any partition thread.
/// - Ownership: owned by the caller of the debugger; it must outlive the debugger.
class DebugTransport
{
public:
    /// Runs one command; called by the transport on the sender's thread.
    using CommandHandler = void (*)(void* user, const Value& command);

    /// Ends the transport.
    virtual ~DebugTransport() = default;
    /// Installs (or, with null, removes) the function that runs commands.
    ///
    /// - Param handler: the function, or null.
    /// - Param user: passed to it.
    /// - Ownership: borrows `user`, which must outlive the handler.
    virtual void SetCommandHandler(CommandHandler handler, void* user) = 0;
    /// Sends an event to the debugger. It never blocks on the debugger: a transport that cannot keep up drops events.
    ///
    /// - Param event: the event; moved.
    virtual void PostEvent(Value&& event) = 0;
};

/// A transport inside the process: the debugger side calls [[script::InProcessDebugTransport::Send]] and
/// [[script::InProcessDebugTransport::NextEvent]] from its own thread.
///
/// - Thread-safety: thread-safe.
/// - Ownership: owns the queued events.
class InProcessDebugTransport final : public DebugTransport
{
public:
    /// Makes a transport with no handler; up to `max_events` events are kept (the oldest are dropped past that).
    ///
    /// - Param max_events: the queue limit (at least 1).
    explicit InProcessDebugTransport(usize max_events = 4096);

    /// See [[script::DebugTransport::SetCommandHandler]].
    /// - Ownership: borrows `user`, which must outlive the handler.
    void SetCommandHandler(CommandHandler handler, void* user) override;
    /// See [[script::DebugTransport::PostEvent]].
    void PostEvent(Value&& event) override;

    /// Sends a command to the engine; it is run on this thread, before this returns.
    ///
    /// - Param command: a map (see [[script::DebugTransport]]).
    /// - Returns: false when no handler is installed.
    bool Send(const Value& command);
    /// Takes the oldest event, waiting for one.
    ///
    /// - Param out: receives the event.
    /// - Param timeout_ms: the longest wait; 0 only checks.
    /// - Returns: true when an event was taken.
    /// - Ownership: `out` is borrowed for the call.
    bool NextEvent(Value* out, u32 timeout_ms);
    /// How many events were dropped because the queue was full.
    ///
    /// - Fails: never.
    u64 Dropped() const { return dropped_.Load(MemoryOrder::Relaxed); }

private:
    Mutex handler_mutex_;
    Mutex mutex_;
    ConditionVariable ready_;
    CommandHandler handler_ = nullptr;
    void* user_ = nullptr;
    Vector<Value> ring_;
    usize head_ = 0;
    usize count_ = 0;
    Atomic<u64> dropped_{0};
};

/// What the debugger holds for one partition (internal to the debugger).
class PartitionDebug;

/// The debugger of a script world: it owns the breakpoints and the stop state, hands each partition's state a
/// [[script::DebugSession]], and answers commands from a [[script::DebugTransport]].
///
/// When a script reaches a breakpoint, the thread that runs it stops inside the language's hook until a command lets it
/// go. With `pause_all` (the default, setting `script.debug_pause_all`) every other partition also stops, at the end of
/// the callback it is running (the next point where it would start another), so the world stays consistent while you
/// look; without it only the partition that hit the breakpoint stops and the others run on until the tick needs them.
///
/// - Thread-safety: thread-safe.
/// - Ownership: borrows the transport; owns the sessions it gives to the states (attach with
/// [[script::ScriptWorld::SetDebugger]] and detach before destroying the debugger).
class ScriptDebugger
{
public:
    /// Makes a debugger.
    ///
    /// - Param transport: where commands come from and events go to; not null.
    /// - Param partitions: how many partitions the world has.
    /// - Param pause_all: whether a stop holds every partition.
    /// - Ownership: borrows `transport`.
    ScriptDebugger(DebugTransport* transport, u32 partitions, bool pause_all);
    /// Detaches (lets every stopped partition run) and removes the transport's handler.
    ~ScriptDebugger();
    ScriptDebugger(const ScriptDebugger&) = delete;
    ScriptDebugger& operator=(const ScriptDebugger&) = delete;

    /// The session of `partition`, for the state of that partition.
    ///
    /// - Param partition: the partition number.
    /// - Returns: the session; null for a number out of range.
    /// - Ownership: owned by the debugger.
    DebugSession* SessionFor(u32 partition);

    /// Where a partition waits while another is stopped (`pause_all`): the world calls it before starting a callback.
    ///
    /// - Fails: never.
    void Gate();

    /// Runs one command (the transport's handler calls this; tests may too).
    ///
    /// - Param command: a map.
    void Handle(const Value& command);

    /// Drops every breakpoint and stop request and lets every stopped partition run on.
    ///
    /// - Fails: never.
    void Detach();

    /// How many partitions are stopped now.
    ///
    /// - Fails: never.
    u32 StoppedCount() const { return stopped_.Load(MemoryOrder::Acquire); }

    /// How many stops there have been.
    ///
    /// - Fails: never.
    u64 StopCount() const { return stop_count_.Load(MemoryOrder::Relaxed); }

private:
    friend class PartitionDebug;
    struct Breakpoint
    {
        String<HeapAllocator> script{HeapAllocator::Default()};
        u32 line = 0;
    };
    bool WantsLines(PartitionDebug* p);
    bool AtLine(PartitionDebug* p, const SourceLocation& where, u32 depth);
    void StopHere(PartitionDebug* p, const SourceLocation& where, u32 depth, DebugInspector* inspector);
    void Serve(PartitionDebug* p, const Value& request, DebugInspector* inspector);
    void SetBreakpoints(const Value& list);
    void Resume(const Value& command, u32 step_mode);
    void Request(const Value& command);
    PartitionDebug* Target(const Value& command);
    void Post(Value&& event);
    void PostError(StringView message);

    DebugTransport* transport_;
    bool pause_all_;
    Mutex mutex_;
    ConditionVariable changed_;
    Vector<UniquePtr<PartitionDebug>> partitions_;
    Vector<Breakpoint> breakpoints_;  // guarded by mutex_
    Atomic<u32> breakpoint_version_{0};
    Atomic<u32> breakpoint_count_{0};
    Atomic<bool> pause_requested_{false};
    u32 inside_ = 0;  // threads in a stop or at the gate; guarded by mutex_
    Atomic<u32> stopped_{0};
    Atomic<u64> stop_count_{0};
};

}  // namespace vse::script
