// The model viewer's orbit camera (maintenance, 2026-11-19): framing, orbit, zoom, pan and strafe, with no window.
#include <catch2/catch_test_macros.hpp>

#include "lang/limits.h"
#include "lang/scalar.h"
#include "orbit_camera.h"

using namespace engine_placeholder;
using model_viewer::OrbitCamera;

namespace
{

bool Near(f32 a, f32 b, f32 tolerance = 1e-3f)
{
    return Abs(a - b) <= tolerance * (1.0f + Abs(b));
}

Aabb Box(const Vec3& min, const Vec3& max)
{
    Aabb box;
    box.min = min;
    box.max = max;
    return box;
}

// Whether `point` projects inside the camera's view (the box corners must, after framing).
bool InView(const renderer::Camera& camera, const Vec3& point)
{
    const Vec3 to = point - camera.position;
    const Vec3 right = camera.rotation.col[0];
    const Vec3 up = camera.rotation.col[1];
    const Vec3 forward = camera.Forward();
    const f32 depth = to.Dot(forward);
    if (!(depth > camera.near_plane) || !(depth < camera.far_plane))
    {
        return false;
    }
    const f32 half_h = Tan(camera.fov_y * 0.5f) * depth;
    const f32 half_w = half_h * camera.aspect;
    return Abs(to.Dot(up)) <= half_h && Abs(to.Dot(right)) <= half_w;
}

}  // namespace

TEST_CASE("Orbit camera: framing puts every corner of the box in the view, for wide and tall views", "[model_viewer]")
{
    const Aabb box = Box(Vec3(-3.0f, 0.0f, -1.0f), Vec3(5.0f, 2.0f, 1.0f));
    for (const f32 aspect : {16.0f / 9.0f, 1.0f, 9.0f / 16.0f})
    {
        OrbitCamera camera;
        camera.Frame(box, aspect);
        const renderer::Camera view = camera.View(aspect);
        CHECK(Near(camera.Target().x, 1.0f));
        CHECK(Near(camera.Target().y, 1.0f));
        CHECK(Near(camera.Target().z, 0.0f));
        for (u32 c = 0; c < 8; ++c)
        {
            const Vec3 corner((c & 1) ? box.max.x : box.min.x, (c & 2) ? box.max.y : box.min.y,
                              (c & 4) ? box.max.z : box.min.z);
            INFO("aspect " << aspect << ", corner " << c);
            CHECK(InView(view, corner));
        }
    }
}

TEST_CASE("Orbit camera: an empty or broken box frames a unit view at the origin", "[model_viewer]")
{
    OrbitCamera camera;
    Aabb empty;
    empty.min = Vec3(1.0f, 1.0f, 1.0f);
    empty.max = Vec3(1.0f, 1.0f, 1.0f);
    camera.Frame(empty, 1.0f);
    CHECK(camera.Distance() > 1.0f);
    CHECK(camera.Distance() < 10.0f);
    Aabb broken;
    broken.min = Vec3(Limits<f32>::QuietNaN(), 0.0f, 0.0f);
    broken.max = Vec3(1.0f, 1.0f, 1.0f);
    camera.Frame(broken, 1.0f);
    CHECK(IsFinite(camera.Eye().x));
    CHECK(IsFinite(camera.Distance()));
}

TEST_CASE("Orbit camera: orbiting keeps the distance and clamps the pitch short of the poles", "[model_viewer]")
{
    OrbitCamera camera;
    camera.Frame(Box(Vec3(-1, -1, -1), Vec3(1, 1, 1)), 1.0f);
    const f32 distance = camera.Distance();
    for (u32 i = 0; i < 50; ++i)
    {
        camera.Orbit(0.3f, 0.2f);
        CHECK(Near((camera.Eye() - camera.Target()).Length(), distance));
    }
    CHECK(camera.Pitch() <= OrbitCamera::kPitchLimit);
    camera.Orbit(0.0f, -100.0f);
    CHECK(camera.Pitch() >= -OrbitCamera::kPitchLimit);
}

TEST_CASE("Orbit camera: zoom moves toward the target and stops at the limits", "[model_viewer]")
{
    OrbitCamera camera;
    camera.Frame(Box(Vec3(-1, -1, -1), Vec3(1, 1, 1)), 1.0f);
    const f32 start = camera.Distance();
    camera.Zoom(1.0f);
    CHECK(camera.Distance() < start);
    camera.Zoom(-2.0f);
    CHECK(camera.Distance() > start);
    camera.Zoom(1000.0f);
    CHECK(Near(camera.Distance(), camera.MinDistance()));
    camera.Zoom(-1000.0f);
    CHECK(Near(camera.Distance(), camera.MaxDistance()));
}

TEST_CASE("Orbit camera: a pan moves the target in the view plane, as far as the mouse moved at its depth",
          "[model_viewer]")
{
    OrbitCamera camera;
    camera.Frame(Box(Vec3(-1, -1, -1), Vec3(1, 1, 1)), 1.0f);
    const renderer::Camera before = camera.View(1.0f);
    const Vec3 target = camera.Target();
    const f32 height = 720.0f;
    camera.Pan(100.0f, 0.0f, height);  // the mouse moves 100 pixels right: the scene follows, the target goes left
    const Vec3 moved = camera.Target() - target;
    CHECK(Near(moved.Dot(before.Forward()), 0.0f, 1e-3f));  // in the view plane
    const f32 expected = 100.0f * 2.0f * camera.Distance() * Tan(OrbitCamera::kFovY * 0.5f) / height;
    CHECK(Near(-moved.Dot(before.rotation.col[0]), expected));
    CHECK(Near(camera.Distance(), (camera.Eye() - camera.Target()).Length()));
}

TEST_CASE("Orbit camera: strafing moves the target level with the view, at the model's size per second",
          "[model_viewer]")
{
    OrbitCamera camera;
    camera.Frame(Box(Vec3(-2, -2, -2), Vec3(2, 2, 2)), 1.0f);
    const Vec3 start = camera.Target();
    camera.Strafe(0.0f, 0.0f, 1.0f, 1.0f);  // forward for a second
    const Vec3 moved = camera.Target() - start;
    CHECK(Near(moved.y, 0.0f));
    CHECK(moved.Dot(camera.View(1.0f).Forward()) > 0.0f);  // toward where the camera looks
    CHECK(Near(moved.Length(), Vec3(2, 2, 2).Length()));
    camera.Strafe(0.0f, 1.0f, 0.0f, 0.5f);  // up for half a second
    CHECK(camera.Target().y > start.y);
}
