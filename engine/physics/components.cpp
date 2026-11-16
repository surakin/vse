#include "physics/components.h"

#include "lang/scalar.h"
#include "physics/check.h"

namespace engine_placeholder::physics
{

namespace
{

bool KnownRule(CombineRule rule)
{
    return static_cast<u8>(rule) <= static_cast<u8>(CombineRule::Maximum);
}

}  // namespace

Validation PhysicsMaterial::Validate() const
{
    if (!IsFinite(friction) || !IsFinite(restitution) || !IsFinite(density))
    {
        return Validation::NotFinite;
    }
    if (!(density > 0.0f))
    {
        return Validation::NotPositive;
    }
    // Between a gas and a neutron star's crust, in kilograms per cubic metre: outside it the mass or its inverse
    // overflows for shapes of ordinary size.
    if (density < 1.0e-3f || density > 1.0e9f)
    {
        return Validation::OutOfRange;
    }
    if (friction < 0.0f || restitution < 0.0f || restitution > 1.0f)
    {
        return Validation::OutOfRange;
    }
    return KnownRule(friction_rule) && KnownRule(restitution_rule) ? Validation::Ok : Validation::UnknownKind;
}

f32 Combine(CombineRule rule, f32 a, f32 b)
{
    switch (rule)
    {
    case CombineRule::Average: return 0.5f * (a + b);
    case CombineRule::Minimum: return a < b ? a : b;
    case CombineRule::Multiply: return a * b;
    case CombineRule::Maximum: return a > b ? a : b;
    }
    return 0.5f * (a + b);
}

CombineRule CombineRuleFor(CombineRule a, CombineRule b)
{
    return static_cast<u8>(a) >= static_cast<u8>(b) ? a : b;
}

Validation RigidBody::Validate() const
{
    if (static_cast<u8>(type) > static_cast<u8>(BodyType::Dynamic) || (flags & ~static_cast<u32>(kBodyKnownFlags)) != 0)
    {
        return Validation::UnknownKind;
    }
    if (!IsFinite(mass) || !IsFinite(linear_damping) || !IsFinite(angular_damping) || !IsFinite(gravity_scale))
    {
        return Validation::NotFinite;
    }
    // A mass override is zero (the shape's own) or between `kMinMass` and `kMaxMass`: outside it the inverse mass or
    // the inertia overflows.
    const bool mass_ok = mass == 0.0f || (mass >= kMinMass && mass <= kMaxMass);
    return mass_ok && linear_damping >= 0.0f && angular_damping >= 0.0f && Abs(gravity_scale) <= 1.0e6f
               ? Validation::Ok
               : Validation::OutOfRange;
}

Validation Velocity::Validate() const
{
    return FiniteVec3(linear) && FiniteVec3(angular) ? Validation::Ok : Validation::NotFinite;
}

Validation Collider::Validate() const
{
    const Validation shape_result = shape.Validate();
    if (shape_result != Validation::Ok)
    {
        return shape_result;
    }
    const Validation material_result = material.Validate();
    if (material_result != Validation::Ok)
    {
        return material_result;
    }
    if (!FiniteVec3(offset) || !IsFinite(rotation.x) || !IsFinite(rotation.y) || !IsFinite(rotation.z) ||
        !IsFinite(rotation.w))
    {
        return Validation::NotFinite;
    }
    if (!InsideExtent(offset))
    {
        return Validation::OutOfRange;
    }
    const f32 n2 = rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z +
                   rotation.w * rotation.w;
    if (Abs(n2 - 1.0f) > 2e-3f)
    {
        return Validation::NotNormalized;
    }
    return layer < 32 ? Validation::Ok : Validation::OutOfRange;
}

}  // namespace engine_placeholder::physics
