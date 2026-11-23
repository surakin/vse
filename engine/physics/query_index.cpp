#include "physics/query_index.h"

#include "algorithm.h"
#include "lang/scalar.h"

namespace vse::physics
{

namespace
{

bool BoxesEqual(const Aabbd& a, const Aabbd& b)
{
    return a.min.x == b.min.x && a.min.y == b.min.y && a.min.z == b.min.z && a.max.x == b.max.x && a.max.y == b.max.y &&
           a.max.z == b.max.z;
}

// A box the set takes: not empty, corners finite (the set aborts on anything else).
bool Takeable(const Aabbd& box)
{
    return !box.IsEmpty() && IsFinite(box.min.x) && IsFinite(box.min.y) && IsFinite(box.min.z) && IsFinite(box.max.x) &&
           IsFinite(box.max.y) && IsFinite(box.max.z);
}

}  // namespace

// The dynamic side's margins: wider than the broad phase's, since a box that leaves its fat box costs a reinsertion at
// every step end while a wider box only adds candidates the query's own test then rejects.
spatial::SpatialSetConfig QueryIndexSetConfig()
{
    spatial::SpatialSetConfig config;
    config.bvh.fat_margin = 0.5f;
    config.bvh.predictive = 4.0f;
    return config;
}

QueryIndex::QueryIndex() : set_(QueryIndexSetConfig()) {}

void QueryIndex::Drop(u32 slot)
{
    Entry& e = entries_[slot];
    if (e.handle != spatial::kInvalidSetHandle)
    {
        set_.Remove(e.handle);
    }
    if (e.unindexed)
    {
        --unindexed_;
    }
    e = Entry();
}

void QueryIndex::Refresh(const PhysicsWorld& world, ColliderId id)
{
    if (id.index >= entries_.Size())
    {
        entries_.Resize(id.index + 1);
    }
    Entry& e = entries_[id.index];
    const ColliderRecord* record = world.FindCollider(id);
    const Body* body = record != nullptr ? world.Find(record->owner) : nullptr;
    if (body == nullptr)
    {
        if (e.generation == id.generation)
        {
            Drop(id.index);  // the collider is gone; a newer one in the slot is left alone
        }
        return;
    }
    Update(id, *record, *body);
}

void QueryIndex::Update(ColliderId id, const ColliderRecord& collider, const Body& owner)
{
    const ColliderRecord* record = &collider;
    const Body* body = &owner;
    Entry& e = entries_[id.index];
    if (e.generation != 0 && e.generation != id.generation)
    {
        Drop(id.index);  // the slot was reused
    }
    const Aabbd box = ColliderBounds(*body, record->value);
    const bool is_static = body->settings.type == BodyType::Static;
    if (e.generation != 0 && !e.unindexed && e.is_static == is_static)
    {
        e.asleep = body->sleeping;
        if (BoxesEqual(set_.ItemOf(e.handle).box, box))
        {
            return;
        }
        if (!is_static && Takeable(box))
        {
            reinsertions_ += set_.Move(e.handle, box) ? 1 : 0;
            ++refreshes_;
            return;
        }
    }
    if (e.generation != 0)
    {
        Drop(id.index);  // a static collider moved, its class changed, or it was not indexed: added again below
    }
    ++refreshes_;
    ++reinsertions_;
    Entry fresh;
    fresh.generation = id.generation;
    fresh.is_static = is_static;
    fresh.asleep = body->sleeping;
    if (Takeable(box))
    {
        fresh.handle = is_static ? set_.AddStatic(box, id.index, 0) : set_.AddDynamic(box, id.index, 0);
    }
    if (fresh.handle == spatial::kInvalidSetHandle)
    {
        fresh.unindexed = true;
        ++unindexed_;
    }
    entries_[id.index] = fresh;
    if (is_static)
    {
        // Static colliders wait in a list the queries scan until the tree is rebuilt: rebuild once the list is a
        // quarter of the static colliders, so a world built and queried without a step does not stay linear.
        const spatial::SpatialSetStats stats = set_.Stats();
        if (stats.pending_static >= 64 && stats.pending_static * 4 >= stats.static_items)
        {
            set_.RebuildStatic();
        }
    }
}

void QueryIndex::RefreshMoving(const PhysicsWorld& world)
{
    const u32 slots = world.Stats().collider_slots;
    for (u32 slot = 0; slot < slots; ++slot)
    {
        const ColliderId id = world.ColliderAtSlot(slot);
        if (!id.IsValid())
        {
            continue;
        }
        const ColliderRecord* record = world.FindCollider(id);
        const Body* body = record != nullptr ? world.Find(record->owner) : nullptr;
        if (body == nullptr || body->settings.type == BodyType::Static)
        {
            continue;
        }
        // A body that slept at its last refresh and sleeps now did not move: a step leaves sleeping bodies alone, and
        // every change from outside (SetTransform, SetCollider, SetSettings) wakes the body and refreshes it.
        if (slot < entries_.Size() && body->sleeping && entries_[slot].asleep &&
            entries_[slot].generation == id.generation && !entries_[slot].unindexed)
        {
            continue;
        }
        if (slot >= entries_.Size())
        {
            entries_.Resize(slot + 1);
        }
        Update(id, *record, *body);
    }
    if (set_.NeedsRebuild())
    {
        set_.RebuildStatic();
    }
}

void QueryIndex::Rebuild(const PhysicsWorld& world)
{
    set_.Clear();
    entries_.Clear();
    unindexed_ = 0;
    const u32 slots = world.Stats().collider_slots;
    for (u32 slot = 0; slot < slots; ++slot)
    {
        const ColliderId id = world.ColliderAtSlot(slot);
        if (id.IsValid())
        {
            Refresh(world, id);
        }
    }
    set_.RebuildStatic();
}

bool QueryIndex::Holds(u32 slot, u32 generation, const Aabbd& box) const
{
    if (slot >= entries_.Size())
    {
        return false;
    }
    const Entry& e = entries_[slot];
    if (e.generation != generation)
    {
        return false;
    }
    return e.unindexed || (set_.IsLive(e.handle) && BoxesEqual(set_.ItemOf(e.handle).box, box));
}

QueryIndexStats QueryIndex::Stats() const
{
    QueryIndexStats out;
    out.indexed = set_.ItemCount();
    out.unindexed = unindexed_;
    out.refreshes = refreshes_;
    out.reinsertions = reinsertions_;
    return out;
}

void QueryIndex::SortSlots(Vector<u32>* slots)
{
    Sort(Span<u32>(slots->Data(), slots->Size()), [](u32 a, u32 b) { return a < b; });
}

}  // namespace vse::physics
