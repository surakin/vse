#include "renderer/instance_scatter.h"

#include "lang/scalar.h"

namespace vse::renderer
{

namespace
{

// SplitMix64: a fixed generator, so a seed means the same numbers everywhere.
struct SplitMix
{
    u64 state;
    u64 Next()
    {
        state += 0x9E3779B97F4A7C15ull;
        u64 z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    // A number in [0, 1) with 53 random bits.
    f64 Unit() { return static_cast<f64>(Next() >> 11) * (1.0 / 9007199254740992.0); }
};

}  // namespace

AddManyResult AddMany(InstanceSet* set, Span<const InstanceTransform> transforms, Span<const InstanceExtra> data)
{
    AddManyResult result;
    if (set == nullptr || (data.Size() != 0 && data.Size() != transforms.Size()))
    {
        result.status = InstanceStatus::InvalidValue;
        return result;
    }
    for (usize i = 0; i < transforms.Size(); ++i)
    {
        const InstanceStatus status = set->Add(transforms[i], data.Size() != 0 ? &data[i] : nullptr, nullptr);
        if (status != InstanceStatus::Ok)
        {
            result.status = status;
            return result;
        }
        ++result.added;
    }
    return result;
}

AddManyResult Scatter(InstanceSet* set, const ScatterRule& rule, const InstanceExtra* data, ScatterHeightFn height_fn,
                      void* user)
{
    AddManyResult result;
    if (set == nullptr || !IsFinite(rule.min_x) || !IsFinite(rule.max_x) || !IsFinite(rule.min_z) ||
        !IsFinite(rule.max_z) || !IsFinite(rule.height) || !IsFinite(rule.min_scale) || !IsFinite(rule.max_scale) ||
        !IsFinite(rule.lift) || rule.max_x < rule.min_x || rule.max_z < rule.min_z || !(rule.min_scale > 0.0f) ||
        rule.max_scale < rule.min_scale)
    {
        result.status = InstanceStatus::InvalidValue;
        return result;
    }
    SplitMix rng{rule.seed};
    for (u32 i = 0; i < rule.count; ++i)
    {
        // The draws are made in a fixed order whether or not the point is used, so a skipped point does not shift the
        // ones after it.
        const f64 x = rule.min_x + (rule.max_x - rule.min_x) * rng.Unit();
        const f64 z = rule.min_z + (rule.max_z - rule.min_z) * rng.Unit();
        const f32 scale = rule.min_scale + (rule.max_scale - rule.min_scale) * static_cast<f32>(rng.Unit());
        const f32 yaw = rule.random_yaw ? static_cast<f32>(rng.Unit() * 6.28318530717958647692) : 0.0f;
        f64 y = rule.height;
        if (height_fn != nullptr && !height_fn(user, x, z, &y))
        {
            continue;
        }
        InstanceTransform t;
        t.position = Vec3d(x, y + static_cast<f64>(rule.lift), z);
        t.rotation = Quaternion::FromAxisAngle(Vec3(0, 1, 0), yaw);
        t.scale = Vec3(scale, scale, scale);
        const InstanceStatus status = set->Add(t, data, nullptr);
        if (status != InstanceStatus::Ok)
        {
            result.status = status;
            return result;
        }
        ++result.added;
    }
    return result;
}

}  // namespace vse::renderer
