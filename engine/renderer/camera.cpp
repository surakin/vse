#include "renderer/camera.h"

#include "lang/scalar.h"

#include "log/assert.h"
#include "math/quaternion.h"

namespace engine_placeholder::renderer
{

namespace
{

void CheckProjection(f32 fov_y, f32 aspect, f32 near_plane, f32 far_plane)
{
    ENGINE_ASSERT(fov_y > 0.0f && fov_y < 3.14159265f, "projection: fov_y must be in (0, pi)");
    ENGINE_ASSERT(aspect > 0.0f, "projection: aspect must be greater than zero");
    ENGINE_ASSERT(near_plane > 0.0f && far_plane > near_plane, "projection: need 0 < near < far");
}

}  // namespace

Mat4 PerspectiveVulkan(f32 fov_y, f32 aspect, f32 near_plane, f32 far_plane)
{
    CheckProjection(fov_y, aspect, near_plane, far_plane);
    const f32 f = 1.0f / Tan(fov_y * 0.5f);
    const f32 range = near_plane - far_plane;
    return Mat4::FromColumns(Vec4(f / aspect, 0, 0, 0), Vec4(0, -f, 0, 0), Vec4(0, 0, far_plane / range, -1),
                             Vec4(0, 0, near_plane * far_plane / range, 0));
}

Mat4 InversePerspectiveVulkan(f32 fov_y, f32 aspect, f32 near_plane, f32 far_plane)
{
    CheckProjection(fov_y, aspect, near_plane, far_plane);
    // With B = far / (near - far) and C = near * far / (near - far), the projection maps view (x, y, z, 1) to
    // clip (f/a x, -f y, B z + C, -z), so view x = a/f cx, y = -cy/f, z = -cw, w = (cz + B cw) / C.
    const f32 f = 1.0f / Tan(fov_y * 0.5f);
    const f32 range = near_plane - far_plane;
    const f32 b = far_plane / range;
    const f32 c = near_plane * far_plane / range;
    return Mat4::FromColumns(Vec4(aspect / f, 0, 0, 0), Vec4(0, -1.0f / f, 0, 0), Vec4(0, 0, 0, 1.0f / c),
                             Vec4(0, 0, -1.0f, b / c));
}

Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up)
{
    return MakeLookAtCamera(eye, target, up).View();
}

Mat4 NormalMatrix(const Mat4& model)
{
    const Vec3 a(model.col[0].x, model.col[0].y, model.col[0].z);
    const Vec3 b(model.col[1].x, model.col[1].y, model.col[1].z);
    const Vec3 c(model.col[2].x, model.col[2].y, model.col[2].z);
    const f32 sign = a.Dot(b.Cross(c)) < 0.0f ? -1.0f : 1.0f;  // a mirroring model would flip every normal
    return Mat4::FromBasis(b.Cross(c) * sign, c.Cross(a) * sign, a.Cross(b) * sign, Vec3(0, 0, 0));
}

Mat4 ModelMatrix(const Vec3& position, f32 scale, f32 yaw_radians)
{
    const Mat3 r = Quaternion::FromAxisAngle(Vec3(0, 1, 0), yaw_radians).ToMat3();
    return Mat4::FromBasis(r.col[0] * scale, r.col[1] * scale, r.col[2] * scale, position);
}

void Camera::SetLookAt(const Vec3& eye, const Vec3& target, const Vec3& up)
{
    const Vec3 to_target = target - eye;
    ENGINE_ASSERT(to_target.LengthSquared() > 0.0f, "LookAt: target equals eye");
    const Vec3 forward = to_target.Normalized();
    const Vec3 side = forward.Cross(up);
    ENGINE_ASSERT(up.LengthSquared() > 0.0f, "LookAt: up is the zero vector");
    ENGINE_ASSERT(side.LengthSquared() > 1e-12f * up.LengthSquared(), "LookAt: up is parallel to the view direction");
    const Vec3 right = side.Normalized();
    const Vec3 true_up = right.Cross(forward);
    position = eye;
    rotation = Mat3::FromBasis(right, true_up, Vec3(0, 0, 0) - forward);
}

Mat4 Camera::View() const
{
    const Vec3 r = rotation.col[0];
    const Vec3 u = rotation.col[1];
    const Vec3 b = rotation.col[2];
    return Mat4::FromColumns(Vec4(r.x, u.x, b.x, 0), Vec4(r.y, u.y, b.y, 0), Vec4(r.z, u.z, b.z, 0),
                             Vec4(-r.Dot(position), -u.Dot(position), -b.Dot(position), 1));
}

Mat4 Camera::InverseView() const
{
    return Mat4::FromBasis(rotation.col[0], rotation.col[1], rotation.col[2], position);
}

Mat4 Camera::Projection() const
{
    return PerspectiveVulkan(fov_y, aspect, near_plane, far_plane);
}

Mat4 Camera::InverseProjection() const
{
    return InversePerspectiveVulkan(fov_y, aspect, near_plane, far_plane);
}

Mat4 Camera::ViewProjection() const
{
    return Projection() * View();
}

Mat4 Camera::InverseViewProjection() const
{
    return InverseView() * InverseProjection();
}

Camera MakeLookAtCamera(const Vec3& eye, const Vec3& target, const Vec3& up)
{
    Camera camera;
    camera.SetLookAt(eye, target, up);
    return camera;
}

}  // namespace engine_placeholder::renderer
