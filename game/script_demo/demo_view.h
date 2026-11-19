#pragma once

// The script demo's picture (Phase 27 Day 475): the bodies of a `DemoWorld` mirrored into a deferred render scene each
// frame, read-only, so drawing changes nothing the world decides (its state hash is the headless run's). A sphere per
// body, colored by behavior; the player brighter; the pickups as glowing beacons; the planner's marker as a red box;
// the floor as one wide box. The camera circles the floor.

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "demo_world.h"
#include "renderer/camera.h"
#include "renderer/device_resources.h"
#include "renderer/gbuffer.h"
#include "renderer/material.h"
#include "renderer/mesh.h"
#include "renderer/render_scene.h"
#include "rhi/rhi.h"
#include "types.h"

namespace script_demo
{

class DemoView
{
public:
    DemoView() = default;
    ~DemoView();
    DemoView(const DemoView&) = delete;
    DemoView& operator=(const DemoView&) = delete;

    /// Makes the meshes and materials, and an object for every body `world` lists.
    ///
    /// - Param resources: the registry that recreates the meshes after a device reset; may be null (offscreen).
    /// - Returns: false when a mesh, a material or the scene could not be made (the reason is logged).
    bool Create(rhi::Device* device, renderer::MaterialSystem* materials, const renderer::GBufferTemplates& templates,
                renderer::DeviceResources* resources, const DemoWorld& world);
    /// The device after a reset.
    void Rebind(rhi::Device* device) { device_ = device; }
    void Destroy();

    /// Moves every object to its body's place.
    void Sync(const DemoWorld& world);

    const renderer::RenderScene& Scene() const { return *scene_; }
    u32 ObjectCount() const { return scene_.Get() != nullptr ? scene_->ObjectCount() : 0; }

    /// A camera at `angle` radians around the floor, looking at its middle.
    renderer::Camera CameraAt(f32 angle, f32 aspect) const;

private:
    rhi::Device* device_ = nullptr;
    renderer::GpuMeshPtr sphere_;
    renderer::GpuMeshPtr box_;
    Vector<u32> materials_;  // by VisualKind, then the floor
    UniquePtr<renderer::RenderScene> scene_;
    Vector<renderer::ObjectId> objects_;  // one per visual, in ForEachVisual's order
    Vector<VisualKind> kinds_;
};

}  // namespace script_demo
