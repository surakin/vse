// Values shared by the physics tests.
#pragma once

#include "lang/scalar.h"
#include "math/quaternion.h"
#include "math/vector.h"
#include "physics/contact.h"
#include "physics/state_hash.h"
#include "types.h"

namespace engine_placeholder::physics::testing
{

/// A quiet NaN, for the tests that feed bad numbers.
inline const f32 kNaN = __builtin_nanf("");

/// A small deterministic generator (xorshift) for the randomized tests.
struct Rng
{
    u64 state;
    explicit Rng(u64 seed) : state(seed * 2862933555777941757ull + 3037000493ull) {}
    u32 Next()
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return static_cast<u32>(state >> 16);
    }
    f32 Unit() { return static_cast<f32>(Next() % 100000u) / 100000.0f; }
    f32 Range(f32 lo, f32 hi) { return lo + (hi - lo) * Unit(); }
};

// The inputs of a hash that is compared across machines are built with exact operations only (+ - * /, Sqrt, Floor,
// Abs, comparisons), never with libm's Sin or Cos: those differ by an ulp between glibc, the UCRT and Apple's libm, and
// the hash then measures the libm, not the physics (Phase 25 Day 411, the Mac).

/// A random rotation without trigonometry: a random four-vector, normalized (four draws: x, y, z, then w; the identity
/// for a vector too short to normalize).
inline Quaternion RandomTurn(Rng& rng)
{
    const f32 x = rng.Range(-1.0f, 1.0f);
    const f32 y = rng.Range(-1.0f, 1.0f);
    const f32 z = rng.Range(-1.0f, 1.0f);
    const f32 w = rng.Range(-1.0f, 1.0f);
    const Quaternion q(x, y, z, w);
    if (q.LengthSquared() < 1e-4f)
    {
        return Quaternion::Identity();
    }
    return q.Normalized();
}

/// A random rotation about `unit_axis` without trigonometry: a random (sine, cosine) pair of the half-angle, normalized
/// (two draws; the identity for a pair too short to normalize).
inline Quaternion RandomTurnAbout(Rng& rng, const Vec3& unit_axis)
{
    const f32 s = rng.Range(-1.0f, 1.0f);
    const f32 c = rng.Range(-1.0f, 1.0f);
    const f32 length2 = s * s + c * c;
    if (length2 < 1e-4f)
    {
        return Quaternion::Identity();
    }
    const f32 k = 1.0f / Sqrt(length2);
    return Quaternion(unit_axis.x * s * k, unit_axis.y * s * k, unit_axis.z * s * k, c * k);
}

/// Adds a pose (position, then the rotation's x, y, z, w) to a hash.
inline void HashPose(StateHasher& out, const Pose& pose)
{
    out.AddF32(pose.position.x);
    out.AddF32(pose.position.y);
    out.AddF32(pose.position.z);
    out.AddF32(pose.rotation.x);
    out.AddF32(pose.rotation.y);
    out.AddF32(pose.rotation.z);
    out.AddF32(pose.rotation.w);
}

/// A smooth wave of period 2 pi between -1 and 1 shaped like a sine (two parabolas), made without trigonometry.
inline f32 Wave(f32 t)
{
    f32 u = t * 0.15915494f;  // turns
    u -= Floor(u);
    const f32 v = 2.0f * u - 1.0f;  // [-1, 1)
    return -4.0f * v * (1.0f - Abs(v));
}

}  // namespace engine_placeholder::physics::testing
