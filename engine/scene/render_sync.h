#pragma once

//! The bridge from ECS entities to the renderer: a system that mirrors renderable entities into a
//! [[renderer::RenderScene]] and one that builds the camera.

#include "ecs/schedule.h"
#include "ecs/world.h"
#include "renderer/render_scene.h"
#include "scene/components.h"
#include "types.h"

namespace engine_placeholder::scene
{

/// The resource that tells the sync which scene to mirror into.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: `scene` is borrowed; it outlives the world's schedule and is not replaced once the sync has run.
struct SceneLink
{
    /// The render scene; null makes the sync do nothing (counted in `SceneSyncReport::no_scene`).
    renderer::RenderScene* scene = nullptr;
};

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
    /// `SetFlags` calls.
    u64 flags_set = 0;
    /// `SetLight` calls.
    u64 lights_set = 0;
    /// Adds the full render scene refused (an entity is retried every frame until there is room).
    u64 refused_full = 0;
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

}  // namespace engine_placeholder::scene
