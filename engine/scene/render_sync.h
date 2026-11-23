#pragma once

//! The bridge from ECS entities to the renderer: a system that mirrors renderable entities into a
//! [[renderer::RenderScene]] and one that builds the camera.

#include "ecs/schedule.h"
#include "ecs/transform.h"
#include "ecs/world.h"
#include "renderer/render_scene.h"
#include "scene/components.h"
#include "math/vector.h"
#include "types.h"

namespace vse::scene
{

/// The resource that tells the sync which scene to mirror into.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: `scene` is borrowed; it outlives the world's schedule and is not replaced once the sync has run.
struct SceneLink
{
    /// The render scene; null makes the sync do nothing (counted in `SceneSyncReport::no_scene`).
    renderer::RenderScene* scene = nullptr;
    /// The render origin: the world position that is (0, 0, 0) in the render scene. Everything the sync writes to the
    /// scene (objects, lights, terrains) and the camera system's camera is relative to it, narrowed to `f32`, so a
    /// world far from the world origin renders with the precision of a world near it. Zero until
    /// [[scene::UpdateRenderOrigin]] moves it.
    Vec3d origin = Vec3d(0, 0, 0);
    /// How far the camera may drift from `origin` (on any axis) before [[scene::UpdateRenderOrigin]] re-bases it, and
    /// the grid the new origin snaps to, in world units; 0 keeps the origin at zero (the default).
    f64 rebase_step = 0.0;
    /// Counts re-bases; the sync rewrites every object and light when it changes.
    u64 generation = 0;
};

/// Moves the render origin of `link` to the camera's position, snapped to a multiple of `link.rebase_step`, when the
/// camera has drifted more than that step from it; call it once per frame, before the schedule runs, with the active
/// camera's `WorldTransform::position`. Does nothing while `rebase_step` is 0.
///
/// - Param link: the link of the world's sync systems.
/// - Param camera: the camera's exact world position.
/// - Returns: true when the origin moved (the next sync rewrites every object and light, and the scene's
///   transforms change by the difference).
/// - Fails: never.
bool UpdateRenderOrigin(SceneLink& link, const Vec3d& camera);

/// The world transform's matrix relative to `origin`: the rotation and scale of `w.matrix`, the translation
/// `w.position - origin` computed in `f64` and then narrowed. With a zero origin it is `w.matrix` itself.
///
/// - Fails: never.
Mat4 RelativeMatrix(const ecs::WorldTransform& w, const Vec3d& origin);

/// What the sync did, accumulated over its frames (a resource the caller sets with `SetResource(SceneSyncReport{})`).
///
/// - Thread-safety: thread-compatible plain data.
struct SceneSyncReport
{
    /// Frames the sync ran.
    u64 frames = 0;
    /// Frames it ran with no scene linked.
    u64 no_scene = 0;
    /// Objects added to the render scene (a re-add counts again).
    u64 objects_added = 0;
    /// Objects removed from the render scene.
    u64 objects_removed = 0;
    /// Lights added.
    u64 lights_added = 0;
    /// Lights removed.
    u64 lights_removed = 0;
    /// Objects removed and added again because the mesh or the material changed.
    u64 readded = 0;
    /// `SetTransform` calls (only for objects whose matrix really differs).
    u64 transforms_set = 0;
    /// Joint palettes pointed at a skinned object (a new object, or a palette that moved or changed length).
    u64 joints_set = 0;
    /// `SetMorphWeights` calls.
    u64 morph_set = 0;
    /// `SetFlags` calls.
    u64 flags_set = 0;
    /// `SetLight` calls.
    u64 lights_set = 0;
    /// Adds the full render scene refused (an entity is retried every frame until there is room).
    u64 refused_full = 0;
    /// Terrains added to the render scene (a re-add counts again).
    u64 terrains_added = 0;
    /// Terrains removed from the render scene.
    u64 terrains_removed = 0;
    /// `SetTerrain` calls (a new source, material, placement or hidden state).
    u64 terrains_set = 0;
    /// Entities skipped because their mesh is missing or empty, or their light has values the scene would abort on.
    u64 invalid = 0;
};

/// The render sync system: it mirrors entities into the linked `RenderScene` and touches it only for what changed.
///
/// It keeps a [[scene::RenderHandle]] on every mirrored entity:
///
/// - an entity with `MeshRenderer` and `WorldTransform` (and no `RenderHandle`) gets an object; one with `LightSource`
/// (and no `MeshRenderer`) gets a light;
/// - a changed `WorldTransform` moves the object (or recomputes the light), only when the matrix differs from the
/// scene's;
/// - a changed `MeshRenderer` re-adds the object when the mesh or material differs, or updates the flags; the `Hidden`
/// tag maps to `ObjectFlags::kHidden`;
/// - a destroyed entity (found through the world's removal log of `RenderHandle`) or one that lost its `MeshRenderer`
/// or `LightSource` has its object or light removed;
/// - a full scene is a counted refusal, retried every frame; an unusable mesh or light is counted as invalid on each
/// frame it is looked at.
///
/// Needs the resources [[scene::SceneLink]] and [[scene::SceneSyncReport]] in the world. Register it in `ecs::kRender`;
/// an entity does not mirror both a mesh and a light. The sync holds a reference to each object's mesh asset, so the
/// geometry outlives its scene entry.
///
/// - Param name: the system's name, copied by the schedule.
/// - Returns: the system description to `Schedule::Add`.
/// - Aborts: the linked scene changes between frames; a needed resource is missing.
/// - Thread-safety: thread-compatible; it writes the render scene, so it must not run while the renderer reads it.
/// - Ownership: the system owns its queries and the references it keeps; the scene is borrowed.
ecs::SystemDesc MakeRenderSyncSystem(const char* name);

/// The camera system: builds [[scene::CameraOutput]] from the [[scene::ActiveCamera]] entity's `Camera` and
/// `WorldTransform` every frame.
///
/// Needs the resources `ActiveCamera` and `CameraOutput`.
///
/// - Param name: the system's name.
/// - Returns: the system description to `Schedule::Add`.
/// - Aborts: a needed resource is missing.
/// - Thread-safety: thread-compatible.
ecs::SystemDesc MakeCameraSystem(const char* name);

/// Builds the system that mirrors [[scene::Terrain]] entities into the [[scene::SceneLink]] scene: it adds a terrain
/// when an entity gets the component, replaces it when the source, the material, the placement or the [[scene::Hidden]]
/// tag changes, and removes it when the component or the entity goes. It reports in [[scene::SceneSyncReport]] and
/// counts a terrain with no valid source or a placement that is not a translation and a turn about y as `invalid`.
///
/// - Param name: the system's name.
/// - Returns: the description to add to a schedule; it reads `Terrain`, `Hidden` and `WorldTransform`, writes
///   `TerrainHandle`, and writes the link and report resources.
/// - Fails: never.
/// - Ownership: the system keeps one share of each terrain's source and textures through the render scene.
ecs::SystemDesc MakeTerrainSyncSystem(const char* name);

}  // namespace vse::scene
