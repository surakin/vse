#include "physics/broad_phase.h"

namespace engine_placeholder::physics
{

namespace
{

spatial::SpatialSetConfig SetConfig(const BroadPhaseConfig& config)
{
    spatial::SpatialSetConfig out;
    out.bvh.fat_margin = config.fat_margin;
    return out;
}

spatial::PairTrackerConfig TrackerConfig(const BroadPhaseConfig& config)
{
    spatial::PairTrackerConfig out;
    out.margin = config.pair_margin;
    for (u32 a = 0; a < 32; ++a)
    {
        out.meets[a] = config.filter.meets[a];
    }
    return out;
}

bool SameBox(const Aabb& a, const Aabb& b)
{
    return a.min.x == b.min.x && a.min.y == b.min.y && a.min.z == b.min.z && a.max.x == b.max.x && a.max.y == b.max.y &&
           a.max.z == b.max.z;
}

// The world box of a collider: its shape at the body's place, and for a continuous body also where the body goes in one
// step.
Aabb WorldBox(const Body& body, const Collider& collider, f32 step_seconds)
{
    const Mat3 r = body.orientation.ToMat3();
    const Vec3 origin = body.position + r * collider.offset;
    Aabb box = collider.shape.Bounds(origin, body.orientation * collider.rotation);
    if ((body.settings.flags & kBodyContinuous) != 0 && body.settings.type != BodyType::Static)
    {
        const Vec3 move = body.linear_velocity * step_seconds;
        box = Union(box, Aabb{box.min + move, box.max + move});
    }
    return box;
}

}  // namespace

void CollisionFilter::SetMeets(u32 a, u32 b, bool value)
{
    if (a >= 32 || b >= 32)
    {
        return;
    }
    if (value)
    {
        meets[a] |= 1u << b;
        meets[b] |= 1u << a;
    }
    else
    {
        meets[a] &= ~(1u << b);
        meets[b] &= ~(1u << a);
    }
}

BroadPhase::BroadPhase(const BroadPhaseConfig& config)
    : config_(config), set_(SetConfig(config)), tracker_(TrackerConfig(config))
{
}

void BroadPhase::Remove(u32 slot)
{
    Tracked& t = tracked_[slot];
    set_.Remove(t.handle);
    tracker_.Remove(ColliderId{slot, t.generation}.ToKey());
    t = Tracked();
    ++stats_.removed;
}

void BroadPhase::Update(const PhysicsWorld& world)
{
    stats_ = BroadPhaseStats();
    const u32 slots = world.Stats().collider_slots;
    if (tracked_.Size() < slots)
    {
        tracked_.Resize(slots);
    }

    // 1. Bring the set in line with the colliders, in slot order.
    for (u32 slot = 0; slot < slots; ++slot)
    {
        const ColliderId id = world.ColliderAtSlot(slot);
        Tracked& t = tracked_[slot];
        if (!id.IsValid())
        {
            if (t.generation != 0)
            {
                Remove(slot);
            }
            continue;
        }
        const ColliderRecord* record = world.FindCollider(id);
        const Body* body = record != nullptr ? world.Find(record->owner) : nullptr;
        if (body == nullptr)
        {
            if (t.generation != 0)
            {
                Remove(slot);
            }
            continue;
        }
        if (t.generation != 0 && t.generation != id.generation)
        {
            Remove(slot);  // the slot was reused by another collider
        }
        const Aabb box = WorldBox(*body, record->value, config_.step_seconds);
        const bool is_static = body->settings.type == BodyType::Static;
        const u8 layer = record->value.layer;
        if (t.generation == 0)
        {
            t.handle = is_static ? set_.AddStatic(box, id.ToKey(), layer) : set_.AddDynamic(box, id.ToKey(), layer);
            t.generation = id.generation;
            t.box = box;
            t.is_static = is_static;
            t.layer = layer;
            tracker_.Touch(t.handle);
            ++stats_.added;
            continue;
        }
        if (t.layer != layer)
        {
            // A layer is fixed when an item is added: take the item out and put it back.
            Remove(slot);
            t.handle = is_static ? set_.AddStatic(box, id.ToKey(), layer) : set_.AddDynamic(box, id.ToKey(), layer);
            t.generation = id.generation;
            t.box = box;
            t.is_static = is_static;
            t.layer = layer;
            tracker_.Touch(t.handle);
            ++stats_.added;
            continue;
        }
        if (t.is_static == is_static && SameBox(t.box, box))
        {
            continue;  // nothing moved: this costs nothing
        }
        if (t.is_static && !is_static)
        {
            set_.MakeDynamic(t.handle);
            set_.Move(t.handle, box);
        }
        else if (!t.is_static && is_static)
        {
            set_.Move(t.handle, box);
            set_.MakeStatic(t.handle);
        }
        else if (is_static)
        {
            // A static body that was moved: through the dynamic side and back (a static item refuses `Move`), and the
            // tree is rebuilt below.
            set_.MakeDynamic(t.handle);
            set_.Move(t.handle, box);
            set_.MakeStatic(t.handle);
        }
        else
        {
            set_.Move(t.handle, box);
        }
        t.box = box;
        t.is_static = is_static;
        tracker_.Touch(t.handle);
        ++stats_.moved;
    }

    // 2. The static tree must be current before any query: pending statics are scanned by every query.
    if (set_.NeedsRebuild())
    {
        set_.RebuildStatic();
    }

    // 3. The pairs.
    tracker_.Update(set_, &events_);
    begun_.Clear();
    ended_.Clear();
    // The group and mask words and the trigger rule apply on top of the layer matrix; an event for a collider that is
    // gone (a pair that ends because a body was destroyed) passes.
    const auto allowed = [&](const ColliderId a, const ColliderId b, bool* trigger)
    {
        const ColliderRecord* ra = world.FindCollider(a);
        const ColliderRecord* rb = world.FindCollider(b);
        *trigger = (ra != nullptr && ra->value.is_trigger) || (rb != nullptr && rb->value.is_trigger);
        if (ra == nullptr || rb == nullptr)
        {
            return true;
        }
        if (ra->value.is_trigger && rb->value.is_trigger && !config_.trigger_pairs)
        {
            return false;
        }
        return GroupsCollide(ra->value.group, ra->value.mask, rb->value.group, rb->value.mask);
    };
    for (usize i = 0; i < events_.begin.Size(); ++i)
    {
        const ColliderId a = ColliderId::FromKey(events_.begin[i].a);
        const ColliderId b = ColliderId::FromKey(events_.begin[i].b);
        bool trigger = false;
        if (allowed(a, b, &trigger))
        {
            begun_.Push(ColliderPair{a, b, trigger});
        }
    }
    for (usize i = 0; i < events_.end.Size(); ++i)
    {
        const ColliderId a = ColliderId::FromKey(events_.end[i].a);
        const ColliderId b = ColliderId::FromKey(events_.end[i].b);
        bool trigger = false;
        if (allowed(a, b, &trigger))
        {
            ended_.Push(ColliderPair{a, b, trigger});
        }
    }
    pairs_.Clear();
    tracker_.ForEachPair(
        [&](const spatial::KeyPair& pair)
        {
            const ColliderId a = ColliderId::FromKey(pair.a);
            const ColliderId b = ColliderId::FromKey(pair.b);
            const ColliderRecord* ra = world.FindCollider(a);
            const ColliderRecord* rb = world.FindCollider(b);
            if (ra == nullptr || rb == nullptr || ra->owner == rb->owner)
            {
                return;
            }
            const Body* ba = world.Find(ra->owner);
            const Body* bb = world.Find(rb->owner);
            bool trigger = false;
            if (!allowed(a, b, &trigger))
            {
                return;
            }
            const bool any_dynamic = ba->settings.type == BodyType::Dynamic || bb->settings.type == BodyType::Dynamic;
            const bool both_static = ba->settings.type == BodyType::Static && bb->settings.type == BodyType::Static;
            if (both_static || (!any_dynamic && !trigger))
            {
                return;
            }
            pairs_.Push(ColliderPair{a, b, trigger});
        });
    stats_.begun = static_cast<u32>(begun_.Size());
    stats_.ended = static_cast<u32>(ended_.Size());
    stats_.pairs = static_cast<u32>(tracker_.PairCount());
    const spatial::SpatialSetStats set_stats = set_.Stats();
    stats_.static_rebuilds = set_stats.static_rebuilds;
    stats_.pending_static = set_stats.pending_static;
}

}  // namespace engine_placeholder::physics
