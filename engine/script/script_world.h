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

private:
    friend class PartitionLease;
    void ReleaseSlot(PartitionSlot* slot);
    Outcome PrepareState(PartitionSlot* slot);
    Outcome CallIn(PartitionSlot* slot, StringView path, Span<const Value> args, const Budget& budget, CallOutput* out);

    struct Script
    {
        Script(StringView name_, SharedBytesPtr source_)
            : name(HeapAllocator::Default(), name_), source(static_cast<SharedBytesPtr&&>(source_))
        {
        }
        String<HeapAllocator> name;
        SharedBytesPtr source;
    };

    ScriptWorldConfig config_;
    u32 partition_count_ = 0;
    Vector<UniquePtr<PartitionSlot>> slots_;
    Mutex scripts_mutex_;
    Vector<Script> scripts_;
};

}  // namespace engine_placeholder::script
