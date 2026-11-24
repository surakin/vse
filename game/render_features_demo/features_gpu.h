#pragma once

// The render features demo's device side: the meshes, the materials, the terrain's height texture and layers, the cube
// map sky, and the render scene (terrain, the forest, the rocks, the character and the bodies) mirrored from a
// FeaturesWorld.

#include "containers/array.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "features_world.h"
#include "renderer/device_resources.h"
#include "renderer/gbuffer.h"
#include "renderer/material.h"
#include "renderer/mesh.h"
#include "renderer/render_scene.h"
#include "renderer/sky.h"
#include "rhi/rhi.h"
#include "types.h"

namespace render_features
{

using namespace vse;

struct GpuOptions
{
    renderer::DeviceResources* resources = nullptr;  ///< Device objects register here to survive a reset (null: none).
    bool cube_sky = true;                            ///< The cube map sky; false is the procedural one.
};

class FeaturesGpu
{
public:
    FeaturesGpu();
    ~FeaturesGpu();
    FeaturesGpu(const FeaturesGpu&) = delete;
    FeaturesGpu& operator=(const FeaturesGpu&) = delete;

    /// Makes everything the scene draws on `device`. `world` outlives this object.
    bool Create(rhi::Device* device, renderer::MaterialSystem* materials, const renderer::GBufferTemplates& templates,
                FeaturesWorld* world, const GpuOptions& options);
    /// The device after a reset (the registered objects were made again by the registry).
    void Rebind(rhi::Device* device) { device_ = device; }
    void Destroy();

    /// Moves the character's and the bodies' objects to where the physics put them.
    void Sync();

    /// Chooses the sky: the cube map (true) or the procedural gradient.
    void SetCubeSky(bool cube);
    const renderer::SkySettings& Sky() const { return sky_; }

    const renderer::RenderScene& Scene() const { return *scene_; }
    const char* Error() const { return error_; }

private:
    bool Fail(const char* what);
    renderer::GpuTexturePtr MakeSkyCube();
    renderer::GpuTexturePtr MakeLayerArray();
    bool PrepareMeshes();

    rhi::Device* device_ = nullptr;
    FeaturesWorld* world_ = nullptr;
    renderer::DeviceResources* resources_ = nullptr;
    const char* error_ = "";
    UniquePtr<renderer::RenderScene> scene_;
    Array<renderer::GpuMesh, kTreeMeshCount> tree_meshes_;
    renderer::GpuMesh rock_mesh_;
    renderer::GpuMesh sphere_;
    renderer::GpuMesh cube_;
    renderer::SkySettings sky_;
    renderer::GpuTexturePtr sky_cube_;
    Vector<renderer::ObjectId> body_objects_;
    renderer::ObjectId character_object_;
    u32 default_material_ = 0;
    u32 instanced_material_ = 0;
};

}  // namespace render_features
