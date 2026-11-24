#pragma once

//! Renderable components: what an entity carries so that the render sync can mirror it into a
//! [[renderer::RenderScene]].

#include "containers/array.h"
#include "containers/shared_ptr.h"
#include "ecs/entity.h"
#include "math/vector.h"
#include "renderer/camera.h"
#include "renderer/mesh.h"
#include "renderer/render_scene.h"
#include "renderer/terrain_material.h"
#include "renderer/terrain_source.h"
#include "types.h"

namespace vse::scene
{

/// The most mesh slots a [[scene::MeshRenderer]] gives a material of their own.
inline constexpr u32 kMaxSlotMaterials = 16;
/// A `slot_materials` entry that means "the entity's `material`".
inline constexpr u32 kInheritMaterial = 0xFFFFFFFFu;

/// Draws a mesh at the entity's `WorldTransform`.
///
/// - Thread-safety: thread-compatible plain data.
/// The mesh is shared by every entity that draws it; the render sync holds one more share per object it added, so the
/// geometry outlives the scene entry even when the entity that drew it is destroyed first.
///
/// - Ownership: shares `mesh`.
struct MeshRenderer
{
    /// The geometry; null or an empty mesh means "draw nothing" (counted as invalid by the sync).
    renderer::GpuMeshPtr mesh;
    /// The material id (see [[renderer::RenderObjectDesc::material]]).
    u32 material = 0;
    /// Extra `renderer::ObjectFlags` bits; [[scene::Hidden]] adds `kHidden`.
    u32 flags = 0;
    /// The material of each of the mesh's slots (index ranges), for a mesh with slots: slot `i` uses
    /// `slot_materials[i]`, or `material` when that is [[scene::kInheritMaterial]] or `i` is at or past
    /// `slot_count`. A mesh without slots, a `slot_count` of 0 and a mesh with more than `kMaxSlotMaterials` slots
    /// (counted as invalid by the sync, never drawn in part) draw the whole mesh with `material`, as before.
    u32 slot_materials[kMaxSlotMaterials] = {};
    /// How many entries of `slot_materials` are set; 0 draws the mesh whole.
    u32 slot_count = 0;
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
    /// For a directional or spot light, the unit vector from the surface toward the light in the entity's own space
    /// (a spot shines along its opposite); the entity's rotation turns it into world space.
    Vec3 direction = Vec3(0, 1, 0);
    /// For a spot light, the inner half-angle in radians (stored from version 2).
    f32 inner_cone = 0.0f;
    /// For a spot light, the outer half-angle in radians, above `inner_cone`, at most pi/2.
    f32 outer_cone = 0.7853981634f;
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
    /// The entity's object (its first slot's), or invalid.
    renderer::ObjectId object;
    /// The entity's light, or invalid.
    renderer::LightId light;
    /// The objects of the mesh's other slots; the first `more_count` are valid.
    renderer::ObjectId more[kMaxSlotMaterials - 1];
    /// How many of `more` exist.
    u32 more_count = 0;
};

/// A terrain at the entity's `WorldTransform`: the height map's data and its surface. The transform must be a
/// translation and a turn about the y axis ([[renderer::TerrainWorldValid]]): a scaled or tilted terrain is not drawn
/// (counted as invalid by the sync) because the quadtree, the morph distances and the height-field collider work in
/// the terrain's own metres. A world may hold several terrains (neighbouring maps); each is drawn alone, and hiding the
/// seam between two maps is the caller's. [[scene::Hidden]] skips it. Giving the entity a new `source` (a reloaded
/// height map) makes the next sync draw the new heights.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: shares the source and the material's textures; the render sync holds one more share per terrain it
///   added.
struct Terrain
{
    renderer::TerrainSourcePtr source;   ///< The height map's data; null or not valid means "draw nothing".
    renderer::TerrainMaterial material;  ///< The surface.
};

/// What the sync keeps on a mirrored terrain entity: its id in the render scene. The world logs the removal of this
/// component, which is how the sync learns that a terrain entity was destroyed.
///
/// - Thread-safety: thread-compatible plain data.
struct TerrainHandle
{
    /// The world logs removals of this component (see [[ecs::World::RemovedCount]]).
    static constexpr bool kTrackRemoval = true;
    /// The terrain in the render scene, or invalid.
    renderer::TerrainId id;
};

/// Many copies of one mesh drawn from an [[renderer::InstanceSet]]: a forest, a field of rocks. The instances are not
/// entities; whoever owns the entity fills the set (`AddMany`, `Scatter`, `set->Add`). The entity's own transform is
/// the set's parent: instances are in the entity's local space, so moving, turning or scaling the entity moves the
/// whole set without touching it. The entity's `WorldTransform` must be a translation, a rotation and a uniform
/// positive scale (anything else is counted as invalid by the sync and not drawn). [[scene::Hidden]] skips it.
///
/// The material must be one made with `GBufferOptions::instanced`. Edits to the set show on the next frame; do not edit
/// it while a frame is being prepared.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: shares `mesh` and `set`; the render sync holds one more share of each per set it added, so the
///   geometry outlives its scene entry even when the entity is destroyed first.
struct InstancedMesh
{
    renderer::GpuMeshPtr mesh;     ///< The geometry; null or an empty mesh means "draw nothing" (counted invalid).
    renderer::InstanceSetPtr set;  ///< The instances; null means "draw nothing" (counted invalid).
    u32 material = 0;              ///< The material id (made with `GBufferOptions::instanced`).
    u32 flags = 0;                 ///< Extra `renderer::ObjectFlags` bits; [[scene::Hidden]] adds `kHidden`.
    f32 max_distance = 0.0f;       ///< Instances farther than this from the camera are not drawn; 0 for no limit.
    f32 min_size_ratio = 0.0f;  ///< Instances smaller than this fraction of their distance are not drawn; 0 for none.
};

/// What the sync keeps on a mirrored instanced entity: its id in the render scene. The world logs the removal of this
/// component, which is how the sync learns that the entity was destroyed.
///
/// - Thread-safety: thread-compatible plain data.
struct InstancedHandle
{
    /// The world logs removals of this component (see [[ecs::World::RemovedCount]]).
    static constexpr bool kTrackRemoval = true;
    /// The instanced mesh in the render scene, or invalid.
    renderer::InstancedId id;
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

}  // namespace vse::scene
