// Distance models and cones (Phase 26 Day 428): how loud an emitter is at a distance and off its axis, and how far away
// it is still worth a voice.
#pragma once

#include "math/vector.h"
#include "types.h"

namespace vse::audio
{

/// How the gain falls with distance.
enum class DistanceModel : u8
{
    /// `min / (min + rolloff * (d - min))`.
    Inverse,
    /// `1 - rolloff * (d - min) / (max - min)`, never below 0.
    Linear,
    /// `(d / min) ^ -rolloff`.
    Exponential,
    /// A piecewise-linear table of (distance, gain) points.
    Curve,
};

/// The most points of a custom curve.
inline constexpr u32 kMaxCurvePoints = 16;

/// A custom distance curve: `count` points with strictly rising distances; the gain is interpolated between them and
/// held at the first and last value outside.
///
/// - Thread-safety: thread-compatible plain data.
struct DistanceCurve
{
    /// The points in use, 0 to `kMaxCurvePoints`.
    u32 count = 0;
    /// The distances.
    f32 distance[kMaxCurvePoints] = {};
    /// The gains at those distances.
    f32 gain[kMaxCurvePoints] = {};
};

/// A distance model with its numbers. Beyond `max_distance` the emitter is silent whatever the model.
///
/// - Thread-safety: thread-compatible plain data.
struct Attenuation
{
    /// The model.
    DistanceModel model = DistanceModel::Inverse;
    /// Up to this distance the gain is 1 (not for `Curve`).
    f32 min_distance = 1.0f;
    /// The range: beyond it nothing is heard.
    f32 max_distance = 100.0f;
    /// How fast the gain falls (not for `Curve`).
    f32 rolloff = 1.0f;
    /// The table of the `Curve` model.
    DistanceCurve curve;
};

/// A cone of sound: full level inside `inner_angle`, `outer_gain` outside `outer_angle`, a blend between.
///
/// - Thread-safety: thread-compatible plain data.
struct Cone
{
    /// The full angle of the inner cone in radians; at or above 2 pi the emitter is omnidirectional.
    f32 inner_angle = 6.2831853f;
    /// The full angle of the outer cone in radians, at least `inner_angle`.
    f32 outer_angle = 6.2831853f;
    /// The gain outside the outer cone.
    f32 outer_gain = 0.0f;
};

/// Whether the attenuation's numbers can be used (finite, positive distances, `min` below `max`, a curve with rising
/// distances).
///
/// - Param a: the attenuation.
/// - Returns: true when valid.
/// - Fails: never.
bool IsValid(const Attenuation& a);

/// The gain at `distance`.
///
/// - Param a: the attenuation (checked by `IsValid`; an invalid one gives 0).
/// - Param distance: the distance, not negative.
/// - Returns: the gain from 0 to 1 (a curve may exceed 1 if its table does).
/// - Fails: returns 0 for an invalid attenuation or a distance past `max_distance`.
f32 DistanceGain(const Attenuation& a, f32 distance);

/// The distance beyond which `DistanceGain` is at most `threshold`.
///
/// - Param a: the attenuation.
/// - Param threshold: the gain below which the emitter is not worth a voice, above 0.
/// - Returns: a distance from `min_distance` to `max_distance` (0 for an invalid attenuation).
/// - Fails: never.
f32 AudibleRadius(const Attenuation& a, f32 threshold);

/// The cone gain for a listener at `angle_cos` (the cosine of the angle between the emitter's axis and the direction to
/// the listener).
///
/// - Param cone: the cone.
/// - Param angle_cos: the cosine, from -1 to 1.
/// - Returns: the gain from `outer_gain` to 1.
/// - Fails: never.
f32 ConeGain(const Cone& cone, f32 angle_cos);

/// The cutoff of air absorption at a distance: a table from 20 kHz at the listener down to 1.2 kHz at 400 m (the
/// distances are scaled by `factor`), interpolated linearly.
///
/// - Param distance: the distance in metres, not negative.
/// - Param factor: the world factor; 0 turns absorption off (the cutoff stays at 20 kHz), 1 is the table.
/// - Returns: the cutoff in Hz.
/// - Fails: a negative or non-finite input gives 20 kHz.
f32 AirCutoffHz(f32 distance, f32 factor);

/// The coefficient of a one-pole low-pass at a cutoff: `1 - exp(-2 pi cutoff / sample_rate)`, or 1 when the cutoff is
/// at or above 0.45 of the sample rate (so nothing is filtered).
///
/// - Param cutoff_hz: the cutoff.
/// - Param sample_rate: the output rate in Hz.
/// - Returns: the coefficient, above 0 and at most 1.
/// - Fails: an invalid input gives 1.
f32 AirCoefficient(f32 cutoff_hz, u32 sample_rate);

/// The pitch ratio of the Doppler effect.
///
/// `(c + v_listener . u) / (c + v_emitter . u)` where `u` points from the listener to the emitter: a listener moving
/// toward the emitter or an emitter moving toward the listener raises the pitch. The velocities are scaled by
/// `factor`, the speeds are limited to 0.9 of the speed of sound and the ratio to 0.25 to 4.
///
/// - Param speed_of_sound: c in units per second, above 0.
/// - Param factor: the Doppler factor; 0 gives 1.
/// - Param to_emitter: the unit vector from the listener to the emitter.
/// - Param listener_velocity: the listener's velocity.
/// - Param emitter_velocity: the emitter's velocity.
/// - Returns: the ratio to multiply the pitch by.
/// - Fails: an invalid input gives 1.
f32 DopplerRatio(f32 speed_of_sound, f32 factor, const Vec3& to_emitter, const Vec3& listener_velocity,
                 const Vec3& emitter_velocity);

}  // namespace vse::audio
