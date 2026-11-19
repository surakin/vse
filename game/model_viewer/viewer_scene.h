#pragma once

// The model viewer's scene: a glTF or GLB file imported in memory (asset_import), its default scene spawned as a level
// through the resource manager (scene::SpawnLevel), a skinned model posed by the animation system with a clip, and the
// entities mirrored into a render scene by the render sync, as the other demos draw theirs.

#include "animation/components.h"
#include "animation/manager_resolver.h"
#include "asset/gpu_loaders.h"
#include "asset/resource_manager.h"
#include "asset_import/gltf_import.h"
#include "containers/hash_map.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "ecs/schedule.h"
#include "ecs/world.h"
#include "jobs/scheduler.h"
#include "math/geometry.h"
#include "renderer/device_resources.h"
#include "renderer/gbuffer.h"
#include "renderer/material.h"
#include "renderer/render_scene.h"
#include "rhi/rhi.h"
#include "scene/level_spawner.h"
#include "storage/component_registry.h"
#include "storage/identity.h"
#include "string/string.h"
#include "types.h"

namespace model_viewer
{

using namespace engine_placeholder;

/// What to load.
struct ViewerOptions
{
    /// The file's bytes (a `.glb`, or a `.gltf`'s JSON).
    ByteSpan file;
    /// Reads the files a `.gltf` names (buffers, images), from its folder.
    asset::GltfSource* source = nullptr;
    /// The clip to play first (0: the first of the file's); `--clip`.
    u32 clip = 0;
    /// Whether a skinned model plays its clip (false: the rest pose).
    bool animate = true;
    /// The registry device objects register with, to come back after a device reset (null offscreen).
    renderer::DeviceResources* resources = nullptr;
};

/// One clip of the file.
struct ClipInfo
{
    asset::ResourceId id;
    String<HeapAllocator> name{HeapAllocator::Default()};
};

class ViewerScene
{
public:
    ViewerScene();
    ~ViewerScene();
    ViewerScene(const ViewerScene&) = delete;
    ViewerScene& operator=(const ViewerScene&) = delete;

    /// Imports the file and spawns its default scene on `device`.
    ///
    /// - Returns: false when the import, a load or the spawn failed; `Error()` says why.
    bool Create(rhi::Device* device, renderer::MaterialSystem* materials, const renderer::GBufferTemplates& templates,
                const ViewerOptions& options);
    /// The device after a reset.
    void Rebind(rhi::Device* device) { device_ = device; }
    void Destroy();

    /// Advances the animation by `seconds` (0 when paused) and mirrors the entities into the render scene.
    void Update(f32 seconds, jobs::Scheduler* scheduler);

    /// Plays the next clip (after the last, the first again); nothing without clips.
    void NextClip();
    /// Stops or resumes the clip.
    void SetPaused(bool paused) { paused_ = paused; }
    bool Paused() const { return paused_; }

    const renderer::RenderScene& Scene() const { return *scene_; }
    /// The world-space box around everything drawn, after the first `Update`; an empty box when nothing is drawn.
    Aabb Bounds() const;

    const char* Error() const { return error_.CStr(); }
    const asset::ImportReport& Report() const { return report_; }
    u32 ObjectCount() const { return scene_.Get() != nullptr ? scene_->ObjectCount() : 0; }
    u32 Triangles() const { return triangles_; }
    u32 SkinnedEntities() const { return skinned_; }
    const Vector<ClipInfo>& Clips() const { return clips_; }
    /// The clip playing (an index into `Clips()`), or none (`Clips().Size()`).
    usize CurrentClip() const { return current_clip_; }

private:
    bool Fail(const char* what);
    void BindPose(ecs::Entity e);  // the entity's palette from its skeleton's bind pose
    void PlayClip(usize index);
    // Moves the buffers of the meshes the level uses out of their upload state (after a load, and after a device
    // reset made them again), in one submission.
    void PrepareMeshes();

    rhi::Device* device_ = nullptr;
    String<HeapAllocator> error_{HeapAllocator::Default()};
    asset::ImportReport report_;
    asset::TypeRegistry types_;
    UniquePtr<asset::HostMemoryBackend> backend_;
    UniquePtr<asset::ResourceManager> manager_;
    asset::GpuAssetContext context_;
    UniquePtr<storage::ComponentRegistry> registry_;  // made by each Create (a registry takes each component once)
    storage::KeyIndex index_;
    UniquePtr<renderer::RenderScene> scene_;
    UniquePtr<ecs::World> world_;
    UniquePtr<ecs::Schedule> schedule_;
    scene::SpawnedLevel level_;
    UniquePtr<animation::ManagerResolver> resolver_;
    UniquePtr<animation::AnimationSystem> animation_;
    Vector<asset::Handle<asset::ClipView>> clip_handles_;
    Vector<ClipInfo> clips_;
    // Per mesh resource, for framing: the box of the vertices without morph targets (a morphed mesh's blob bounds hold
    // every target at full weight), and of a skinned mesh the bind-pose box of each joint's vertices (joint, box).
    struct MeshBoxes
    {
        Aabb box;
        Vector<u32> joints;
        Vector<Aabb> joint_boxes;
    };
    HashMap<u64, MeshBoxes, HeapAllocator> mesh_boxes_{HeapAllocator::Default()};
    Vector<ecs::Entity> posed_;    // the entities with a skeleton
    Vector<ecs::Entity> morphed_;  // the entities with morph weights
    usize current_clip_ = 0;
    u32 triangles_ = 0;
    u32 skinned_ = 0;
    bool animate_ = true;
    bool paused_ = false;
};

}  // namespace model_viewer
