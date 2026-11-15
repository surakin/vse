#pragma once
//! What a surface draws a world with (Phase 24 Day 359): a frame object (the G-buffer, lighting and tonemap passes and
//! the renderer) on the surface's device and swapchain, a render scene mirrored from the world's mesh references, and a
//! resource manager of its own that makes the world's meshes and materials resident on that device. Internal to the
//! module `engine_c`.

#include "asset/gpu_loaders.h"
#include "asset/resource_manager.h"
#include "containers/shared_ptr.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "engine/engine_c.h"
#include "renderer/camera.h"
#include "renderer/render_scene.h"
#include "renderer/scene_frame.h"
#include "types.h"

namespace engine_placeholder::engine_c
{

struct WorldObject;
class ResourceHost;

/// What one frame of a surface's scene did.
///
/// - Thread-safety: plain data.
struct SceneFrameReport
{
    renderer::FrameStatus status = renderer::FrameStatus::Ok;  ///< What the renderer answered.
    u32 objects_drawn = 0;                                     ///< Objects the G-buffer pass drew.
    u32 entities_unresolved = 0;  ///< Entities whose mesh or material did not resolve (they draw nothing).
};

/// The scene of one surface.
///
/// - Thread-safety: thread-compatible; owned by its surface, used with the world mutex held.
/// - Ownership: borrows the device, the swapchain and the resource host's backend; owns the rest.
class SurfaceScene
{
public:
    /// Makes the frame object and the resource manager on `device`; `Valid` says whether it worked.
    ///
    /// - Param before_present: called with the swapchain image of a frame that presents, before the present.
    /// - Fails: reported through `Valid`.
    SurfaceScene(rhi::Device* device, rhi::Swapchain* swapchain, ResourceHost* resources,
                 renderer::BeforePresentFn before_present, void* before_present_user);
    ~SurfaceScene();
    SurfaceScene(const SurfaceScene&) = delete;
    SurfaceScene& operator=(const SurfaceScene&) = delete;

    /// Whether the scene was made.
    bool Valid() const { return valid_; }

    /// Points the frame at a swapchain made after the old one went (a rebind).
    void Rebind(rhi::Device* device, rhi::Swapchain* swapchain);

    /// Mirrors the world's entities that have a mesh reference and a world matrix into the render scene, resolving
    /// what they name, then renders and presents one frame.
    ///
    /// - Param aspect: the swapchain's width over its height.
    /// - Thread-safety: the caller holds the world mutex.
    SceneFrameReport Render(WorldObject& world, f32 aspect, f32 seconds, f32 delta_seconds);

private:
    struct Entry
    {
        u64 entity = 0;
        u64 mesh = 0;
        u64 material = 0;
        renderer::ObjectId object;
        Mat4 transform;
        bool seen = false;
    };
    struct Mesh
    {
        u64 id = 0;
        asset::Handle<asset::GpuMeshAsset> handle;
    };
    struct Material
    {
        u64 id = 0;
        asset::Handle<asset::GpuMaterialAsset> handle;
    };

    const renderer::GpuMesh* ResolveMesh(u64 id);
    bool ResolveMaterial(u64 id, u32* index);

    bool valid_ = false;
    rhi::Device* device_ = nullptr;
    asset::GpuAssetContext context_;
    UniquePtr<asset::ResourceManager> manager_;
    asset::MountId mount_;
    renderer::SceneFrame frame_;
    UniquePtr<renderer::RenderScene> scene_;
    Vector<Entry> entries_;
    Vector<Mesh> meshes_;
    Vector<Material> materials_;
    renderer::MaterialId default_material_;
};

}  // namespace engine_placeholder::engine_c
