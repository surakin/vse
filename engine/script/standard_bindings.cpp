#include "lang/scalar.h"
#include "script/bindings.h"
#include "script/component_bindings.h"
#include "script/entity_bindings.h"
#include "script/event_bindings.h"
#include "script/settings_bindings.h"

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

f32 NumSign(f32 x)
{
    return x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f);
}

f32 NumSmoothstep(f32 edge0, f32 edge1, f32 x)
{
    if (!(edge1 > edge0))
    {
        return x < edge0 ? 0.0f : 1.0f;
    }
    const f32 t = NumClamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

f32 NumApproach(f32 current, f32 target, f32 step)
{
    const f32 amount = step < 0.0f ? -step : step;
    if (current < target)
    {
        return current + amount < target ? current + amount : target;
    }
    return current - amount > target ? current - amount : target;
}

Vec2 Vec2New(f32 x, f32 y)
{
    return Vec2(x, y);
}

f32 Vec2Dot(Vec2 a, Vec2 b)
{
    return a.Dot(b);
}

f32 Vec2Length(Vec2 a)
{
    return a.Length();
}

Vec2 Vec2Normalize(Vec2 a)
{
    const f32 length = a.Length();
    return length > 0.0f ? Vec2(a.x / length, a.y / length) : Vec2(0.0f, 0.0f);
}

Vec2 Vec2Lerp(Vec2 a, Vec2 b, f32 t)
{
    return a + (b - a) * t;
}

Vec4 Vec4New(f32 x, f32 y, f32 z, f32 w)
{
    return Vec4(x, y, z, w);
}

f32 Vec4Dot(Vec4 a, Vec4 b)
{
    return a.Dot(b);
}

f32 Vec4Length(Vec4 a)
{
    return a.Length();
}

Vec4 Vec4Normalize(Vec4 a)
{
    const f32 length = a.Length();
    return length > 0.0f ? Vec4(a.x / length, a.y / length, a.z / length, a.w / length) : Vec4(0.0f, 0.0f, 0.0f, 0.0f);
}

Vec4 Vec4Lerp(Vec4 a, Vec4 b, f32 t)
{
    return a + (b - a) * t;
}

Quaternion QuatSlerp(Quaternion a, Quaternion b, f32 t)
{
    return Slerp(a, b, t);
}

Quaternion QuatFromEuler(f32 x, f32 y, f32 z)
{
    const Quaternion qx = Quaternion::FromAxisAngle(Vec3(1.0f, 0.0f, 0.0f), x);
    const Quaternion qy = Quaternion::FromAxisAngle(Vec3(0.0f, 1.0f, 0.0f), y);
    const Quaternion qz = Quaternion::FromAxisAngle(Vec3(0.0f, 0.0f, 1.0f), z);
    return qz * qy * qx;
}

Quaternion QuatConjugate(Quaternion q)
{
    return q.Conjugate();
}

Quaternion QuatNormalize(Quaternion q)
{
    const f32 length = Sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return length > 0.0f ? Quaternion(q.x / length, q.y / length, q.z / length, q.w / length) : Quaternion::Identity();
}

Mat4 MatIdentity()
{
    return Mat4::Identity();
}

Mat4 MatFromTrs(Vec3 translation, Quaternion rotation, Vec3 scale)
{
    const Quaternion q = QuatNormalize(rotation);
    return Mat4::FromBasis(q.RotateVector(Vec3(scale.x, 0.0f, 0.0f)), q.RotateVector(Vec3(0.0f, scale.y, 0.0f)),
                           q.RotateVector(Vec3(0.0f, 0.0f, scale.z)), translation);
}

Mat4 MatMul(Mat4 a, Mat4 b)
{
    return a * b;
}

Vec3 MatTransformPoint(Mat4 m, Vec3 p)
{
    const Vec4 r = m * Vec4(p, 1.0f);
    return Vec3(r.x, r.y, r.z);
}

Vec3 MatTransformDirection(Mat4 m, Vec3 d)
{
    const Vec4 r = m * Vec4(d, 0.0f);
    return Vec3(r.x, r.y, r.z);
}

Mat4 MatTranspose(Mat4 m)
{
    return m.Transpose();
}

// The view matrix of a camera at `eye` looking at `target` (right-handed, the camera looks down -Z). A zero
// distance, or `up` parallel to the view direction, gives the identity.
Mat4 MatLookAt(Vec3 eye, Vec3 target, Vec3 up)
{
    const Vec3 to = target - eye;
    const f32 to_length = to.Length();
    if (!(to_length > 0.0f))
    {
        return Mat4::Identity();
    }
    const Vec3 f(to.x / to_length, to.y / to_length, to.z / to_length);
    const Vec3 side = f.Cross(up);
    const f32 side_length = side.Length();
    if (!(side_length > 0.0f))
    {
        return Mat4::Identity();
    }
    const Vec3 s(side.x / side_length, side.y / side_length, side.z / side_length);
    const Vec3 u = s.Cross(f);
    Mat4 m = Mat4::Identity();
    m.col[0] = Vec4(s.x, u.x, -f.x, 0.0f);
    m.col[1] = Vec4(s.y, u.y, -f.y, 0.0f);
    m.col[2] = Vec4(s.z, u.z, -f.z, 0.0f);
    m.col[3] = Vec4(-s.Dot(eye), -u.Dot(eye), f.Dot(eye), 1.0f);
    return m;
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
    ok &= registry->Add<&NumSign>("num", "sign", ThreadRule::Pure, "x", "Returns -1, 0 or 1 by the sign of `x`.");
    ok &= registry->Add<&NumSmoothstep>("num", "smoothstep", ThreadRule::Pure, "edge0, edge1, x",
                                        "Returns 0 below `edge0`, 1 above `edge1` and a smooth curve between.");
    ok &= registry->Add<&NumApproach>("num", "approach", ThreadRule::Pure, "current, target, step",
                                      "Moves `current` toward `target` by at most `step` and never past it.");
    ok &= registry->Add<&Vec2New>("vec2", "new", ThreadRule::Pure, "x, y", "Makes a 2D vector.");
    ok &= registry->Add<&Vec2Dot>("vec2", "dot", ThreadRule::Pure, "a, b", "Returns the dot product.");
    ok &= registry->Add<&Vec2Length>("vec2", "length", ThreadRule::Pure, "a", "Returns the Euclidean length.");
    ok &= registry->Add<&Vec2Normalize>("vec2", "normalize", ThreadRule::Pure, "a",
                                        "Returns the vector scaled to unit length; the zero vector stays zero.");
    ok &= registry->Add<&Vec2Lerp>("vec2", "lerp", ThreadRule::Pure, "a, b, t",
                                   "Interpolates linearly between two vectors.");
    ok &= registry->Add<&Vec4New>("vec4", "new", ThreadRule::Pure, "x, y, z, w", "Makes a 4D vector.");
    ok &= registry->Add<&Vec4Dot>("vec4", "dot", ThreadRule::Pure, "a, b", "Returns the dot product.");
    ok &= registry->Add<&Vec4Length>("vec4", "length", ThreadRule::Pure, "a", "Returns the Euclidean length.");
    ok &= registry->Add<&Vec4Normalize>("vec4", "normalize", ThreadRule::Pure, "a",
                                        "Returns the vector scaled to unit length; the zero vector stays zero.");
    ok &= registry->Add<&Vec4Lerp>("vec4", "lerp", ThreadRule::Pure, "a, b, t",
                                   "Interpolates linearly between two vectors.");
    ok &= registry->Add<&QuatSlerp>("quat", "slerp", ThreadRule::Pure, "a, b, t",
                                    "Interpolates between two rotations along the shorter arc (uses `acos`, `sin`: not "
                                    "bit-reproducible across machines).");
    ok &= registry->Add<&QuatFromEuler>(
        "quat", "from_euler", ThreadRule::Pure, "x, y, z",
        "Returns the rotation that turns `x` radians about X, then `y` about Y, then `z` "
        "about Z (uses `sin`, `cos`: not bit-reproducible across machines).");
    ok &= registry->Add<&QuatConjugate>("quat", "conjugate", ThreadRule::Pure, "q",
                                        "Returns the opposite rotation of a unit rotation.");
    ok &= registry->Add<&QuatNormalize>("quat", "normalize", ThreadRule::Pure, "q",
                                        "Scales the rotation to unit length (identity for a zero rotation).");
    ok &= registry->Add<&MatIdentity>("mat4", "identity", ThreadRule::Pure, "",
                                      "Returns the identity matrix (16 numbers, column-major).");
    ok &= registry->Add<&MatFromTrs>("mat4", "from_trs", ThreadRule::Pure, "translation, rotation, scale",
                                     "Returns the matrix that scales, then rotates, then translates.");
    ok &= registry->Add<&MatMul>("mat4", "mul", ThreadRule::Pure, "a, b", "Returns `a * b`: applies `b` first.");
    ok &= registry->Add<&MatTransformPoint>("mat4", "transform_point", ThreadRule::Pure, "m, p",
                                            "Applies the matrix to a point (w = 1).");
    ok &= registry->Add<&MatTransformDirection>("mat4", "transform_direction", ThreadRule::Pure, "m, d",
                                                "Applies the matrix to a direction (w = 0: no translation).");
    ok &= registry->Add<&MatTranspose>("mat4", "transpose", ThreadRule::Pure, "m", "Returns the transposed matrix.");
    ok &= registry->Add<&MatLookAt>(
        "mat4", "look_at", ThreadRule::Pure, "eye, target, up",
        "Returns the view matrix of a camera at `eye` looking at `target` (right-handed, "
        "looking down -Z); the identity when the view direction is zero or parallel to `up`.");
    ok &= RegisterRuntimeBindings(registry);
    ok &= RegisterSettingsBindings(registry);
    ok &= RegisterEntityBindings(registry);
    ok &= RegisterComponentBindings(registry);
    ok &= RegisterEventBindings(registry);
    return ok;
}

}  // namespace engine_placeholder::script
