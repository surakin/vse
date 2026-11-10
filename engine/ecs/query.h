#pragma once

//! Typed queries: the entities whose archetype has certain components, visited one by one or one chunk at a time.

#include "containers/span.h"
#include "containers/vector.h"
#include "ecs/archetype.h"
#include "ecs/chunk.h"
#include "ecs/component.h"
#include "ecs/component_set.h"
#include "ecs/entity.h"
#include "ecs/world.h"
#include "lang/traits.h"
#include "log/assert.h"
#include "types.h"

namespace engine_placeholder::ecs {

/// A query term: the entities must have `T`; the query function gets it as `const T&`. `T` may not be a tag (use [[ecs::With]]).
///
/// Thread-safety: thread-safe empty type; it only names a component.
template <class T>
struct Read {};
/// A query term: the entities must have `T`; the query function gets it as `T&`, and the query holds `T` for writing. `T` may not be a tag.
///
/// Thread-safety: thread-safe empty type; it only names a component.
template <class T>
struct Write {};
/// A query term: the entities may have `T`; the query function gets a `const T*`, null when the archetype lacks it. `T` may not be a tag.
///
/// Thread-safety: thread-safe empty type; it only names a component.
template <class T>
struct Optional {};
/// A query term: like [[ecs::Optional]] but writable: the query function gets a `T*`, null when the archetype lacks it.
///
/// Thread-safety: thread-safe empty type; it only names a component.
template <class T>
struct OptionalWrite {};
/// A query term: the entities must have `T`, and the query function gets nothing for it (a filter). `T` may be a tag.
///
/// Thread-safety: thread-safe empty type; it only names a component.
template <class T>
struct With {};
/// A query term: the entities must not have `T` (a filter, no argument). `T` may be a tag.
///
/// Thread-safety: thread-safe empty type; it only names a component.
template <class T>
struct Without {};

namespace detail {

template <class... Ts>
struct TypeList {};

template <usize... I>
struct IndexList {};
template <usize N, usize... I>
struct MakeIndexList : MakeIndexList<N - 1, N - 1, I...> {};
template <usize... I>
struct MakeIndexList<0, I...> {
    using Type = IndexList<I...>;
};

/// What a term is: its component, whether it hands the query function an argument, and how that argument is read from a column.
template <class Term>
struct TermTraits;

template <class T>
struct TermTraits<Read<T>> {
    using Component = T;
    using Arg = const T&;
    static constexpr bool kHasArg = true;
    static constexpr bool kRequired = true;
    static constexpr bool kExcluded = false;
    static constexpr bool kWrites = false;
    static constexpr bool kOptional = false;
    static Arg Fetch(void* column, u32 row) { return static_cast<const T*>(column)[row]; }
};
template <class T>
struct TermTraits<Write<T>> {
    using Component = T;
    using Arg = T&;
    static constexpr bool kHasArg = true;
    static constexpr bool kRequired = true;
    static constexpr bool kExcluded = false;
    static constexpr bool kWrites = true;
    static constexpr bool kOptional = false;
    static Arg Fetch(void* column, u32 row) { return static_cast<T*>(column)[row]; }
};
template <class T>
struct TermTraits<Optional<T>> {
    using Component = T;
    using Arg = const T*;
    static constexpr bool kHasArg = true;
    static constexpr bool kRequired = false;
    static constexpr bool kExcluded = false;
    static constexpr bool kWrites = false;
    static constexpr bool kOptional = true;
    static Arg Fetch(void* column, u32 row) { return column == nullptr ? nullptr : static_cast<const T*>(column) + row; }
};
template <class T>
struct TermTraits<OptionalWrite<T>> {
    using Component = T;
    using Arg = T*;
    static constexpr bool kHasArg = true;
    static constexpr bool kRequired = false;
    static constexpr bool kExcluded = false;
    static constexpr bool kWrites = true;
    static constexpr bool kOptional = true;
    static Arg Fetch(void* column, u32 row) { return column == nullptr ? nullptr : static_cast<T*>(column) + row; }
};
template <class T>
struct TermTraits<With<T>> {
    using Component = T;
    static constexpr bool kHasArg = false;
    static constexpr bool kRequired = true;
    static constexpr bool kExcluded = false;
    static constexpr bool kWrites = false;
    static constexpr bool kOptional = false;
};
template <class T>
struct TermTraits<Without<T>> {
    using Component = T;
    static constexpr bool kHasArg = false;
    static constexpr bool kRequired = false;
    static constexpr bool kExcluded = true;
    static constexpr bool kWrites = false;
    static constexpr bool kOptional = false;
};

template <class... Ts>
struct AllDistinct : TrueType {};
template <class T, class... Rest>
struct AllDistinct<T, Rest...> : IntegralConstant<bool, (!IsSame<T, Rest> && ...) && AllDistinct<Rest...>::value> {};

/// A term that carries data (not a filter) may not name a tag: a tag has no column.
template <class Term>
inline constexpr bool kTermIsUsable = !TermTraits<Term>::kHasArg || !IsEmpty<typename TermTraits<Term>::Component>;

template <class List>
struct ListSize;
template <class... Ts>
struct ListSize<TypeList<Ts...>> {
    static constexpr usize value = sizeof...(Ts);
};

template <class Out, class... In>
struct FilterData;
template <class... Out>
struct FilterData<TypeList<Out...>> {
    using Type = TypeList<Out...>;
};
template <class... Out, class T, class... Rest>
struct FilterData<TypeList<Out...>, T, Rest...>
    : FilterData<Conditional<TermTraits<T>::kHasArg, TypeList<Out..., T>, TypeList<Out...>>, Rest...> {};

} // namespace detail

/// One chunk of a query's archetype, as the function of [[ecs::Query::ForEachChunk]] sees it: the entity ids and the columns the query declared.
///
/// Thread-safety: thread-compatible.
/// Ownership: borrows the chunk; the spans are valid until the next structural change of the world (the iteration guard prevents one while the query runs).
class ChunkView {
public:
    /// Makes a view; only queries do.
    ///
    /// Ownership: borrows the archetype, the chunk and the three sets, which must outlive the view.
    ChunkView(const Archetype* archetype, Chunk* chunk, const ComponentSet* readable, const ComponentSet* writable, const ComponentSet* optional)
        : archetype_(archetype), chunk_(chunk), readable_(readable), writable_(writable), optional_(optional) {}

    /// The number of entities (rows) in the chunk.
    /// Fails: never.
    u32 Count() const { return chunk_->count; }
    /// The entity ids of the rows, in row order.
    /// Fails: never.
    Span<Entity> Entities() const { return archetype_->Entities(chunk_); }

    /// The chunk's column of `T` for reading: a `Read`, `Write` or optional term of the query.
    ///
    /// Returns: the column, or an empty span when `T` is optional and the archetype lacks it.
    /// Aborts: `T` is not a data term of the query.
    template <class T>
    Span<const T> Read() const {
        static_assert(!IsEmpty<T>, "ChunkView::Read: a tag has no column");
        const ComponentId id = ComponentOf<T>();
        ENGINE_ASSERT(readable_->Has(id), "ChunkView::Read of a component the query does not declare");
        const T* data = static_cast<const T*>(archetype_->ColumnPtr(chunk_, id));
        ENGINE_ASSERT(data != nullptr || optional_->Has(id), "ChunkView::Read: the archetype lacks a required component");
        return data == nullptr ? Span<const T>() : Span<const T>(data, chunk_->count);
    }
    /// The chunk's column of `T` for writing: a `Write` or `OptionalWrite` term of the query.
    ///
    /// Returns: the column, or an empty span when `T` is an `OptionalWrite` term and the archetype lacks it.
    /// Aborts: `T` is not a writable term of the query.
    template <class T>
    Span<T> Write() const {
        static_assert(!IsEmpty<T>, "ChunkView::Write: a tag has no column");
        const ComponentId id = ComponentOf<T>();
        ENGINE_ASSERT(writable_->Has(id), "ChunkView::Write of a component the query does not declare for writing");
        T* data = static_cast<T*>(archetype_->ColumnPtr(chunk_, id));
        ENGINE_ASSERT(data != nullptr || optional_->Has(id), "ChunkView::Write: the archetype lacks a required component");
        return data == nullptr ? Span<T>() : Span<T>(data, chunk_->count);
    }

private:
    const Archetype* archetype_;
    Chunk* chunk_;
    const ComponentSet* readable_;
    const ComponentSet* writable_;
    const ComponentSet* optional_;
};

/// A cached, typed selection of the entities that have the components the terms ask for, and none that a `Without` excludes.
///
/// Made by `world.Query<Terms...>()`, for example `world.Query<Write<Position>, Read<Velocity>, Without<Frozen>>()`. The terms are
/// [[ecs::Read]], [[ecs::Write]], [[ecs::Optional]], [[ecs::OptionalWrite]], [[ecs::With]] and [[ecs::Without]]: one to 16 of them, each
/// component named once. A list that breaks this, or a `Read`/`Write`/`Optional` of a tag, does not compile.
///
/// The query remembers the archetypes that match and how many archetypes the world had when it last looked; each use looks only at the
/// archetypes made since, so keeping a query across frames is cheap. An archetype that has no entities stays in the list and is skipped
/// while iterating.
///
/// Thread-safety: thread-compatible. A query object has one user at a time. Running two queries of one world on different threads is not
/// supported yet: starting an iteration updates the world's (unsynchronized) iteration counter and write set; the parallel scheduler of Day 231
/// makes that safe.
/// Ownership: borrows the world, which must outlive the query; owns its list of archetype indices.
template <class... Terms>
class Query {
    static_assert(sizeof...(Terms) >= 1, "a query needs at least one term");
    static_assert(sizeof...(Terms) <= 16, "a query has at most 16 terms");
    static_assert(detail::AllDistinct<typename detail::TermTraits<Terms>::Component...>::value,
                  "a query names each component once (Read and Write, or With and Without, of one type contradict)");
    static_assert((detail::kTermIsUsable<Terms> && ...), "Read, Write and Optional of a tag: a tag has no data, use With<T>");

    using DataTerms = typename detail::FilterData<detail::TypeList<>, Terms...>::Type;
    static constexpr usize kDataCount = detail::ListSize<DataTerms>::value;

public:
    /// Makes a query over `world`; `world.Query<Terms...>()` does the same.
    ///
    /// Ownership: borrows `world`.
    explicit Query(World& world) : world_(&world) {
        (Collect<Terms>(), ...);
        BuildData(DataTerms{}, typename detail::MakeIndexList<kDataCount>::Type{});
    }

    /// Calls `fn` for every matching entity, in archetype order, then chunk order, then row order.
    ///
    /// `fn` takes the data terms in order, each as the term says (`const T&`, `T&`, `const T*`, `T*`; filters give nothing), and may take an
    /// [[ecs::Entity]] before them. If `fn` returns `bool`, returning false stops the iteration. Structural calls on the world abort while it runs.
    ///
    /// Param fn: the function to call per entity.
    /// Aborts: `fn` changes the world's structure (`Create`, `Destroy`, `Add`, `Remove`), or calls `World::Get` of a component the query writes.
    template <class Fn>
    void ForEach(Fn&& fn) {
        ForEachImpl(fn, DataTerms{}, typename detail::MakeIndexList<kDataCount>::Type{});
    }

    /// Calls `fn(ChunkView&)` for every non-empty chunk of every matching archetype, in the order of [[ecs::Query::ForEach]].
    ///
    /// The view gives the entity ids and the columns as spans, the form to vectorize or to split across jobs. If `fn` returns `bool`,
    /// returning false stops the iteration.
    ///
    /// Param fn: the function to call per chunk.
    /// Aborts: as for [[ecs::Query::ForEach]].
    template <class Fn>
    void ForEachChunk(Fn&& fn) {
        Refresh();
        IterationGuard guard(*world_, writes_);
        for (u32 m = 0; m < matched_.Size(); ++m) {
            Archetype& a = world_->ArchetypeAt(matched_[m]);
            for (u32 c = 0; c < a.ChunkCount() && a.EntityCount() != 0; ++c) {
                Chunk* chunk = a.ChunkAt(c);
                if (chunk->count == 0) {
                    continue;
                }
                ChunkView view(&a, chunk, &readable_, &writes_, &optional_);
                if (!Proceed([&]() -> decltype(auto) { return fn(view); })) {
                    return;
                }
            }
        }
    }

    /// Returns how many entities match right now.
    /// Fails: never.
    u32 Count() {
        Refresh();
        u32 total = 0;
        for (u32 m = 0; m < matched_.Size(); ++m) {
            total += world_->ArchetypeAt(matched_[m]).EntityCount();
        }
        return total;
    }
    /// Returns whether no entity matches right now.
    /// Fails: never.
    bool IsEmpty() { return Count() == 0; }

    /// Returns how many archetypes match (including empty ones), after looking at the archetypes made since the last use.
    /// Fails: never.
    u32 MatchedArchetypes() {
        Refresh();
        return static_cast<u32>(matched_.Size());
    }

private:
    // Holds the world's iteration guard for a scope, so an early return or a returning `false` releases it.
    class IterationGuard {
    public:
        IterationGuard(World& world, const ComponentSet& writes) : world_(world), previous_(world.BeginIteration(writes)) {}
        ~IterationGuard() { world_.EndIteration(previous_); }
        IterationGuard(const IterationGuard&) = delete;
        IterationGuard& operator=(const IterationGuard&) = delete;

    private:
        World& world_;
        ComponentSet previous_;
    };

    // Calls `call()` and says whether to go on: a function that returns bool stops the iteration by returning false.
    template <class Call>
    static bool Proceed(Call&& call) {
        if constexpr (IsSame<decltype(call()), bool>) {
            return call();
        } else {
            call();
            return true;
        }
    }

    template <class Term>
    void Collect() {
        using Traits = detail::TermTraits<Term>;
        const ComponentId id = ComponentOf<typename Traits::Component>();
        if (id == kInvalidComponent) {
            // The registry is full: a required component can never match; an excluded or optional one is simply absent.
            impossible_ = impossible_ || Traits::kRequired;
            return;
        }
        if constexpr (Traits::kRequired) {
            required_.Add(id);
        }
        if constexpr (Traits::kExcluded) {
            excluded_.Add(id);
        }
        if constexpr (Traits::kHasArg) {
            readable_.Add(id);
        }
        if constexpr (Traits::kWrites) {
            writes_.Add(id);
        }
        if constexpr (Traits::kOptional) {
            optional_.Add(id);
        }
    }

    template <class... Data, usize... I>
    void BuildData(detail::TypeList<Data...>, detail::IndexList<I...>) {
        ((ids_[I] = ComponentOf<typename detail::TermTraits<Data>::Component>()), ...);
    }

    // Looks at the archetypes made since the last call.
    void Refresh() {
        const u32 total = world_->ArchetypeCount();
        for (; seen_ < total; ++seen_) {
            if (impossible_) {
                continue;
            }
            const Archetype& a = world_->ArchetypeAt(seen_);
            if (a.Valid() && required_.IsSubsetOf(a.Components()) && !a.Components().Intersects(excluded_)) {
                matched_.Push(seen_);
            }
        }
    }

    template <class Fn, class... Data, usize... I>
    void ForEachImpl(Fn& fn, detail::TypeList<Data...>, detail::IndexList<I...>) {
        constexpr bool kWithEntity = IsInvocable<Fn&, Entity, typename detail::TermTraits<Data>::Arg...>;
        constexpr bool kWithoutEntity = IsInvocable<Fn&, typename detail::TermTraits<Data>::Arg...>;
        static_assert(kWithEntity || kWithoutEntity,
                      "Query::ForEach: the function must take (Entity, then) the data terms in order: const T& for Read, T& for Write, const T* for Optional, T* for OptionalWrite");
        Refresh();
        IterationGuard guard(*world_, writes_);
        void* columns[kDataCount == 0 ? 1 : kDataCount];
        for (u32 m = 0; m < matched_.Size(); ++m) {
            Archetype& a = world_->ArchetypeAt(matched_[m]);
            for (u32 c = 0; c < a.ChunkCount() && a.EntityCount() != 0; ++c) {
                Chunk* chunk = a.ChunkAt(c);
                const u32 rows = chunk->count;
                if (rows == 0) {
                    continue;
                }
                ((columns[I] = a.ColumnPtr(chunk, ids_[I])), ...);
                const Span<Entity> entities = a.Entities(chunk);
                for (u32 row = 0; row < rows; ++row) {
                    bool keep_going;
                    if constexpr (kWithEntity) {
                        keep_going = Proceed([&]() -> decltype(auto) { return fn(entities[row], detail::TermTraits<Data>::Fetch(columns[I], row)...); });
                    } else {
                        keep_going = Proceed([&]() -> decltype(auto) { return fn(detail::TermTraits<Data>::Fetch(columns[I], row)...); });
                    }
                    if (!keep_going) {
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
    bool impossible_ = false;
    u32 seen_ = 0;
    Vector<u32> matched_;
};

template <class... Terms>
ecs::Query<Terms...> World::Query() {
    return ecs::Query<Terms...>(*this);
}

} // namespace engine_placeholder::ecs
