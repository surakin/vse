#include "algorithm.h"
#include "lang/utility.h"
#include "physics/broad_phase.h"
#include "physics/collide.h"
#include "physics/query_index.h"
#include "physics/world.h"

namespace vse::physics
{

namespace
{

bool ContactLess(const ContactRecord& x, const ContactRecord& y)
{
    const u64 xa = x.a.ToKey();
    const u64 ya = y.a.ToKey();
    return xa != ya ? xa < ya : x.b.ToKey() < y.b.ToKey();
}

bool TriggerLess(const TriggerEvent& x, const TriggerEvent& y)
{
    const u64 xa = x.trigger.ToKey();
    const u64 ya = y.trigger.ToKey();
    return xa != ya ? xa < ya : x.other.ToKey() < y.other.ToKey();
}

}  // namespace

PhysicsStats PhysicsWorld::AllStats() const
{
    PhysicsStats stats;
    stats.world = Stats();
    stats.step = step_stats_;
    stats.solver = solver_.Stats();
    stats.joints = JointCount();
    stats.contacts = static_cast<u32>(contacts_.Size());
    stats.events = static_cast<u32>(events_.Count());
    stats.broad_pairs = broad_.Get() != nullptr ? static_cast<u32>(broad_->Pairs().Size()) : 0;
    return stats;
}

void PhysicsWorld::ClearEvents()
{
    events_.contacts.Clear();
    events_.triggers.Clear();
    events_.joint_breaks.Clear();
}

void PhysicsWorld::DropEventsOf(BodyId id)
{
    const auto drop = [&](auto& list, auto&& names)
    {
        usize kept = 0;
        for (usize i = 0; i < list.Size(); ++i)
        {
            if (!names(list[i]))
            {
                if (kept != i)
                {
                    list[kept] = list[i];
                }
                ++kept;
            }
        }
        while (list.Size() > kept)
        {
            list.Pop();
        }
    };
    drop(events_.contacts, [&](const ContactEvent& e) { return e.body_a == id || e.body_b == id; });
    drop(events_.triggers, [&](const TriggerEvent& e) { return e.body_trigger == id || e.body_other == id; });
    drop(events_.joint_breaks, [&](const JointBreakEvent& e) { return e.body_a == id || e.body_b == id; });
    // The pairs that touched it are forgotten without an end event: the body is gone.
    drop(contacts_, [&](const ContactRecord& r) { return r.body_a == id || r.body_b == id; });
    drop(triggers_touching_, [&](const TriggerEvent& e) { return e.body_trigger == id || e.body_other == id; });
}

void PhysicsWorld::UpdateContactEvents(const Vector<ContactInput>& inputs, const Vector<ColliderPair>& carried)
{
    Vector<ContactRecord> next;
    for (usize i = 0; i < inputs.Size(); ++i)
    {
        const ContactInput& input = inputs[i];
        ContactRecord record;
        record.a = input.a;
        record.b = input.b;
        record.body_a = input.body_a;
        record.body_b = input.body_b;
        record.manifold = input.manifold;
        record.normal_impulse = solver_.PairImpulse(input.a, input.b);
        next.Push(record);
    }
    // A pair of resting bodies is not collided again: it keeps touching as it did.
    for (usize i = 0; i < carried.Size(); ++i)
    {
        ContactRecord probe;
        probe.a = carried[i].a;
        probe.b = carried[i].b;
        usize lo = 0;
        usize hi = contacts_.Size();
        while (lo < hi)
        {
            const usize mid = (lo + hi) / 2;
            if (ContactLess(contacts_[mid], probe))
            {
                lo = mid + 1;
            }
            else
            {
                hi = mid;
            }
        }
        if (lo < contacts_.Size() && !ContactLess(probe, contacts_[lo]))
        {
            next.Push(contacts_[lo]);
        }
    }
    Sort(Span<ContactRecord>(next.Data(), next.Size()), ContactLess);
    const auto emit = [&](ContactEventKind kind, const ContactRecord& r, bool summary)
    {
        ContactEvent e;
        e.kind = kind;
        e.a = r.a;
        e.b = r.b;
        e.body_a = r.body_a;
        e.body_b = r.body_b;
        const Body* ba = Find(r.body_a);
        const Body* bb = Find(r.body_b);
        e.entity_a = ba != nullptr ? ba->entity : ecs::Entity();
        e.entity_b = bb != nullptr ? bb->entity : ecs::Entity();
        if (summary && r.manifold.count > 0)
        {
            u32 deepest = 0;
            for (u32 k = 1; k < r.manifold.count; ++k)
            {
                if (r.manifold.points[k].depth > r.manifold.points[deepest].depth)
                {
                    deepest = k;
                }
            }
            e.points = r.manifold.count;
            e.point = r.manifold.anchor + Vec3d(r.manifold.points[deepest].position_a);
            e.normal = r.manifold.normal;
            e.depth = r.manifold.points[deepest].depth;
            e.normal_impulse = r.normal_impulse;
        }
        events_.contacts.Push(e);
    };
    usize i = 0;
    usize j = 0;
    while (i < contacts_.Size() || j < next.Size())
    {
        if (j == next.Size() || (i < contacts_.Size() && ContactLess(contacts_[i], next[j])))
        {
            emit(ContactEventKind::End, contacts_[i], false);
            ++i;
        }
        else if (i == contacts_.Size() || ContactLess(next[j], contacts_[i]))
        {
            emit(ContactEventKind::Begin, next[j], true);
            ++j;
        }
        else
        {
            if (step_config_.stay_events)
            {
                emit(ContactEventKind::Stay, next[j], true);
            }
            ++i;
            ++j;
        }
    }
    contacts_ = Move(next);
}

void PhysicsWorld::UpdateTriggerEvents(const Vector<ColliderPair>& pairs)
{
    Vector<TriggerEvent> next;
    for (usize i = 0; i < pairs.Size(); ++i)
    {
        const ColliderRecord* ra = FindCollider(pairs[i].a);
        const ColliderRecord* rb = FindCollider(pairs[i].b);
        if (ra == nullptr || rb == nullptr || ra->owner == rb->owner)
        {
            continue;
        }
        const Body* ba = Find(ra->owner);
        const Body* bb = Find(rb->owner);
        if (ba == nullptr || bb == nullptr ||
            (ba->settings.type == BodyType::Static && bb->settings.type == BodyType::Static))
        {
            continue;
        }
        const bool a_is_trigger = ra->value.is_trigger;
        TriggerEvent t;
        t.enter = true;
        t.trigger = a_is_trigger ? pairs[i].a : pairs[i].b;
        t.other = a_is_trigger ? pairs[i].b : pairs[i].a;
        t.body_trigger = a_is_trigger ? ra->owner : rb->owner;
        t.body_other = a_is_trigger ? rb->owner : ra->owner;
        t.entity_trigger = a_is_trigger ? ba->entity : bb->entity;
        t.entity_other = a_is_trigger ? bb->entity : ba->entity;
        bool inside = false;
        if (!Disturbing(*ba) && !Disturbing(*bb))
        {
            // Two bodies at rest: nothing changed since the last step.
            usize lo = 0;
            usize hi = triggers_touching_.Size();
            while (lo < hi)
            {
                const usize mid = (lo + hi) / 2;
                if (TriggerLess(triggers_touching_[mid], t))
                {
                    lo = mid + 1;
                }
                else
                {
                    hi = mid;
                }
            }
            inside = lo < triggers_touching_.Size() && !TriggerLess(t, triggers_touching_[lo]);
        }
        else
        {
            const Pose pose_a = PoseFrom(*ba, ra->value, ba->position);
            const Pose pose_b = PoseFrom(*bb, rb->value, ba->position);
            ContactManifold manifold;
            inside = Collide(ra->value.shape, pose_a, rb->value.shape, pose_b, 0.0f, &manifold, &hulls_, &meshes_,
                             &compounds_, &height_fields_) == CollideResult::Contact;
        }
        if (inside)
        {
            next.Push(t);
        }
    }
    Sort(Span<TriggerEvent>(next.Data(), next.Size()), TriggerLess);
    usize i = 0;
    usize j = 0;
    while (i < triggers_touching_.Size() || j < next.Size())
    {
        if (j == next.Size() || (i < triggers_touching_.Size() && TriggerLess(triggers_touching_[i], next[j])))
        {
            TriggerEvent e = triggers_touching_[i];
            e.enter = false;
            events_.triggers.Push(e);
            ++i;
        }
        else if (i == triggers_touching_.Size() || TriggerLess(next[j], triggers_touching_[i]))
        {
            events_.triggers.Push(next[j]);
            ++j;
        }
        else
        {
            ++i;
            ++j;
        }
    }
    triggers_touching_ = Move(next);
}

}  // namespace vse::physics
