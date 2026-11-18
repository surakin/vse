// The script world (Phase 27 Day 451): P partitions, one language state each, and the rule that no state is ever used
// by two threads at once. A partition is checked out by one job, used, and handed back; an entity's scripts live in
// the partition a fixed hash gives, so the assignment never depends on the thread count (docs/guide/scripting.md,
// "Partitions").
#pragma once

#include "containers/shared_bytes.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "mem/heap_allocator.h"
#include "script/backend.h"
#include "script/bindings.h"
#include "string/string.h"
#include "sync/atomic.h"
#include "sync/mutex.h"
#include "types.h"

namespace engine_placeholder::script
{

/// The most partitions a world can have (the setting's upper bound).
inline constexpr u32 kMaxPartitions = 1024;

/// The partition of a stable entity id: a fixed 64-bit mix reduced modulo `partitions`.
///
/// The value depends on nothing but the two arguments: not on the thread count, the load, the order entities were
/// made in, or the platform (committed values are in the tests).
///
/// - Param stable_id: the entity's stable id (the 64-bit hash of its storage key; any value).
/// - Param partitions: the world's partition count; 1 to [[script::kMaxPartitions]].
/// - Returns: a number below `partitions`.
/// - Aborts: `partitions` is 0.
/// - Thread-safety: thread-safe.
u32 PartitionOf(u64 stable_id, u32 partitions);

/// What a script world is made of.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: every pointer is borrowed and must outlive the world.
struct ScriptWorldConfig
{
    /// The language; its `CreateState` makes each partition's state.
    Backend* backend = nullptr;
    /// The bindings installed into every state; may be null (a bare state).
    const BindingRegistry* bindings = nullptr;
    /// How the bindings are installed; the default installs nothing, use [[script::InstallProfile::Partition]].
    InstallProfile profile;
    /// The number of partitions, 1 to [[script::kMaxPartitions]] (setting `script.partitions`).
    u32 partitions = 64;
    /// The memory cap of one partition's state in bytes; 0 is the backend's default (setting
    /// `script.partition_memory`).
    u64 partition_memory = 0;
    /// Whether a partition whose state ran out of memory is reset (a new state, the same scripts) at once.
    bool reset_on_out_of_memory = true;
    /// Whether the per-function profile starts on (setting `script.profile`; [[script::ScriptWorld::SetProfiling]]
    /// changes it later).
    bool profiling = false;
};

/// What one partition has done, read without a lock.
///
/// - Thread-safety: thread-compatible plain data; a snapshot of counters that other threads may be updating.
struct PartitionCounters
{
    /// Calls made through [[script::PartitionLease::Call]].
    u64 calls = 0;
    /// Wall-clock nanoseconds those calls took.
    u64 nanoseconds = 0;
    /// Calls that ended `BudgetExceeded`.
    u64 budget_hits = 0;
    /// Calls that ended `OutOfMemory`.
    u64 out_of_memory = 0;
    /// Times the state was replaced after running out of memory.
    u64 resets = 0;
    /// The bytes the state held when it was last handed back.
    u64 memory_bytes = 0;
};

/// How a checkout ended.
///
/// - Thread-safety: thread-compatible plain data.
enum class CheckoutStatus : u8
{
    /// The lease is valid.
    Ok,
    /// Another job holds the partition.
    Busy,
    /// The partition number is out of range.
    Invalid,
    /// The state could not be made, bindings installed or scripts loaded (see the outcome).
    Failed,
};

class ScriptWorld;
class ScriptDebugger;
struct PartitionSlot;

/// The right to use one partition's state: held by one job, handed back by `Release` or the destructor.
///
/// A lease belongs to the thread that took it; do not keep one across a suspension point of a job (the job may
/// resume on another thread).
///
/// - Thread-safety: thread-compatible; use it on the thread that checked it out.
/// - Ownership: borrows the world, which must outlive it; the state is the partition's, not the lease's.
class PartitionLease
{
public:
    /// Creates a lease that holds nothing.
    PartitionLease() = default;
    /// Hands the partition back if held.
    ~PartitionLease();
    /// Takes the partition of `other`, which is left empty.
    PartitionLease(PartitionLease&& other) noexcept;
    /// Hands back what this lease holds, then takes the partition of `other`, which is left empty.
    PartitionLease& operator=(PartitionLease&& other) noexcept;
    PartitionLease(const PartitionLease&) = delete;
    PartitionLease& operator=(const PartitionLease&) = delete;

    /// Whether the lease holds a partition.
    ///
    /// - Fails: never.
    bool Valid() const { return slot_ != nullptr; }

    /// The partition number.
    ///
    /// - Aborts: the lease is not valid.
    u32 Partition() const;

    /// The partition's state, owned by the calling thread while the lease is held.
    ///
    /// - Returns: the state; null when a reset could not make a new one.
    /// - Ownership: borrowed; valid until `Release`, and until a [[script::PartitionLease::Call]] that ends
    /// `OutOfMemory` replaces the state (fetch it again after a call).
    /// - Aborts: the lease is not valid.
    State* GetState() const;

    /// Calls `function_path` in the partition's state and records the call in the partition's counters. A call that
    /// ends `OutOfMemory` resets the partition when the world's policy says so.
    ///
    /// - Returns: the outcome of [[script::State::Call]]; when the partition has no state, the failure of making one.
    /// A reset invalidates [[script::State]] pointers and the coroutine ids of suspended calls of the old state (a
    /// `Resume` of one answers `Invalid`).
    /// - Ownership: as `State::Call`.
    /// - Aborts: the lease is not valid.
    Outcome Call(StringView function_path, Span<const Value> args, const Budget& budget, CallOutput* out);

    /// Resumes a call that suspended (see [[script::CallOutput::coroutine]]) with `values` as the result of its wait,
    /// and records the resume in the partition's counters; an `OutOfMemory` resume resets the partition like a call
    /// does.
    ///
    /// - Returns: the outcome of [[script::State::Resume]]; `Invalid` when the partition has no state.
    /// - Ownership: as `State::Resume`.
    /// - Aborts: the lease is not valid.
    Outcome Resume(u64 coroutine, Span<const Value> values, const Budget& budget, CallOutput* out);

    /// Runs `source` as a console chunk in the partition's state ([[script::State::Eval]]) and records it in the
    /// partition's counters. Like a call, it waits first while another partition is stopped by the debugger with
    /// `pause_all`.
    ///
    /// - Param chunk_name: the name for messages; not empty.
    /// - Param source: the chunk.
    /// - Param budget: the limits.
    /// - Param out: receives the results; not null.
    /// - Returns: the outcome of [[script::State::Eval]]; when the partition has no state, the failure of making one.
    /// - Aborts: the lease is not valid.
    /// - Ownership: borrows every argument for the call.
    Outcome Eval(StringView chunk_name, StringView source, const Budget& budget, CallOutput* out);

    /// Hands the partition back; afterwards the lease is not valid. Does nothing for an empty lease.
    ///
    /// - Fails: never.
    void Release();

private:
    friend class ScriptWorld;
    PartitionLease(ScriptWorld* world, PartitionSlot* slot) : world_(world), slot_(slot) {}

    ScriptWorld* world_ = nullptr;
    PartitionSlot* slot_ = nullptr;
};

/// One module's new text for a hot reload.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns the key; shares the bytes.
struct ModuleReplacement
{
    /// The key the module was added with.
    String<HeapAllocator> key{HeapAllocator::Default()};
    /// The shared buffer holding the new text.
    SharedBytesPtr bytes;
    /// Where the text starts in `bytes`.
    usize offset = 0;
    /// The text's length.
    usize size = 0;
};

/// What a hot reload did.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns its text.
struct ReloadResult
{
    /// The partitions that had a state and were reloaded.
    u32 partitions = 0;
    /// The modules run again in each of them: the replaced ones and every module that requires one of them.
    u32 modules_run = 0;
    /// The instances moved to a new version of their behavior, over all partitions.
    u64 instances_rebound = 0;
    /// How many `on_reload_save` and `on_reload_restore` hooks failed (the reload went on).
    u64 hook_errors = 0;
    /// The first hook error's text.
    String<HeapAllocator> hook_error{HeapAllocator::Default()};
};

/// The Lua source of the module host a state loads (with [[script::State::LoadHost]]) before its first minted script:
/// `module_env`, `module_store`, `module_run`, `module_rerun` and the `reload_*` functions, kept outside the globals.
///
/// - Returns: a string literal.
/// - Ownership: static; not freed.
/// - Fails: never.
const char* LuaModuleHostSource();

/// A set of partitions, each with its own language state, sharing one binding registry and one list of scripts.
///
/// States are made on the first checkout of a partition (the bindings installed, every script loaded in the order it
/// was added) and caught up with scripts added later at the next checkout. Source text is shared and immutable: each
/// state gets a read-only share of the same bytes, and nothing else crosses between states (only [[script::Value]]
/// copies do).
///
/// - Thread-safety: thread-safe: any thread may check out any partition, but one partition has one holder at a time;
/// the counters and `AddScript` are safe from any thread.
/// - Ownership: borrows the config's pointers; owns the partitions and their states. Destroy the world only when no
/// lease is held.
class ScriptWorld
{
public:
    /// Creates a world with `config.partitions` empty partitions (no state is made yet).
    ///
    /// - Param config: copied; `backend` must not be null and `partitions` must be 1 to `kMaxPartitions`.
    /// - Aborts: the config breaks those rules.
    explicit ScriptWorld(const ScriptWorldConfig& config);
    /// Destroys the states.
    ~ScriptWorld();
    ScriptWorld(const ScriptWorld&) = delete;
    ScriptWorld& operator=(const ScriptWorld&) = delete;

    /// The number of partitions.
    ///
    /// - Fails: never.
    u32 PartitionCount() const { return partition_count_; }

    /// Adds a script every partition loads (now in the partitions that already have a state, at their next checkout;
    /// in a new state as part of its creation).
    ///
    /// - Param name: the chunk name for messages; copied.
    /// - Param source: the script text, shared and immutable.
    /// - Fails: never; a script that fails to load makes the checkouts of the partitions fail with its outcome.
    void AddScript(StringView name, SharedBytesPtr source);

    /// Adds a minted script (a module): its text is a range of `bytes`, compiled without running in every partition
    /// and, when `entry` is true, run once in each partition at that point of the load order (the modules it requires
    /// are added before it; the first `require` of any module runs it). A script reaches another only through
    /// `__require("<key>")` with a key in its `deps`, and only while it loads (docs/guide/scripting.md, "Minted
    /// scripts").
    ///
    /// - Param key: the module's key, the hex text of its resource id; not empty, no line break.
    /// - Param name: the chunk name for messages; copied.
    /// - Param bytes: the shared buffer holding the text; kept alive by the world.
    /// - Param offset: where the text starts in `bytes`.
    /// - Param size: the text's length.
    /// - Param deps: the keys the module may require, one per line; copied.
    /// - Param entry: whether to run the module when it loads.
    /// - Fails: never; a module that fails to load or run makes the checkouts of the partitions fail with its outcome.
    /// - Thread-safety: thread-safe.
    void AddModule(StringView key, StringView name, SharedBytesPtr bytes, usize offset, usize size, StringView deps,
                   bool entry);

    /// Replaces the text of modules and runs them, and every module that requires one of them, again in every
    /// partition that has a state, so that no tick sees two versions: first every partition runs the new modules
    /// (the old behavior classes and module values stay in place), then, only if all succeeded, every partition
    /// commits (instances move to the new behavior classes, with the `on_reload_save` and `on_reload_restore` hooks).
    /// A failure in any partition puts every partition back and leaves the world's list as it was. Partitions without a
    /// state load the new text when they are made.
    ///
    /// Call it between ticks, on one thread, with no partition held (the check of the ownership tokens fails the call
    /// with `Invalid` otherwise).
    ///
    /// - Param replacements: the new texts; each key must have been added with [[script::ScriptWorld::AddModule]].
    /// - Param result: receives what was done; not null.
    /// - Returns: `Ok`, or the failure (a syntax or run error of the new text, a partition that is busy, a key that is
    /// not a module, a partition left failed by an earlier load).
    /// - Ownership: borrows the replacements for the call; the world keeps their bytes.
    /// - Thread-safety: thread-compatible: not safe with ticks or checkouts.
    Outcome ReloadModules(Span<const ModuleReplacement> replacements, ReloadResult* result);

    /// Takes a partition, or reports why not. The second job to ask for a held partition gets `Busy`.
    ///
    /// - Param partition: the partition number.
    /// - Param lease: receives the lease on `Ok`; not null.
    /// - Param error: receives the reason on `Failed`; may be null.
    /// - Returns: the status.
    /// - Ownership: the lease keeps the partition until it is released; the world must outlive it.
    /// - Thread-safety: thread-safe.
    CheckoutStatus TryCheckout(u32 partition, PartitionLease* lease, Outcome* error);

    /// As `TryCheckout`, but a busy partition is a program error: the jobs of a phase are one per partition.
    ///
    /// - Aborts: the partition is held by another job (debug checks); release builds return `Busy`.
    /// - Ownership: as `TryCheckout`.
    /// - Thread-safety: thread-safe.
    CheckoutStatus Checkout(u32 partition, PartitionLease* lease, Outcome* error);

    /// The counters of one partition.
    ///
    /// - Returns: a snapshot; zeros for a number out of range.
    /// - Thread-safety: thread-safe, lock-free.
    PartitionCounters Counters(u32 partition) const;

    /// The sum of every partition's counters (`memory_bytes` is the world's total).
    ///
    /// - Thread-safety: thread-safe, lock-free.
    PartitionCounters Total() const;

    /// Attaches a debugger: every partition's state reports its lines to the debugger's session for that partition (at
    /// its next checkout), and a partition waits at the debugger's gate before each call (`pause_all`). With null,
    /// detaches every state now.
    ///
    /// Call it between ticks with no partition held; detaching must happen before the debugger is destroyed.
    ///
    /// - Param debugger: the debugger, or null; borrowed until replaced.
    /// - Returns: false (and nothing changes) when a partition is held by a job.
    /// - Ownership: borrows `debugger`.
    /// - Thread-safety: thread-compatible: not safe with ticks or checkouts.
    bool SetDebugger(ScriptDebugger* debugger);

    /// The attached debugger, or null.
    ///
    /// - Fails: never.
    /// - Ownership: borrowed; the debugger is the caller's.
    ScriptDebugger* Debugger() const { return debugger_.Load(MemoryOrder::Acquire); }

    /// Turns the per-function profile on or off in every partition (at its next checkout; a state made later starts
    /// the way this was last set). Turning it on starts every profile from zero.
    ///
    /// - Param on: whether to profile.
    /// - Thread-safety: thread-safe.
    void SetProfiling(bool on);

    /// Whether profiling is on.
    ///
    /// - Fails: never.
    bool Profiling() const { return profiling_.Load(MemoryOrder::Acquire); }

    /// Adds up the profiles of the partitions that have a state and are not held (a held one is skipped), one entry
    /// per function (chunk, name and line), sorted by time spent, then calls, then name; at most `top` entries.
    ///
    /// - Param out: receives the entries (emptied first); not null.
    /// - Param top: the most entries to return; 0 is all.
    /// - Returns: `Ok`, or `Unsupported` when the backend cannot profile.
    /// - Thread-safety: thread-compatible: call it between ticks.
    /// - Ownership: `out` is borrowed for the call.
    Outcome ProfileReport(Vector<ProfileEntry>* out, usize top);

private:
    friend class PartitionLease;
    void ReleaseSlot(PartitionSlot* slot);
    Outcome PrepareState(PartitionSlot* slot);
    Outcome CallIn(PartitionSlot* slot, StringView path, Span<const Value> args, const Budget& budget, CallOutput* out);
    Outcome ResumeIn(PartitionSlot* slot, u64 coroutine, Span<const Value> values, const Budget& budget,
                     CallOutput* out);
    Outcome EvalIn(PartitionSlot* slot, StringView chunk_name, StringView source, const Budget& budget,
                   CallOutput* out);
    Outcome Settle(PartitionSlot* slot, Outcome result, u64 start);
    void SyncDebug(PartitionSlot* slot);
    void WaitAtGate();

    struct Script
    {
        Script(StringView name_, SharedBytesPtr source_)
            : name(HeapAllocator::Default(), name_), source(static_cast<SharedBytesPtr&&>(source_)),
              key(HeapAllocator::Default()), deps(HeapAllocator::Default())
        {
        }
        String<HeapAllocator> name;
        SharedBytesPtr source;
        // A module (key not empty): the text is source[offset, offset + size).
        String<HeapAllocator> key;
        String<HeapAllocator> deps;
        usize offset = 0;
        usize size = 0;
        bool entry = false;
    };

    ScriptWorldConfig config_;
    u32 partition_count_ = 0;
    Vector<UniquePtr<PartitionSlot>> slots_;
    Mutex scripts_mutex_;
    Vector<Script> scripts_;
    Atomic<ScriptDebugger*> debugger_{nullptr};
    Atomic<bool> profiling_{false};
};

}  // namespace engine_placeholder::script
