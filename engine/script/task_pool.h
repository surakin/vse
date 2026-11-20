// Script tasks (Phase 27 Day 461): parallel work a script starts itself. A task is a function given as source text that
// runs on a task state, a pooled language state that has only the `pure` bindings (no world, no events, no settings),
// with its arguments copied in and its result copied out. The pool runs the tasks queued in one tick at the end of the
// tick, on the job scheduler, and the results are the same for any number of threads (docs/guide/scripting.md,
// "Script tasks").
#pragma once

#include "containers/hash_map.h"
#include "containers/span.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "jobs/scheduler.h"
#include "mem/heap_allocator.h"
#include "script/backend.h"
#include "script/bindings.h"
#include "script/script_world.h"
#include "script/value.h"
#include "string/string.h"
#include "string_view.h"
#include "types.h"

namespace vse::script
{

/// A task handle is `(partition + 1) << 40 | sequence`: scripts see it as a number, and it is the same for any thread
/// count.
inline constexpr u32 kTaskIdShift = 40;

/// The most chunks one `task.map` or `task.parallel_for` may make (the results of the chunks are one array).
inline constexpr u64 kMaxTaskChunks = kMaxContainerItems;

/// The default number of elements one chunk holds when the script gives none; it does not depend on the thread count,
/// so the budget and the result of a chunk are the same on any machine.
inline constexpr u32 kDefaultTaskChunk = 256;

/// What a task does with its source.
enum class TaskMode : u8
{
    /// `task.run`: the source is the body of `function(args)`; the result is its first result.
    Run = 1,
    /// `task.map`: the source is the body of `function(item, index)`; the result is an array of the results.
    Map = 2,
    /// `task.parallel_for`: the source is the body of `function(first, last, args)` run once per chunk of indices
    /// (`first` to `last`, both included, counted from 1); the result is an array with one result per chunk.
    For = 3,
};

/// One task a script asked for, waiting to run.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns the source and the arguments.
struct TaskRequest
{
    /// The handle.
    u64 id = 0;
    /// The stable key of the instance that asked.
    u64 owner_key = 0;
    /// The kind of work.
    TaskMode mode = TaskMode::Run;
    /// The function body.
    String<HeapAllocator> source{HeapAllocator::Default()};
    /// `Run`: the argument; `Map`: the array to map; `For`: the argument given to every chunk.
    Value data;
    /// The seed of the task's random numbers.
    u64 seed = 0;
    /// `Map`: the number of elements; `For`: the number of indices.
    u64 count = 0;
    /// The elements per chunk (0: [[kDefaultTaskChunk]]).
    u32 chunk = 0;
    /// What the request weighs against the byte limit.
    u64 bytes = 0;
};

/// The tasks one partition asked for in the running tick.
///
/// - Thread-safety: thread-compatible: used by the partition's job only.
struct TaskOutbox
{
    /// The requests in the order they were made.
    Vector<TaskRequest> requests;
    /// The next handle's sequence number.
    u64 next_sequence = 1;
    /// The bytes of the requests so far.
    u64 bytes = 0;
};

/// What a finished task holds until its owner takes it.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns the result and the error text.
struct TaskRecord
{
    /// The instance that asked; only it may take the result.
    u64 owner_key = 0;
    /// The partition of the owner.
    u32 partition = 0;
    /// Whether the task failed (then `error` says why and `result` is empty).
    bool failed = false;
    /// The result of a task that did not fail.
    Value result;
    /// The error of a task that failed.
    String<HeapAllocator> error{HeapAllocator::Default()};
};

/// The finished tasks by handle. Changed only at the sync point, between phases; read by every partition's job while it
/// runs, so a lookup needs no lock.
///
/// - Thread-safety: thread-compatible: concurrent reads are safe while nothing changes the table.
/// - Ownership: owns the records.
class TaskTable
{
public:
    /// Makes an empty table.
    TaskTable();

    /// The record of `id`, or null when there is none (never made, taken, or dropped with its owner).
    ///
    /// - Ownership: borrowed; valid until the next change.
    /// - Fails: never.
    const TaskRecord* Find(u64 id) const;

    /// Adds the record of `id`; an id already present is replaced.
    ///
    /// - Ownership: borrowed; valid until the next change.
    /// - Fails: never.
    TaskRecord* Insert(u64 id);

    /// Removes the record of `id`; an id that is not present is ignored.
    ///
    /// - Fails: never.
    void Erase(u64 id);

    /// Removes the records of the instances in `owners`.
    ///
    /// - Param owners: stable instance keys, sorted ascending.
    /// - Param removed_per_partition: receives, per partition, how many records were removed; grown as needed.
    /// - Ownership: borrows both for the call.
    /// - Fails: never.
    void EraseOwners(Span<const u64> owners, Vector<u32>* removed_per_partition);

    /// How many records the table holds.
    ///
    /// - Fails: never.
    usize Size() const { return live_; }

private:
    struct Slot
    {
        u64 id = 0;
        bool live = false;
        TaskRecord record;
    };

    Vector<Slot> slots_;
    Vector<u32> free_;
    HashMap<u64, u32, HeapAllocator> index_;
    usize live_ = 0;
};

/// What `task.*` bindings reach in a partition's job: where to queue a request and what the partition may read.
///
/// - Thread-safety: thread-compatible: one job uses it.
/// - Ownership: borrows everything.
struct TaskPartition
{
    /// Where new requests go.
    TaskOutbox* outbox = nullptr;
    /// The finished tasks.
    const TaskTable* table = nullptr;
    /// The handles this partition took this tick (erased from the table at the sync point).
    Vector<u64>* taken = nullptr;
    /// The partition's number.
    u32 partition = 0;
    /// How many finished and queued tasks the partition holds now.
    u32 held = 0;
    /// The most tasks (queued and held) one partition may have (`script.task_queue_limit`).
    u32 queue_limit = 1024;
    /// The most bytes of arguments one request may have, and one partition may queue in a tick
    /// (`script.task_arg_bytes`).
    u64 arg_bytes_limit = 0;
    /// The tick's number, for the tasks' own bookkeeping.
    u64 tick = 0;
};

/// The weight of a value in bytes, as the task limits count it (nested values add up).
///
/// - Fails: never.
u64 TaskValueBytes(const Value& value);

/// What taking a task's result found.
enum class TakeStatus : u8
{
    /// The task is queued in this tick and has not run.
    Pending,
    /// The task finished; the value is its result.
    Ready,
    /// The task failed; the value is the error text.
    Failed,
    /// No such task: never made, already taken, or dropped with its owner.
    Unknown,
    /// The task belongs to another instance.
    NotOwner,
};

/// Takes the result of task `id` for the instance `owner_key`: copies it into `out` and marks it taken (the record is
/// erased at the sync point), so a second take finds nothing.
///
/// - Param view: the partition's task access; not null.
/// - Param out: receives the result, or the error text of a failed task; not null.
/// - Returns: what was found.
/// - Ownership: borrows `view` for the call; `out` receives a copy.
/// - Fails: returns `Unknown`, `NotOwner` or `Pending` as described.
TakeStatus TakeTask(TaskPartition* view, u64 owner_key, u64 id, Value* out);

/// What one request produced.
///
/// - Thread-safety: thread-compatible plain data.
struct TaskOutcome
{
    /// Whether the task failed.
    bool failed = false;
    /// The result.
    Value value;
    /// The error text of a failure.
    String<HeapAllocator> error{HeapAllocator::Default()};
};

/// What a task pool is made of.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: borrows the pointers; they must outlive the pool.
struct TaskPoolConfig
{
    /// The language; not null.
    Backend* backend = nullptr;
    /// The bindings; the pool installs only the `pure` ones.
    const BindingRegistry* bindings = nullptr;
    /// How many states the pool has (at least 1).
    u32 states = 1;
    /// The memory cap of one state; 0 is the backend's default.
    u64 state_memory = 0;
    /// The limits of one chunk's call.
    Budget budget;
    /// The source of the task host; empty uses [[script::LuaTaskHostSource]].
    StringView host_source;
};

/// The Lua source of the task host: `__task_define`, `__task_run`, the restoring of the globals after a task and the
/// `math.random`-based `random` table of a task.
///
/// - Returns: a string literal.
/// - Ownership: static; not freed.
/// - Fails: never.
const char* LuaTaskHostSource();

/// A pool of language states for tasks, created when the first task runs.
///
/// - Thread-safety: thread-compatible: `Execute` is called by one thread at a time; inside it the states run as jobs.
/// - Ownership: owns the states.
class TaskPool
{
public:
    /// Makes the pool; the states are made when first used.
    explicit TaskPool(const TaskPoolConfig& config);
    /// Destroys the states.
    ~TaskPool();
    TaskPool(const TaskPool&) = delete;
    TaskPool& operator=(const TaskPool&) = delete;

    /// Runs the requests and fills `outcomes` (one per request, in order). A `Map` or `For` request is cut into chunks
    /// that run on the pool's states at once; the result is assembled in index order, so it does not depend on which
    /// state or thread ran a chunk.
    ///
    /// - Param requests: the requests; read only.
    /// - Param outcomes: replaced.
    /// - Param scheduler: the job scheduler whose participant 0 is the calling thread, outside any job; null (or one
    /// participant, or inside a job) runs everything on this thread.
    /// - Returns: how many chunks ran.
    /// - Ownership: borrows `requests` and `scheduler` for the call; the outcomes own their values.
    /// - Fails: a failing task is reported in its outcome, never as a return value.
    usize Execute(Span<const TaskRequest> requests, Vector<TaskOutcome>* outcomes, jobs::Scheduler* scheduler);

    /// The number of states.
    ///
    /// - Fails: never.
    u32 StateCount() const { return config_.states; }

    /// How many state resets the pool has seen (a state that ran out of memory is replaced).
    ///
    /// - Fails: never.
    u64 Resets() const;

private:
    struct Impl;
    TaskPoolConfig config_;
    UniquePtr<Impl> impl_;
};

}  // namespace vse::script
