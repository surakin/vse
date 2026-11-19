// The model viewer's orbit camera. See orbit_camera.h.
#include "orbit_camera.h"

#include "lang/scalar.h"

namespace model_viewer
{

namespace
{

f32 Clamp(f32 v, f32 lo, f32 hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

}  // namespace

void OrbitCamera::Frame(const Aabb& box, f32 aspect)
{
    const Vec3 center = (box.min + box.max) * 0.5f;
    const Vec3 half = (box.max - box.min) * 0.5f;
    f32 radius = half.Length();
    if (!(radius > 1e-4f) || !IsFinite(radius))
    {
        radius = 1.0f;  // an empty or degenerate box: a unit view around its middle
    }
    target_ = IsFinite(center.x) && IsFinite(center.y) && IsFinite(center.z) ? center : Vec3(0.0f, 0.0f, 0.0f);
    size_ = radius;
    // The sphere around the box fits the narrower of the two fields of view.
    const f32 half_v = kFovY * 0.5f;
    // The narrower field's half angle: the horizontal one is narrower when the view is taller than wide.
    const f32 tan_h = Tan(half_v) * (aspect > 0.0f ? aspect : 1.0f);
    const f32 tan_min = tan_h < Tan(half_v) ? tan_h : Tan(half_v);
    const f32 sin_min = tan_min / Sqrt(1.0f + tan_min * tan_min);
    distance_ = radius / sin_min * 1.1f;
    min_distance_ = radius * 0.05f;
    max_distance_ = distance_ * 20.0f;
    yaw_ = 0.6f;
    pitch_ = 0.35f;
}

void OrbitCamera::Orbit(f32 dx, f32 dy)
{
    yaw_ -= dx;
    pitch_ = Clamp(pitch_ + dy, -kPitchLimit, kPitchLimit);
}

void OrbitCamera::Zoom(f32 notches)
{
    distance_ = Clamp(distance_ * Pow(0.88f, notches), min_distance_, max_distance_);
}

void OrbitCamera::Pan(f32 dx, f32 dy, f32 viewport_height)
{
    if (!(viewport_height > 0.0f))
    {
        return;
    }
    // At the target's depth the view is 2 * distance * tan(fov / 2) units high.
    const f32 units_per_pixel = 2.0f * distance_ * Tan(kFovY * 0.5f) / viewport_height;
    const renderer::Camera view = View(1.0f);
    const Vec3 right = view.rotation.col[0];
    const Vec3 up = view.rotation.col[1];
    target_ = target_ - right * (dx * units_per_pixel) + up * (dy * units_per_pixel);
}

void OrbitCamera::Strafe(f32 right, f32 up, f32 forward, f32 seconds)
{
    const f32 step = size_ * seconds;  // the framed model's radius per second: across it in about two seconds
    const Vec3 level_forward(-Sin(yaw_), 0.0f, -Cos(yaw_));
    const Vec3 level_right(Cos(yaw_), 0.0f, -Sin(yaw_));
    target_ = target_ + level_right * (right * step) + Vec3(0.0f, 1.0f, 0.0f) * (up * step) +
              level_forward * (forward * step);
}

Vec3 OrbitCamera::Eye() const
{
    const f32 c = Cos(pitch_);
    return target_ + Vec3(c * Sin(yaw_), Sin(pitch_), c * Cos(yaw_)) * distance_;
}

renderer::Camera OrbitCamera::View(f32 aspect) const
{
    renderer::Camera camera = renderer::MakeLookAtCamera(Eye(), target_, Vec3(0.0f, 1.0f, 0.0f));
    camera.fov_y = kFovY;
    camera.aspect = aspect > 0.0f ? aspect : 1.0f;
    camera.near_plane = Clamp(distance_ * 0.01f, 1e-3f, 10.0f);
    camera.far_plane = distance_ + size_ * 50.0f + 10.0f;
    return camera;
}

}  // namespace model_viewer
