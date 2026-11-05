#pragma once

//! Cameras and the matrices they produce, in the conventions every renderer pass shares.
//!
//! * World and view space are right-handed with Y up; a camera looks down -Z in view space.
//! * [[renderer::PerspectiveVulkan]] maps view space to Vulkan clip space: X right, Y *down* (the matrix flips Y) and
//!   depth 0..1 (near -> 0, far -> 1, no reverse-Z). Viewports take a positive height; the flip lives in the projection.
//! * Meshes are counter-clockwise seen from outside. With the projection above that is a front face for
//!   `FrontFace::CounterClockwise`, and `CullMode::Back` removes faces that point away.
//! * Matrices are column-major ([[Mat4]]), which is std140's layout for a `mat4`, so one can be copied into a uniform
//!   buffer as it is.

#include "math/matrix.h"
#include "math/vector.h"
#include "types.h"

namespace engine_placeholder::renderer {

/// A perspective projection for a right-handed view space and Vulkan clip space (Y down, depth 0..1).
///
/// Param fov_y: vertical field of view in radians, in (0, pi).
/// Param aspect: width / height, greater than zero.
/// Param near_plane: distance to the near plane, greater than zero.
/// Param far_plane: distance to the far plane, greater than `near_plane`.
/// Returns: the projection matrix.
/// Aborts: any argument is outside the range above.
/// Thread-safety: thread-safe. A pure function of its arguments.
Mat4 PerspectiveVulkan(f32 fov_y, f32 aspect, f32 near_plane, f32 far_plane);

/// The exact inverse of [[renderer::PerspectiveVulkan]] for the same arguments, computed analytically.
///
/// Param fov_y: as for [[renderer::PerspectiveVulkan]].
/// Param aspect: as for [[renderer::PerspectiveVulkan]].
/// Param near_plane: as for [[renderer::PerspectiveVulkan]].
/// Param far_plane: as for [[renderer::PerspectiveVulkan]].
/// Returns: the matrix that maps clip space back to view space (homogeneous: divide by w afterwards).
/// Aborts: as for [[renderer::PerspectiveVulkan]].
/// Thread-safety: thread-safe. A pure function of its arguments.
Mat4 InversePerspectiveVulkan(f32 fov_y, f32 aspect, f32 near_plane, f32 far_plane);

/// A view matrix for a camera at `eye` looking at `target`, with `up` as the approximate up direction.
///
/// Param eye: the camera position.
/// Param target: the point looked at; different from `eye`.
/// Param up: the approximate up direction; not parallel to the view direction.
/// Returns: the view matrix.
/// Aborts: `target` equals `eye` or `up` is parallel to the view direction (no unique orientation).
/// Thread-safety: thread-safe. A pure function of its arguments.
Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up);

/// Translation, uniform scale and rotation about Y, applied in the order scale, rotate, translate.
///
/// Param position: where the model's origin goes.
/// Param scale: uniform scale factor.
/// Param yaw_radians: rotation about the Y axis.
/// Returns: the model matrix.
/// Fails: never.
/// Thread-safety: thread-safe. A pure function of its arguments.
Mat4 ModelMatrix(const Vec3& position, f32 scale, f32 yaw_radians);

/// The matrix that carries normals with `model`: the inverse transpose of its upper 3x3 up to a positive scale (the cofactor
/// matrix divided by the sign of the determinant, so a mirroring model does not turn normals inside out), in a Mat4.
///
/// Param model: an affine model matrix.
/// Returns: the normal matrix; normalize transformed normals in the shader.
/// Fails: never.
/// Thread-safety: thread-safe. A pure function of its argument.
Mat4 NormalMatrix(const Mat4& model);

/// A perspective camera: a position, an orientation and the projection parameters.
///
/// The orientation is an orthonormal basis stored as three columns: right, up and back (the camera looks along minus
/// back). Everything else is derived, so a camera never holds a matrix that can go stale.
///
/// Thread-safety: thread-compatible plain data.
struct Camera {
    /// The camera position in world space.
    Vec3 position = Vec3(0, 0, 0);
    /// The orientation: column 0 is right, column 1 is up, column 2 is back (minus the viewing direction).
    Mat3 rotation = Mat3::Identity();
    /// Vertical field of view in radians, in (0, pi).
    f32 fov_y = 1.0471976f;
    /// Width / height of the render target, greater than zero.
    f32 aspect = 16.0f / 9.0f;
    /// Distance to the near plane, greater than zero.
    f32 near_plane = 0.1f;
    /// Distance to the far plane, greater than `near_plane`.
    f32 far_plane = 1000.0f;

    /// Points the camera from `eye` at `target`, keeping the projection parameters.
    ///
    /// Param eye: the new position.
    /// Param target: the point looked at.
    /// Param up: the approximate up direction.
    /// Aborts: as for [[renderer::LookAt]].
    void SetLookAt(const Vec3& eye, const Vec3& target, const Vec3& up);

    /// Returns the viewing direction (a unit vector).
    Vec3 Forward() const { return Vec3(0, 0, 0) - rotation.col[2]; }
    /// Returns the camera's right direction (a unit vector).
    Vec3 Right() const { return rotation.col[0]; }
    /// Returns the camera's up direction (a unit vector).
    Vec3 Up() const { return rotation.col[1]; }

    /// Returns the view matrix: world space to view space.
    Mat4 View() const;
    /// Returns the inverse of the view matrix: view space to world space (the camera's own transform).
    Mat4 InverseView() const;
    /// Returns the projection matrix ([[renderer::PerspectiveVulkan]] of this camera's parameters).
    /// Aborts: the projection parameters are out of range.
    Mat4 Projection() const;
    /// Returns the inverse projection matrix ([[renderer::InversePerspectiveVulkan]]).
    /// Aborts: the projection parameters are out of range.
    Mat4 InverseProjection() const;
    /// Returns projection times view: world space to clip space.
    /// Aborts: the projection parameters are out of range.
    Mat4 ViewProjection() const;
    /// Returns the inverse of the view-projection: clip space to world space (homogeneous).
    /// Aborts: the projection parameters are out of range.
    Mat4 InverseViewProjection() const;
};

/// Builds a camera at `eye` looking at `target`.
///
/// Param eye: the position.
/// Param target: the point looked at.
/// Param up: the approximate up direction.
/// Returns: the camera with default projection parameters; set `fov_y`, `aspect` and the planes afterwards.
/// Aborts: as for [[renderer::LookAt]].
/// Thread-safety: thread-safe. A pure function of its arguments.
Camera MakeLookAtCamera(const Vec3& eye, const Vec3& target, const Vec3& up);

} // namespace engine_placeholder::renderer
