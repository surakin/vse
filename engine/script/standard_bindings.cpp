#include "lang/scalar.h"
#include "script/bindings.h"

namespace engine_placeholder::script
{

namespace
{

f32 NumClamp(f32 x, f32 lo, f32 hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

f32 NumLerp(f32 a, f32 b, f32 t)
{
    return a + (b - a) * t;
}

f32 NumSqrt(f32 x)
{
    return x < 0.0f ? 0.0f : Sqrt(x);
}

Vec3 VecNew(f32 x, f32 y, f32 z)
{
    return Vec3(x, y, z);
}

f32 VecDot(Vec3 a, Vec3 b)
{
    return a.Dot(b);
}

Vec3 VecCross(Vec3 a, Vec3 b)
{
    return a.Cross(b);
}

f32 VecLength(Vec3 a)
{
    return a.Length();
}

f32 VecDistance(Vec3 a, Vec3 b)
{
    return (a - b).Length();
}

Vec3 VecNormalize(Vec3 a)
{
    const f32 length = a.Length();
    return length > 0.0f ? Vec3(a.x / length, a.y / length, a.z / length) : Vec3(0.0f, 0.0f, 0.0f);
}

Vec3 VecLerp(Vec3 a, Vec3 b, f32 t)
{
    return a + (b - a) * t;
}

Quaternion QuatIdentity()
{
    return Quaternion::Identity();
}

Quaternion QuatFromAxisAngle(Vec3 axis, f32 radians)
{
    return axis.Length() > 0.0f ? Quaternion::FromAxisAngle(axis, radians) : Quaternion::Identity();
}

Quaternion QuatMul(Quaternion a, Quaternion b)
{
    return a * b;
}

Vec3 QuatRotate(Quaternion q, Vec3 v)
{
    return q.RotateVector(v);
}

}  // namespace

bool RegisterStandardBindings(BindingRegistry* registry)
{
    bool ok = true;
    ok &= registry->Add<&NumClamp>("num", "clamp", ThreadRule::Pure, "x, lo, hi",
                                   "Limits `x` to the range `lo` to `hi`.");
    ok &= registry->Add<&NumLerp>("num", "lerp", ThreadRule::Pure, "a, b, t", "Returns `a + (b - a) * t`.");
    ok &= registry->Add<&NumSqrt>("num", "sqrt", ThreadRule::Pure, "x",
                                  "Returns the square root of `x`, or 0 for a negative `x`.");
    ok &= registry->Add<&VecNew>("vec3", "new", ThreadRule::Pure, "x, y, z", "Makes a vector.");
    ok &= registry->Add<&VecDot>("vec3", "dot", ThreadRule::Pure, "a, b", "Returns the dot product.");
    ok &= registry->Add<&VecCross>("vec3", "cross", ThreadRule::Pure, "a, b",
                                   "Returns the cross product `a x b` (right-handed).");
    ok &= registry->Add<&VecLength>("vec3", "length", ThreadRule::Pure, "a", "Returns the Euclidean length.");
    ok &= registry->Add<&VecDistance>("vec3", "distance", ThreadRule::Pure, "a, b",
                                      "Returns the distance between two points.");
    ok &= registry->Add<&VecNormalize>("vec3", "normalize", ThreadRule::Pure, "a",
                                       "Returns the vector scaled to unit length; the zero vector stays zero.");
    ok &= registry->Add<&VecLerp>("vec3", "lerp", ThreadRule::Pure, "a, b, t",
                                  "Interpolates linearly between two vectors.");
    ok &= registry->Add<&QuatIdentity>("quat", "identity", ThreadRule::Pure, "",
                                       "Returns the rotation that does nothing.");
    ok &= registry->Add<&QuatFromAxisAngle>(
        "quat", "from_axis_angle", ThreadRule::Pure, "axis, radians",
        "Returns the rotation of `radians` about `axis` (identity for a zero axis).");
    ok &= registry->Add<&QuatMul>("quat", "mul", ThreadRule::Pure, "a, b",
                                  "Returns the rotation that applies `b` first and then `a`.");
    ok &= registry->Add<&QuatRotate>("quat", "rotate", ThreadRule::Pure, "q, v", "Rotates the vector `v` by `q`.");
    return ok;
}

}  // namespace engine_placeholder::script
