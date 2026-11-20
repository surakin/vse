#pragma once

//! Typed queries: the entities whose archetype has certain components, visited one by one or one chunk at a time.

#include "containers/span.h"
#include "containers/vector.h"
#include "ecs/archetype.h"
#include "ecs/chunk.h"
#include "ecs/command_buffer.h"
#include "ecs/component.h"
#include "ecs/component_set.h"
#include "ecs/entity.h"
#include "ecs/index_list.h"
#include "ecs/world.h"
#include "jobs/parallel_for.h"
#include "jobs/scheduler.h"
#include "lang/traits.h"
#include "log/assert.h"
#include "types.h"

namespace vse::ecs
{

/// A query term: the entities must have `T`; the query function gets it as `const T&`. `T` may not be a tag (use
/// [[ecs::With]]).
///
/// - Thread-safety: thread-safe empty type; it only names a component.
template <class T> struct Read
{
};
/// A query term: the entities must have `T`; the query function gets it as `T&`, and the query holds `T` for writing.
/// `T` may not be a tag.
///
/// - Thread-safety: thread-safe empty type; it only names a component.
template <class T> struct Write
{
};
/// A query term: the entities may have `T`; the query function gets a `const T*`, null when the archetype lacks it. `T`
/// may not be a tag.
///
/// - Thread-safety: thread-safe empty type; it only names a component.
template <class T> struct Optional
{
};
/// A query term: like [[ecs::Optional]] but writable: the query function gets a `T*`, null when the archetype lacks it.
///
/// - Thread-safety: thread-safe empty type; it only names a component.
template <class T> struct OptionalWrite
{
};
/// A query term: like [[ecs::Write]] (the function gets `T&`, the query holds `T` for writing) but visiting a chunk
/// does not stamp it as written: the function writes only when it needs to and calls `ChunkView::MarkWritten<T>()`
/// after a real change, so `Changed<T>` sees exact changes. `T` may not be a tag.
///
/// - Thread-safety: thread-safe empty type; it only names a component.
template <class T> struct LazyWrite
{
};
/// A query term: the entities must have `T`, and the query function gets nothing for it (a filter). `T` may be a tag.
///
/// - Thread-safety: thread-safe empty type; it only names a component.
template <class T> struct With
{
};
/// A query term: the entities must not have `T` (a filter, no argument). `T` may be a tag.
///
/// - Thread-safety: thread-safe empty type; it only names a component.
template <class T> struct Without
{
};

/// A query filter: the entities must have `T`, and only chunks whose `T` column was written (by a `Write` query, `Set`,
/// `Add` or a migration) after the query's `since` tick are visited. The query function gets nothing for it; combine it
/// with `Read<T>`/`Write<T>` for the data. `T` may not be a tag. The precision is a chunk: one write marks every row of
/// its chunk.
///
/// - Thread-safety: thread-safe empty type; it only names a component.
template <class T> struct Changed
{
};
/// A query filter: the entities must have `T`, and only chunks that received a row with `T` (a created entity, or one
/// that gained `T` or migrated in) after the query's `since` tick are visited. No function argument. `T` may not be a
/// tag. The precision is a chunk.
///
/// - Thread-safety: thread-safe empty type; it only names a component.
template <class T> struct Added
{
};

namespace detail
{

template <class... Ts> struct TypeList
{
};

/// What a term is: its component, whether it hands the query function an argument, and how that argument is read from a
/// column.
template <class Term> struct TermTraits;

template <class T> struct TermTraits<Read<T>>
{
    using Component = T;
    using Arg = const T&;
    static constexpr bool kHasArg = true;
    static constexpr bool kRequired = true;
    static constexpr bool kExcluded = false;
    static constexpr bool kWrites = false;
    static constexpr bool kOptional = false;
    static constexpr u32 kChange = 0;
    static Arg Fetch(void* column, u32 row) { return static_cast<const T*>(column)[row]; }
};
template <class T> struct TermTraits<Write<T>>
{
    using Component = T;
    using Arg = T&;
    static constexpr bool kHasArg = true;
    static constexpr bool kRequired = true;
    static constexpr bool kExcluded = false;
    static constexpr bool kWrites = true;
    static constexpr bool kOptional = false;
    static constexpr u32 kChange = 0;
    static Arg Fetch(void* column, u32 row) { return static_cast<T*>(column)[row]; }
};
template <class T> struct TermTraits<LazyWrite<T>>
{
    using Component = T;
    using Arg = T&;
    static constexpr bool kHasArg = true;
    static constexpr bool kRequired = true;
    static constexpr bool kExcluded = false;
    static constexpr bool kWrites = true;
    static constexpr bool kOptional = false;
    static constexpr u32 kChange = 0;
    static Arg Fetch(void* column, u32 row) { return static_cast<T*>(column)[row]; }
};
template <class Term> inline constexpr bool kStampsOnVisit = true;
template <class T> inline constexpr bool kStampsOnVisit<LazyWrite<T>> = false;
template <class T> struct TermTraits<Optional<T>>
{
    using Component = T;
    using Arg = const T*;
    static constexpr bool kHasArg = true;
    static constexpr bool kRequired = false;
    static constexpr bool kExcluded = false;
    static constexpr bool kWrites = false;
    static constexpr bool kOptional = true;
    static constexpr u32 kChange = 0;
    static Arg Fetch(void* column, u32 row)
    {
        return column == nullptr ? nullptr : static_cast<const T*>(column) + row;
    }
};
template <class T> struct TermTraits<OptionalWrite<T>>
{
    using Component = T;
    using Arg = T*;
    static constexpr bool kHasArg = true;
    static constexpr bool kRequired = false;
    static constexpr bool kExcluded = false;
    static constexpr bool kWrites = true;
    static constexpr bool kOptional = true;
    static constexpr u32 kChange = 0;
    static Arg Fetch(void* column, u32 row) { return column == nullptr ? nullptr : static_cast<T*>(column) + row; }
};
template <class T> struct TermTraits<With<T>>
{
    using Component = T;
    static constexpr bool kHasArg = false;
    static constexpr bool kRequired = true;
    static constexpr bool kExcluded = false;
    static constexpr bool kWrites = false;
    static constexpr bool kOptional = false;
    static constexpr u32 kChange = 0;
};
template <class T> struct TermTraits<Without<T>>
{
    using Component = T;
    static constexpr bool kHasArg = false;
    static constexpr bool kRequired = false;
    static constexpr bool kExcluded = true;
    static constexpr bool kWrites = false;
    static constexpr bool kOptional = false;
    static constexpr u32 kChange = 0;
};

template <class T> struct TermTraits<Changed<T>>
{
    using Component = T;
    static constexpr bool kHasArg = false;
    static constexpr bool kRequired = true;
    static constexpr bool kExcluded = false;
    static constexpr bool kWrites = false;
    static constexpr bool kOptional = false;
    static constexpr u32 kChange = 1;
};
template <class T> struct TermTraits<Added<T>>
{
    using Component = T;
    static constexpr bool kHasArg = false;
    static constexpr bool kRequired = true;
    static constexpr bool kExcluded = false;
    static constexpr bool kWrites = false;
    static constexpr bool kOptional = false;
    static constexpr u32 kChange = 2;
};

/// What a term must not repeat: its component, except for the change filters, which may sit beside a data term of the
/// same component.
template <class Term>
using DistinctKey = Conditional<TermTraits<Term>::kChange != 0, Term, typename TermTraits<Term>::Component>;

template <class... Ts> struct AllDistinct : TrueType
{
};
template <class T, class... Rest>
struct AllDistinct<T, Rest...> : IntegralConstant<bool, (!IsSame<T, Rest> && ...) && AllDistinct<Rest...>::value>
{
};

/// A term that carries data (not a filter) may not name a tag: a tag has no column.
template <class Term>
inline constexpr bool kTermIsUsable = (!TermTraits<Term>::kHasArg && TermTraits<Term>::kChange == 0) ||
                                      !IsEmpty<typename TermTraits<Term>::Component>;

template <class List> struct ListSize;
template <class... Ts> struct ListSize<TypeList<Ts...>>
{
    static constexpr usize value = sizeof...(Ts);
};

template <class Out, class... In> struct FilterData;
template <class... Out> struct FilterData<TypeList<Out...>>
{
    using Type = TypeList<Out...>;
};
template <class... Out, class T, class... Rest>
struct FilterData<TypeList<Out...>, T, Rest...>
    : FilterData<Conditional<TermTraits<T>::kHasArg, TypeList<Out..., T>, TypeList<Out...>>, Rest...>
{
};

}  // namespace detail

/// What a query visited since its counters were last reset.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct QueryStats
{
    /// Entities in the chunks that passed the filters.
    u64 entities = 0;
    /// Chunks that passed the filters and were visited.
    u64 chunks_visited = 0;
    /// Non-empty chunks that a `Changed` or `Added` filter skipped.
    u64 chunks_skipped = 0;
};

/// One chunk of a query's archetype, as the function of [[ecs::Query::ForEachChunk]] sees it: the entity ids and the
/// columns the query declared.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: borrows the chunk; the spans are valid until the next structural change of the world (the iteration
/// guard prevents one while the query runs).
class ChunkView
{
public:
    /// Makes a view; only queries do.
    ///
    /// - Ownership: borrows the archetype, the chunk and the three sets, which must outlive the view.
    ChunkView(const Archetype* archetype, Chunk* chunk, const ComponentSet* readable, const ComponentSet* writable,
              const ComponentSet* optional, u64 tick)
        : archetype_(archetype), chunk_(chunk), readable_(readable), writable_(writable), optional_(optional),
          tick_(tick)
    {
    }

    /// The number of entities (rows) in the chunk.
    /// - Fails: never.
    u32 Count() const { return chunk_->count; }
    /// The entity ids of the rows, in row order.
    /// - Fails: never.
    Span<Entity> Entities() const { return archetype_->Entities(chunk_); }

    /// The chunk's column of `T` for reading: a `Read`, `Write` or optional term of the query.
    ///
    /// - Returns: the column, or an empty span when `T` is optional and the archetype lacks it.
    /// - Aborts: `T` is not a data term of the query.
    template <class T> Span<const T> Read() const
    {
        static_assert(!IsEmpty<T>, "ChunkView::Read: a tag has no column");
        const ComponentId id = ComponentOf<T>();
        ENGINE_ASSERT(readable_->Has(id), "ChunkView::Read of a component the query does not declare");
        const T* data = static_cast<const T*>(archetype_->ColumnPtr(chunk_, id));
        ENGINE_ASSERT(data != nullptr || optional_->Has(id),
                      "ChunkView::Read: the archetype lacks a required component");
        return data == nullptr ? Span<const T>() : Span<const T>(data, chunk_->count);
    }
    /// The chunk's column of `T` for writing: a `Write` or `OptionalWrite` term of the query.
    ///
    /// - Returns: the column, or an empty span when `T` is an `OptionalWrite` term and the archetype lacks it.
    /// - Aborts: `T` is not a writable term of the query.
    template <class T> Span<T> Write() const
    {
        static_assert(!IsEmpty<T>, "ChunkView::Write: a tag has no column");
        const ComponentId id = ComponentOf<T>();
        ENGINE_ASSERT(writable_->Has(id), "ChunkView::Write of a component the query does not declare for writing");
        T* data = static_cast<T*>(archetype_->ColumnPtr(chunk_, id));
        ENGINE_ASSERT(data != nullptr || optional_->Has(id),
                      "ChunkView::Write: the archetype lacks a required component");
        return data == nullptr ? Span<T>() : Span<T>(data, chunk_->count);
    }

    /// Stamps the chunk's column of `T` as written at the current tick: for a `LazyWrite` term, after the function
    /// really changed a value. Safe to call from the job that owns this chunk.
    ///
    /// - Aborts: `T` is not a writable term of the query.
    template <class T> void MarkWritten() const
    {
        const ComponentId id = ComponentOf<T>();
        ENGINE_ASSERT(writable_->Has(id),
                      "ChunkView::MarkWritten of a component the query does not declare for writing");
        archetype_->MarkWritten(chunk_, id, tick_);
    }

private:
    const Archetype* archetype_;
    Chunk* chunk_;
    const ComponentSet* readable_;
    const ComponentSet* writable_;
    const ComponentSet* optional_;
    u64 tick_;
};

/// A cached, typed selection of the entities that have the components the terms ask for, and none that a `Without`
/// excludes.
///
/// Made by `world.Query<Terms...>()`, for example `world.Query<Write<Position>, Read<Velocity>, Without<Frozen>>()`.
/// The terms are
/// [[ecs::Read]], [[ecs::Write]], [[ecs::Optional]], [[ecs::OptionalWrite]], [[ecs::With]] and [[ecs::Without]]: one to
/// 16 of them, each component named once (a `Changed<T>` or `Added<T>` may sit beside a data term of `T`). A list that
/// breaks this, or a `Read`/`Write`/`Optional` of a tag, does not compile.
///
/// The query remembers the archetypes that match and how many archetypes the world had when it last looked; each use
/// looks only at the archetypes made since, so keeping a query across frames is cheap. An archetype that has no
/// entities stays in the list and is skipped while iterating.
///
/// - Thread-safety: thread-compatible. A query object has one user at a time. Running two queries of one world on
/// different threads is not supported yet: starting an iteration updates the world's (unsynchronized) iteration counter
/// and write set; the parallel scheduler of Day 231 makes that safe.
/// - Ownership: borrows the world, which must outlive the query; owns its list of archetype indices.
template <class... Terms> class Query
{
    static_assert(sizeof...(Terms) >= 1, "a query needs at least one term");
    static_assert(sizeof...(Terms) <= 16, "a query has at most 16 terms");
    static_assert(detail::AllDistinct<detail::DistinctKey<Terms>...>::value,
                  "a query names each component once (Read and Write, or With and Without, of one type contradict)");
    static_assert((detail::kTermIsUsable<Terms> && ...),
                  "Read, Write, Optional, Changed and Added of a tag: a tag has no data, use With<T>");

    using DataTerms = typename detail::FilterData<detail::TypeList<>, Terms...>::Type;
    static constexpr usize kDataCount = detail::ListSize<DataTerms>::value;

public:
    /// Makes a query over `world`; `world.Query<Terms...>()` does the same.
    ///
    /// - Ownership: borrows `world`.
    explicit Query(World& world) : world_(&world)
    {
        (Collect<Terms>(), ...);
        BuildData(DataTerms{}, typename detail::MakeIndexList<kDataCount>::Type{});
    }

    /// Calls `fn` for every matching entity, in archetype order, then chunk order, then row order.
    ///
    /// `fn` takes the data terms in order, each as the term says (`const T&`, `T&`, `const T*`, `T*`; filters give
    /// nothing), and may take an
    /// [[ecs::Entity]] before them. If `fn` returns `bool`, returning false stops the iteration. Structural calls on
    /// the world abort while it runs.
    ///
    /// - Param fn: the function to call per entity.
    /// - Aborts: `fn` changes the world's structure (`Create`, `Destroy`, `Add`, `Remove`), or calls `World::Get` of a
    /// component the query writes.
    template <class Fn> void ForEach(Fn&& fn)
    {
        ForEachImpl(fn, DataTerms{}, typename detail::MakeIndexList<kDataCount>::Type{});
    }

    /// Calls `fn(ChunkView&)` for every non-empty chunk of every matching archetype, in the order of
    /// [[ecs::Query::ForEach]].
    ///
    /// The view gives the entity ids and the columns as spans, the form to vectorize or to split across jobs. If `fn`
    /// returns `bool`, returning false stops the iteration.
    ///
    /// - Param fn: the function to call per chunk.
    /// - Aborts: as for [[ecs::Query::ForEach]].
    template <class Fn> void ForEachChunk(Fn&& fn)
    {
        Refresh();
        IterationGuard guard(*world_, writes_);
        for (u32 m = 0; m < matched_.Size(); ++m)
        {
            Archetype& a = world_->ArchetypeAt(matched_[m]);
            for (u32 c = 0; c < a.ChunkCount() && a.EntityCount() != 0; ++c)
            {
                Chunk* chunk = a.ChunkAt(c);
                if (chunk->count == 0)
                {
                    continue;
                }
                if (!PassesChange(a, chunk))
                {
                    ++stats_.chunks_skipped;
                    continue;
                }
                CountVisit(chunk);
                MarkWrites(a, chunk);
                ChunkView view(&a, chunk, &readable_, &writes_, &optional_, world_->Tick());
                if (!Proceed([&]() -> decltype(auto) { return fn(view); }))
                {
                    return;
                }
            }
        }
    }

    /// Calls `fn(ChunkView&)` for every chunk that passes the filters, the chunks split into ranges of `grain`
    /// consecutive chunks that run as jobs.
    ///
    /// A chunk is the unit of work: no two jobs touch one chunk. When `scheduler` is null, has one participant, or the
    /// caller is itself a job (a worker cannot wait), the ranges run one after another on the calling thread, in order.
    /// `fn` must not change the world's structure; record changes in a command buffer (the overload below).
    ///
    /// - Param scheduler: the job scheduler, or null to run inline.
    /// - Param fn: called with each chunk's view, possibly from several threads at once (on different chunks).
    /// - Param grain: chunks per job; 0 chooses a few jobs per participant.
    /// - Aborts: as for [[ecs::Query::ForEach]].
    /// - Ownership: borrows the scheduler and `fn` for the call; the jobs finish before it returns.
    template <class Fn> void ForEachChunkParallel(jobs::Scheduler* scheduler, Fn&& fn, usize grain = 0)
    {
        RunChunks(scheduler, nullptr, fn, grain);
    }
    /// As above, and each range of chunks gets its own command buffer from `pool` (`fn(ChunkView&, CommandBuffer&)`),
    /// so the recorded commands apply in chunk order whatever the schedule of the jobs (`pool.ApplyAll` applies them by
    /// range number).
    ///
    /// - Param pool: at least [[ecs::Query::RangesFor]] buffers.
    /// - Aborts: `pool` has fewer buffers than ranges; as for [[ecs::Query::ForEach]].
    /// - Ownership: borrows the scheduler, `pool` and `fn` for the call; the buffers keep their commands for
    /// `pool.ApplyAll`.
    template <class Fn>
    void ForEachChunkParallel(jobs::Scheduler* scheduler, CommandBufferPool& pool, Fn&& fn, usize grain = 0)
    {
        RunChunks(scheduler, &pool, fn, grain);
    }
    /// Folds one value per chunk into one result, always in chunk order, so a floating-point sum is the same whatever
    /// the number of threads.
    ///
    /// - Param scheduler: the job scheduler, or null to run inline.
    /// - Param init: the starting value. `T` must be default-constructible and copyable.
    /// - Param per_chunk: `T(ChunkView&)`, called once per chunk that passes the filters, possibly in parallel.
    /// - Param combine: `T(T accumulated, T chunk_value)`, called in chunk order on the calling thread.
    /// - Param grain: chunks per job; 0 chooses a few jobs per participant.
    /// - Returns: the folded value.
    /// - Aborts: as for [[ecs::Query::ForEach]].
    /// - Ownership: borrows the scheduler and the functions for the call; the jobs finish before it returns.
    template <class T, class PerChunk, class Combine>
    T ReduceChunks(jobs::Scheduler* scheduler, T init, PerChunk&& per_chunk, Combine&& combine, usize grain = 0)
    {
        Refresh();
        IterationGuard guard(*world_, writes_);
        Vector<ChunkRef> chunks;
        Gather(&chunks);
        Vector<T> partial;
        partial.Resize(chunks.Size());
        auto body = [&](usize begin, usize end)
        {
            for (usize i = begin; i < end; ++i)
            {
                ChunkView view(chunks[i].archetype, chunks[i].chunk, &readable_, &writes_, &optional_, world_->Tick());
                partial[i] = per_chunk(view);
            }
        };
        Fan(scheduler, chunks.Size(), grain, body);
        T result = init;
        for (usize i = 0; i < partial.Size(); ++i)
        {
            result = combine(result, partial[i]);
        }
        return result;
    }
    /// The number of ranges `ForEachChunkParallel` makes for `chunks` chunks with this grain (the buffers a pool
    /// needs).
    /// - Fails: never.
    static usize RangesFor(usize chunks, usize grain)
    {
        return grain == 0 ? 1 : chunks / grain + (chunks % grain != 0 ? 1 : 0);
    }
    /// An upper bound on the chunks a pass visits: the chunks of the matching archetypes (before the filters).
    /// - Fails: never.
    usize ChunkCountUpperBound()
    {
        Refresh();
        usize total = 0;
        for (u32 m = 0; m < matched_.Size(); ++m)
        {
            total += world_->ArchetypeAt(matched_[m]).ChunkCount();
        }
        return total;
    }

    /// The tick after which a chunk must have changed to pass `Changed` and `Added` filters (0 at first: everything
    /// stamped passes).
    /// - Fails: never.
    u64 Since() const { return since_; }
    /// Sets the `since` tick explicitly (tests, or a system that keeps its own).
    /// - Fails: never.
    void SetSince(u64 tick) { since_ = tick; }
    /// Marks the changes seen so far as handled: later passes see only what is stamped after this call.
    ///
    /// Sets `since` to the world's tick and then advances the world's tick, so a write made after this call (by a
    /// system that runs later in the same frame) is newer than `since` and is seen by the next pass; the query's own
    /// writes are not. Call it right after the pass, at a point where no other writer runs (a write made by another job
    /// between the pass and this call, in the same tick, would be missed).
    /// - Fails: never.
    void Finish()
    {
        since_ = world_->Tick();
        world_->Advance();
    }

    /// What `ForEach` and `ForEachChunk` visited since the last `ResetStats`.
    /// - Fails: never.
    const QueryStats& Stats() const { return stats_; }
    /// Zeroes the counters.
    /// - Fails: never.
    void ResetStats() { stats_ = QueryStats(); }

    /// Returns how many entities match right now (the `Changed` and `Added` filters are not applied: this counts by
    /// archetype).
    /// - Fails: never.
    u32 Count()
    {
        Refresh();
        u32 total = 0;
        for (u32 m = 0; m < matched_.Size(); ++m)
        {
            total += world_->ArchetypeAt(matched_[m]).EntityCount();
        }
        return total;
    }
    /// Returns whether no entity matches right now.
    /// - Fails: never.
    bool IsEmpty() { return Count() == 0; }

    /// Returns how many archetypes match (including empty ones), after looking at the archetypes made since the last
    /// use.
    /// - Fails: never.
    u32 MatchedArchetypes()
    {
        Refresh();
        return static_cast<u32>(matched_.Size());
    }

private:
    // Holds the world's iteration guard for a scope, so an early return or a returning `false` releases it.
    class IterationGuard
    {
    public:
        // In a parallel phase the schedule holds the world's iteration and the bookkeeping below is not thread-safe, so
        // the guard does nothing.
        IterationGuard(World& world, const ComponentSet& writes) : world_(world), active_(!world.IsParallel())
        {
            if (active_)
            {
                previous_ = world.BeginIteration(writes);
            }
        }
        ~IterationGuard()
        {
            if (active_)
            {
                world_.EndIteration(previous_);
            }
        }
        IterationGuard(const IterationGuard&) = delete;
        IterationGuard& operator=(const IterationGuard&) = delete;

    private:
        World& world_;
        bool active_;
        ComponentSet previous_;
    };

    // Calls `call()` and says whether to go on: a function that returns bool stops the iteration by returning false.
    template <class Call> static bool Proceed(Call&& call)
    {
        if constexpr (IsSame<decltype(call()), bool>)
        {
            return call();
        }
        else
        {
            call();
            return true;
        }
    }

    template <class Term> void Collect()
    {
        using Traits = detail::TermTraits<Term>;
        const ComponentId id = ComponentOf<typename Traits::Component>();
        if (id == kInvalidComponent)
        {
            // The registry is full: a required component can never match; an excluded or optional one is simply absent.
            impossible_ = impossible_ || Traits::kRequired;
            return;
        }
        if constexpr (Traits::kRequired)
        {
            required_.Add(id);
        }
        if constexpr (Traits::kExcluded)
        {
            excluded_.Add(id);
        }
        if constexpr (Traits::kHasArg)
        {
            readable_.Add(id);
        }
        if constexpr (Traits::kWrites)
        {
            writes_.Add(id);
            if constexpr (detail::kStampsOnVisit<Term>)
            {
                write_ids_[write_count_++] = id;
            }
        }
        if constexpr (Traits::kChange == 1)
        {
            changed_ids_[changed_count_++] = id;
        }
        if constexpr (Traits::kChange == 2)
        {
            added_ids_[added_count_++] = id;
        }
        if constexpr (Traits::kOptional)
        {
            optional_.Add(id);
        }
    }

    template <class... Data, usize... I> void BuildData(detail::TypeList<Data...>, detail::IndexList<I...>)
    {
        ((ids_[I] = ComponentOf<typename detail::TermTraits<Data>::Component>()), ...);
    }

    struct ChunkRef
    {
        Archetype* archetype;
        Chunk* chunk;
    };

    // The non-empty chunks that pass the filters, in iteration order; counts them and stamps the written columns.
    // Single-threaded.
    void Gather(Vector<ChunkRef>* out)
    {
        for (u32 m = 0; m < matched_.Size(); ++m)
        {
            Archetype& a = world_->ArchetypeAt(matched_[m]);
            for (u32 c = 0; c < a.ChunkCount() && a.EntityCount() != 0; ++c)
            {
                Chunk* chunk = a.ChunkAt(c);
                if (chunk->count == 0)
                {
                    continue;
                }
                if (!PassesChange(a, chunk))
                {
                    ++stats_.chunks_skipped;
                    continue;
                }
                CountVisit(chunk);
                MarkWrites(a, chunk);
                out->Push(ChunkRef{&a, chunk});
            }
        }
    }

    // Runs `body(begin, end)` over [0, count) in ranges of `grain` as jobs, or in order on this thread when it cannot
    // fan out.
    template <class Body> static void Fan(jobs::Scheduler* scheduler, usize count, usize grain, Body& body)
    {
        const usize participants = scheduler == nullptr ? 1 : scheduler->ParticipantCount();
        const usize g = ResolveGrain(count, grain, participants);
        const usize ranges = RangesFor(count, g);
        auto by_range = [&](usize first, usize last)
        {
            for (usize r = first; r < last; ++r)
            {
                const usize begin = r * g;
                const usize end = begin + g < count ? begin + g : count;
                body(begin, end);
            }
        };
        const bool can_fan = scheduler != nullptr && participants > 1 && ranges > 1 && !jobs::Scheduler::InsideJob() &&
                             jobs::Scheduler::Current() == scheduler;
        if (can_fan)
        {
            jobs::ParallelForBlocking(*scheduler, ranges, 1, by_range);
        }
        else
        {
            by_range(usize{0}, ranges);
        }
    }

    // The chunks per range: the caller's, or a few ranges per participant.
    static usize ResolveGrain(usize count, usize grain, usize participants)
    {
        if (grain != 0)
        {
            return grain;
        }
        const usize wanted = participants * 4;
        const usize g = count / wanted + (count % wanted != 0 ? 1 : 0);
        return g == 0 ? 1 : g;
    }

    template <class Fn> void RunChunks(jobs::Scheduler* scheduler, CommandBufferPool* pool, Fn& fn, usize grain)
    {
        Refresh();
        IterationGuard guard(*world_, writes_);
        Vector<ChunkRef> chunks;
        Gather(&chunks);
        const usize participants = scheduler == nullptr ? 1 : scheduler->ParticipantCount();
        const usize g = ResolveGrain(chunks.Size(), grain, participants);
        if (pool != nullptr)
        {
            ENGINE_ASSERT(pool->Count() >= RangesFor(chunks.Size(), g),
                          "Query::ForEachChunkParallel: the pool has fewer buffers than ranges");
        }
        auto body = [&](usize begin, usize end)
        {
            for (usize i = begin; i < end; ++i)
            {
                ChunkView view(chunks[i].archetype, chunks[i].chunk, &readable_, &writes_, &optional_, world_->Tick());
                if constexpr (IsInvocable<Fn&, ChunkView&, CommandBuffer&>)
                {
                    ENGINE_ASSERT(
                        pool != nullptr,
                        "Query::ForEachChunkParallel: a function that takes a CommandBuffer needs the overload with a pool");
                    fn(view, pool->At(static_cast<u32>(begin / g)));
                }
                else
                {
                    fn(view);
                }
            }
        };
        Fan(scheduler, chunks.Size(), g, body);
    }

    // Whether the chunk passes the Changed and Added filters (all of them).
    bool PassesChange(const Archetype& a, const Chunk* chunk) const
    {
        for (u32 i = 0; i < changed_count_; ++i)
        {
            if (a.WrittenTick(chunk, changed_ids_[i]) <= since_)
            {
                return false;
            }
        }
        for (u32 i = 0; i < added_count_; ++i)
        {
            if (a.AddedTick(chunk, added_ids_[i]) <= since_)
            {
                return false;
            }
        }
        return true;
    }

    void CountVisit(const Chunk* chunk)
    {
        ++stats_.chunks_visited;
        stats_.entities += chunk->count;
    }

    // Stamps the columns the query writes in a chunk it is about to visit (the whole chunk, whatever the function does
    // with it).
    void MarkWrites(const Archetype& a, Chunk* chunk) const
    {
        for (u32 i = 0; i < write_count_; ++i)
        {
            a.MarkWritten(chunk, write_ids_[i], world_->Tick());
        }
    }

    // Looks at the archetypes made since the last call.
    void Refresh()
    {
        const u32 total = world_->ArchetypeCount();
        for (; seen_ < total; ++seen_)
        {
            if (impossible_)
            {
                continue;
            }
            const Archetype& a = world_->ArchetypeAt(seen_);
            if (a.Valid() && required_.IsSubsetOf(a.Components()) && !a.Components().Intersects(excluded_))
            {
                matched_.Push(seen_);
            }
        }
    }

    template <class Fn, class... Data, usize... I>
    void ForEachImpl(Fn& fn, detail::TypeList<Data...>, detail::IndexList<I...>)
    {
        constexpr bool kWithEntity = IsInvocable<Fn&, Entity, typename detail::TermTraits<Data>::Arg...>;
        constexpr bool kWithoutEntity = IsInvocable<Fn&, typename detail::TermTraits<Data>::Arg...>;
        static_assert(
            kWithEntity || kWithoutEntity,
            "Query::ForEach: the function must take (Entity, then) the data terms in order: const T& for Read, T& for Write, const T* for Optional, T* for OptionalWrite");
        Refresh();
        IterationGuard guard(*world_, writes_);
        void* columns[kDataCount == 0 ? 1 : kDataCount];
        for (u32 m = 0; m < matched_.Size(); ++m)
        {
            Archetype& a = world_->ArchetypeAt(matched_[m]);
            for (u32 c = 0; c < a.ChunkCount() && a.EntityCount() != 0; ++c)
            {
                Chunk* chunk = a.ChunkAt(c);
                const u32 rows = chunk->count;
                if (rows == 0)
                {
                    continue;
                }
                if (!PassesChange(a, chunk))
                {
                    ++stats_.chunks_skipped;
                    continue;
                }
                CountVisit(chunk);
                MarkWrites(a, chunk);
                ((columns[I] = a.ColumnPtr(chunk, ids_[I])), ...);
                const Span<Entity> entities = a.Entities(chunk);
                for (u32 row = 0; row < rows; ++row)
                {
                    bool keep_going;
                    if constexpr (kWithEntity)
                    {
                        keep_going = Proceed(
                            [&]() -> decltype(auto)
                            { return fn(entities[row], detail::TermTraits<Data>::Fetch(columns[I], row)...); });
                    }
                    else
                    {
                        keep_going = Proceed([&]() -> decltype(auto)
                                             { return fn(detail::TermTraits<Data>::Fetch(columns[I], row)...); });
                    }
                    if (!keep_going)
                    {
                        return;
                    }
                }
            }
        }
    }

    World* world_;
    ComponentSet required_;
    ComponentSet excluded_;
    ComponentSet readable_;
    ComponentSet writes_;
    ComponentSet optional_;
    ComponentId ids_[kDataCount == 0 ? 1 : kDataCount] = {};
    ComponentId write_ids_[16] = {};
    ComponentId changed_ids_[16] = {};
    ComponentId added_ids_[16] = {};
    u32 write_count_ = 0;
    u32 changed_count_ = 0;
    u32 added_count_ = 0;
    u64 since_ = 0;
    QueryStats stats_;
    bool impossible_ = false;
    u32 seen_ = 0;
    Vector<u32> matched_;
};

template <class... Terms> ecs::Query<Terms...> World::Query()
{
    return ecs::Query<Terms...>(*this);
}

}  // namespace vse::ecs
