#pragma once

// The model viewer's camera: it orbits a target point. Plain math with no window or device, so a test checks it alone.
// Left-drag orbits (yaw, pitch), the wheel zooms (the distance), right- or middle-drag pans the target in the view
// plane, and the keys strafe the target. `Frame` fits a box in the view.

#include "math/geometry.h"
#include "math/vector.h"
#include "renderer/camera.h"
#include "types.h"

namespace model_viewer
{

using namespace engine_placeholder;

class OrbitCamera
{
public:
    /// The vertical field of view, in radians.
    static constexpr f32 kFovY = 0.8f;
    /// The pitch stays this far short of straight up and straight down, in radians.
    static constexpr f32 kPitchLimit = 1.50f;

    /// Points the camera at the middle of `box` from a three-quarter view, far enough that the whole box is in a view
    /// of aspect `aspect`; the zoom limits follow the box's size.
    void Frame(const Aabb& box, f32 aspect);

    /// Turns around the target: `dx`, `dy` in radians (the mouse's pixels times a rate).
    void Orbit(f32 dx, f32 dy);
    /// Moves toward the target (positive notches) or away, by a factor per notch, within the zoom limits.
    void Zoom(f32 notches);
    /// Moves the target in the view plane by `dx`, `dy` pixels of a view `viewport_height` pixels high, so a point at
    /// the target follows the mouse.
    void Pan(f32 dx, f32 dy, f32 viewport_height);
    /// Moves the target by `right`, `up` and `forward` (each -1 to 1) for `seconds`, at a speed that crosses the framed
    /// model in about two seconds; forward is the view direction kept level.
    void Strafe(f32 right, f32 up, f32 forward, f32 seconds);

    /// Where the camera is.
    Vec3 Eye() const;
    /// The camera for a view of aspect `aspect`, with near and far planes that follow the distance.
    renderer::Camera View(f32 aspect) const;

    Vec3 Target() const { return target_; }
    f32 Distance() const { return distance_; }
    f32 Yaw() const { return yaw_; }
    f32 Pitch() const { return pitch_; }
    f32 MinDistance() const { return min_distance_; }
    f32 MaxDistance() const { return max_distance_; }

private:
    Vec3 target_ = Vec3(0.0f, 0.0f, 0.0f);
    f32 yaw_ = 0.6f;
    f32 pitch_ = 0.35f;
    f32 distance_ = 5.0f;
    f32 size_ = 1.0f;  // the framed box's radius
    f32 min_distance_ = 0.01f;
    f32 max_distance_ = 1000.0f;
};

}  // namespace model_viewer
