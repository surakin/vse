#pragma once
//! What a surface draws a world with (Phase 24 Day 359): a frame object (the G-buffer, lighting and tonemap passes and
//! the renderer) on the surface's device and swapchain, a render scene mirrored from the world's mesh references, and a
//! resource manager of its own that makes the world's meshes and materials resident on that device. Internal to the
//! module `engine_c`.

#include "asset/cpu_loaders.h"
#include "asset/gpu_loaders.h"
#include "asset/resource_manager.h"
#include "containers/shared_ptr.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "engine/engine_c.h"
#include "engine_c/live_textures.h"
#include "engine_c/runtime.h"
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
    u32 lights_refused = 0;       ///< Lights of the world the scene had no room for, or that point nowhere.
    u32 labels_drawn = 0;         ///< Labels queued for the frame's text pass.
    u32 labels_refused = 0;       ///< Labels not drawn: no font, no room, or text the font could not lay out.
    i32 camera_status = 0;        ///< ENGINE_OK, or NOT_FOUND when the named camera entity was not usable.
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

    /// Mirrors the world's entities that have a mesh reference and a world matrix into the render scene, resolving
    /// what they name, then renders and presents one frame.
    ///
    /// - Param target: the off-screen colour target of a surface with no swapchain, null for a swapchain surface.
    /// - Param camera_entity: the entity key of the camera to look through, 0 for the default camera.
    /// - Param width: the swapchain's width in pixels.
    /// - Param height: the swapchain's height in pixels.
    /// - Thread-safety: the caller holds the world mutex.
    SceneFrameReport Render(WorldObject& world, u64 camera_entity, u32 width, u32 height, f32 seconds,
                            f32 delta_seconds, const renderer::FrameTarget* target = nullptr);

    /// Names the runtime the surface's host textures and label font come from (null: a slot naming a host texture
    /// keeps its own value, and no label is drawn).
    void AttachRuntime(Runtime* runtime)
    {
        runtime_ = runtime;
        live_.Attach(device_, runtime != nullptr ? &runtime->textures : nullptr);
    }
    /// Host texture uploads and bytes this surface made.
    u64 TextureUploads() const { return live_.Uploads(); }
    u64 TextureBytes() const { return live_.UploadedBytes(); }

    /// Turns a surface pixel into a world ray with the camera of the last frame.
    ///
    /// - Returns: false when no frame has drawn yet.
    bool PixelRay(f32 x, f32 y, f32* origin, f32* direction) const;
    /// Projects a world point with the camera of the last frame (`out`: pixel x, pixel y, distance along the view).
    ///
    /// - Returns: false when no frame has drawn yet.
    bool Project(const f32* point, f32* out) const;

private:
    static constexpr u32 kMaxParts = 16;  // the most mesh slots an entity gives a material of their own
    struct Entry
    {
        u64 entity = 0;
        u64 mesh = 0;
        u64 material = 0;
        // One object per mesh slot (one for a mesh drawn whole): what each was made with, in order.
        u32 count = 0;
        u32 part_material[kMaxParts] = {};  // the material in the material system the object was made with
        u32 part_first[kMaxParts] = {};
        u32 part_count[kMaxParts] = {};
        renderer::ObjectId objects[kMaxParts];
        Mat4 transform;
        bool seen = false;
    };
    struct LightEntry
    {
        u64 entity = 0;
        renderer::LightId id;
        bool seen = false;
    };
    // A copy of a material that one entity draws with because it overrides slots of it.
    struct Instance
    {
        u64 entity = 0;
        u32 part = 0;  // the mesh slot it is for (0 for a mesh drawn whole)
        u32 base_index = 0;
        SlotOverrides content;
        renderer::MaterialId material;
        Vector<asset::Handle<asset::GpuTextureAsset>> textures;  // keeps the overriding textures resident
        rhi::TextureHandle host_handles[SlotOverrides::kMax];    // the device textures bound for host textures
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

    void SyncLights(WorldObject& world, SceneFrameReport* report);
    // Projects the world's labels through the frame's camera and queues them for the text pass.
    void SyncLabels(WorldObject& world, const renderer::Camera& camera, u32 width, u32 height,
                    SceneFrameReport* report);
    // The material an entity draws with: the base material, or its own copy with the entity's slot overrides applied.
    u32 MaterialFor(const SlotOverrides& overrides, u64 entity, u32 part, u32 base_index, SceneFrameReport* report);
    void DestroyInstance(usize index);
    const renderer::GpuMesh* ResolveMesh(u64 id);
    bool ResolveMaterial(u64 id, bool skinned, u32* index);

    bool valid_ = false;
    rhi::Device* device_ = nullptr;
    asset::GpuAssetContext context_;
    UniquePtr<asset::ResourceManager> manager_;
    asset::MountId mount_;
    renderer::SceneFrame frame_;
    UniquePtr<renderer::RenderScene> scene_;
    Vector<Entry> entries_;
    Vector<LightEntry> light_entries_;
    Vector<Instance> instances_;
    renderer::LightId ambient_;  // the environment's ambient term
    renderer::LightId sun_;      // the default sun of a world with no light and no environment
    Vector<Mesh> meshes_;
    Vector<Material> materials_;
    Runtime* runtime_ = nullptr;
    u64 label_font_version_ = 0;  // of the font this surface's frame opened
    LiveTextures live_;           // the device side of the host textures the world's slots name
    renderer::MaterialId default_material_;
    renderer::MaterialId default_skinned_material_;  // for meshes with a skin stream (Day 363)
    renderer::Camera last_camera_;                   // the camera of the last frame, for PixelRay and Project
    u32 last_width_ = 0;
    u32 last_height_ = 0;
};

}  // namespace engine_placeholder::engine_c
