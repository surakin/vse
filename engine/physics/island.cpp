// Islands and sleeping (Phase 25 Day 400). Everything is ordered by body slot, so the result does not depend on the
// order anything was visited or created in.
#include "physics/broad_phase.h"
#include "physics/world.h"

namespace engine_placeholder::physics
{

namespace
{

// The root of the set of x, halving the path as it goes.
u32 FindRoot(Vector<u32>& parent, u32 x)
{
    while (parent[x] != x)
    {
        parent[x] = parent[parent[x]];
        x = parent[x];
    }
    return x;
}

// Joins two sets; the root is always the lower slot.
void Unite(Vector<u32>& parent, u32 a, u32 b)
{
    const u32 ra = FindRoot(parent, a);
    const u32 rb = FindRoot(parent, b);
    if (ra < rb)
    {
        parent[rb] = ra;
    }
    else if (rb < ra)
    {
        parent[ra] = rb;
    }
}

}  // namespace

bool PhysicsWorld::Disturbing(const Body& body)
{
    switch (body.settings.type)
    {
    case BodyType::Dynamic: return !body.sleeping;
    case BodyType::Kinematic:
        return body.disturbed || body.linear_velocity.LengthSquared() > 0.0f ||
               body.angular_velocity.LengthSquared() > 0.0f;
    case BodyType::Static: return body.disturbed;
    }
    return false;
}

void PhysicsWorld::WakeIsland(Body* body)
{
    body->sleep_time = 0.0f;
    if (!body->sleeping)
    {
        return;
    }
    const u32 island = body->island;
    const u32 slots = bodies_.Slots();
    for (u32 slot = 0; slot < slots; ++slot)
    {
        Body* other = bodies_.Find(slot, bodies_.LiveGeneration(slot));
        if (other != nullptr && other->sleeping && other->island == island)
        {
            other->sleeping = false;
            other->sleep_time = 0.0f;
            ++step_stats_.bodies_woken;
        }
    }
    body->sleeping = false;
}

void PhysicsWorld::WakeNeighbours(BodyId id)
{
    if (step_stats_.sleeping_bodies == 0 || broad_.Get() == nullptr)
    {
        return;
    }
    const Vector<ColliderPair>& pairs = broad_->Pairs();
    for (usize i = 0; i < pairs.Size(); ++i)
    {
        const ColliderRecord* ra = FindCollider(pairs[i].a);
        const ColliderRecord* rb = FindCollider(pairs[i].b);
        if (ra == nullptr || rb == nullptr)
        {
            continue;
        }
        Body* other = nullptr;
        if (ra->owner == id)
        {
            other = Find(rb->owner);
        }
        else if (rb->owner == id)
        {
            other = Find(ra->owner);
        }
        if (other != nullptr && other->sleeping)
        {
            WakeIsland(other);
        }
    }
}

void PhysicsWorld::Disturb(Body* body)
{
    const bool moves_things = body->settings.type != BodyType::Dynamic;
    WakeIsland(body);
    body->disturbed = true;
    if (moves_things)
    {
        WakeNeighbours(body->id);
    }
}

BodyStatus PhysicsWorld::IslandOf(BodyId id, u32* island) const
{
    const Body* body = Find(id);
    if (body == nullptr)
    {
        return BodyStatus::StaleId;
    }
    *island = body->island;
    return BodyStatus::Ok;
}

BodyStatus PhysicsWorld::Wake(BodyId id)
{
    Body* body = Find(id);
    if (body == nullptr)
    {
        return BodyStatus::StaleId;
    }
    WakeIsland(body);
    return BodyStatus::Ok;
}

BodyStatus PhysicsWorld::Sleep(BodyId id)
{
    Body* body = Find(id);
    if (body == nullptr)
    {
        return BodyStatus::StaleId;
    }
    if (body->settings.type != BodyType::Dynamic)
    {
        return BodyStatus::WrongType;
    }
    const auto put_to_sleep = [](Body* b)
    {
        b->sleeping = true;
        b->linear_velocity = Vec3(0.0f, 0.0f, 0.0f);
        b->angular_velocity = Vec3(0.0f, 0.0f, 0.0f);
        b->previous_position = b->position;
        b->previous_orientation = b->orientation;
    };
    if (body->island == 0)
    {
        put_to_sleep(body);
        return BodyStatus::Ok;
    }
    const u32 island = body->island;
    const u32 slots = bodies_.Slots();
    for (u32 slot = 0; slot < slots; ++slot)
    {
        Body* other = bodies_.Find(slot, bodies_.LiveGeneration(slot));
        if (other != nullptr && !other->sleeping && other->settings.type == BodyType::Dynamic &&
            other->island == island)
        {
            put_to_sleep(other);
        }
    }
    return BodyStatus::Ok;
}

void PhysicsWorld::UpdateSleep(const Vector<u32>& edges, f32 dt)
{
    const u32 slots = bodies_.Slots();
    Vector<u32> parent;
    parent.Reserve(slots);
    for (u32 slot = 0; slot < slots; ++slot)
    {
        parent.Push(slot);
    }
    const auto awake = [&](const Body* body)
    { return body != nullptr && body->settings.type == BodyType::Dynamic && !body->sleeping; };
    // The rest timers, and the bodies that touch something moving (an edge from a body to itself) stay awake.
    Vector<u8> restless;
    restless.Resize(slots);
    for (usize i = 0; i + 1 < edges.Size(); i += 2)
    {
        if (edges[i] == edges[i + 1])
        {
            restless[edges[i]] = 1;
        }
        else
        {
            Unite(parent, edges[i], edges[i + 1]);
        }
    }
    for (u32 slot = 0; slot < joints_.Slots(); ++slot)
    {
        const JointRecord* joint = joints_.Find(slot, joints_.LiveGeneration(slot));
        if (joint == nullptr || !joint->desc.body_a.IsValid() || !joint->desc.body_b.IsValid())
        {
            continue;
        }
        if (awake(Find(joint->desc.body_a)) && awake(Find(joint->desc.body_b)))
        {
            Unite(parent, joint->desc.body_a.index, joint->desc.body_b.index);
        }
    }
    const f32 linear2 = step_config_.sleep_linear * step_config_.sleep_linear;
    const f32 angular2 = step_config_.sleep_angular * step_config_.sleep_angular;
    Vector<f32> rest;  // the shortest rest in each island, at the root's slot
    rest.Resize(slots);
    for (u32 slot = 0; slot < slots; ++slot)
    {
        rest[slot] = 1e30f;
    }
    Vector<u8> blocked;
    blocked.Resize(slots);
    for (u32 slot = 0; slot < slots; ++slot)
    {
        Body* body = bodies_.Find(slot, bodies_.LiveGeneration(slot));
        if (body == nullptr || body->settings.type != BodyType::Dynamic)
        {
            if (body != nullptr)
            {
                body->island = 0;
            }
            continue;
        }
        if (body->sleeping)
        {
            continue;  // its island keeps its number
        }
        const bool still = restless[slot] == 0 && (body->settings.flags & kBodyNoSleep) == 0 &&
                           body->linear_velocity.LengthSquared() < linear2 &&
                           body->angular_velocity.LengthSquared() < angular2;
        body->sleep_time = still ? body->sleep_time + dt : 0.0f;
        const u32 root = FindRoot(parent, slot);
        body->island = root + 1;
        if (body->sleep_time < rest[root])
        {
            rest[root] = body->sleep_time;
        }
        if ((body->settings.flags & kBodyNoSleep) != 0)
        {
            blocked[root] = 1;
        }
    }
    u32 islands = 0;
    u32 sleeping = 0;
    for (u32 slot = 0; slot < slots; ++slot)
    {
        Body* body = bodies_.Find(slot, bodies_.LiveGeneration(slot));
        if (body == nullptr || body->settings.type != BodyType::Dynamic)
        {
            continue;
        }
        if (!body->sleeping)
        {
            const u32 root = body->island - 1;
            if (root == slot)
            {
                ++islands;
            }
            if (step_config_.allow_sleep && blocked[root] == 0 && rest[root] >= step_config_.sleep_time)
            {
                body->sleeping = true;
                body->linear_velocity = Vec3(0.0f, 0.0f, 0.0f);
                body->angular_velocity = Vec3(0.0f, 0.0f, 0.0f);
                body->previous_position = body->position;
                body->previous_orientation = body->orientation;
                if (root == slot)
                {
                    ++step_stats_.islands_slept;
                }
            }
        }
        else if (body->island == slot + 1)
        {
            ++islands;
        }
        if (body->sleeping)
        {
            ++sleeping;
        }
    }
    step_stats_.islands = islands;
    step_stats_.sleeping_bodies = sleeping;
}

}  // namespace engine_placeholder::physics
