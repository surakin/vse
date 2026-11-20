#pragma once

#include "math/quaternion.h"
#include "math/vector.h"
#include "types.h"

// Small comparison helpers shared across multiple test files (float/vector/
// quaternion near-equality, raw byte-buffer equality) — kept here instead of
// duplicated per file since several subsystems' tests need the same ones.
namespace vse::test
{

inline bool NearlyEqual(f32 a, f32 b, f32 epsilon = 1e-4f)
{
    f32 diff = a - b;
    return (diff < 0 ? -diff : diff) <= epsilon;
}

inline bool Vec2NearlyEqual(const Vec2& a, const Vec2& b)
{
    return NearlyEqual(a.x, b.x) && NearlyEqual(a.y, b.y);
}

inline bool Vec3NearlyEqual(const Vec3& a, const Vec3& b)
{
    return NearlyEqual(a.x, b.x) && NearlyEqual(a.y, b.y) && NearlyEqual(a.z, b.z);
}

inline bool QuaternionNearlyEqual(const Quaternion& a, const Quaternion& b)
{
    return NearlyEqual(a.x, b.x) && NearlyEqual(a.y, b.y) && NearlyEqual(a.z, b.z) && NearlyEqual(a.w, b.w);
}

inline bool BytesEqual(const u8* a, const u8* b, usize n)
{
    for (usize i = 0; i < n; ++i)
    {
        if (a[i] != b[i])
        {
            return false;
        }
    }
    return true;
}

}  // namespace vse::test
