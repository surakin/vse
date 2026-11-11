#pragma once

// The renderer demo's world: a tiled ground, a grid of spheres and cubes in eight materials, hundreds of point lights,
// one directional light and a little ambient. Built deterministically from the options, so the demo, the golden-image
// tests and the depth tests all see the same scene.

#include "containers/shared_ptr.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "renderer/camera.h"
#include "renderer/gbuffer.h"
#include "renderer/material.h"
#include "renderer/mesh.h"
#include "renderer/render_scene.h"
#include "renderer/texture.h"
#include "rhi/rhi.h"
#include "types.h"

namespace engine_placeholder::ecs
{
class World;
class Schedule;
}  // namespace engine_placeholder::ecs
namespace engine_placeholder::scene
{
struct MeshAsset;
}  // namespace engine_placeholder::scene

namespace renderer_demo
{

using namespace engine_placeholder;

// What to build.
struct SceneOptions
{
    // The total number of objects, ground tiles included; at least 64 (the 49 ground tiles and some shapes).
    u32 objects = 540;
    // The number of point lights.
    u32 lights = 300;
    // Seeds the placement and colors.
    u32 seed = 1;
    // 0: add the objects in their natural order; any other value adds them in an order shuffled by this seed. The
    // picture must not depend on it (a depth-correctness test).
    u32 add_order_seed = 0;
    // The directory with the .rtex files; a texture that cannot be loaded is replaced by its procedural version.
    const char* asset_dir = nullptr;
    // The registry the scene's meshes and textures register with so they survive a device reset; null for a scene that
    // does not.
    renderer::DeviceResources* resources = nullptr;
    // True adds one more object, a flat screen standing in the scene (a material whose texture the program binds each
    // frame with `MaterialSystem::SetTexture(ScreenMaterial(), "albedo_tex", handle)`), facing +z, `screen_width` by
    // `screen_height` world units, its middle at `screen_center`.
    bool screen = false;
    Vec3 screen_center = Vec3(0, 2.1f, -3.6f);
    f32 screen_width = 4.8f;
    f32 screen_height = 3.0f;
};

struct DemoTexture;

// Where a demo texture comes from; kept by the scene so the texture can be loaded again after a device reset.
struct TextureLoadInfo
{
    const char* asset_dir = nullptr;  // null: procedural only
    const DemoTexture* texture = nullptr;
};

class DemoScene
{
public:
    DemoScene();
    ~DemoScene();
    DemoScene(const DemoScene&) = delete;
    DemoScene& operator=(const DemoScene&) = delete;

    // Creates the meshes, textures and materials, uploads and prepares the meshes (one submission, waiting for the GPU)
    // and fills the scene. Call it outside a frame, before the render loop. Returns false, logging why, on failure; the
    // object is then empty.
    bool Create(rhi::Device* device, renderer::MaterialSystem* materials, const renderer::GBufferTemplates& templates,
                const SceneOptions& options);
    // Points the scene at the device a reset made. Its meshes and textures were rebuilt through the registry; the scene
    // only keeps the pointer to wait on the device when it is destroyed.
    void Rebind(rhi::Device* device) { device_ = device; }

    // Releases everything, idling the device first. Safe on an empty scene; the destructor calls it. The materials of
    // the frame this scene was created with follow its textures by pointer: destroy the scene before the frame (declare
    // it after).
    void Destroy();

    // Moves the point lights to where they are at `seconds` (a system of the scene's schedule runs, so the render scene
    // is up to date on return).
    void Animate(f32 seconds);

    // The material of the screen object (`SceneOptions::screen`), or an invalid id.
    renderer::MaterialId ScreenMaterial() const { return screen_material_; }
    const renderer::RenderScene& Scene() const { return *scene_; }
    u32 ObjectCount() const { return scene_ ? scene_->ObjectCount() : 0; }
    u32 LightCount() const { return scene_ ? scene_->LightCount() : 0; }
    // The light-buffer capacity a renderer needs for this scene.
    u32 LightCapacity() const { return light_capacity_; }
    // The camera that frames the whole scene from above and to the side, for the given aspect ratio.
    renderer::Camera OverviewCamera(f32 aspect) const;
    // The middle of the grid of shapes, and a radius that contains it.
    Vec3 Center() const { return Vec3(0, 0, 0); }
    f32 Radius() const { return radius_; }

private:
    rhi::Device* device_ = nullptr;
    renderer::MaterialSystem* materials_ = nullptr;
    // The meshes are shared with the entities that draw them (scene::MeshAsset keeps one alive while a scene entry uses
    // it).
    SharedPtr<scene::MeshAsset> sphere_;
    SharedPtr<scene::MeshAsset> colored_sphere_;
    SharedPtr<scene::MeshAsset> cube_;
    SharedPtr<scene::MeshAsset> tile_;
    renderer::GpuTexture textures_[3];
    TextureLoadInfo load_info_[3];  // how each texture is loaded again after a reset
    char asset_dir_[512] = {};
    // The scene is built from entities: the world holds them, the schedule animates the lights, propagates transforms
    // and mirrors everything into scene_.
    UniquePtr<renderer::RenderScene> scene_;
    UniquePtr<ecs::World> world_;
    UniquePtr<ecs::Schedule> schedule_;
    renderer::Camera overview_;
    u32 expected_objects_ = 0;
    u32 expected_lights_ = 0;
    u32 light_capacity_ = 0;
    f32 radius_ = 1.0f;
    renderer::MaterialId screen_material_;
};

}  // namespace renderer_demo
