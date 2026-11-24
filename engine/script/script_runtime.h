// The script component, the lifecycle and the script system (Phase 27 Day 457): how a script is attached to an entity
// and called. [[script::ScriptComponent]] names a behavior (a table of callbacks a script registered with
// `behavior(name, table)`); [[script::ScriptRuntime]], a world resource, owns the script world and the instances;
// [[script::MakeScriptSystem]] runs the callbacks each tick (docs/guide/scripting.md, "Behaviors and the script
// system").
#pragma once

#include "containers/shared_bytes.h"
#include "containers/unique_ptr.h"
#include "ecs/entity.h"
#include "ecs/schedule.h"
#include "ecs/world.h"
#include "jobs/scheduler.h"
#include "mem/heap_allocator.h"
#include "script/backend.h"
#include "script/bindings.h"
#include "script/component_catalog.h"
#include "script/entity_bindings.h"
#include "script/event_bus.h"
#include "script/merge.h"
#include "script/merge_services.h"
#include "script/script_debug.h"
#include "script/script_persist.h"
#include "script/script_settings.h"
#include "script/script_world.h"
#include "lang/utility.h"
#include "script/value.h"
#include "storage/identity.h"
#include "string/string.h"
#include "string_view.h"
#include "types.h"

namespace vse::script
{

/// Attaches a behavior to an entity.
///
/// The script system makes an instance (a table `self` with `self.entity` and `self.params`, private to the entity's
/// partition) on the first tick after the component appears, and closes it when the entity or the component goes away.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns its name and parameters.
struct ScriptComponent
{
    /// Names the component in logs.
    static constexpr const char* kComponentName = "script";
    /// The behavior's name: the first argument of `behavior(name, table)` in a loaded script.
    String<HeapAllocator> behavior{HeapAllocator::Default()};
    /// Instance parameters, a map (or nil); the instance reads them as `self.params` (a copy).
    Value params;
    /// Whether the callbacks run; changing it calls `on_disable` or `on_enable` at the next tick.
    bool enabled = true;

    /// Makes an empty component (no behavior; the script system reports it and ignores it).
    ScriptComponent() = default;

    /// Makes a component for the behavior `name`.
    ///
    /// - Param name: the behavior's name; copied.
    /// - Param instance_params: the parameters; copied.
    /// - Param is_enabled: whether the instance starts enabled.
    explicit ScriptComponent(StringView name, Value instance_params = Value(), bool is_enabled = true)
        : behavior(HeapAllocator::Default(), name), params(Move(instance_params)), enabled(is_enabled)
    {
    }
};

/// One partition's share of the last tick's script phase, for profiling tools and overlays (a measurement, not part of
/// the simulation: it differs run to run, the world does not).
///
/// - Thread-safety: thread-compatible plain data.
struct PartitionLoad
{
    /// The instances the partition ran.
    u32 instances = 0;
    /// The wall time its job took, in nanoseconds (0 for a partition with no instances).
    u64 nanoseconds = 0;
};

/// What the script system has done, counted since the runtime was made.
///
/// - Thread-safety: thread-compatible plain data.
struct ScriptRuntimeStats
{
    /// Ticks run.
    u64 ticks = 0;
    /// Instances that exist now.
    u64 instances = 0;
    /// Instances made (`on_create` called).
    u64 created = 0;
    /// `on_start` calls.
    u64 started = 0;
    /// `on_fixed_update` calls.
    u64 fixed_updates = 0;
    /// `on_update` calls.
    u64 updates = 0;
    /// Instances closed (their entity or component went away).
    u64 destroyed = 0;
    /// Callbacks that failed.
    u64 errors = 0;
    /// Errors that were new for their instance (a different message or callback from its last error): each was logged
    /// and sent as an `on_script_error` event; the rest of `errors` are repeats.
    u64 distinct_errors = 0;
    /// Instances disabled by an error (`script.on_error` = `disable`).
    u64 disabled_by_error = 0;
    /// Partitions skipped in a tick because their state could not be made or a script did not load.
    u64 skipped_partitions = 0;
    /// Commands the merges applied.
    u64 commands_applied = 0;
    /// The wall time the merges took, in nanoseconds (the apply of the command buffers at the sync points).
    u64 merge_nanoseconds = 0;
    /// Events scripts emitted that the bus accepted.
    u64 events_emitted = 0;
    /// Handler calls made for delivered events.
    u64 events_delivered = 0;
    /// Waiting coroutines resumed.
    u64 waits_resumed = 0;
    /// Coroutines waiting now (after the last phase).
    u64 coroutines_waiting = 0;
    /// Tasks scripts started that ran.
    u64 tasks_run = 0;
    /// Tasks that failed (a failing task is an error value for its owner, not a script error).
    u64 tasks_failed = 0;
    /// Chunks the task pool ran (a `Run` is one chunk).
    u64 task_chunks = 0;
    /// Finished tasks whose owners have not taken the result yet.
    u64 tasks_held = 0;
};

/// What a script runtime is made of.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: every pointer is borrowed and must outlive the runtime.
struct ScriptRuntimeConfig
{
    /// The language; not null.
    Backend* backend = nullptr;
    /// The bindings installed in every state; not null.
    const BindingRegistry* bindings = nullptr;
    /// The number of partitions (setting `script.partitions`).
    u32 partitions = 64;
    /// The memory cap of one partition's state; 0 is the backend's default.
    u64 partition_memory = 0;
    /// The seed of the per-entity random streams (setting `script.random_seed`).
    u32 random_seed = 1;
    /// Log messages one script may add per partition per tick (setting `script.log_limit`); 0 is no limit.
    u32 log_limit = 64;
    /// The most coroutines one partition keeps waiting (setting `script.coroutine_limit`); a call that would suspend
    /// past it is abandoned and fails.
    u32 coroutine_limit = 65536;
    /// The most handler hops of an event chain (setting `script.event_chain_limit`).
    u32 event_chain_limit = 8;
    /// What a failed callback does (setting `script.on_error`).
    OnScriptError on_error = OnScriptError::Disable;
    /// What happens when two partitions change the same thing (setting `script.conflicts`).
    ConflictPolicy conflicts = ConflictPolicy::LastWins;
    /// The limits of every callback call.
    Budget budget = DefaultBudget();
    /// The states of the task pool (setting `script.task_states`); 0 is one per scheduler participant at the first
    /// tick that runs a task.
    u32 task_states = 0;
    /// The most queued and finished-but-not-taken tasks one partition may hold (setting `script.task_queue_limit`);
    /// `task.run` past it returns an error value.
    u32 task_queue_limit = 1024;
    /// The most bytes of task source and arguments one partition may queue in a tick (setting `script.task_arg_bytes`).
    u64 task_arg_bytes = 16777216;
    /// The limits of one task chunk's call.
    Budget task_budget = TaskDefaultBudget();
    /// The memory cap of one task state; 0 is the backend's default.
    u64 task_state_memory = 0;
    /// The components scripts can name; may be null (then `component.*` fails).
    ComponentCatalog* components = nullptr;
    /// The scene's names and prefabs; may be null.
    const SceneServices* scene = nullptr;
    /// The prefab source and settings the merge uses; copied.
    MergeServices services;
    /// Whether a debugger's breakpoint stops every partition (setting `script.debug_pause_all`).
    bool debug_pause_all = true;
    /// Whether the per-function profile starts on (setting `script.profile`).
    bool profile = false;
    /// The source of the script that defines `behavior`, instance creation and callback dispatch for the language;
    /// empty uses the LuaJIT host of [[script::LuaBehaviorHostSource]].
    StringView host_source;

    /// The default budget of a task chunk: five hundred million instructions.
    static Budget TaskDefaultBudget()
    {
        Budget b;
        b.instructions = 500000000;
        return b;
    }
    /// The default budget of a callback: ten million instructions.
    static Budget DefaultBudget()
    {
        Budget b;
        b.instructions = 10000000;
        return b;
    }
};

/// One script error as the runtime reported it (the `on_script_error` event carries the same fields).
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns its text.
struct ScriptErrorRecord
{
    /// The tick the error happened in (the runtime's tick count when it ran).
    u64 tick = 0;
    /// The partition that ran the callback.
    u32 partition = 0;
    /// The failing instance's entity (`Entity::ToU64`).
    u64 entity_key = 0;
    /// The entity's name; empty for an entity without one.
    String<HeapAllocator> entity_name{HeapAllocator::Default()};
    /// The behavior's name.
    String<HeapAllocator> behavior{HeapAllocator::Default()};
    /// What was running: `on_update`, `on_create`, `an event handler`, `a wait`, ...
    String<HeapAllocator> callback{HeapAllocator::Default()};
    /// The language's message, which names the script and the line (`script name:line: message`).
    String<HeapAllocator> message{HeapAllocator::Default()};
    /// The stack trace of script frames; empty when the language gave none.
    String<HeapAllocator> trace{HeapAllocator::Default()};
    /// How many times this error happened in a row for this instance, repeats included.
    u64 count = 1;
};

/// What a console chunk did.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns its values and text.
struct ConsoleResult
{
    /// Whether the chunk ran to the end.
    bool ok = false;
    /// The failure's status when it did not.
    Status status = Status::Ok;
    /// The failure's message (the script's own words), empty when `ok`.
    String<HeapAllocator> message{HeapAllocator::Default()};
    /// The failure's stack trace, when there is one.
    String<HeapAllocator> trace{HeapAllocator::Default()};
    /// The values the chunk returned (an expression's value, or what a `return` gave).
    Vector<Value> values;
};

/// The partition number that makes [[script::ScriptRuntime::ConsoleRun]] use a scratch state.
inline constexpr u32 kConsoleScratch = 0xFFFFFFFFu;

/// The Lua source of the behavior host: `behavior(name, table)`, instance creation and callback dispatch. It is the
/// first script every state loads.
///
/// - Returns: a string literal.
/// - Ownership: static; not freed.
/// - Fails: never.
const char* LuaBehaviorHostSource();

/// The script world, the instances and the merge, as one world resource. Add it with
/// `world.SetResource(ScriptRuntime(config))` and run [[script::MakeScriptSystem]].
///
/// - Thread-safety: thread-compatible: one thread runs a tick at a time (the system runs alone); inside the tick the
/// partitions run on the scheduler's threads, each used by one job, and the result does not depend on their number.
/// - Ownership: owns the script world, the instance table and the command buffers; borrows the config's pointers.
class ScriptRuntime
{
public:
    /// Makes a runtime and loads the behavior host as the first script.
    ///
    /// - Param config: copied; `backend` and `bindings` must not be null.
    /// - Aborts: `backend` or `bindings` is null, or `partitions` is out of range.
    explicit ScriptRuntime(const ScriptRuntimeConfig& config);
    /// Destroys the runtime and its states.
    ~ScriptRuntime();
    /// Takes the contents of `other` (a resource is moved into the world).
    ScriptRuntime(ScriptRuntime&& other) noexcept;
    /// Takes the contents of `other`.
    ScriptRuntime& operator=(ScriptRuntime&& other) noexcept;
    ScriptRuntime(const ScriptRuntime&) = delete;
    ScriptRuntime& operator=(const ScriptRuntime&) = delete;

    /// Adds a script every state loads (in the order added, after the behavior host).
    ///
    /// - Param name: the chunk name for messages; copied.
    /// - Param source: the script text, shared and immutable.
    void AddScript(StringView name, SharedBytesPtr source);

    /// Sets how many fixed steps the next ticks hold and how long each is (`on_fixed_update(self, dt)` runs that many
    /// times per tick); the host sets it from its physics step count each tick. The default is 0.
    ///
    /// - Param steps: the number of calls per tick.
    /// - Param fixed_dt: the step length in seconds.
    void SetFixedSteps(u32 steps, f64 fixed_dt);

    /// Says whether a replay of the session is being recorded (the host's replay log, docs/guide/scripting.md,
    /// "Saving scripted state"). While it is, `time.real` raises an error in every script: a wall-clock reading would
    /// make the recorded run impossible to repeat. The default is off.
    ///
    /// - Param on: whether the next ticks are recorded.
    /// - Fails: never.
    void SetReplayRecording(bool on);

    /// Whether a replay is being recorded (see `SetReplayRecording`).
    ///
    /// - Fails: never.
    bool ReplayRecording() const;

    /// The script world, for reading its counters.
    ///
    /// - Ownership: borrowed; valid for the runtime's life.
    ScriptWorld& Scripts();
    /// The counts so far.
    ///
    /// - Ownership: borrowed.
    const ScriptRuntimeStats& Stats() const;
    /// What each partition did in the last tick's script phase: its instances and its job's wall time, one entry per
    /// partition in partition order (the script demo's overlay).
    ///
    /// - Param out: receives the entries; not null.
    /// - Thread-safety: thread-compatible: between ticks.
    /// - Ownership: `out` is borrowed for the call.
    void LastPartitionLoad(Vector<PartitionLoad>* out) const;
    /// The event bus: native code emits and subscribes here; scripts reach it through `event.*`.
    ///
    /// - Ownership: borrowed; valid for the runtime's life.
    EventBus& Events();
    /// Whether the entity with stable id `entity_key` listens for the event `name` as of the end of the last tick: a
    /// handler made with `event.on` or `event.once` in one of its behaviors, or a coroutine parked in `wait_event`. A
    /// native emitter asks before it builds a payload, so an event nobody handles costs one lookup. Call it between
    /// ticks, not while a tick runs.
    ///
    /// - Param entity_key: the entity's stable id (`ecs::Entity::ToU64`).
    /// - Param name: the event name.
    /// - Returns: true if a handler is subscribed.
    /// - Thread-safety: thread-compatible; not while a tick runs.
    /// - Fails: returns false for an entity with no script.
    bool HasHandler(u64 entity_key, StringView name) const;
    /// The merge's services, to fill after construction (prefabs, settings, components).
    ///
    /// - Ownership: borrowed.
    MergeServices& Services();
    /// The command kinds the merge applies: a target above `script` registers its own after construction (the ids
    /// below 256 that the built-in kinds do not use; see `script_physics`). Register before the first tick.
    ///
    /// - Ownership: borrowed.
    CommandKindTable& Kinds();

    /// The errors reported lately, oldest first, at most 64 (the log has them all, the events too). A repeat of the
    /// last error of an instance raises that record's `count`.
    ///
    /// - Ownership: borrowed; changes in a tick.
    const Vector<ScriptErrorRecord>& RecentErrors() const;

    /// Starts a debugger on the world: breakpoints, stepping and inspection through `transport` (see
    /// [[script::ScriptDebugger]]); `script.debug_pause_all` decides whether a stop holds every partition. A debugger
    /// already attached is replaced.
    ///
    /// - Param transport: where commands come from and events go to; not null; it must outlive the debugger.
    /// - Returns: the debugger; null when a partition is held (call it between ticks).
    /// - Ownership: owned by the runtime until [[script::ScriptRuntime::DetachDebugger]] or its destruction.
    /// - Thread-safety: thread-compatible: between ticks.
    ScriptDebugger* AttachDebugger(DebugTransport* transport);

    /// Ends the debugging: stopped partitions run on, breakpoints are dropped, the debugger is destroyed.
    ///
    /// - Thread-safety: thread-compatible: between ticks, from a thread that is not stopped.
    void DetachDebugger();

    /// Runs `chunk` now in the state of `partition`, or in a bare scratch state (`kConsoleScratch`: the language's
    /// standard library only, nothing of the behaviors), and returns what it gave. The chunk is first tried as an
    /// expression, then as statements. It sees the partition's globals and functions; it is not an entity's callback,
    /// so bindings that need one (`event.on`, `spawn`) fail with their own message.
    ///
    /// - Param partition: the partition number, or `kConsoleScratch`.
    /// - Param chunk: the text to run.
    /// - Param result: receives the outcome; not null.
    /// - Thread-safety: thread-compatible: call it between ticks, or use [[script::ScriptRuntime::ConsoleSubmit]]
    /// from any thread.
    /// - Ownership: borrows `chunk` and `result` for the call.
    void ConsoleRun(u32 partition, StringView chunk, ConsoleResult* result);

    /// Queues a console chunk: it runs at the start of the next tick, in the order queued, before any callback (the
    /// sync point), and its result waits for [[script::ScriptRuntime::ConsoleTake]].
    ///
    /// - Param partition: the partition number, or `kConsoleScratch`.
    /// - Param chunk: the text; copied.
    /// - Returns: the id to take the result with.
    /// - Thread-safety: thread-safe.
    u64 ConsoleSubmit(u32 partition, StringView chunk);

    /// Takes the result of a queued chunk.
    ///
    /// - Param id: from [[script::ScriptRuntime::ConsoleSubmit]].
    /// - Param result: receives the outcome when it is ready; not null.
    /// - Returns: false while the chunk has not run (or when the id is unknown or taken already).
    /// - Thread-safety: thread-safe.
    /// - Ownership: `result` is borrowed for the call.
    bool ConsoleTake(u64 id, ConsoleResult* result);

    /// Turns the per-function profile on or off in every partition.
    ///
    /// - Param on: whether to profile; turning it on starts from zero.
    /// - Thread-safety: thread-safe.
    void SetProfiling(bool on);

    /// What the JIT compilers of the partitions have done, added up: whether any state compiles, the traces and the
    /// bytes of machine code they hold (a partition that is held is skipped).
    ///
    /// - Param totals: receives the sums; not null.
    /// - Returns: `Ok`, or `Unsupported` when the language has no JIT.
    /// - Thread-safety: thread-compatible: between ticks.
    /// - Ownership: `totals` is borrowed for the call.
    Outcome JitTotals(JitStats* totals);

    /// The hottest functions: calls and time added up over the partitions that are not held.
    ///
    /// - Param out: receives the entries, longest time first; not null.
    /// - Param top: the most entries; 0 is all.
    /// - Returns: `Ok`, or `Unsupported` when the language cannot profile.
    /// - Thread-safety: thread-compatible: between ticks.
    /// - Ownership: `out` is borrowed for the call.
    Outcome ProfileReport(Vector<ProfileEntry>* out, usize top);

    /// Writes the persistent fields of every running instance into `script_persist` components, so a save of the world
    /// carries them (the fields a script marked with `persist(...)`, read from its partition's state; see
    /// [[script::ScriptPersist]]). An entity whose instance has no persistent fields loses a record it had; an entity
    /// whose instance has not run yet keeps (and gets written back) the fields a load gave it. Coroutines waiting at
    /// the time are not saved; the report counts them.
    ///
    /// Call it between ticks, after `index.Sync`, and then save the world with the registry that
    /// [[script::RegisterScriptComponents]] filled.
    ///
    /// - Param world: the world the instances live in; not iterated now.
    /// - Param index: the keys of `world`, synced; entity handles inside values are written as keys.
    /// - Param report: receives the counts; may be null.
    /// - Thread-safety: thread-compatible: between ticks.
    /// - Aborts: `world` is being iterated.
    /// - Ownership: all pointers are borrowed for the call.
    /// - Fails: an instance that cannot be read is counted in the report (`instances_failed`) and keeps its old record.
    void CapturePersistent(ecs::World* world, const storage::KeyIndex& index, PersistReport* report);

    /// Takes the `script_persist` components of a world that was just loaded: each is decoded (entity keys resolved
    /// through `index`), removed from its entity, and held until the entity's instance is made, which gets the fields
    /// back before `on_load` and `on_start`. Call it once after a load, before the first tick that sees the entities.
    ///
    /// - Param world: the loaded world; not iterated now.
    /// - Param index: the keys of `world`, synced.
    /// - Param report: receives the counts; may be null.
    /// - Thread-safety: thread-compatible: between ticks.
    /// - Aborts: `world` is being iterated.
    /// - Ownership: all pointers are borrowed for the call.
    /// - Fails: a record that does not decode is counted (`corrupt`) and dropped; its instance starts without it.
    void ResolveLoaded(ecs::World* world, const storage::KeyIndex& index, LoadReport* report);

    /// The tick count and the simulation time the runtime has reached; the per-entity random streams and the timers
    /// follow them, so a save keeps them ([[script::PersistReport]]).
    ///
    /// - Param tick: receives the ticks run.
    /// - Param sim_time: receives the simulation seconds.
    /// - Ownership: both pointers are borrowed for the call.
    /// - Fails: never.
    void Clock(u64* tick, f64* sim_time) const;

    /// Sets the tick count and the simulation time, for a runtime that continues a saved session: call it once before
    /// the first tick after a load with the values the save reported, so the random streams go on where they stopped.
    ///
    /// - Param tick: the ticks already run.
    /// - Param sim_time: the simulation seconds already run.
    /// - Thread-safety: thread-compatible: between ticks.
    void RestoreClock(u64 tick, f64 sim_time);

    /// Runs one tick on `world`: destroys, creates and starts, enables, fixed updates, updates, the merge, and the
    /// destroys the merge caused. The script system calls it; tests may call it directly.
    ///
    /// - Param world: the world to run scripts on; not in a parallel phase.
    /// - Param dt: the simulation seconds this tick advances.
    /// - Param scheduler: the job scheduler whose participant 0 is the calling thread, outside any job; the partitions
    /// then run as jobs, one each. Null (or one participant, or a call from inside a job) runs them one after another.
    /// The world, the stats and the merged result are the same either way.
    /// - Ownership: borrows `world` and `scheduler` for the call.
    /// - Aborts: `world` is being iterated.
    void Tick(ecs::World* world, f64 dt, jobs::Scheduler* scheduler = nullptr);

private:
    struct Impl;
    UniquePtr<Impl> impl_;
};

/// Makes the script system: it runs [[script::ScriptRuntime::Tick]] on the world's `ScriptRuntime` resource every
/// frame. It declares that it reads and writes every component (scripts name them at run time), so it runs alone in its
/// stage, and that it writes the `ScriptRuntime` resource.
///
/// - Returns: the system, to add to an [[ecs::Schedule]].
/// - Fails: never; running it without the resource aborts.
ecs::SystemDesc MakeScriptSystem();

}  // namespace vse::script
