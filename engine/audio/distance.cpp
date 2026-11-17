#include "audio/distance.h"

#include "lang/scalar.h"

namespace engine_placeholder::audio
{

bool IsValid(const Attenuation& a)
{
    if (!(a.min_distance > 0.0f) || !(a.max_distance > a.min_distance) || !IsFinite(a.max_distance) ||
        !(a.rolloff >= 0.0f) || !IsFinite(a.rolloff))
    {
        return false;
    }
    if (a.model == DistanceModel::Curve)
    {
        if (a.curve.count == 0 || a.curve.count > kMaxCurvePoints)
        {
            return false;
        }
        for (u32 i = 0; i < a.curve.count; ++i)
        {
            if (!IsFinite(a.curve.gain[i]) || !(a.curve.distance[i] >= 0.0f) || !IsFinite(a.curve.distance[i]) ||
                (i > 0 && !(a.curve.distance[i] > a.curve.distance[i - 1])))
            {
                return false;
            }
        }
    }
    return true;
}

namespace
{

f32 CurveGain(const DistanceCurve& c, f32 d)
{
    if (d <= c.distance[0])
    {
        return c.gain[0];
    }
    for (u32 i = 1; i < c.count; ++i)
    {
        if (d <= c.distance[i])
        {
            const f32 t = (d - c.distance[i - 1]) / (c.distance[i] - c.distance[i - 1]);
            return c.gain[i - 1] + (c.gain[i] - c.gain[i - 1]) * t;
        }
    }
    return c.gain[c.count - 1];
}

}  // namespace

f32 DistanceGain(const Attenuation& a, f32 distance)
{
    if (!IsValid(a) || !(distance >= 0.0f) || distance > a.max_distance)
    {
        return 0.0f;
    }
    if (a.model == DistanceModel::Curve)
    {
        return CurveGain(a.curve, distance);
    }
    const f32 d = distance < a.min_distance ? a.min_distance : distance;
    switch (a.model)
    {
    case DistanceModel::Inverse: return a.min_distance / (a.min_distance + a.rolloff * (d - a.min_distance));
    case DistanceModel::Linear:
    {
        const f32 g = 1.0f - a.rolloff * (d - a.min_distance) / (a.max_distance - a.min_distance);
        return g < 0.0f ? 0.0f : g;
    }
    case DistanceModel::Exponential:
        return static_cast<f32>(Pow(static_cast<f64>(d / a.min_distance), -static_cast<f64>(a.rolloff)));
    case DistanceModel::Curve: break;
    }
    return 0.0f;
}

f32 AudibleRadius(const Attenuation& a, f32 threshold)
{
    if (!IsValid(a))
    {
        return 0.0f;
    }
    f32 r = a.max_distance;
    if (a.model == DistanceModel::Curve)
    {
        const DistanceCurve& c = a.curve;
        // The last distance at which the curve is still above the threshold.
        for (u32 i = c.count; i-- > 0;)
        {
            if (c.gain[i] > threshold)
            {
                if (i + 1 < c.count && c.gain[i + 1] < threshold)
                {
                    const f32 t = (c.gain[i] - threshold) / (c.gain[i] - c.gain[i + 1]);
                    r = c.distance[i] + (c.distance[i + 1] - c.distance[i]) * t;
                }
                else if (i + 1 < c.count)
                {
                    r = c.distance[i + 1];
                }
                break;
            }
            if (i == 0)
            {
                r = 0.0f;
            }
        }
        return r < a.max_distance ? r : a.max_distance;
    }
    if (a.rolloff > 0.0f && threshold > 0.0f)
    {
        switch (a.model)
        {
        case DistanceModel::Inverse: r = a.min_distance + a.min_distance * (1.0f / threshold - 1.0f) / a.rolloff; break;
        case DistanceModel::Linear:
            r = a.min_distance + (1.0f - threshold) * (a.max_distance - a.min_distance) / a.rolloff;
            break;
        case DistanceModel::Exponential:
            r = a.min_distance * static_cast<f32>(Pow(static_cast<f64>(threshold), -1.0 / static_cast<f64>(a.rolloff)));
            break;
        case DistanceModel::Curve: break;
        }
    }
    r = r < a.min_distance ? a.min_distance : r;
    return r < a.max_distance ? r : a.max_distance;
}

f32 ConeGain(const Cone& cone, f32 angle_cos)
{
    if (cone.inner_angle >= 6.2831853f)
    {
        return 1.0f;
    }
    const f32 cos_inner = static_cast<f32>(Cos(static_cast<f64>(cone.inner_angle) * 0.5));
    const f32 cos_outer = static_cast<f32>(Cos(static_cast<f64>(cone.outer_angle) * 0.5));
    if (angle_cos >= cos_inner)
    {
        return 1.0f;
    }
    if (angle_cos <= cos_outer || !(cos_inner > cos_outer))
    {
        return cone.outer_gain;
    }
    const f32 t = (angle_cos - cos_outer) / (cos_inner - cos_outer);
    return cone.outer_gain + (1.0f - cone.outer_gain) * t;
}

namespace
{

constexpr u32 kAirPoints = 8;
constexpr f32 kAirDistance[kAirPoints] = {0.0f, 10.0f, 25.0f, 50.0f, 100.0f, 200.0f, 300.0f, 400.0f};
constexpr f32 kAirCutoff[kAirPoints] = {20000.0f, 16000.0f, 10000.0f, 6000.0f, 3500.0f, 2000.0f, 1500.0f, 1200.0f};

f32 ClampSpeed(f32 v, f32 limit)
{
    return v < -limit ? -limit : (v > limit ? limit : v);
}

}  // namespace

f32 AirCutoffHz(f32 distance, f32 factor)
{
    if (!(distance >= 0.0f) || !IsFinite(distance) || !(factor > 0.0f) || !IsFinite(factor))
    {
        return 20000.0f;
    }
    const f32 d = distance * factor;
    if (d >= kAirDistance[kAirPoints - 1])
    {
        return kAirCutoff[kAirPoints - 1];
    }
    for (u32 i = 1; i < kAirPoints; ++i)
    {
        if (d <= kAirDistance[i])
        {
            const f32 t = (d - kAirDistance[i - 1]) / (kAirDistance[i] - kAirDistance[i - 1]);
            return kAirCutoff[i - 1] + (kAirCutoff[i] - kAirCutoff[i - 1]) * t;
        }
    }
    return kAirCutoff[kAirPoints - 1];
}

f32 AirCoefficient(f32 cutoff_hz, u32 sample_rate)
{
    if (!(cutoff_hz > 0.0f) || sample_rate == 0 || cutoff_hz >= 0.45f * static_cast<f32>(sample_rate))
    {
        return 1.0f;
    }
    const f64 c = 1.0 - Exp(-6.283185307179586 * static_cast<f64>(cutoff_hz) / static_cast<f64>(sample_rate));
    return c > 1.0 ? 1.0f : static_cast<f32>(c);
}

f32 DopplerRatio(f32 speed_of_sound, f32 factor, const Vec3& to_emitter, const Vec3& listener_velocity,
                 const Vec3& emitter_velocity)
{
    if (!(speed_of_sound > 0.0f) || !(factor > 0.0f) || !IsFinite(factor) || !IsFinite(to_emitter.x) ||
        !IsFinite(to_emitter.y) || !IsFinite(to_emitter.z) || !IsFinite(listener_velocity.x) ||
        !IsFinite(listener_velocity.y) || !IsFinite(listener_velocity.z) || !IsFinite(emitter_velocity.x) ||
        !IsFinite(emitter_velocity.y) || !IsFinite(emitter_velocity.z))
    {
        return 1.0f;
    }
    const f32 limit = 0.9f * speed_of_sound;
    const f32 vl = ClampSpeed(factor * listener_velocity.Dot(to_emitter), limit);
    const f32 ve = ClampSpeed(factor * emitter_velocity.Dot(to_emitter), limit);
    const f32 ratio = (speed_of_sound + vl) / (speed_of_sound + ve);
    return ratio < 0.25f ? 0.25f : (ratio > 4.0f ? 4.0f : ratio);
}

}  // namespace engine_placeholder::audio
