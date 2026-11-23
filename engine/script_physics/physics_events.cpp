#include "script_physics/physics_events.h"

#include "script/value.h"

namespace vse::script_physics
{

namespace
{

const char* ContactName(physics::ContactEventKind kind)
{
    switch (kind)
    {
    case physics::ContactEventKind::Begin: return kEventCollisionBegin;
    case physics::ContactEventKind::Stay: return kEventCollisionStay;
    case physics::ContactEventKind::End: return kEventCollisionEnd;
    }
    return kEventCollisionBegin;
}

bool LayerAllowed(const physics::PhysicsWorld& world, physics::ColliderId other, u32 mask)
{
    if (mask == 0xFFFFFFFFu)
    {
        return true;
    }
    const physics::ColliderRecord* record = world.FindCollider(other);
    return record != nullptr && (mask & (1u << record->value.layer)) != 0;
}

// The entity of an event's body: the event's own, or the character's whose body it is.
ecs::Entity EntityOf(const PhysicsEventOptions& options, ecs::Entity named, physics::BodyId body)
{
    if (named.IsValid() || options.characters == nullptr)
    {
        return named;
    }
    return options.characters->EntityOfBody(body);
}

// Who receives a contact event and what they see of the other side.
struct Side
{
    ecs::Entity self;
    ecs::Entity other;
    physics::ColliderId other_collider;
    f32 sign;  // +1 when the normal already points from self to other, -1 when it must be flipped
};

}  // namespace

PhysicsEventReport EmitPhysicsEvents(const physics::PhysicsWorld& world, script::ScriptRuntime* runtime,
                                     const PhysicsEventOptions& options)
{
    PhysicsEventReport report;
    script::EventBus& bus = runtime->Events();
    const physics::PhysicsEvents& events = world.Events();
    if (options.contacts)
    {
        for (usize i = 0; i < events.contacts.Size(); ++i)
        {
            const physics::ContactEvent& event = events.contacts[i];
            const ecs::Entity entity_a = EntityOf(options, event.entity_a, event.body_a);
            const ecs::Entity entity_b = EntityOf(options, event.entity_b, event.body_b);
            if (!entity_a.IsValid() || !entity_b.IsValid())
            {
                continue;
            }
            const StringView name(ContactName(event.kind));
            Side sides[2] = {{entity_a, entity_b, event.b, 1.0f}, {entity_b, entity_a, event.a, -1.0f}};
            if (sides[1].self.ToU64() < sides[0].self.ToU64())
            {
                const Side swapped = sides[0];
                sides[0] = sides[1];
                sides[1] = swapped;
            }
            for (u32 s = 0; s < 2; ++s)
            {
                const Side& side = sides[s];
                const u64 key = side.self.ToU64();
                if (!runtime->HasHandler(key, name))
                {
                    ++report.unhandled;
                    continue;
                }
                if (!LayerAllowed(world, side.other_collider, options.layer_mask))
                {
                    ++report.filtered;
                    continue;
                }
                script::Value payload = script::Value::MakeMap();
                payload.Set("other", script::Value::FromEntity(side.other));
                payload.Set("point", script::Value::FromVec3d(event.point));
                payload.Set("normal", script::Value::FromVec3(event.normal * side.sign));
                payload.Set("depth", script::Value::FromFloat(event.depth));
                payload.Set("impulse", script::Value::FromFloat(event.normal_impulse));
                payload.Set("points", script::Value::FromInt(static_cast<i64>(event.points)));
                if (bus.Emit(name, Move(payload), key))
                {
                    ++report.emitted;
                }
            }
        }
    }
    if (options.triggers)
    {
        for (usize i = 0; i < events.triggers.Size(); ++i)
        {
            const physics::TriggerEvent& event = events.triggers[i];
            const ecs::Entity trigger = EntityOf(options, event.entity_trigger, event.body_trigger);
            const ecs::Entity other = EntityOf(options, event.entity_other, event.body_other);
            if (!trigger.IsValid() || !other.IsValid())
            {
                continue;
            }
            const StringView name(event.enter ? kEventTriggerEnter : kEventTriggerExit);
            const u64 key = trigger.ToU64();
            if (!runtime->HasHandler(key, name))
            {
                ++report.unhandled;
                continue;
            }
            if (!LayerAllowed(world, event.other, options.layer_mask))
            {
                ++report.filtered;
                continue;
            }
            script::Value payload = script::Value::MakeMap();
            payload.Set("other", script::Value::FromEntity(other));
            if (bus.Emit(name, Move(payload), key))
            {
                ++report.emitted;
            }
        }
    }
    return report;
}

}  // namespace vse::script_physics
