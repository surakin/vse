#include "script_physics/physics_bindings.h"

#include "physics/components.h"
#include "storage/field.h"

namespace vse::script_physics
{

namespace
{

// A field of a member of a member (`Collider::shape.radius`): the registry's `Field` takes a direct member, so this
// builds the same description for a nested one.
template <auto Outer, auto Inner> storage::FieldDesc Nested(const char* name, storage::FieldOptions options = {})
{
    using OuterTraits = storage::detail::MemberTraits<decltype(Outer)>;
    using InnerTraits = storage::detail::MemberTraits<decltype(Inner)>;
    using C = typename OuterTraits::Class;
    using V = typename InnerTraits::Value;
    storage::FieldDesc d;
    d.name = name;
    d.kind = storage::detail::KindOf<V>();
    d.options = options;
    d.memory_size = sizeof(V);
    d.needs_range = storage::detail::NeedsRange<V>();
    d.encode = [](const storage::FieldDesc& desc, const void* component, bytes::ByteWriter& out,
                  const storage::CodecLimits& limits)
    { return storage::detail::EncodeValue<V>((static_cast<const C*>(component)->*Outer).*Inner, desc, out, limits); };
    d.decode =
        [](const storage::FieldDesc& desc, bytes::ByteReader& in, void* component, const storage::CodecLimits& limits)
    { return storage::detail::DecodeValue<V>(&((static_cast<C*>(component)->*Outer).*Inner), desc, in, limits); };
    return d;
}

const char* ReasonText(physics::Validation result)
{
    switch (result)
    {
    case physics::Validation::Ok: return nullptr;
    case physics::Validation::NotFinite: return "a number is not finite";
    case physics::Validation::NotPositive: return "a size, mass or density that must be above zero is not";
    case physics::Validation::OutOfRange: return "a number is out of its range";
    case physics::Validation::UnknownKind: return "an unknown kind";
    case physics::Validation::NotNormalized: return "the rotation is not a unit quaternion";
    }
    return "not valid";
}

const char* CheckBody(const void* component)
{
    return ReasonText(static_cast<const physics::RigidBody*>(component)->Validate());
}

const char* CheckVelocity(const void* component)
{
    return ReasonText(static_cast<const physics::Velocity*>(component)->Validate());
}

const char* CheckCollider(const void* component)
{
    return ReasonText(static_cast<const physics::Collider*>(component)->Validate());
}

}  // namespace

bool RegisterPhysicsComponents(storage::ComponentRegistry* registry)
{
    using physics::Collider;
    using physics::RigidBody;
    using physics::Shape;
    using storage::Field;
    using storage::FieldOptions;
    bool ok = true;
    ok &= registry
              ->Register<RigidBody>({.id = kRigidBodyId,
                                     .name = "rigid_body",
                                     .fields = {Field<&RigidBody::type>("type", FieldOptions::Enum(0, 2)),
                                                Field<&RigidBody::flags>("flags"), Field<&RigidBody::mass>("mass"),
                                                Field<&RigidBody::linear_damping>("linear_damping"),
                                                Field<&RigidBody::angular_damping>("angular_damping"),
                                                Field<&RigidBody::gravity_scale>("gravity_scale")}})
              .Ok();
    ok &= registry
              ->Register<physics::Velocity>({.id = kVelocityId,
                                             .name = "velocity",
                                             .fields = {Field<&physics::Velocity::linear>("linear"),
                                                        Field<&physics::Velocity::angular>("angular")}})
              .Ok();
    ok &= registry
              ->Register<Collider>(
                  {.id = kColliderId,
                   .name = "collider",
                   .fields = {Nested<&Collider::shape, &Shape::kind>("shape", FieldOptions::Enum(0, 5)),
                              Nested<&Collider::shape, &Shape::radius>("radius"),
                              Nested<&Collider::shape, &Shape::half_height>("half_height"),
                              Nested<&Collider::shape, &Shape::half_extents>("half_extents"),
                              Nested<&Collider::shape, &Shape::hull>("hull"),
                              Nested<&Collider::shape, &Shape::mesh>("mesh"),
                              Nested<&Collider::shape, &Shape::compound>("compound"),
                              Field<&Collider::offset>("offset"), Field<&Collider::rotation>("rotation"),
                              Nested<&Collider::material, &physics::PhysicsMaterial::friction>("friction"),
                              Nested<&Collider::material, &physics::PhysicsMaterial::restitution>("restitution"),
                              Nested<&Collider::material, &physics::PhysicsMaterial::density>("density"),
                              Nested<&Collider::material, &physics::PhysicsMaterial::friction_rule>(
                                  "friction_rule", FieldOptions::Enum(0, 3)),
                              Nested<&Collider::material, &physics::PhysicsMaterial::restitution_rule>(
                                  "restitution_rule", FieldOptions::Enum(0, 3)),
                              Field<&Collider::layer>("layer"), Field<&Collider::is_trigger>("trigger"),
                              Field<&Collider::group>("group"), Field<&Collider::mask>("mask")}})
              .Ok();
    return ok;
}

void RegisterPhysicsValidators(script::ComponentCatalog* catalog)
{
    catalog->SetValidator(kRigidBodyId, &CheckBody);
    catalog->SetValidator(kVelocityId, &CheckVelocity);
    catalog->SetValidator(kColliderId, &CheckCollider);
}

}  // namespace vse::script_physics
