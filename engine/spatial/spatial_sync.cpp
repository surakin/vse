#include "spatial/spatial_sync.h"

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "ecs/query.h"
#include "ecs/transform.h"
#include "lang/scalar.h"
#include "sync/atomic.h"

namespace engine_placeholder::spatial
{

namespace
{

using ecs::Added;
using ecs::Changed;
using ecs::Entity;
using ecs::LazyWrite;
using ecs::Query;
using ecs::Read;
using ecs::With;
using ecs::Without;
using ecs::Write;

bool FiniteSyncBox(const Aabb& box)
{
    return IsFinite(box.min.x) && IsFinite(box.min.y) && IsFinite(box.min.z) && IsFinite(box.max.x) &&
           IsFinite(box.max.y) && IsFinite(box.max.z) && !box.IsEmpty();
}

// The queries for one value of the SpatialStatic filter.
template <class StaticFilter> struct Queries
{
    static constexpr bool kStatic = IsSame<StaticFilter, With<SpatialStatic>>;
    using Join = Query<Read<SpatialBounds>, Read<ecs::WorldTransform>, Read<SpatialMembership>, Without<SpatialProxy>,
                       StaticFilter>;
    using Edited = Query<Changed<SpatialMembership>, Read<SpatialMembership>, Write<SpatialProxy>, Read<SpatialBounds>,
                         Read<ecs::WorldTransform>, StaticFilter>;
    using Arrived = Query<Added<SpatialProxy>, Read<SpatialMembership>, Write<SpatialProxy>, Read<SpatialBounds>,
                          Read<ecs::WorldTransform>, StaticFilter>;
    UniquePtr<Join> join;
    UniquePtr<Edited> edited;
    UniquePtr<Arrived> arrived;
};

using BoundsByTransform = Query<Changed<ecs::WorldTransform>, Read<SpatialBounds>, Read<ecs::WorldTransform>,
                                LazyWrite<WorldBounds>>;
using BoundsByLocal = Query<Changed<SpatialBounds>, Read<SpatialBounds>, Read<ecs::WorldTransform>,
                            LazyWrite<WorldBounds>>;
using MovedBounds = Query<Changed<WorldBounds>, Read<WorldBounds>, Read<SpatialProxy>>;

bool SameBox(const Aabb& a, const Aabb& b)
{
    return a.min.x == b.min.x && a.min.y == b.min.y && a.min.z == b.min.z && a.max.x == b.max.x && a.max.y == b.max.y &&
           a.max.z == b.max.z;
}

using NoBounds = Query<Write<SpatialProxy>, Without<SpatialBounds>>;
using NoTransform = Query<Write<SpatialProxy>, Without<ecs::WorldTransform>>;
using NoMembership = Query<Write<SpatialProxy>, Without<SpatialMembership>>;

class SpatialSync
{
public:
    SpatialSync() = default;
    SpatialSync(SpatialSync&&) = default;

    void operator()(ecs::SystemContext& context)
    {
        SpatialWorld& spatial = context.Resource<SpatialWorld>();
        if (!built_ || world_id_ != context.WorldId())
        {
            Build(context);
            built_ = true;
            world_id_ = context.WorldId();
        }
        ++spatial.MutableStats().frames;
        Sync(context, spatial);
    }

private:
    void Build(ecs::SystemContext& context)
    {
        yes_.join = MakeUnique<Queries<With<SpatialStatic>>::Join>(
            context.Query<Read<SpatialBounds>, Read<ecs::WorldTransform>, Read<SpatialMembership>,
                          Without<SpatialProxy>, With<SpatialStatic>>());
        yes_.edited = MakeUnique<Queries<With<SpatialStatic>>::Edited>(
            context.Query<Changed<SpatialMembership>, Read<SpatialMembership>, Write<SpatialProxy>, Read<SpatialBounds>,
                          Read<ecs::WorldTransform>, With<SpatialStatic>>());
        yes_.arrived = MakeUnique<Queries<With<SpatialStatic>>::Arrived>(
            context.Query<Added<SpatialProxy>, Read<SpatialMembership>, Write<SpatialProxy>, Read<SpatialBounds>,
                          Read<ecs::WorldTransform>, With<SpatialStatic>>());
        no_.join = MakeUnique<Queries<Without<SpatialStatic>>::Join>(
            context.Query<Read<SpatialBounds>, Read<ecs::WorldTransform>, Read<SpatialMembership>,
                          Without<SpatialProxy>, Without<SpatialStatic>>());
        no_.edited = MakeUnique<Queries<Without<SpatialStatic>>::Edited>(
            context.Query<Changed<SpatialMembership>, Read<SpatialMembership>, Write<SpatialProxy>, Read<SpatialBounds>,
                          Read<ecs::WorldTransform>, Without<SpatialStatic>>());
        no_.arrived = MakeUnique<Queries<Without<SpatialStatic>>::Arrived>(
            context.Query<Added<SpatialProxy>, Read<SpatialMembership>, Write<SpatialProxy>, Read<SpatialBounds>,
                          Read<ecs::WorldTransform>, Without<SpatialStatic>>());
        by_transform_ = MakeUnique<BoundsByTransform>(
            context.Query<Changed<ecs::WorldTransform>, Read<SpatialBounds>, Read<ecs::WorldTransform>,
                          LazyWrite<WorldBounds>>());
        by_local_ = MakeUnique<BoundsByLocal>(context.Query<Changed<SpatialBounds>, Read<SpatialBounds>,
                                                            Read<ecs::WorldTransform>, LazyWrite<WorldBounds>>());
        moved_ = MakeUnique<MovedBounds>(context.Query<Changed<WorldBounds>, Read<WorldBounds>, Read<SpatialProxy>>());
        no_bounds_ = MakeUnique<NoBounds>(context.Query<Write<SpatialProxy>, Without<SpatialBounds>>());
        no_transform_ = MakeUnique<NoTransform>(context.Query<Write<SpatialProxy>, Without<ecs::WorldTransform>>());
        no_membership_ = MakeUnique<NoMembership>(context.Query<Write<SpatialProxy>, Without<SpatialMembership>>());
        pending_.Clear();
        last_removal_seq_ = 0;  // a new world numbers its removals from 1
    }

    // The bits of `sets` that name a set of the world; counts the others.
    static u32 ValidSets(const SpatialWorld& spatial, u8 sets, bool count, SpatialWorldStats& stats)
    {
        const u32 all = spatial.SetCount() >= 8 ? 0xFFu : ((1u << spatial.SetCount()) - 1u);
        if (count && (static_cast<u32>(sets) & ~all) != 0)
        {
            ++stats.unknown_set_bits;
        }
        return static_cast<u32>(sets) & all;
    }

    static void AddItem(SpatialWorld& spatial, SetId id, Entity entity, const Aabb& box, u32 layer, bool is_static,
                        SetHandle& out)
    {
        SpatialSet* set = spatial.Set(id);
        out = is_static ? set->AddStatic(box, entity.ToU64(), layer) : set->AddDynamic(box, entity.ToU64(), layer);
        if (out == kInvalidSetHandle)
        {
            ++spatial.MutableStats().refused_full;
        }
        else
        {
            ++spatial.MutableStats().items_added;
        }
    }

    static void RemoveItem(SpatialWorld& spatial, SetId id, SetHandle& handle)
    {
        if (handle == kInvalidSetHandle)
        {
            return;
        }
        if (SpatialSet* set = spatial.Set(id))
        {
            if (set->Remove(handle))
            {
                ++spatial.MutableStats().items_removed;
            }
        }
        handle = kInvalidSetHandle;
    }

    // Takes the entity out of every set; the proxy keeps no handle and no set bit.
    static void Leave(SpatialWorld& spatial, SpatialProxy& proxy)
    {
        for (SetId id = 0; id < kMaxSpatialSets; ++id)
        {
            RemoveItem(spatial, id, proxy.handle[id]);
        }
        proxy.sets = 0;
    }

    // Brings the items of `proxy` in line with the entity's membership and static tag.
    // `count_unknown` is true on the looks that happen once per change (a join, an edit), so a set bit that names no
    // set is counted once.
    void Reconcile(SpatialWorld& spatial, Entity entity, const SpatialMembership& membership, SpatialProxy& proxy,
                   const Aabb& box, bool is_static, bool count_unknown)
    {
        SpatialWorldStats& stats = spatial.MutableStats();
        const u32 want = ValidSets(spatial, membership.sets, count_unknown && proxy.requested != membership.sets,
                                   stats);
        proxy.requested = membership.sets;
        bool complete = true;
        const bool relayer = proxy.layer != membership.layer;
        for (SetId id = 0; id < spatial.SetCount(); ++id)
        {
            const bool have = proxy.handle[id] != kInvalidSetHandle;
            const bool wanted = ((want >> id) & 1u) != 0;
            if (have && (!wanted || relayer))
            {
                RemoveItem(spatial, id, proxy.handle[id]);
            }
            if (wanted && proxy.handle[id] == kInvalidSetHandle)
            {
                AddItem(spatial, id, entity, box, membership.layer, is_static, proxy.handle[id]);
                complete = complete && proxy.handle[id] != kInvalidSetHandle;
            }
            else if (wanted && proxy.is_static != is_static)
            {
                SpatialSet* set = spatial.Set(id);
                if (is_static ? set->MakeStatic(proxy.handle[id]) : set->MakeDynamic(proxy.handle[id]))
                {
                    ++stats.moved_between;
                    // A static item ignores moves, so its box may be stale: bring it to the entity's box.
                    if (!is_static && !SameBox(set->ItemOf(proxy.handle[id]).box, box))
                    {
                        set->Move(proxy.handle[id], box);
                    }
                }
                else
                {
                    complete = false;
                }
            }
        }
        // The proxy records what the items are, so a refused add or move is tried again at the next look (an edit or an
        // arrival).
        u32 held = 0;
        for (SetId id = 0; id < spatial.SetCount(); ++id)
        {
            held |= proxy.handle[id] != kInvalidSetHandle ? (1u << id) : 0u;
        }
        proxy.sets = static_cast<u8>(held);
        if (complete)
        {
            proxy.layer = membership.layer;
            proxy.is_static = is_static;
        }
    }

    template <class Q> void SyncEdits(SpatialWorld& spatial, Q& q)
    {
        constexpr bool is_static = Q::kStatic;
        bool count_unknown = true;
        auto reconcile = [&](Entity entity, const SpatialMembership& m, SpatialProxy& proxy,
                             const SpatialBounds& bounds, const ecs::WorldTransform& w)
        {
            if (proxy.leaving)
            {
                return;
            }
            const Aabb box = TransformAabb(bounds.local, w.matrix);
            if (!FiniteSyncBox(bounds.local) || !FiniteSyncBox(box) || m.layer >= 32)
            {
                ++spatial.MutableStats().refused_bounds;  // nothing is added or moved, but the sets the entity no
                                                          // longer asks for are left (a removal needs no box)
                const u32 want = ValidSets(spatial, m.sets, false, spatial.MutableStats());
                u32 held = 0;
                for (SetId id = 0; id < spatial.SetCount(); ++id)
                {
                    if (proxy.handle[id] != kInvalidSetHandle && ((want >> id) & 1u) == 0)
                    {
                        RemoveItem(spatial, id, proxy.handle[id]);
                    }
                    held |= proxy.handle[id] != kInvalidSetHandle ? (1u << id) : 0u;
                }
                proxy.sets = static_cast<u8>(held);
                return;
            }
            Reconcile(spatial, entity, m, proxy, box, is_static, count_unknown);
        };
        q.edited->ForEach(reconcile);
        count_unknown = false;
        // A tag change moves the entity to another archetype, which the arrival stamp of its proxy shows.
        q.arrived->ForEach(reconcile);
    }

    template <class Q> void JoinNew(ecs::SystemContext& context, SpatialWorld& spatial, Q& q)
    {
        constexpr bool is_static = Q::kStatic;
        SpatialWorldStats& stats = spatial.MutableStats();
        q.join->ForEach(
            [&](Entity entity, const SpatialBounds& bounds, const ecs::WorldTransform& w, const SpatialMembership& m)
            {
                const Aabb box = TransformAabb(bounds.local, w.matrix);
                if (!FiniteSyncBox(bounds.local) || !FiniteSyncBox(box) || m.layer >= 32)
                {
                    ++stats.refused_bounds;
                    return;
                }
                SpatialProxy proxy;
                proxy.layer = m.layer;
                proxy.is_static = is_static;
                Reconcile(spatial, entity, m, proxy, box, is_static, true);
                ++stats.joined;
                WorldBounds world_bounds;
                world_bounds.box = box;
                context.Commands().Add<SpatialProxy>(entity, proxy);
                context.Commands().Set<WorldBounds>(entity, world_bounds);
                pending_.Push(Pending{entity, proxy});
            });
    }

    // An entity that lost a component the sets need: out of every set, then the proxy and the derived bounds go.
    template <class Q> void LeaveAll(ecs::SystemContext& context, SpatialWorld& spatial, Q& q)
    {
        q.ForEach(
            [&](Entity entity, SpatialProxy& proxy)
            {
                if (proxy.leaving)
                {
                    return;
                }
                Leave(spatial, proxy);
                proxy.leaving = true;
                context.Commands().Remove<SpatialProxy>(entity);
                context.Commands().Remove<WorldBounds>(entity);
            });
    }

    // Recomputes WorldBounds for the chunks whose transform or local box changed, writing (and stamping) only where the
    // box differs; a chunk loop that runs as jobs when the system has the frame thread to itself.
    template <class Q>
    void ComputeBounds(ecs::SystemContext& context, SpatialWorld& spatial, Q& q, Atomic<u64>& written,
                       Atomic<u64>& refused)
    {
        auto chunk = [&written, &refused](ecs::ChunkView& view, ecs::CommandBuffer&)
        {
            Span<const SpatialBounds> locals = view.Read<SpatialBounds>();
            Span<const ecs::WorldTransform> worlds = view.Read<ecs::WorldTransform>();
            Span<WorldBounds> boxes = view.Write<WorldBounds>();
            u64 wrote = 0;
            u64 bad = 0;
            for (usize i = 0; i < locals.Size(); ++i)
            {
                const Aabb box = TransformAabb(locals[i].local, worlds[i].matrix);
                if (!FiniteSyncBox(box))
                {
                    ++bad;  // the item keeps its old box
                }
                else if (!SameBox(box, boxes[i].box))
                {
                    boxes[i].box = box;
                    ++wrote;
                }
            }
            if (bad != 0)
            {
                refused.FetchAdd(bad, MemoryOrder::Relaxed);
            }
            if (wrote != 0)
            {
                view.MarkWritten<WorldBounds>();
                written.FetchAdd(wrote, MemoryOrder::Relaxed);
            }
        };
        if (spatial.Parallel())
        {
            context.ForEachChunkParallel(q, chunk);
        }
        else
        {
            ecs::CommandBuffer unused;
            q.ForEachChunk([&](ecs::ChunkView& view) { chunk(view, unused); });
        }
    }

    // Moves the items whose WorldBounds changed. The moved entities are listed in chunk order on one thread, then each
    // set applies the list to itself as one job (a set is written by one thread only, in list order, so the result does
    // not depend on the schedule; sets run side by side).
    void ApplyMoves(ecs::SystemContext& context, SpatialWorld& spatial)
    {
        SpatialWorldStats& stats = spatial.MutableStats();
        moves_.Clear();
        moved_->ForEach(
            [&](const WorldBounds& bounds, const SpatialProxy& proxy)
            {
                if (proxy.leaving)
                {
                    return;
                }
                if (!FiniteSyncBox(bounds.box))
                {  // a WorldBounds written by hand: the sync's own are finite
                    for (SetId id = 0; id < spatial.SetCount(); ++id)
                    {
                        if (proxy.handle[id] != kInvalidSetHandle)
                        {
                            ++stats.refused_bounds;  // one per item it would have moved
                        }
                    }
                    return;
                }
                MoveEntry entry;
                entry.box = bounds.box;
                for (SetId id = 0; id < kMaxSpatialSets; ++id)
                {
                    entry.handle[id] = id < spatial.SetCount() ? proxy.handle[id] : kInvalidSetHandle;
                }
                moves_.Push(entry);
            });
        if (moves_.Size() == 0)
        {
            return;
        }
        Array<u64, kMaxSpatialSets> moved{};
        Array<u64, kMaxSpatialSets> reinserted{};
        auto by_set = [&](usize first, usize last)
        {
            for (usize set_index = first; set_index < last; ++set_index)
            {
                const SetId id = static_cast<SetId>(set_index);
                SpatialSet* set = spatial.Set(id);
                u64 moves = 0;
                u64 reinserts = 0;
                for (usize i = 0; i < moves_.Size(); ++i)
                {
                    const SetHandle handle = moves_[i].handle[id];
                    if (handle == kInvalidSetHandle || !set->IsLive(handle) ||
                        SameBox(set->ItemOf(handle).box, moves_[i].box))
                    {
                        continue;
                    }
                    ++moves;
                    if (set->Move(handle, moves_[i].box))
                    {
                        ++reinserts;
                    }
                }
                moved[id] = moves;
                reinserted[id] = reinserts;
            }
        };
        if (spatial.Parallel())
        {
            context.ParallelFor(spatial.SetCount(), 1, by_set);
        }
        else
        {
            by_set(usize{0}, static_cast<usize>(spatial.SetCount()));
        }
        for (SetId id = 0; id < spatial.SetCount(); ++id)
        {
            stats.items_moved += moved[id];
            stats.items_reinserted += reinserted[id];
        }
    }

    void Sync(ecs::SystemContext& context, SpatialWorld& spatial)
    {
        SpatialWorldStats& stats = spatial.MutableStats();
        // 0. Items made last frame for entities that were destroyed before their proxy was applied.
        for (usize i = 0; i < pending_.Size(); ++i)
        {
            if (!context.Exists(pending_[i].entity))
            {
                Leave(spatial, pending_[i].proxy);
            }
        }
        pending_.Clear();

        // 1. Destroyed entities and removed proxies, from the world's removal log.
        const usize removed = context.RemovedCount<SpatialProxy>();
        for (usize i = 0; i < removed; ++i)
        {
            ecs::RemovedComponent<SpatialProxy> entry = context.RemovedAt<SpatialProxy>(i);
            if (entry.seq <= last_removal_seq_)
            {
                continue;
            }
            last_removal_seq_ = entry.seq;
            Leave(spatial, entry.value);
            ++stats.left;
        }

        // 2. Entities that lost a component.
        LeaveAll(context, spatial, *no_bounds_);
        LeaveAll(context, spatial, *no_transform_);
        LeaveAll(context, spatial, *no_membership_);

        // 3. Membership, layer and static tag changes.
        SyncEdits(spatial, yes_);
        SyncEdits(spatial, no_);

        // 4. Moved entities: the boxes (parallel), then the items (one thread).
        Atomic<u64> written{0};
        Atomic<u64> refused{0};
        ComputeBounds(context, spatial, *by_transform_, written, refused);
        ComputeBounds(context, spatial, *by_local_, written, refused);
        stats.bounds_written += written.Load(MemoryOrder::Relaxed);
        stats.refused_bounds += refused.Load(MemoryOrder::Relaxed);
        ApplyMoves(context, spatial);

        // 5. New entities.
        JoinNew(context, spatial, yes_);
        JoinNew(context, spatial, no_);

        // 6. A static tree is rebuilt only when its items changed (queries are exact meanwhile: they scan the pending
        // list and skip stale entries).
        for (SetId id = 0; id < spatial.SetCount(); ++id)
        {
            SpatialSet* set = spatial.Set(id);
            if (set->NeedsRebuild())
            {
                set->RebuildStatic();
                ++stats.static_rebuilds;
            }
        }

        yes_.join->Finish();
        yes_.edited->Finish();
        yes_.arrived->Finish();
        no_.join->Finish();
        no_.edited->Finish();
        no_.arrived->Finish();
        by_transform_->Finish();
        by_local_->Finish();
        moved_->Finish();
        no_bounds_->Finish();
        no_transform_->Finish();
        no_membership_->Finish();
    }

    struct Pending
    {
        Entity entity;
        SpatialProxy proxy;
    };

    Queries<With<SpatialStatic>> yes_;
    Queries<Without<SpatialStatic>> no_;
    UniquePtr<BoundsByTransform> by_transform_;
    UniquePtr<BoundsByLocal> by_local_;
    UniquePtr<MovedBounds> moved_;
    struct MoveEntry
    {
        Aabb box;
        Array<SetHandle, kMaxSpatialSets> handle;
    };
    Vector<MoveEntry> moves_;  // reused between frames
    UniquePtr<NoBounds> no_bounds_;
    UniquePtr<NoTransform> no_transform_;
    UniquePtr<NoMembership> no_membership_;
    Vector<Pending> pending_;
    bool built_ = false;
    u64 world_id_ = 0;
    u64 last_removal_seq_ = 0;
};

}  // namespace

ecs::SystemDesc MakeSpatialSyncSystem(const char* name)
{
    ecs::SystemDesc desc(name, ecs::detail::SystemFn(SpatialSync()));
    return Move(desc)
        .Reads<SpatialBounds, SpatialMembership, ecs::WorldTransform>()
        .Writes<SpatialProxy, WorldBounds>()
        .WritesResource<SpatialWorld>();
}

}  // namespace engine_placeholder::spatial
