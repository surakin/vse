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
#include "script/script_settings.h"
#include "script/script_world.h"
#include "lang/utility.h"
#include "script/value.h"
#include "string/string.h"
#include "string_view.h"
#include "types.h"

namespace engine_placeholder::script
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
    /// Instances disabled by an error (`script.on_error` = `disable`).
    u64 disabled_by_error = 0;
    /// Partitions skipped in a tick because their state could not be made or a script did not load.
    u64 skipped_partitions = 0;
    /// Commands the merges applied.
    u64 commands_applied = 0;
    /// Events scripts emitted that the bus accepted.
    u64 events_emitted = 0;
    /// Handler calls made for delivered events.
    u64 events_delivered = 0;
    /// Waiting coroutines resumed.
    u64 waits_resumed = 0;
    /// Coroutines waiting now (after the last phase).
    u64 coroutines_waiting = 0;
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
    /// The components scripts can name; may be null (then `component.*` fails).
    ComponentCatalog* components = nullptr;
    /// The scene's names and prefabs; may be null.
    const SceneServices* scene = nullptr;
    /// The prefab source and settings the merge uses; copied.
    MergeServices services;
    /// The source of the script that defines `behavior`, instance creation and callback dispatch for the language;
    /// empty uses the LuaJIT host of [[script::LuaBehaviorHostSource]].
    StringView host_source;

    /// The default budget of a callback: ten million instructions.
    static Budget DefaultBudget()
    {
        Budget b;
        b.instructions = 10000000;
        return b;
    }
};

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

    /// The script world, for reading its counters.
    ///
    /// - Ownership: borrowed; valid for the runtime's life.
    ScriptWorld& Scripts();
    /// The counts so far.
    ///
    /// - Ownership: borrowed.
    const ScriptRuntimeStats& Stats() const;
    /// The event bus: native code emits and subscribes here; scripts reach it through `event.*`.
    ///
    /// - Ownership: borrowed; valid for the runtime's life.
    EventBus& Events();
    /// The merge's services, to fill after construction (prefabs, settings, components).
    ///
    /// - Ownership: borrowed.
    MergeServices& Services();

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

}  // namespace engine_placeholder::script
