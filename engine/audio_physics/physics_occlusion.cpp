#include "audio_physics/physics_occlusion.h"

#include "lang/scalar.h"

namespace engine_placeholder::audio_physics
{

f32 PhysicsOcclusion::Occlusion(const Vec3& from, const Vec3& to, u32* crossed)
{
    if (crossed != nullptr)
    {
        *crossed = 0;
    }
    if (world_ == nullptr)
    {
        return 0.0f;
    }
    const Vec3 delta = to - from;
    const f32 length = delta.Length();
    if (!(length > 1e-4f) || !IsFinite(length))
    {
        return 0.0f;
    }
    const Vec3 direction = delta * (1.0f / length);
    physics::PhysicsQueryFilter filter;
    filter.layer_mask = config_.layer_mask;
    filter.include_triggers = config_.include_triggers;
    physics::RayCastAll(*world_, from, direction, length, filter, &forward_);
    if (forward_.Empty())
    {
        return 0.0f;
    }
    physics::RayCastAll(*world_, to, direction * -1.0f, length, filter, &backward_);
    f32 depth = 0.0f;
    for (usize i = 0; i < forward_.Size(); ++i)
    {
        const physics::PhysicsHit& in = forward_[i];
        f32 thickness = config_.default_thickness;
        for (usize k = 0; k < backward_.Size(); ++k)
        {
            if (backward_[k].collider == in.collider)
            {
                // The reverse ray enters the collider at the far side of the path.
                const f32 exit = length - backward_[k].distance;
                thickness = exit > in.distance ? exit - in.distance : 0.0f;
                break;
            }
        }
        const physics::ColliderRecord* record = world_->FindCollider(in.collider);
        const u32 layer = record != nullptr ? static_cast<u32>(record->value.layer) & 31u : 0u;
        depth += config_.absorption_per_metre[layer] * thickness;
    }
    if (crossed != nullptr)
    {
        *crossed = static_cast<u32>(forward_.Size());
    }
    const f64 occlusion = 1.0 - Exp(-static_cast<f64>(depth));
    return occlusion < 0.0 ? 0.0f : (occlusion > 1.0 ? 1.0f : static_cast<f32>(occlusion));
}

}  // namespace engine_placeholder::audio_physics
