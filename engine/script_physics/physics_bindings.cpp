#include "script_physics/physics_bindings.h"

#include "containers/hash_map.h"
#include "containers/hash_set.h"
#include "cstr/mem_ops.h"
#include "ecs/transform.h"
#include "ecs/world.h"
#include "lang/scalar.h"
#include "physics/components.h"
#include "physics/character.h"
#include "physics/joint_sync.h"
#include "physics/query.h"
#include "physics/sync.h"
#include "script/merge_services.h"
#include "script/partition_context.h"

namespace vse::script_physics
{

using script::CommandKind;
using script::CommandKindTable;
using script::MergeContext;
using script::PartitionContext;
using script::ThreadRule;
using script::Value;
using script::ValueKind;

namespace
{

// ------------------------------------------------------------------ the context

PartitionContext* Context()
{
    PartitionContext* context = script::CurrentPartitionContext();
    if (context == nullptr || context->world == nullptr)
    {
        script::RaiseScriptError("no world is available to this script");
        return nullptr;
    }
    return context;
}

const PhysicsScriptServices* ServicesOf(const PartitionContext& context)
{
    return static_cast<const PhysicsScriptServices*>(context.extensions[script::kServicePhysics]);
}

// The services, or null with a script error when the host gave scripts no physics.
const PhysicsScriptServices* NeedServices(PartitionContext* context)
{
    const PhysicsScriptServices* services = ServicesOf(*context);
    if (services == nullptr || services->world == nullptr || services->layers == nullptr)
    {
        script::RaiseScriptError("this world has no physics");
        return nullptr;
    }
    return services;
}

void Raise(StringView a, StringView b = StringView(), StringView c = StringView(), StringView d = StringView(),
           StringView e = StringView())
{
    String<HeapAllocator> text(HeapAllocator::Default());
    text.Append(a);
    text.Append(b);
    text.Append(c);
    text.Append(d);
    text.Append(e);
    script::RaiseScriptError(text.View());
}

bool Finite3(const Vec3& v)
{
    return IsFinite(v.x) && IsFinite(v.y) && IsFinite(v.z);
}

// The body of an entity as of the tick start; raises when there is none.
bool BodyOfEntity(PartitionContext* context, const PhysicsScriptServices* services, ecs::Entity e, physics::BodyId* id)
{
    const physics::BodyLink* link = e.IsPlaceholder() ? nullptr : context->world->Get<physics::BodyLink>(e);
    if (link == nullptr || services->world->Find(link->id) == nullptr)
    {
        Raise("the entity has no physics body yet (it needs `rigid_body` and `collider`; the body exists after the "
              "next physics sync)");
        return false;
    }
    *id = link->id;
    return true;
}

bool Record(PartitionContext* context, u16 kind, ecs::Entity target, const void* payload, u32 size)
{
    if (context->commands == nullptr)
    {
        script::RaiseScriptError("this script cannot change the world (no command buffer)");
        return false;
    }
    if (!context->commands->Record(kind, context->current_entity_key, target, payload, size))
    {
        script::RaiseScriptError("the command buffer is full");
        return false;
    }
    return true;
}

// ------------------------------------------------------------------ the commands' payloads

// Plain floats and integers only, so a payload has no padding.
struct VectorPayload
{
    f32 value[3];
    f32 point[3];
    u32 at_point;
};

struct ScalarPayload
{
    f32 value;
};

VectorPayload MakeVector(const Vec3& value, const Vec3* point)
{
    VectorPayload p;
    p.value[0] = value.x;
    p.value[1] = value.y;
    p.value[2] = value.z;
    p.point[0] = point != nullptr ? point->x : 0.0f;
    p.point[1] = point != nullptr ? point->y : 0.0f;
    p.point[2] = point != nullptr ? point->z : 0.0f;
    p.at_point = point != nullptr ? 1u : 0u;
    return p;
}

// ------------------------------------------------------------------ writes

// A force, an impulse or a torque for a dynamic body.
void RecordPush(u16 kind, ecs::Entity e, const Vec3& value, const Vec3* point, const char* what)
{
    PartitionContext* context = Context();
    const PhysicsScriptServices* services = context != nullptr ? NeedServices(context) : nullptr;
    if (services == nullptr)
    {
        return;
    }
    if (!Finite3(value) || (point != nullptr && !Finite3(*point)))
    {
        Raise("the ", StringView(what), " must be finite");
        return;
    }
    physics::BodyId id;
    if (!BodyOfEntity(context, services, e, &id))
    {
        return;
    }
    if (services->world->Find(id)->settings.type != physics::BodyType::Dynamic)
    {
        Raise("a ", StringView(what), " needs a dynamic body");
        return;
    }
    const VectorPayload payload = MakeVector(value, point);
    Record(context, kind, e, &payload, sizeof(payload));
}

void PhysicsAddForce(ecs::Entity e, Vec3 force)
{
    RecordPush(kCommandPhysicsForce, e, force, nullptr, "force");
}

void PhysicsAddForceAt(ecs::Entity e, Vec3 force, Vec3 point)
{
    RecordPush(kCommandPhysicsForce, e, force, &point, "force");
}

void PhysicsAddImpulse(ecs::Entity e, Vec3 impulse)
{
    RecordPush(kCommandPhysicsImpulse, e, impulse, nullptr, "impulse");
}

void PhysicsAddImpulseAt(ecs::Entity e, Vec3 impulse, Vec3 point)
{
    RecordPush(kCommandPhysicsImpulse, e, impulse, &point, "impulse");
}

void PhysicsAddTorque(ecs::Entity e, Vec3 torque)
{
    RecordPush(kCommandPhysicsTorque, e, torque, nullptr, "torque");
}

void RecordVelocity(u16 kind, ecs::Entity e, const Vec3& velocity)
{
    PartitionContext* context = Context();
    const PhysicsScriptServices* services = context != nullptr ? NeedServices(context) : nullptr;
    if (services == nullptr)
    {
        return;
    }
    if (!Finite3(velocity))
    {
        script::RaiseScriptError("the velocity must be finite");
        return;
    }
    physics::BodyId id;
    if (!BodyOfEntity(context, services, e, &id))
    {
        return;
    }
    if (services->world->Find(id)->settings.type == physics::BodyType::Static)
    {
        script::RaiseScriptError("a static body has no velocity");
        return;
    }
    const VectorPayload payload = MakeVector(velocity, nullptr);
    Record(context, kind, e, &payload, sizeof(payload));
}

void PhysicsSetVelocity(ecs::Entity e, Vec3 velocity)
{
    RecordVelocity(kCommandPhysicsSetLinear, e, velocity);
}

void PhysicsSetAngularVelocity(ecs::Entity e, Vec3 velocity)
{
    RecordVelocity(kCommandPhysicsSetAngular, e, velocity);
}

void PhysicsSetGravityScale(ecs::Entity e, f32 scale)
{
    PartitionContext* context = Context();
    if (context == nullptr)
    {
        return;
    }
    if (!IsFinite(scale))
    {
        script::RaiseScriptError("the gravity scale must be finite");
        return;
    }
    if (e.IsPlaceholder() || !context->world->Has<physics::RigidBody>(e))
    {
        script::RaiseScriptError("the entity has no `rigid_body`");
        return;
    }
    const ScalarPayload payload{scale};
    Record(context, kCommandPhysicsGravityScale, e, &payload, sizeof(payload));
}

void RecordBodyCommand(u16 kind, ecs::Entity e)
{
    PartitionContext* context = Context();
    const PhysicsScriptServices* services = context != nullptr ? NeedServices(context) : nullptr;
    if (services == nullptr)
    {
        return;
    }
    physics::BodyId id;
    if (!BodyOfEntity(context, services, e, &id))
    {
        return;
    }
    Record(context, kind, e, nullptr, 0);
}

void PhysicsWake(ecs::Entity e)
{
    RecordBodyCommand(kCommandPhysicsWake, e);
}

void PhysicsSleep(ecs::Entity e)
{
    RecordBodyCommand(kCommandPhysicsSleep, e);
}

// ------------------------------------------------------------------ joints

bool ReadNumber(const Value& map, StringView what, StringView key, f32* out)
{
    const Value* v = map.Find(key);
    if (v == nullptr)
    {
        return true;
    }
    if (!v->IsNumber() || !IsFinite(static_cast<f32>(v->ToDouble())))
    {
        Raise("the ", what, "'s `", key, "` must be a finite number");
        return false;
    }
    *out = static_cast<f32>(v->ToDouble());
    return true;
}

bool ReadBool(const Value& map, StringView what, StringView key, bool* out)
{
    const Value* v = map.Find(key);
    if (v == nullptr)
    {
        return true;
    }
    if (v->Kind() != ValueKind::Bool)
    {
        Raise("the ", what, "'s `", key, "` must be true or false");
        return false;
    }
    *out = v->AsBool();
    return true;
}

bool ReadPoint(const Value& map, StringView what, StringView key, Vec3* out)
{
    const Value* v = map.Find(key);
    if (v == nullptr)
    {
        return true;
    }
    if (v->Kind() != ValueKind::Vec3 || !Finite3(v->AsVec3()))
    {
        Raise("the ", what, "'s `", key, "` must be a finite vec3");
        return false;
    }
    *out = v->AsVec3();
    return true;
}

bool ReadEntity(PartitionContext* context, const Value& map, StringView what, StringView key, ecs::Entity* out)
{
    const Value* v = map.Find(key);
    *out = ecs::Entity();
    if (v == nullptr || v->IsNil())
    {
        return true;
    }
    if (v->Kind() != ValueKind::Entity)
    {
        Raise("the ", what, "'s `", key, "` must be an entity or nil (the world)");
        return false;
    }
    *out = v->AsEntity();
    const bool alive = out->IsPlaceholder() ? (context->commands != nullptr && context->commands->IsReserved(*out))
                                            : context->world->Exists(*out);
    if (!alive)
    {
        Raise("the ", what, "'s `", key, "` is not an entity that exists");
        return false;
    }
    return true;
}

// `owner` is the entity the joint component goes on (any entity; often a third one that exists only for the joint).
void PhysicsAddJoint(ecs::Entity owner, Value desc)
{
    PartitionContext* context = Context();
    const PhysicsScriptServices* services = context != nullptr ? NeedServices(context) : nullptr;
    if (services == nullptr)
    {
        return;
    }
    if (desc.Kind() != ValueKind::Map)
    {
        script::RaiseScriptError("the joint is described by a table");
        return;
    }
    physics::Joint joint;
    const Value* type = desc.Find("type");
    if (type == nullptr || type->Kind() != ValueKind::Text)
    {
        script::RaiseScriptError("the joint needs a `type`: ball, distance, fixed or hinge");
        return;
    }
    const StringView name = type->Text();
    if (name == StringView("ball"))
    {
        joint.type = physics::JointType::Ball;
    }
    else if (name == StringView("distance"))
    {
        joint.type = physics::JointType::Distance;
    }
    else if (name == StringView("fixed"))
    {
        joint.type = physics::JointType::Fixed;
    }
    else if (name == StringView("hinge"))
    {
        joint.type = physics::JointType::Hinge;
    }
    else
    {
        Raise("the joint type `", name, "` is not ball, distance, fixed or hinge");
        return;
    }
    bool ok = ReadEntity(context, desc, "joint", "a", &joint.entity_a) &&
              ReadEntity(context, desc, "joint", "b", &joint.entity_b) &&
              ReadPoint(desc, "joint", "point_a", &joint.point_a) &&
              ReadPoint(desc, "joint", "point_b", &joint.point_b) && ReadPoint(desc, "joint", "axis", &joint.axis) &&
              ReadNumber(desc, "joint", "min_length", &joint.min_length) &&
              ReadNumber(desc, "joint", "max_length", &joint.max_length) &&
              ReadNumber(desc, "joint", "hertz", &joint.hertz) &&
              ReadNumber(desc, "joint", "damping_ratio", &joint.damping_ratio) &&
              ReadNumber(desc, "joint", "break_force", &joint.break_force) &&
              ReadNumber(desc, "joint", "break_torque", &joint.break_torque) &&
              ReadBool(desc, "joint", "collide_connected", &joint.collide_connected) &&
              ReadBool(desc, "joint", "enable_limit", &joint.enable_limit) &&
              ReadNumber(desc, "joint", "lower_angle", &joint.lower_angle) &&
              ReadNumber(desc, "joint", "upper_angle", &joint.upper_angle) &&
              ReadBool(desc, "joint", "enable_motor", &joint.enable_motor) &&
              ReadNumber(desc, "joint", "motor_speed", &joint.motor_speed) &&
              ReadNumber(desc, "joint", "max_motor_torque", &joint.max_motor_torque);
    if (!ok)
    {
        return;
    }
    if (!joint.entity_a.IsValid() && !joint.entity_b.IsValid())
    {
        script::RaiseScriptError("a joint needs at least one body (`a` or `b`)");
        return;
    }
    if (joint.min_length >= 0.0f && joint.max_length >= 0.0f && joint.max_length < joint.min_length)
    {
        script::RaiseScriptError("the joint's `max_length` is below its `min_length`");
        return;
    }
    if (joint.enable_limit && joint.upper_angle < joint.lower_angle)
    {
        script::RaiseScriptError("the joint's `upper_angle` is below its `lower_angle`");
        return;
    }
    Record(context, kCommandPhysicsAddJoint, owner, &joint, sizeof(joint));
}

void PhysicsRemoveJoint(ecs::Entity owner)
{
    PartitionContext* context = Context();
    if (context == nullptr)
    {
        return;
    }
    if (!owner.IsPlaceholder() && !context->world->Has<physics::Joint>(owner))
    {
        script::RaiseScriptError("the entity has no joint");
        return;
    }
    Record(context, kCommandPhysicsRemoveJoint, owner, nullptr, 0);
}

// ------------------------------------------------------------------ reads

Value PhysicsGetVelocity(ecs::Entity e)
{
    PartitionContext* context = Context();
    const PhysicsScriptServices* services = context != nullptr ? NeedServices(context) : nullptr;
    physics::BodyId id;
    if (services == nullptr || !BodyOfEntity(context, services, e, &id))
    {
        return Value();
    }
    return Value::FromVec3(services->world->Find(id)->linear_velocity);
}

Value PhysicsGetAngularVelocity(ecs::Entity e)
{
    PartitionContext* context = Context();
    const PhysicsScriptServices* services = context != nullptr ? NeedServices(context) : nullptr;
    physics::BodyId id;
    if (services == nullptr || !BodyOfEntity(context, services, e, &id))
    {
        return Value();
    }
    return Value::FromVec3(services->world->Find(id)->angular_velocity);
}

bool PhysicsIsSleeping(ecs::Entity e)
{
    PartitionContext* context = Context();
    const PhysicsScriptServices* services = context != nullptr ? NeedServices(context) : nullptr;
    physics::BodyId id;
    return services != nullptr && BodyOfEntity(context, services, e, &id) && services->world->IsSleeping(id);
}

// ------------------------------------------------------------------ layers and filters

i64 PhysicsLayer(StringView name)
{
    PartitionContext* context = Context();
    const PhysicsScriptServices* services = context != nullptr ? NeedServices(context) : nullptr;
    if (services == nullptr)
    {
        return 0;
    }
    u32 index = 0;
    if (!services->layers->Find(name, &index))
    {
        Raise("there is no physics layer named `", name, "`");
        return 0;
    }
    return index;
}

i64 PhysicsLayerMask(Value layers)
{
    PartitionContext* context = Context();
    const PhysicsScriptServices* services = context != nullptr ? NeedServices(context) : nullptr;
    if (services == nullptr)
    {
        return 0;
    }
    u32 mask = 0;
    String<HeapAllocator> error(HeapAllocator::Default());
    if (!services->layers->MaskOf(layers, &mask, &error))
    {
        script::RaiseScriptError(error.View());
        return 0;
    }
    return mask;
}

bool MakeFilter(PartitionContext* context, const PhysicsScriptServices* services, const Value& layers,
                ecs::Entity ignore, physics::PhysicsQueryFilter* filter, physics::BodyId* ignored)
{
    String<HeapAllocator> error(HeapAllocator::Default());
    if (!services->layers->MaskOf(layers, &filter->layer_mask, &error))
    {
        script::RaiseScriptError(error.View());
        return false;
    }
    if (ignore.IsValid() && !ignore.IsPlaceholder())
    {
        const physics::BodyLink* link = context->world->Get<physics::BodyLink>(ignore);
        if (link != nullptr)
        {
            *ignored = link->id;
            filter->ignore = Span<const physics::BodyId>(ignored, 1);
        }
    }
    return true;
}

ecs::Entity EntityOfBody(const physics::PhysicsWorld& world, physics::BodyId id)
{
    const physics::Body* body = world.Find(id);
    return body != nullptr ? body->entity : ecs::Entity();
}

Value HitValue(const physics::PhysicsWorld& world, const physics::PhysicsHit& hit)
{
    Value map = Value::MakeMap();
    map.Set("entity", Value::FromEntity(EntityOfBody(world, hit.body)));
    map.Set("point", Value::FromVec3(hit.point));
    map.Set("normal", Value::FromVec3(hit.normal));
    map.Set("distance", Value::FromFloat(hit.distance));
    return map;
}

Value Raycast(const Vec3& origin, const Vec3& direction, f32 max_distance, const Value& layers, ecs::Entity ignore)
{
    PartitionContext* context = Context();
    const PhysicsScriptServices* services = context != nullptr ? NeedServices(context) : nullptr;
    if (services == nullptr)
    {
        return Value();
    }
    if (!Finite3(origin) || !Finite3(direction) || direction.LengthSquared() == 0.0f || !IsFinite(max_distance) ||
        !(max_distance > 0.0f))
    {
        script::RaiseScriptError("the ray needs a finite origin, a direction that is not zero and a length above zero");
        return Value();
    }
    physics::PhysicsQueryFilter filter;
    physics::BodyId ignored;
    if (!MakeFilter(context, services, layers, ignore, &filter, &ignored))
    {
        return Value();
    }
    physics::PhysicsHit hit;
    if (!physics::RayCast(*services->world, origin, direction, max_distance, filter, &hit))
    {
        return Value();
    }
    return HitValue(*services->world, hit);
}

Value PhysicsRaycast(Vec3 origin, Vec3 direction, f32 max_distance, Value layers)
{
    return Raycast(origin, direction, max_distance, layers, ecs::Entity());
}

Value PhysicsRaycastEx(Vec3 origin, Vec3 direction, f32 max_distance, Value layers, Value ignore)
{
    if (!ignore.IsNil() && ignore.Kind() != ValueKind::Entity)
    {
        script::RaiseScriptError("`ignore` is an entity or nil");
        return Value();
    }
    return Raycast(origin, direction, max_distance, layers, ignore.IsNil() ? ecs::Entity() : ignore.AsEntity());
}

// A shape from a table: `{kind="sphere", radius=}`, `{kind="box", half_extents=}` or `{kind="capsule", half_height=,
// radius=}`.
bool ParseShape(const Value& desc, physics::Shape* shape)
{
    if (desc.Kind() != ValueKind::Map)
    {
        script::RaiseScriptError("the shape is a table: {kind=\"sphere\", radius=...}, box or capsule");
        return false;
    }
    const Value* kind = desc.Find("kind");
    if (kind == nullptr || kind->Kind() != ValueKind::Text)
    {
        script::RaiseScriptError("the shape needs a `kind`: sphere, box or capsule");
        return false;
    }
    f32 radius = 0.5f;
    f32 half_height = 0.0f;
    Vec3 half_extents(0.5f, 0.5f, 0.5f);
    if (!ReadNumber(desc, "shape", "radius", &radius) || !ReadNumber(desc, "shape", "half_height", &half_height) ||
        !ReadPoint(desc, "shape", "half_extents", &half_extents))
    {
        return false;
    }
    if (kind->Text() == StringView("sphere"))
    {
        *shape = physics::Shape::MakeSphere(radius);
    }
    else if (kind->Text() == StringView("box"))
    {
        *shape = physics::Shape::MakeBox(half_extents);
    }
    else if (kind->Text() == StringView("capsule"))
    {
        *shape = physics::Shape::MakeCapsule(half_height, radius);
    }
    else
    {
        Raise("the shape kind `", kind->Text(), "` is not sphere, box or capsule");
        return false;
    }
    if (shape->Validate() != physics::Validation::Ok)
    {
        script::RaiseScriptError("the shape's sizes must be finite and above zero");
        return false;
    }
    return true;
}

Value PhysicsShapeCast(Value shape_desc, Vec3 position, Quaternion rotation, Vec3 direction, f32 distance, Value layers)
{
    PartitionContext* context = Context();
    const PhysicsScriptServices* services = context != nullptr ? NeedServices(context) : nullptr;
    physics::Shape shape;
    if (services == nullptr || !ParseShape(shape_desc, &shape))
    {
        return Value();
    }
    if (!Finite3(position) || !Finite3(direction) || direction.LengthSquared() == 0.0f || !IsFinite(distance) ||
        !(distance > 0.0f))
    {
        script::RaiseScriptError("the cast needs a finite position, a direction that is not zero and a distance above "
                                 "zero");
        return Value();
    }
    physics::PhysicsQueryFilter filter;
    physics::BodyId ignored;
    if (!MakeFilter(context, services, layers, ecs::Entity(), &filter, &ignored))
    {
        return Value();
    }
    physics::PhysicsHit hit;
    if (!physics::ShapeCast(*services->world, shape, position, rotation, direction, distance, filter, &hit))
    {
        return Value();
    }
    return HitValue(*services->world, hit);
}

// The entities whose colliders overlap a shape, once each, by collider id.
Value Overlapping(const physics::Shape& shape, const Vec3& position, const Quaternion& rotation, const Value& layers)
{
    PartitionContext* context = Context();
    const PhysicsScriptServices* services = context != nullptr ? NeedServices(context) : nullptr;
    if (services == nullptr)
    {
        return Value();
    }
    physics::PhysicsQueryFilter filter;
    physics::BodyId ignored;
    if (!MakeFilter(context, services, layers, ecs::Entity(), &filter, &ignored))
    {
        return Value();
    }
    Vector<physics::ColliderId> found;
    physics::Overlap(*services->world, shape, position, rotation, filter, &found);
    Value out = Value::MakeArray();
    HashSet<u64, HeapAllocator> seen(HeapAllocator::Default());
    for (usize i = 0; i < found.Size(); ++i)
    {
        const physics::ColliderRecord* record = services->world->FindCollider(found[i]);
        if (record == nullptr)
        {
            continue;
        }
        const ecs::Entity e = EntityOfBody(*services->world, record->owner);
        if (!e.IsValid() || seen.Contains(e.ToU64()))
        {
            continue;  // a body with several colliders is one entity
        }
        seen.Insert(e.ToU64());
        out.Push(Value::FromEntity(e));
    }
    return out;
}

Value PhysicsOverlapSphere(Vec3 center, f32 radius, Value layers)
{
    if (!Finite3(center) || !IsFinite(radius) || !(radius > 0.0f))
    {
        script::RaiseScriptError("the sphere needs a finite centre and a radius above zero");
        return Value();
    }
    return Overlapping(physics::Shape::MakeSphere(radius), center, Quaternion::Identity(), layers);
}

Value PhysicsOverlapBox(Vec3 center, Vec3 half_extents, Quaternion rotation, Value layers)
{
    const physics::Shape box = physics::Shape::MakeBox(half_extents);
    if (!Finite3(center) || box.Validate() != physics::Validation::Ok)
    {
        script::RaiseScriptError("the box needs a finite centre and half extents above zero");
        return Value();
    }
    return Overlapping(box, center, rotation, layers);
}

Value PhysicsClosestPoint(Vec3 point, f32 max_distance, Value layers)
{
    PartitionContext* context = Context();
    const PhysicsScriptServices* services = context != nullptr ? NeedServices(context) : nullptr;
    if (services == nullptr)
    {
        return Value();
    }
    if (!Finite3(point) || !IsFinite(max_distance) || !(max_distance > 0.0f))
    {
        script::RaiseScriptError("the query needs a finite point and a distance above zero");
        return Value();
    }
    physics::PhysicsQueryFilter filter;
    physics::BodyId ignored;
    if (!MakeFilter(context, services, layers, ecs::Entity(), &filter, &ignored))
    {
        return Value();
    }
    physics::PhysicsHit hit;
    if (!physics::ClosestPoint(*services->world, point, max_distance, filter, &hit))
    {
        return Value();
    }
    return HitValue(*services->world, hit);
}

// ------------------------------------------------------------------ characters

struct CharacterPayload
{
    f32 radius;
    f32 height;
    f32 step_height;
    f32 min_ground_y;
    f32 skin_width;
    f32 gravity_scale;
};

// The characters of the host, or null with a script error.
physics::CharacterSet* NeedCharacters(const PhysicsScriptServices* services)
{
    if (services->characters == nullptr)
    {
        script::RaiseScriptError("this world has no characters");
        return nullptr;
    }
    return services->characters;
}

// The character of an entity as of the tick start; raises when there is none.
const physics::CharacterController* CharacterOf(PartitionContext* context, ecs::Entity e)
{
    const PhysicsScriptServices* services = context != nullptr ? NeedServices(context) : nullptr;
    const physics::CharacterSet* characters = services != nullptr ? NeedCharacters(services) : nullptr;
    if (characters == nullptr)
    {
        return nullptr;
    }
    const physics::CharacterController* controller = characters->Find(e);
    if (controller == nullptr)
    {
        script::RaiseScriptError("the entity has no character (`character.create` makes one)");
    }
    return controller;
}

bool CharacterConfigValid(const physics::CharacterConfig& c)
{
    return IsFinite(c.radius) && IsFinite(c.height) && IsFinite(c.step_height) && IsFinite(c.min_ground_y) &&
           IsFinite(c.skin_width) && IsFinite(c.gravity_scale) && c.radius > 0.0f && c.height >= 2.0f * c.radius &&
           c.step_height >= 0.0f && c.skin_width >= 1e-4f && c.min_ground_y >= 0.0f && c.min_ground_y <= 1.0f;
}

void CharacterCreate(ecs::Entity e, Value desc)
{
    PartitionContext* context = Context();
    const PhysicsScriptServices* services = context != nullptr ? NeedServices(context) : nullptr;
    const physics::CharacterSet* characters = services != nullptr ? NeedCharacters(services) : nullptr;
    if (characters == nullptr)
    {
        return;
    }
    if (!desc.IsNil() && desc.Kind() != ValueKind::Map)
    {
        script::RaiseScriptError("the character is described by a table (or nil for the defaults)");
        return;
    }
    // An entity spawned in this tick (a placeholder) takes a character too: the merge creates it with its transform
    // first, then this command, in the tick's order (C API script physics, 2026-11-20).
    if (!e.IsPlaceholder() && !context->world->Has<ecs::LocalTransform>(e))
    {
        script::RaiseScriptError("the entity needs a transform to hold a character");
        return;
    }
    if (!e.IsPlaceholder() && characters->Find(e) != nullptr)
    {
        script::RaiseScriptError("the entity already has a character");
        return;
    }
    physics::CharacterConfig config;
    if (!desc.IsNil() && (!ReadNumber(desc, "character", "radius", &config.radius) ||
                          !ReadNumber(desc, "character", "height", &config.height) ||
                          !ReadNumber(desc, "character", "step_height", &config.step_height) ||
                          !ReadNumber(desc, "character", "min_ground_y", &config.min_ground_y) ||
                          !ReadNumber(desc, "character", "skin_width", &config.skin_width) ||
                          !ReadNumber(desc, "character", "gravity_scale", &config.gravity_scale)))
    {
        return;
    }
    if (!CharacterConfigValid(config))
    {
        script::RaiseScriptError("the character's sizes are not valid (radius above zero, height at least twice the "
                                 "radius, skin_width at least 0.0001, min_ground_y from 0 to 1)");
        return;
    }
    const CharacterPayload payload{config.radius,       config.height,     config.step_height,
                                   config.min_ground_y, config.skin_width, config.gravity_scale};
    Record(context, kCommandCharacterCreate, e, &payload, sizeof(payload));
}

void CharacterDestroy(ecs::Entity e)
{
    PartitionContext* context = Context();
    if (context != nullptr && CharacterOf(context, e) != nullptr)
    {
        Record(context, kCommandCharacterDestroy, e, nullptr, 0);
    }
}

void CharacterMove(ecs::Entity e, Vec3 velocity)
{
    PartitionContext* context = Context();
    if (context == nullptr || CharacterOf(context, e) == nullptr)
    {
        return;
    }
    if (!Finite3(velocity))
    {
        script::RaiseScriptError("the velocity must be finite");
        return;
    }
    const VectorPayload payload = MakeVector(velocity, nullptr);
    Record(context, kCommandCharacterMove, e, &payload, sizeof(payload));
}

void CharacterScalar(u16 kind, ecs::Entity e, f32 value, const char* what)
{
    PartitionContext* context = Context();
    if (context == nullptr || CharacterOf(context, e) == nullptr)
    {
        return;
    }
    if (!IsFinite(value))
    {
        Raise("the ", StringView(what), " must be finite");
        return;
    }
    const ScalarPayload payload{value};
    Record(context, kind, e, &payload, sizeof(payload));
}

void CharacterJump(ecs::Entity e, f32 speed)
{
    CharacterScalar(kCommandCharacterJump, e, speed, "jump speed");
}

void CharacterSetPosition(ecs::Entity e, Vec3 position)
{
    PartitionContext* context = Context();
    if (context == nullptr || CharacterOf(context, e) == nullptr)
    {
        return;
    }
    if (!Finite3(position))
    {
        script::RaiseScriptError("the position must be finite");
        return;
    }
    const VectorPayload payload = MakeVector(position, nullptr);
    Record(context, kCommandCharacterPosition, e, &payload, sizeof(payload));
}

void CharacterSetHeight(ecs::Entity e, f32 height)
{
    PartitionContext* context = Context();
    const physics::CharacterController* controller = context != nullptr ? CharacterOf(context, e) : nullptr;
    if (controller == nullptr)
    {
        return;
    }
    if (!IsFinite(height) || !(height >= 2.0f * controller->Config().radius))
    {
        script::RaiseScriptError("the height must be finite and at least twice the character's radius");
        return;
    }
    const ScalarPayload payload{height};
    Record(context, kCommandCharacterHeight, e, &payload, sizeof(payload));
}

bool CharacterExists(ecs::Entity e)
{
    PartitionContext* context = Context();
    const PhysicsScriptServices* services = context != nullptr ? NeedServices(context) : nullptr;
    return services != nullptr && services->characters != nullptr && services->characters->Find(e) != nullptr;
}

bool CharacterIsGrounded(ecs::Entity e)
{
    PartitionContext* context = Context();
    const physics::CharacterController* controller = context != nullptr ? CharacterOf(context, e) : nullptr;
    return controller != nullptr && controller->State().on_ground;
}

Value CharacterGroundNormal(ecs::Entity e)
{
    PartitionContext* context = Context();
    const physics::CharacterController* controller = context != nullptr ? CharacterOf(context, e) : nullptr;
    if (controller == nullptr || !controller->State().on_ground)
    {
        return Value();
    }
    return Value::FromVec3(controller->State().ground_normal);
}

// The velocity of the body the character stands on (a moving platform), zero in the air or on a static floor.
Value CharacterGroundVelocity(ecs::Entity e)
{
    PartitionContext* context = Context();
    const physics::CharacterController* controller = context != nullptr ? CharacterOf(context, e) : nullptr;
    if (controller == nullptr)
    {
        return Value();
    }
    const physics::Body* ground = controller->State().on_ground
                                      ? ServicesOf(*context)->world->Find(controller->State().ground_body)
                                      : nullptr;
    return Value::FromVec3(ground != nullptr ? ground->linear_velocity : Vec3(0.0f, 0.0f, 0.0f));
}

f32 CharacterVerticalSpeed(ecs::Entity e)
{
    PartitionContext* context = Context();
    const physics::CharacterController* controller = context != nullptr ? CharacterOf(context, e) : nullptr;
    return controller != nullptr ? controller->State().vertical_speed : 0.0f;
}

// ------------------------------------------------------------------ the merge

physics::PhysicsWorld* WorldOf(MergeContext& context)
{
    const script::MergeServices* services = static_cast<const script::MergeServices*>(context.User());
    if (services == nullptr)
    {
        return nullptr;
    }
    const PhysicsScriptServices* physics = static_cast<const PhysicsScriptServices*>(
        services->extensions[script::kServicePhysics]);
    return physics != nullptr ? physics->world : nullptr;
}

// The body of `target` in the world the merge changes.
bool MergeBody(MergeContext& context, ecs::Entity target, physics::PhysicsWorld** world, physics::BodyId* id)
{
    *world = WorldOf(context);
    const physics::BodyLink* link = *world != nullptr ? context.World()->Get<physics::BodyLink>(target) : nullptr;
    if (link == nullptr || (*world)->Find(link->id) == nullptr)
    {
        return false;
    }
    *id = link->id;
    return true;
}

bool ReadPayload(Span<const u8> payload, VectorPayload* out)
{
    if (payload.Size() != sizeof(VectorPayload))
    {
        return false;
    }
    MemCopy(out, payload.Data(), sizeof(VectorPayload));
    return true;
}

Vec3 PayloadValue(const VectorPayload& p)
{
    return Vec3(p.value[0], p.value[1], p.value[2]);
}

Vec3 PayloadPoint(const VectorPayload& p)
{
    return Vec3(p.point[0], p.point[1], p.point[2]);
}

bool ApplyForce(MergeContext& context, ecs::Entity target, Span<const u8> payload)
{
    VectorPayload p;
    physics::PhysicsWorld* world = nullptr;
    physics::BodyId id;
    if (!ReadPayload(payload, &p) || !MergeBody(context, target, &world, &id))
    {
        return false;
    }
    const Vec3 point = p.at_point != 0 ? PayloadPoint(p) : world->WorldCenter(id);
    return world->ApplyForce(id, PayloadValue(p), point) == physics::BodyStatus::Ok;
}

bool ApplyImpulse(MergeContext& context, ecs::Entity target, Span<const u8> payload)
{
    VectorPayload p;
    physics::PhysicsWorld* world = nullptr;
    physics::BodyId id;
    if (!ReadPayload(payload, &p) || !MergeBody(context, target, &world, &id))
    {
        return false;
    }
    const Vec3 point = p.at_point != 0 ? PayloadPoint(p) : world->WorldCenter(id);
    return world->ApplyImpulse(id, PayloadValue(p), point) == physics::BodyStatus::Ok;
}

bool ApplyTorque(MergeContext& context, ecs::Entity target, Span<const u8> payload)
{
    VectorPayload p;
    physics::PhysicsWorld* world = nullptr;
    physics::BodyId id;
    if (!ReadPayload(payload, &p) || !MergeBody(context, target, &world, &id))
    {
        return false;
    }
    physics::Body* body = world->Find(id);
    if (body->settings.type != physics::BodyType::Dynamic)
    {
        return false;
    }
    body->torque = body->torque + PayloadValue(p);
    return world->Wake(id) == physics::BodyStatus::Ok;
}

bool ApplyVelocity(MergeContext& context, ecs::Entity target, Span<const u8> payload, bool angular)
{
    VectorPayload p;
    physics::PhysicsWorld* world = nullptr;
    physics::BodyId id;
    if (!ReadPayload(payload, &p) || !MergeBody(context, target, &world, &id))
    {
        return false;
    }
    const physics::Body* body = world->Find(id);
    physics::Velocity velocity;
    velocity.linear = body->linear_velocity;
    velocity.angular = body->angular_velocity;
    (angular ? velocity.angular : velocity.linear) = PayloadValue(p);
    return world->SetVelocity(id, velocity) == physics::BodyStatus::Ok;
}

bool ApplySetLinear(MergeContext& context, ecs::Entity target, Span<const u8> payload)
{
    return ApplyVelocity(context, target, payload, false);
}

bool ApplySetAngular(MergeContext& context, ecs::Entity target, Span<const u8> payload)
{
    return ApplyVelocity(context, target, payload, true);
}

// The scale is part of the `rigid_body` component, which `SyncBodies` follows: change the component, not the body.
bool ApplyGravityScale(MergeContext& context, ecs::Entity target, Span<const u8> payload)
{
    ScalarPayload p;
    physics::RigidBody* body = payload.Size() == sizeof(p) ? context.World()->Get<physics::RigidBody>(target) : nullptr;
    if (body == nullptr)
    {
        return false;
    }
    MemCopy(&p, payload.Data(), sizeof(p));
    body->gravity_scale = p.value;
    return true;
}

bool ApplyWake(MergeContext& context, ecs::Entity target, Span<const u8>)
{
    physics::PhysicsWorld* world = nullptr;
    physics::BodyId id;
    return MergeBody(context, target, &world, &id) && world->Wake(id) == physics::BodyStatus::Ok;
}

bool ApplySleep(MergeContext& context, ecs::Entity target, Span<const u8>)
{
    physics::PhysicsWorld* world = nullptr;
    physics::BodyId id;
    return MergeBody(context, target, &world, &id) && world->Sleep(id) == physics::BodyStatus::Ok;
}

bool ApplyAddJoint(MergeContext& context, ecs::Entity target, Span<const u8> payload)
{
    physics::Joint joint;
    if (payload.Size() != sizeof(joint) || !context.World()->Exists(target))
    {
        return false;
    }
    MemCopy(&joint, payload.Data(), sizeof(joint));
    joint.entity_a = context.Resolve(joint.entity_a);
    joint.entity_b = context.Resolve(joint.entity_b);
    if (!joint.entity_a.IsValid() && !joint.entity_b.IsValid())
    {
        return false;  // a placeholder that was never bound
    }
    if (context.World()->Has<physics::Joint>(target))
    {
        return context.World()->Set(target, joint) == ecs::Status::Ok;
    }
    return context.World()->Add(target, joint) == ecs::Status::Ok;
}

bool ApplyRemoveJoint(MergeContext& context, ecs::Entity target, Span<const u8>)
{
    return context.World()->Has<physics::Joint>(target) &&
           context.World()->Remove<physics::Joint>(target) == ecs::Status::Ok;
}

// The characters of the merge's host.
physics::CharacterSet* CharactersOf(MergeContext& context)
{
    const script::MergeServices* services = static_cast<const script::MergeServices*>(context.User());
    if (services == nullptr)
    {
        return nullptr;
    }
    const PhysicsScriptServices* physics = static_cast<const PhysicsScriptServices*>(
        services->extensions[script::kServicePhysics]);
    return physics != nullptr ? physics->characters : nullptr;
}

bool ApplyCharacterCreate(MergeContext& context, ecs::Entity target, Span<const u8> payload)
{
    physics::CharacterSet* characters = CharactersOf(context);
    CharacterPayload p;
    const ecs::LocalTransform* transform = context.World()->Get<ecs::LocalTransform>(target);
    if (characters == nullptr || payload.Size() != sizeof(p) || transform == nullptr)
    {
        return false;
    }
    MemCopy(&p, payload.Data(), sizeof(p));
    physics::CharacterConfig config;
    config.radius = p.radius;
    config.height = p.height;
    config.step_height = p.step_height;
    config.min_ground_y = p.min_ground_y;
    config.skin_width = p.skin_width;
    config.gravity_scale = p.gravity_scale;
    return characters->Add(target, config, transform->position);
}

bool ApplyCharacterDestroy(MergeContext& context, ecs::Entity target, Span<const u8>)
{
    physics::CharacterSet* characters = CharactersOf(context);
    return characters != nullptr && characters->Remove(target);
}

bool ApplyCharacterMove(MergeContext& context, ecs::Entity target, Span<const u8> payload)
{
    physics::CharacterSet* characters = CharactersOf(context);
    VectorPayload p;
    return characters != nullptr && ReadPayload(payload, &p) && characters->SetMove(target, PayloadValue(p));
}

bool ReadScalar(Span<const u8> payload, f32* out)
{
    ScalarPayload p;
    if (payload.Size() != sizeof(p))
    {
        return false;
    }
    MemCopy(&p, payload.Data(), sizeof(p));
    *out = p.value;
    return true;
}

bool ApplyCharacterJump(MergeContext& context, ecs::Entity target, Span<const u8> payload)
{
    physics::CharacterSet* characters = CharactersOf(context);
    f32 speed = 0.0f;
    return characters != nullptr && ReadScalar(payload, &speed) && characters->Jump(target, speed);
}

bool ApplyCharacterPosition(MergeContext& context, ecs::Entity target, Span<const u8> payload)
{
    physics::CharacterSet* characters = CharactersOf(context);
    VectorPayload p;
    return characters != nullptr && ReadPayload(payload, &p) && characters->SetPosition(target, PayloadValue(p));
}

bool ApplyCharacterHeight(MergeContext& context, ecs::Entity target, Span<const u8> payload)
{
    physics::CharacterSet* characters = CharactersOf(context);
    f32 height = 0.0f;
    return characters != nullptr && ReadScalar(payload, &height) && characters->SetHeight(target, height);
}

}  // namespace

bool RegisterPhysicsCommandKinds(CommandKindTable* kinds)
{
    bool ok = true;
    ok &= kinds->Register(kCommandPhysicsForce, CommandKind{"physics.add_force", &ApplyForce, false, false});
    ok &= kinds->Register(kCommandPhysicsImpulse, CommandKind{"physics.add_impulse", &ApplyImpulse, false, false});
    ok &= kinds->Register(kCommandPhysicsTorque, CommandKind{"physics.add_torque", &ApplyTorque, false, false});
    ok &= kinds->Register(kCommandPhysicsSetLinear, CommandKind{"physics.set_velocity", &ApplySetLinear, false, true});
    ok &= kinds->Register(kCommandPhysicsSetAngular,
                          CommandKind{"physics.set_angular_velocity", &ApplySetAngular, false, true});
    ok &= kinds->Register(kCommandPhysicsGravityScale,
                          CommandKind{"physics.set_gravity_scale", &ApplyGravityScale, false, true});
    ok &= kinds->Register(kCommandPhysicsWake, CommandKind{"physics.wake", &ApplyWake, false, false});
    ok &= kinds->Register(kCommandPhysicsSleep, CommandKind{"physics.sleep", &ApplySleep, false, false});
    ok &= kinds->Register(kCommandPhysicsAddJoint, CommandKind{"physics.add_joint", &ApplyAddJoint, false, true});
    ok &= kinds->Register(kCommandPhysicsRemoveJoint,
                          CommandKind{"physics.remove_joint", &ApplyRemoveJoint, false, true});
    ok &= kinds->Register(kCommandCharacterCreate, CommandKind{"character.create", &ApplyCharacterCreate, false, true});
    ok &= kinds->Register(kCommandCharacterDestroy,
                          CommandKind{"character.destroy", &ApplyCharacterDestroy, false, true});
    ok &= kinds->Register(kCommandCharacterMove, CommandKind{"character.move", &ApplyCharacterMove, false, true});
    ok &= kinds->Register(kCommandCharacterJump, CommandKind{"character.jump", &ApplyCharacterJump, false, true});
    ok &= kinds->Register(kCommandCharacterHeight,
                          CommandKind{"character.set_height", &ApplyCharacterHeight, false, true});
    ok &= kinds->Register(kCommandCharacterPosition,
                          CommandKind{"character.set_position", &ApplyCharacterPosition, false, true});
    return ok;
}

bool RegisterPhysicsBindings(script::BindingRegistry* registry)
{
    bool ok = true;
    ok &= registry->Add<&PhysicsAddForce>("physics", "add_force", ThreadRule::WorldWrite, "e, force",
                                          "Adds a force in newtons at the body's centre of mass, kept until the next "
                                          "physics step takes it. Forces from every script add up. The body must be "
                                          "dynamic.");
    ok &= registry->Add<&PhysicsAddForceAt>("physics", "add_force_at", ThreadRule::WorldWrite, "e, force, point",
                                            "Adds a force at a world point (it also turns the body).");
    ok &= registry->Add<&PhysicsAddImpulse>("physics", "add_impulse", ThreadRule::WorldWrite, "e, impulse",
                                            "Changes the body's velocity at once by impulse / mass (newton seconds) at "
                                            "the centre of mass; the body must be dynamic.");
    ok &= registry->Add<&PhysicsAddImpulseAt>("physics", "add_impulse_at", ThreadRule::WorldWrite, "e, impulse, point",
                                              "An impulse at a world point (it also turns the body).");
    ok &= registry->Add<&PhysicsAddTorque>("physics", "add_torque", ThreadRule::WorldWrite, "e, torque",
                                           "Adds a torque, kept until the next physics step takes it.");
    ok &= registry->Add<&PhysicsSetVelocity>("physics", "set_velocity", ThreadRule::WorldWrite, "e, velocity",
                                             "Sets the linear velocity (metres per second) of a dynamic or kinematic "
                                             "body.");
    ok &= registry->Add<&PhysicsSetAngularVelocity>("physics", "set_angular_velocity", ThreadRule::WorldWrite,
                                                    "e, velocity", "Sets the angular velocity (radians per second).");
    ok &= registry->Add<&PhysicsSetGravityScale>("physics", "set_gravity_scale", ThreadRule::WorldWrite, "e, scale",
                                                 "Sets how much gravity pulls the body (0 floats it).");
    ok &= registry->Add<&PhysicsWake>("physics", "wake", ThreadRule::WorldWrite, "e",
                                      "Wakes the body and every body of its island.");
    ok &= registry->Add<&PhysicsSleep>("physics", "sleep", ThreadRule::WorldWrite, "e",
                                       "Puts a dynamic body and its island to sleep until something wakes them.");
    ok &= registry->Add<&PhysicsAddJoint>(
        "physics", "add_joint", ThreadRule::WorldWrite, "owner, joint",
        "Puts a joint component on `owner` (any entity): {type=\"ball\"|\"distance\"|\"fixed\"|\"hinge\", a=, b=, "
        "point_a=, point_b=, axis=, min_length=, max_length=, hertz=, damping_ratio=, break_force=, break_torque=, "
        "collide_connected=, enable_limit=, lower_angle=, upper_angle=, enable_motor=, motor_speed=, "
        "max_motor_torque=}. `a` and `b` are entities with bodies (nil is the world, not both).");
    ok &= registry->Add<&PhysicsRemoveJoint>("physics", "remove_joint", ThreadRule::WorldWrite, "owner",
                                             "Takes the joint component off `owner`; the physics joint goes at the "
                                             "next sync.");
    ok &= registry->Add<&PhysicsGetVelocity>("physics", "get_velocity", ThreadRule::WorldRead, "e",
                                             "Returns the body's linear velocity after the last physics step.");
    ok &= registry->Add<&PhysicsGetAngularVelocity>("physics", "get_angular_velocity", ThreadRule::WorldRead, "e",
                                                    "Returns the body's angular velocity after the last step.");
    ok &= registry->Add<&PhysicsIsSleeping>("physics", "is_sleeping", ThreadRule::WorldRead, "e",
                                            "Returns whether the body is asleep.");
    ok &= registry->Add<&PhysicsLayer>("physics", "layer", ThreadRule::WorldRead, "name",
                                       "Returns the number of the physics layer called `name` (for the `layer` field "
                                       "of a collider).");
    ok &= registry->Add<&PhysicsLayerMask>("physics", "layer_mask", ThreadRule::WorldRead, "layers",
                                           "Returns the bit mask of layer names: nil is all layers, a name is one, an "
                                           "array is several (for the `group` and `mask` fields of a collider).");
    ok &= registry->Add<&PhysicsRaycast>(
        "physics", "raycast", ThreadRule::WorldRead, "origin, direction, max_distance, layers",
        "Casts a ray against the physics world as of the tick start; `layers` is nil (all), a layer name or an array "
        "of names. Returns nil, or {entity=, point=, normal=, distance=} of the nearest hit.");
    ok &= registry->Add<&PhysicsRaycastEx>("physics", "raycast_ex", ThreadRule::WorldRead,
                                           "origin, direction, max_distance, layers, ignore",
                                           "As raycast, skipping the colliders of the entity `ignore` (or nil).");
    ok &= registry->Add<&PhysicsShapeCast>(
        "physics", "shape_cast", ThreadRule::WorldRead, "shape, position, rotation, direction, distance, layers",
        "Sweeps {kind=\"sphere\", radius=} / {kind=\"box\", half_extents=} / {kind=\"capsule\", half_height=, "
        "radius=} along a direction; returns nil or the first hit, `distance` being how far the shape moved.");
    ok &= registry->Add<&PhysicsOverlapSphere>("physics", "overlap_sphere", ThreadRule::WorldRead,
                                               "center, radius, layers",
                                               "Returns an array of the entities whose colliders touch the sphere.");
    ok &= registry->Add<&PhysicsOverlapBox>("physics", "overlap_box", ThreadRule::WorldRead,
                                            "center, half_extents, rotation, layers",
                                            "Returns an array of the entities whose colliders touch the box.");
    ok &= registry->Add<&PhysicsClosestPoint>("physics", "closest_point", ThreadRule::WorldRead,
                                              "point, max_distance, layers",
                                              "Returns nil or {entity=, point=, normal=, distance=}: the nearest point "
                                              "on a collider's surface within `max_distance`.");
    ok &= registry->Add<&CharacterCreate>("character", "create", ThreadRule::WorldWrite, "e, config",
                                          "Makes a character (a kinematic capsule that walks, slides, climbs steps and "
                                          "slopes and stands on moving platforms) at the entity's transform: "
                                          "{radius=, height=, step_height=, min_ground_y=, skin_width=, "
                                          "gravity_scale=} or nil for the defaults.");
    ok &= registry->Add<&CharacterDestroy>("character", "destroy", ThreadRule::WorldWrite, "e",
                                           "Removes the entity's character.");
    ok &= registry->Add<&CharacterMove>("character", "move", ThreadRule::WorldWrite, "e, velocity",
                                        "Sets the horizontal velocity the character walks at (the y part is ignored); "
                                        "it holds until set again. The move runs inside the physics step.");
    ok &= registry->Add<&CharacterJump>("character", "jump", ThreadRule::WorldWrite, "e, speed",
                                        "Gives the character an upward speed (m/s) at the start of the next step; "
                                        "check `is_grounded` first for a jump from the ground only.");
    ok &= registry->Add<&CharacterSetPosition>(
        "character", "set_position", ThreadRule::WorldWrite, "e, position",
        "Puts the character's capsule centre at `position` at once (a teleport): "
        "its vertical speed becomes 0, a pending jump is dropped, the held move "
        "stays, and the ground is looked for again at the next step. Setting the "
        "entity's transform does not move a character.");
    ok &= registry->Add<&CharacterSetHeight>("character", "set_height", ThreadRule::WorldWrite, "e, height",
                                             "Changes the capsule's total height (crouching), feet kept in place.");
    ok &= registry->Add<&CharacterExists>("character", "exists", ThreadRule::WorldRead, "e",
                                          "Returns whether the entity has a character.");
    ok &= registry->Add<&CharacterIsGrounded>("character", "is_grounded", ThreadRule::WorldRead, "e",
                                              "Returns whether the character stood on ground after the last step.");
    ok &= registry->Add<&CharacterGroundNormal>("character", "ground_normal", ThreadRule::WorldRead, "e",
                                                "Returns the ground's unit normal, or nil in the air.");
    ok &= registry->Add<&CharacterGroundVelocity>("character", "ground_velocity", ThreadRule::WorldRead, "e",
                                                  "Returns the velocity of the body the character stands on (a moving "
                                                  "platform), zero on a static floor or in the air.");
    ok &= registry->Add<&CharacterVerticalSpeed>("character", "vertical_speed", ThreadRule::WorldRead, "e",
                                                 "Returns the vertical speed the controller keeps (gravity and "
                                                 "jumps), in m/s.");
    return ok;
}

}  // namespace vse::script_physics
