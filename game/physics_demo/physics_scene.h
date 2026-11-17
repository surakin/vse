#pragma once

// The physics demo's world: a flat floor with a few random boxes, spheres and capsules on it, and more of them spawned
// in the air above it as time passes. Everything is an ECS entity: the shape is a `scene::MeshRenderer`, the body a
// `physics::RigidBody` and `physics::Collider`, and the pose is the entity's `ecs::LocalTransform`. The schedule steps
// the physics (which writes the poses back), propagates the transforms and mirrors the entities into a render scene,
// the same pipeline as the renderer demo.

#include "containers/shared_ptr.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "ecs/entity.h"
#include "ecs/schedule.h"
#include "ecs/world.h"
#include "renderer/camera.h"
#include "renderer/device_resources.h"
#include "renderer/gbuffer.h"
#include "renderer/material.h"
#include "renderer/mesh.h"
#include "renderer/render_scene.h"
#include "rhi/rhi.h"
#include "types.h"

namespace physics_demo
{

using namespace engine_placeholder;

/// What to build.
struct SceneOptions
{
    /// Bodies alive at most; a new one replaces the oldest once the limit is reached.
    u32 max_bodies = 160;
    /// Shapes lying on the floor at the start.
    u32 start_bodies = 14;
    /// Seconds between two spawns.
    f32 spawn_interval = 0.15f;
    /// Seeds the shapes, sizes, colors and places.
    u32 seed = 1;
    /// The registry the meshes register with so they survive a device reset; null for a scene that does not.
    renderer::DeviceResources* resources = nullptr;
};

class PhysicsScene
{
public:
    PhysicsScene();
    ~PhysicsScene();
    PhysicsScene(const PhysicsScene&) = delete;
    PhysicsScene& operator=(const PhysicsScene&) = delete;

    /// Makes the meshes and materials (uploading the meshes, waiting for the GPU), the floor, the light and the first
    /// shapes. Call it outside a frame. Returns false, logging why, on failure.
    bool Create(rhi::Device* device, renderer::MaterialSystem* materials, const renderer::GBufferTemplates& templates,
                const SceneOptions& options);
    /// Points the scene at the device a reset made (its meshes were rebuilt through the registry).
    void Rebind(rhi::Device* device) { device_ = device; }
    /// Releases everything, idling the device first. Destroy it before the frame it was created with.
    void Destroy();

    /// Advances the world by `dt` seconds: spawns shapes that are due, makes bodies for the new entities, runs the
    /// schedule (physics step, transforms, render sync).
    void Step(f32 dt);

    const renderer::RenderScene& Scene() const { return *scene_; }
    u32 ObjectCount() const { return scene_ ? scene_->ObjectCount() : 0; }
    /// Shapes alive (the floor is not counted).
    u32 BodyCount() const { return static_cast<u32>(bodies_.Size()); }
    /// The lowest y of the shapes' centers, 0 with none (a test checks that nothing fell through the floor).
    f32 LowestBodyY() const;
    /// Fixed physics steps made so far.
    u64 PhysicsSteps() const;
    /// Moves the camera entity to look at the floor from `angle` radians around it; takes effect in the next `Step`.
    void SetCameraAngle(f32 angle);
    /// The camera the render sync last produced from the camera entity, for the given aspect ratio.
    renderer::Camera Camera(f32 aspect) const;

private:
    void Spawn(f32 x, f32 y, f32 z);

    rhi::Device* device_ = nullptr;
    renderer::GpuMeshPtr box_;
    renderer::GpuMeshPtr sphere_;
    renderer::GpuMeshPtr capsule_;
    u32 material_ids_[7] = {};
    UniquePtr<renderer::RenderScene> scene_;
    UniquePtr<ecs::World> world_;
    UniquePtr<ecs::Schedule> schedule_;
    Vector<ecs::Entity> bodies_;
    ecs::Entity camera_;
    u32 oldest_ = 0;
    u32 max_bodies_ = 0;
    f32 spawn_interval_ = 0.15f;
    f32 until_spawn_ = 0.0f;
    u32 rng_ = 1;
};

}  // namespace physics_demo
