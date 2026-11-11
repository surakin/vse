#pragma once

//! Renderable components: what an entity carries so that the render sync can mirror it into a
//! [[renderer::RenderScene]].

#include "containers/shared_ptr.h"
#include "ecs/entity.h"
#include "math/vector.h"
#include "renderer/camera.h"
#include "renderer/mesh.h"
#include "renderer/render_scene.h"
#include "types.h"

namespace engine_placeholder::scene
{

/// A mesh shared by the entities that draw it: the holder that keeps a [[renderer::GpuMesh]] alive while any entity or
/// scene entry uses it.
///
/// An entity names it through a `SharedPtr<MeshAsset>`; the render sync holds one more reference per object it added,
/// so the geometry outlives the scene entry even when the entity that drew it is destroyed first.
///
/// - Thread-safety: thread-compatible; the reference count is atomic.
/// - Ownership: owns the mesh and, through it, the device buffers; the device must outlive it.
struct MeshAsset
{
    /// The geometry.
    renderer::GpuMesh mesh;
};

/// Draws a mesh at the entity's `WorldTransform`.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: shares `mesh`.
struct MeshRenderer
{
    /// The geometry; null or an empty mesh means "draw nothing" (counted as invalid by the sync).
    SharedPtr<MeshAsset> mesh;
    /// The material id (see [[renderer::RenderObjectDesc::material]]).
    u32 material = 0;
    /// Extra `renderer::ObjectFlags` bits; [[scene::Hidden]] adds `kHidden`.
    u32 flags = 0;
};

/// A tag: the entity's mesh is skipped by culling and never drawn (`ObjectFlags::kHidden`).
///
/// - Thread-safety: thread-compatible plain data.
struct Hidden
{
};

/// A light at the entity's `WorldTransform`: a point light sits at the entity's position, a directional light points
/// along the entity's rotated `direction`.
///
/// - Thread-safety: thread-compatible plain data.
struct LightSource
{
    /// Directional, point or ambient.
    renderer::LightType type = renderer::LightType::Point;
    /// Linear RGB color.
    Vec3 color = Vec3(1, 1, 1);
    /// Scale applied to `color`; not negative.
    f32 intensity = 1.0f;
    /// For a point light, the distance at which its contribution reaches zero; greater than zero.
    f32 radius = 10.0f;
    /// For a directional light, the unit vector from the surface toward the light in the entity's own space; the
    /// entity's rotation turns it into world space.
    Vec3 direction = Vec3(0, 1, 0);
};

/// A perspective camera at the entity's `WorldTransform`: the entity's +X is right, +Y up and -Z the viewing direction.
///
/// - Thread-safety: thread-compatible plain data.
struct Camera
{
    /// Vertical field of view in radians, in (0, pi).
    f32 fov_y = 1.0471976f;
    /// Width / height of the render target.
    f32 aspect = 16.0f / 9.0f;
    /// Distance to the near plane, greater than zero.
    f32 near_plane = 0.1f;
    /// Distance to the far plane, greater than `near_plane`.
    f32 far_plane = 1000.0f;
};

/// What the sync keeps on a mirrored entity: the ids of its object and light in the render scene (invalid when it has
/// none). The world logs the removal of this component, which is how the sync learns that an entity was destroyed.
///
/// - Thread-safety: thread-compatible plain data.
struct RenderHandle
{
    /// The world logs removals of this component (see [[ecs::World::RemovedCount]]).
    static constexpr bool kTrackRemoval = true;
    /// The entity's object, or invalid.
    renderer::ObjectId object;
    /// The entity's light, or invalid.
    renderer::LightId light;
};

/// The resource naming the camera entity (the entity needs [[scene::Camera]] and a `WorldTransform`).
///
/// - Thread-safety: thread-compatible plain data.
struct ActiveCamera
{
    /// The camera entity; the invalid entity means none.
    ecs::Entity entity;
};

/// The renderer camera built from the active camera entity, refreshed every frame by the camera system.
///
/// - Thread-safety: thread-compatible plain data.
struct CameraOutput
{
    /// The camera; valid only when `valid`.
    renderer::Camera camera;
    /// True when the active camera entity exists and has a [[scene::Camera]] and a `WorldTransform`.
    bool valid = false;
};

}  // namespace engine_placeholder::scene
