#pragma once

//! Systems and the sequential scheduler: functions over the world that declare what they touch, ordered by stage and
//! constraints, with their structural changes deferred to the end of the stage.

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "ecs/command_buffer.h"
#include "ecs/component.h"
#include "ecs/component_set.h"
#include "ecs/query.h"
#include "ecs/world.h"
#include "jobs/counter.h"
#include "jobs/job.h"
#include "jobs/parallel_for.h"
#include "jobs/scheduler.h"
#include "lang/new.h"
#include "lang/traits.h"
#include "lang/utility.h"
#include "log/assert.h"
#include "mem/heap_allocator.h"
#include "settings/registry.h"
#include "settings/setting.h"
#include "types.h"

namespace vse::ecs
{

/// Declares `ecs.parallel_systems` (bool, default true: used by the parallel scheduler) and `ecs.stats` (bool, default
/// false: per-system statistics). Each has an environment variable and a flag.
///
/// - Param registry: the registry to declare them in.
/// - Fails: a key already registered is left as it is.
/// - Ownership: borrows `registry` for the call; the descriptions are string literals.
void RegisterEcsSettings(settings::Registry* registry);

/// A stage of the frame; systems run stage by stage, in the order the stages were made.
using StageId = u32;
/// The first stage.
inline constexpr StageId kPreUpdate = 0;
/// The second stage: the default of a system.
inline constexpr StageId kUpdate = 1;
/// The third stage.
inline constexpr StageId kPostUpdate = 2;
/// The fourth stage.
inline constexpr StageId kRender = 3;

namespace detail
{
template <class Fn, class... Terms> class QueryRunner;
}  // namespace detail

/// What a running system sees of the world: queries and resources it declared, a command buffer of its own, the frame's
/// time step and number. It has no structural calls; record them in `Commands()`, they apply at the end of the stage.
///
/// A query or resource outside the declared access is reported with `ENGINE_ENSURE` and counted in
/// `ScheduleStats::access_violations`.
///
/// - Thread-safety: thread-compatible; one system uses it at a time.
/// - Ownership: borrows the world and the system's buffer for the duration of the call.
class SystemContext
{
public:
    /// Makes a context; only the schedule does.
    ///
    /// - Ownership: borrows every pointer; they must outlive the context.
    SystemContext(World* world, CommandBuffer* commands, const ComponentSet* reads, const ComponentSet* writes,
                  const Vector<u32>* resource_reads, const Vector<u32>* resource_writes, u64* violations,
                  CommandBufferPool* parallel_commands, jobs::Scheduler* scheduler, f32 dt, u64 frame)
        : world_(world), commands_(commands), reads_(reads), writes_(writes), resource_reads_(resource_reads),
          resource_writes_(resource_writes), violations_(violations), parallel_commands_(parallel_commands),
          scheduler_(scheduler), dt_(dt), frame_(frame)
    {
    }

    /// The time step of the frame, in seconds.
    /// - Fails: never.
    f32 Dt() const { return dt_; }
    /// The frame number (0 for the first `Schedule::Run`).
    /// - Fails: never.
    u64 Frame() const { return frame_; }
    /// The system's command buffer: structural changes recorded here apply at the end of the stage, in registration
    /// order.
    /// - Fails: never.
    /// - Ownership: owned by the schedule; valid for the call.
    CommandBuffer& Commands() { return *commands_; }

    /// Calls `fn(ChunkView&, CommandBuffer&)` for each chunk of `query` that passes its filters, the chunks split into
    /// ranges that run as jobs when this system has the frame thread to itself (it is alone in its wave and the
    /// schedule has a scheduler); otherwise the ranges run one after another. Each range records into its own command
    /// buffer, applied after the system in chunk order, so the result does not depend on how the jobs were scheduled.
    ///
    /// - Param query: the system's query (from `MakeQuerySystem`).
    /// - Param fn: called per chunk; must not change the world's structure except through the buffer it is given.
    /// - Param grain: chunks per range; 0 chooses a few ranges per worker.
    /// - Aborts: as for [[ecs::Query::ForEach]].
    template <class Q, class Fn> void ForEachChunkParallel(Q& query, Fn&& fn, usize grain = 0)
    {
        const usize chunks = query.ChunkCountUpperBound();
        const usize participants = scheduler_ == nullptr ? 1 : scheduler_->ParticipantCount();
        usize g = grain;
        if (g == 0)
        {
            const usize wanted = participants * 4;
            g = chunks / wanted + (chunks % wanted != 0 ? 1 : 0);
            g = g == 0 ? 1 : g;
        }
        parallel_commands_->EnsureCount(static_cast<u32>(Q::RangesFor(chunks, g) == 0 ? 1 : Q::RangesFor(chunks, g)));
        query.ForEachChunkParallel(scheduler_, *parallel_commands_, fn, g);
    }
    /// Folds one value per chunk of `query` in chunk order (see [[ecs::Query::ReduceChunks]]); parallel under the same
    /// conditions as `ForEachChunkParallel`.
    ///
    /// - Returns: the folded value.
    template <class T, class Q, class PerChunk, class Combine>
    T ReduceChunks(Q& query, T init, PerChunk&& per_chunk, Combine&& combine, usize grain = 0)
    {
        return query.template ReduceChunks<T>(scheduler_, init, per_chunk, combine, grain);
    }

    /// Makes a query over the world. Prefer the query a `MakeQuerySystem` passes in: it is kept between frames.
    ///
    /// - Fails: a `Write` of a component not declared as written, or a read of one not declared, counts a violation
    /// (`ENGINE_ENSURE`).
    template <class... Terms> ecs::Query<Terms...> Query()
    {
        (CheckTerm<Terms>(), ...);
        return ecs::Query<Terms...>(*world_);
    }

    /// The world's resource of type `T`, for writing.
    /// - Fails: `T` was not declared with `WritesResource` counts a violation.
    /// - Aborts: the world has no such resource.
    template <class T> T& Resource()
    {
        Check(Declared(*resource_writes_, detail::ResourceTypeId<T>()),
              "SystemContext::Resource: the system did not declare WritesResource for this type");
        return world_->Resource<T>();
    }
    /// The world's resource of type `T`, for reading.
    /// - Fails: `T` was declared with neither `ReadsResource` nor `WritesResource` counts a violation.
    /// - Aborts: the world has no such resource.
    template <class T> const T& ReadResource()
    {
        const u32 id = detail::ResourceTypeId<T>();
        Check(Declared(*resource_reads_, id) || Declared(*resource_writes_, id),
              "SystemContext::ReadResource: the system did not declare this resource");
        return static_cast<const World*>(world_)->Resource<T>();
    }
    /// The world itself, for a system that declared [[ecs::SystemDesc::WritesAllComponents]] (it runs alone): it may
    /// begin a parallel phase, run jobs and apply structural changes of its own.
    ///
    /// - Fails: a system that did not declare `WritesAllComponents` counts a violation.
    /// - Ownership: borrowed; valid for the call.
    World& ExclusiveWorld()
    {
        Check(writes_->words[0] == ~u64{0} && writes_->words[1] == ~u64{0},
              "SystemContext::ExclusiveWorld: the system did not declare WritesAllComponents");
        return *world_;
    }
    /// The job scheduler this system may fan work out to: set only when the system is alone in its wave and the
    /// schedule has a scheduler with several participants; null otherwise (then run the work on the calling thread).
    /// - Fails: null, as described.
    /// - Ownership: borrowed; valid for the call.
    jobs::Scheduler* JobScheduler() const { return scheduler_; }
    /// Returns whether `entity` is alive.
    /// - Fails: never.
    bool Exists(Entity entity) const { return world_->Exists(entity); }
    /// A number identifying the world this frame runs on (see [[ecs::World::Id]]): a system that keeps queries rebuilds
    /// them when it changes.
    /// - Fails: never.
    u64 WorldId() const { return world_->Id(); }
    /// The number of entities destroyed in this world so far. A system can notice that some were destroyed since its
    /// last run by comparing it with the value it saw then.
    /// - Fails: never.
    u64 DestroyedCount() const { return world_->Stats().destroyed; }
    /// The current change-detection tick.
    /// - Fails: never.
    u64 Tick() const { return world_->Tick(); }

    /// One entity's component for reading (see [[ecs::World::Peek]]): for a system that reads other entities' rows than
    /// the ones its query writes.
    ///
    /// - Returns: the component, or null (dead entity, missing component).
    /// - Fails: `T` declared neither read nor written counts a violation; returns null as described.
    /// - Ownership: views the entity's row; valid until the next structural call.
    template <class T> const T* Peek(Entity entity)
    {
        const ComponentId id = ComponentOf<T>();
        Check(reads_->Has(id) || writes_->Has(id), "SystemContext::Peek of a component the system did not declare");
        return static_cast<const World*>(world_)->Peek<T>(entity);
    }
    /// How many removals of the tracked component `T` the world's log holds (see [[ecs::World::RemovedCount]]).
    ///
    /// - Fails: `T` declared neither read nor written counts a violation.
    template <class T> usize RemovedCount()
    {
        const ComponentId id = ComponentOf<T>();
        Check(reads_->Has(id) || writes_->Has(id),
              "SystemContext::RemovedCount of a component the system did not declare");
        return static_cast<const World*>(world_)->RemovedCount<T>();
    }
    /// The `index`th logged removal of `T` (see [[ecs::World::RemovedAt]]).
    ///
    /// - Aborts: `index` is not below `RemovedCount<T>()`.
    template <class T> RemovedComponent<T> RemovedAt(usize index) const
    {
        return static_cast<const World*>(world_)->RemovedAt<T>(index);
    }
    /// One entity's component for writing without stamping (see [[ecs::World::Poke]]); call `Stamp` afterwards from one
    /// thread.
    ///
    /// - Returns: the component, or null.
    /// - Fails: `T` not declared as written counts a violation; returns null as described.
    /// - Ownership: views the entity's row; valid until the next structural call.
    template <class T> T* Poke(Entity entity)
    {
        Check(writes_->Has(ComponentOf<T>()),
              "SystemContext::Poke of a component the system did not declare as written");
        return world_->Poke<T>(entity);
    }
    /// Stamps the entity's chunk column of `T` as written now. Call from one thread at a time.
    /// - Fails: `T` not declared as written counts a violation.
    template <class T> void Stamp(Entity entity)
    {
        Check(writes_->Has(ComponentOf<T>()),
              "SystemContext::Stamp of a component the system did not declare as written");
        world_->Stamp<T>(entity);
    }
    /// Calls `fn(begin, end)` over `[0, count)` in ranges of `grain` as jobs when this system has the frame thread to
    /// itself, else one range after another.
    ///
    /// - Param fn: an lvalue callable taking two `usize`; the ranges run concurrently, so they must not touch the same
    /// data.
    /// - Param grain: items per range; 0 chooses a few ranges per worker.
    template <class F> void ParallelFor(usize count, usize grain, F& fn)
    {
        if (count == 0)
        {
            return;
        }
        const usize participants = scheduler_ == nullptr ? 1 : scheduler_->ParticipantCount();
        usize g = grain;
        if (g == 0)
        {
            const usize wanted = participants * 4;
            g = count / wanted + (count % wanted != 0 ? 1 : 0);
            g = g == 0 ? 1 : g;
        }
        const usize ranges = count / g + (count % g != 0 ? 1 : 0);
        auto by_range = [&](usize first, usize last)
        {
            for (usize r = first; r < last; ++r)
            {
                const usize begin = r * g;
                fn(begin, begin + g < count ? begin + g : count);
            }
        };
        if (scheduler_ != nullptr && participants > 1 && ranges > 1 && !jobs::Scheduler::InsideJob() &&
            jobs::Scheduler::Current() == scheduler_)
        {
            jobs::ParallelForBlocking(*scheduler_, ranges, 1, by_range);
        }
        else
        {
            by_range(usize{0}, ranges);
        }
    }

private:
    template <class Fn, class... Terms> friend class detail::QueryRunner;
    World& WorldForQuery() { return *world_; }
    static bool Declared(const Vector<u32>& list, u32 id)
    {
        for (usize i = 0; i < list.Size(); ++i)
        {
            if (list[i] == id)
            {
                return true;
            }
        }
        return false;
    }
    void Check(bool ok, const char*)
    {
        if (!ok)
        {
            ++*violations_;
        }
        ENGINE_ENSURE(ok, "a system touched something it did not declare");
    }
    template <class Term> void CheckTerm()
    {
        using Traits = detail::TermTraits<Term>;
        if constexpr (Traits::kExcluded)
        {
            return;
        }
        else
        {
            const ComponentId id = ComponentOf<typename Traits::Component>();
            if constexpr (Traits::kWrites)
            {
                Check(writes_->Has(id), "a query writes a component the system did not declare as written");
            }
            else if constexpr (Traits::kHasArg || Traits::kChange != 0)
            {
                Check(reads_->Has(id) || writes_->Has(id), "a query reads a component the system did not declare");
            }
        }
    }

    World* world_;
    CommandBuffer* commands_;
    const ComponentSet* reads_;
    const ComponentSet* writes_;
    const Vector<u32>* resource_reads_;
    const Vector<u32>* resource_writes_;
    u64* violations_;
    CommandBufferPool* parallel_commands_;
    jobs::Scheduler* scheduler_;
    f32 dt_;
    u64 frame_;
};

namespace detail
{

/// A type-erased owner of a system's function: stored inline up to `kInline` bytes, else on the heap (counted in
/// `HeapFallbacks`).
class SystemFn
{
public:
    /// The inline capacity in bytes.
    static constexpr usize kInline = 64;

    SystemFn() = default;
    SystemFn(const SystemFn&) = delete;
    SystemFn& operator=(const SystemFn&) = delete;
    SystemFn(SystemFn&& other) noexcept { MoveFrom(other); }
    SystemFn& operator=(SystemFn&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            MoveFrom(other);
        }
        return *this;
    }
    ~SystemFn() { Reset(); }

    /// Wraps a callable `void(SystemContext&, Extra...)`; see `Invoke`.
    template <class F>
        requires(!IsSame<RemoveReference<F>, SystemFn>)
    explicit SystemFn(F&& callable)
    {
        using Callable = RemoveReference<F>;
        ops_ = &OpsOf<Callable>::value;
        if constexpr (sizeof(Callable) <= kInline && alignof(Callable) <= 16)
        {
            ConstructAt<Callable>(storage_, Move(callable));
            heap_ = nullptr;
        }
        else
        {
            heap_ = HeapAllocator::Default().Alloc(sizeof(Callable), alignof(Callable) < 16 ? 16 : alignof(Callable));
            ENGINE_ASSERT(heap_ != nullptr, "SystemFn: out of memory");
            ConstructAt<Callable>(static_cast<Callable*>(heap_), Move(callable));
            ++Fallbacks();
        }
    }

    /// Returns whether a callable is held.
    bool Valid() const { return ops_ != nullptr; }
    /// Calls it.
    void Invoke(SystemContext& context) { ops_->invoke(Object(), context); }
    /// Adds the held system's query counters to `out` and zeroes them; false when the callable has no query.
    bool TakeStats(QueryStats* out) { return ops_->take_stats != nullptr && ops_->take_stats(Object(), out); }

    /// The number of callables too big for the inline buffer, since the program started.
    ///
    /// - Internal: defined in `schedule.cpp`, so that the count exists once in the engine library.
    static u64& Fallbacks();

private:
    struct Ops
    {
        void (*invoke)(void*, SystemContext&);
        void (*move)(void* to, void* from);
        void (*destroy)(void*);
        bool (*take_stats)(void*, QueryStats*);
        usize size;
        usize align;
    };
    template <class Callable> struct OpsOf
    {
        static bool TakeStatsOf(void* object, QueryStats* out)
        {
            if constexpr (requires(Callable& c) { c.TakeStats(out); })
            {
                return static_cast<Callable*>(object)->TakeStats(out);
            }
            else
            {
                return false;
            }
        }
        static constexpr Ops value = {
            [](void* object, SystemContext& context) { (*static_cast<Callable*>(object))(context); },
            [](void* to, void* from)
            { ConstructAt<Callable>(static_cast<Callable*>(to), Move(*static_cast<Callable*>(from))); },
            [](void* object) { static_cast<Callable*>(object)->~Callable(); },
            &TakeStatsOf,
            sizeof(Callable),
            alignof(Callable) < 16 ? usize{16} : alignof(Callable),
        };
    };

    void* Object() { return heap_ != nullptr ? heap_ : static_cast<void*>(storage_); }
    void MoveFrom(SystemFn& other)
    {
        ops_ = other.ops_;
        heap_ = other.heap_;
        if (ops_ != nullptr && heap_ == nullptr)
        {
            ops_->move(storage_, other.storage_);
            ops_->destroy(other.storage_);
        }
        other.ops_ = nullptr;
        other.heap_ = nullptr;
    }
    void Reset()
    {
        if (ops_ == nullptr)
        {
            return;
        }
        ops_->destroy(Object());
        if (heap_ != nullptr)
        {
            HeapAllocator::Default().Free(heap_, ops_->size, ops_->align);
        }
        ops_ = nullptr;
        heap_ = nullptr;
    }

    const Ops* ops_ = nullptr;
    void* heap_ = nullptr;
    alignas(16) byte storage_[kInline] = {};
};

// The callable of a query system: owns the query (made on the first frame, when the world is known) and calls the
// user's function with it.
template <class Fn, class... Terms> class QueryRunner
{
public:
    explicit QueryRunner(Fn fn) : fn_(Move(fn)) {}
    QueryRunner(QueryRunner&&) = default;
    void operator()(SystemContext& context)
    {
        World& world = context.WorldForQuery();
        if (query_ == nullptr || world_id_ != world.Id())
        {
            // The first frame, or the schedule now runs another world: the cached matches belong to the old one.
            query_ = MakeUnique<ecs::Query<Terms...>>(world);
            world_id_ = world.Id();
        }
        fn_(context, *query_);
    }
    bool TakeStats(QueryStats* out)
    {
        if (query_ == nullptr)
        {
            return false;
        }
        *out = query_->Stats();
        query_->ResetStats();
        return true;
    }

private:
    Fn fn_;
    UniquePtr<ecs::Query<Terms...>> query_;
    u64 world_id_ = 0;
};

template <class Term> void DeclareTerm(ComponentSet* reads, ComponentSet* writes)
{
    using Traits = TermTraits<Term>;
    if constexpr (!Traits::kExcluded)
    {
        const ComponentId id = ComponentOf<typename Traits::Component>();
        if (id == kInvalidComponent)
        {
            return;
        }
        if constexpr (Traits::kWrites)
        {
            writes->Add(id);
        }
        else if constexpr (Traits::kHasArg || Traits::kChange != 0)
        {
            reads->Add(id);
        }
    }
}

}  // namespace detail

/// A system: a name, a function, what it reads and writes, its stage, ordering constraints and an optional run
/// condition. Built by [[ecs::MakeSystem]] or [[ecs::MakeQuerySystem]], refined with the chained calls, and given to
/// [[ecs::Schedule::Add]].
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns its function (`MakeSystem` moves the callable in); the name and the names given to `After` and
/// `Before` are borrowed and must outlive
///   the schedule (use string literals).
class SystemDesc
{
public:
    /// The most `After` and `Before` names of one system.
    static constexpr u32 kMaxConstraints = 8;

    SystemDesc() = default;
    /// Makes a system from a name and a stored function; use [[ecs::MakeSystem]] or [[ecs::MakeQuerySystem]].
    ///
    /// - Ownership: takes the function; borrows `name`.
    SystemDesc(const char* name, detail::SystemFn fn) : name_(name), fn_(Move(fn)) {}
    SystemDesc(SystemDesc&&) = default;
    SystemDesc& operator=(SystemDesc&&) = default;

    /// Declares the access of a query's terms (what `MakeQuerySystem` does).
    /// - Fails: never.
    template <class... Terms> void DeduceAccess() { (detail::DeclareTerm<Terms>(&reads_, &writes_), ...); }
    /// Declares components the system reads.
    /// - Fails: never.
    template <class... Ts> SystemDesc&& Reads() &&
    {
        (reads_.Add(ComponentOf<Ts>()), ...);
        return Move(*this);
    }
    /// Declares components the system writes (a write includes reading).
    /// - Fails: never.
    template <class... Ts> SystemDesc&& Writes() &&
    {
        (writes_.Add(ComponentOf<Ts>()), ...);
        return Move(*this);
    }
    /// Declares that the system may read every component, including ones registered after this call (a system that
    /// runs scripts, which name components at run time).
    /// - Fails: never.
    SystemDesc&& ReadsAllComponents() &&
    {
        reads_.words[0] = ~u64{0};
        reads_.words[1] = ~u64{0};
        return Move(*this);
    }
    /// Declares that the system may write every component, so it runs alone in its stage's wave and may use
    /// [[ecs::SystemContext::ExclusiveWorld]].
    /// - Fails: never.
    SystemDesc&& WritesAllComponents() &&
    {
        writes_.words[0] = ~u64{0};
        writes_.words[1] = ~u64{0};
        return Move(*this);
    }
    /// Declares resources the system reads.
    /// - Fails: never.
    template <class... Ts> SystemDesc&& ReadsResource() &&
    {
        (resource_reads_.Push(detail::ResourceTypeId<Ts>()), ...);
        return Move(*this);
    }
    /// Declares resources the system writes.
    /// - Fails: never.
    template <class... Ts> SystemDesc&& WritesResource() &&
    {
        (resource_writes_.Push(detail::ResourceTypeId<Ts>()), ...);
        return Move(*this);
    }
    /// Puts the system in a stage (the default is [[ecs::kUpdate]]).
    /// - Fails: never.
    SystemDesc&& InStage(StageId stage) &&
    {
        stage_ = stage;
        return Move(*this);
    }
    /// Runs the system after the named system of the same stage.
    /// - Aborts: more than [[ecs::SystemDesc::kMaxConstraints]] names.
    SystemDesc&& After(const char* name) &&
    {
        ENGINE_ASSERT(after_count_ < kMaxConstraints, "SystemDesc::After: too many constraints");
        after_[after_count_++] = name;
        return Move(*this);
    }
    /// Runs the system before the named system of the same stage.
    /// - Aborts: more than [[ecs::SystemDesc::kMaxConstraints]] names.
    SystemDesc&& Before(const char* name) &&
    {
        ENGINE_ASSERT(before_count_ < kMaxConstraints, "SystemDesc::Before: too many constraints");
        before_[before_count_++] = name;
        return Move(*this);
    }
    /// Runs the system only in frames where `condition` returns true.
    /// - Fails: never.
    SystemDesc&& RunIf(bool (*condition)(SystemContext&)) &&
    {
        condition_ = condition;
        return Move(*this);
    }

private:
    friend class Schedule;
    const char* name_ = nullptr;
    detail::SystemFn fn_;
    ComponentSet reads_;
    ComponentSet writes_;
    Vector<u32> resource_reads_;
    Vector<u32> resource_writes_;
    StageId stage_ = kUpdate;
    const char* after_[kMaxConstraints] = {};
    const char* before_[kMaxConstraints] = {};
    u32 after_count_ = 0;
    u32 before_count_ = 0;
    bool (*condition_)(SystemContext&) = nullptr;
};

/// Makes a system from a function `void(SystemContext&)`; declare what it touches with `Reads`, `Writes`,
/// `ReadsResource`, `WritesResource`.
///
/// - Param name: a string literal naming the system.
/// - Param fn: a callable; moved into the system.
template <class Fn> SystemDesc MakeSystem(const char* name, Fn fn)
{
    return SystemDesc(name, detail::SystemFn(Move(fn)));
}

/// Makes a system from a function `void(SystemContext&, Query<Terms...>&)`; the access is deduced from the terms
/// (`Read`, `Optional`, `Changed` and `Added` are reads, `Write` and `OptionalWrite` are writes) and the query is made
/// once and kept.
///
/// - Param name: a string literal naming the system.
/// - Param fn: a callable; moved into the system.
template <class... Terms, class Fn> SystemDesc MakeQuerySystem(const char* name, Fn fn)
{
    SystemDesc desc(name, detail::SystemFn(detail::QueryRunner<Fn, Terms...>(Move(fn))));
    desc.DeduceAccess<Terms...>();
    return desc;
}

/// The counters of one system.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing (`name` is the borrowed literal).
struct SystemStats
{
    /// The system's name.
    const char* name = nullptr;
    /// Times it ran (statistics on).
    u64 runs = 0;
    /// Times its run condition said no.
    u64 skipped_runs = 0;
    /// Nanoseconds of the last run.
    u64 last_ns = 0;
    /// Nanoseconds over all runs.
    u64 total_ns = 0;
    /// Entities in the chunks its query visited, in the last run (query systems only).
    u64 entities = 0;
    /// Chunks its query visited, in the last run.
    u64 chunks_visited = 0;
    /// Chunks its `Changed`/`Added` filters skipped, in the last run.
    u64 chunks_skipped = 0;
    /// The average run time in nanoseconds (0 before the first run).
    u64 AverageNanos() const { return runs == 0 ? 0 : total_ns / runs; }
};

/// What `Build` says.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct BuildReport
{
    /// True when the order was made.
    bool ok = false;
    /// A sentence naming the problem (an unknown or duplicated name, a cycle with its path, a constraint against the
    /// stage order), or empty.
    char message[256] = {};
};

/// Schedule-wide counters.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct ScheduleStats
{
    /// Frames run.
    u64 frames = 0;
    /// Systems that touched something they did not declare, counted at each touch.
    u64 access_violations = 0;
    /// Commands applied after stages (all frames).
    u64 commands_applied = 0;
    /// Commands skipped when applied (dead entity, component present or missing).
    u64 commands_ignored = 0;
};

/// The systems of a program, ordered, and the frame loop that runs them on one thread.
///
/// `Add` systems, `Build` once (a stable topological order: stages in the order made, within a stage the constraints,
/// ties by registration order; a cycle is rejected with its path), then `Run` once per frame: each stage runs its
/// systems in order, then applies their command buffers in registration order; the frame ends with `World::Advance`. A
/// system that records `Destroy` of an entity another system `Add`s to in the same stage: the buffers apply in
/// registration order, so the earlier system's commands take effect first and a later command on the dead entity is
/// skipped and counted.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns the systems and their buffers; borrows the settings registry given to `UseSettings`.
class Schedule
{
public:
    /// Makes a schedule with the four default stages.
    Schedule();
    Schedule(const Schedule&) = delete;
    Schedule& operator=(const Schedule&) = delete;

    /// Adds a stage after `after` and returns its id (stages run in list order).
    /// - Param name: a string literal.
    /// - Aborts: `after` is not a stage.
    StageId AddStage(const char* name, StageId after);
    /// Adds a system.
    /// - Aborts: its stage does not exist, or it has no function.
    void Add(SystemDesc system);
    /// Orders the systems. Must succeed before `Run`; call again after `Add`.
    /// - Returns: the report; on failure the previous order (if any) stays.
    BuildReport Build();

    /// Reads `ecs.parallel_systems` and `ecs.stats` from `registry` at the start of each frame (see
    /// [[ecs::RegisterEcsSettings]]).
    /// - Aborts: the settings are not registered.
    /// - Ownership: borrows `registry`.
    void UseSettings(const settings::Registry& registry);
    /// Gives the schedule a job scheduler: systems that do not conflict then run as jobs, and a system alone in its
    /// wave may split its chunks over the workers. With none (the default), or with `ecs.parallel_systems` off, every
    /// system runs on the calling thread in the sequential order. `Run` must then be called on the thread that owns the
    /// scheduler (participant 0), outside any job.
    /// - Param scheduler: or null.
    /// - Ownership: borrows `scheduler`, which must outlive every `Run`.
    void SetScheduler(jobs::Scheduler* scheduler) { scheduler_ = scheduler; }
    /// Turns statistics on or off by hand (when no registry is used); takes effect at the next frame.
    void SetStatsEnabled(bool enabled) { stats_enabled_ = enabled; }

    /// Runs one frame.
    /// - Param dt: the time step in seconds.
    /// - Aborts: the schedule was not built since the last `Add`, or the world is being iterated.
    void Run(World& world, f32 dt);

    /// The wave of the `index`-th system in run order (after `Build`): systems of one stage with the same wave number
    /// do not conflict and may run together; a system never runs in an earlier wave than a system before it that
    /// conflicts with it or must precede it.
    /// - Aborts: `index` is not below the count or the schedule is not built.
    u32 WaveOf(u32 index) const;
    /// The number of systems.
    u32 SystemCount() const { return static_cast<u32>(systems_.Size()); }
    /// The names of the systems in run order (after `Build`): the name of the `index`-th system to run.
    /// - Aborts: `index` is not below the count or the schedule is not built.
    /// - Ownership: the name is the literal given to the system.
    const char* RunOrderName(u32 index) const;
    /// The counters of the system with this name, or null.
    /// - Fails: returns null when there is none.
    /// - Ownership: the pointer views the schedule's counters; valid until the next `Add`.
    const SystemStats* StatsOf(const char* name) const;
    /// The schedule-wide counters.
    const ScheduleStats& Stats() const { return stats_; }
    /// Whether the `ecs.parallel_systems` setting asked for parallel systems at the last frame (used by the parallel
    /// scheduler; the sequential one ignores it).
    /// - Fails: never.
    bool ParallelRequested() const { return parallel_requested_; }
    /// The number of system functions stored on the heap because they did not fit the inline buffer (since the program
    /// started).
    static u64 HeapFallbacks() { return detail::SystemFn::Fallbacks(); }

private:
    struct Entry
    {
        Entry() : parallel_commands(1) {}
        SystemDesc desc;
        CommandBuffer commands;
        CommandBufferPool parallel_commands;
        SystemStats stats;
        u64 violations = 0;
    };

    static bool Conflict(const SystemDesc& a, const SystemDesc& b);
    void RunSystem(World& world, Entry& entry, f32 dt, jobs::Scheduler* fan_out, bool timed);
    static jobs::Job SystemJob(Schedule* self, World* world, Entry* entry, f32 dt, bool timed);
    void ApplyStage(World& world, usize begin, usize end);

    Vector<UniquePtr<Entry>> systems_;
    Vector<u32> order_;
    Vector<u32> wave_of_;      // per position in order_
    Vector<u32> apply_order_;  // order_ with each stage's systems sorted by registration index
    jobs::Scheduler* scheduler_ = nullptr;
    Vector<u32> stage_order_;
    Vector<const char*> stage_names_;
    bool built_ = false;
    bool stats_enabled_ = false;
    bool parallel_requested_ = true;
    bool has_settings_ = false;
    settings::Setting<bool> stats_setting_;
    settings::Setting<bool> parallel_setting_;
    ScheduleStats stats_;
};

}  // namespace vse::ecs
