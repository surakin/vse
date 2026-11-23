#pragma once

//! The loaders of the resource types that live on the GPU: meshes, textures and materials.
//! [[asset::RegisterGpuLoaders]] registers them over the CPU loaders, so the same ids load as objects that
//! own device resources, made recreatable: a device reset rebuilds them from the manager's own bytes.

#include "asset/cpu_loaders.h"
#include "asset/height_map.h"
#include "asset/resource_manager.h"
#include "renderer/device_resources.h"
#include "renderer/gbuffer.h"
#include "renderer/material.h"
#include "renderer/mesh.h"
#include "renderer/texture.h"

namespace vse::asset
{

/// What the GPU loaders create objects on. Give the manager a pointer to one that stays alive and unchanged while the
/// loaders are registered (the manager keeps the pointer as the loaders' user data).
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: borrows everything; the device, the registry and the material system must outlive the manager.
struct GpuAssetContext
{
    rhi::Device* device = nullptr;                   ///< The device the objects are created on.
    renderer::DeviceResources* resources = nullptr;  ///< The registry the objects register with (null: no reset).
    renderer::MaterialSystem* materials = nullptr;   ///< Where materials are created.
    renderer::GBufferTemplates templates;            ///< The G-buffer templates a material names.
};

/// A mesh on the GPU. The buffers were made from the blob's own bytes, which this object and the [[renderer::GpuMesh]]
/// both hold a share of ([[renderer::MeshGeometry]] names the sections by offset), so a reset uploads again from the
/// same bytes, nothing is copied, and a holder of the mesh (an entity's renderer) keeps it valid even after the manager
/// unloads or reloads the resource.
///
/// - Thread-safety: thread-compatible; owner-thread-only like the renderer's owners.
/// - Ownership: shares the blob bytes and the mesh.
struct GpuMeshAsset
{
    MeshView view;                    ///< The blob, read in place.
    SharedBytesPtr bytes;             ///< The region `view` reads and `geometry` names.
    renderer::MeshGeometry geometry;  ///< The geometry as the renderer sees it.
    renderer::GpuMeshPtr mesh;        ///< The buffers, shared with whoever draws them.
};

/// A texture on the GPU, uploaded from the blob's mip sections; the texture holds a share of the blob bytes, so a reset
/// uploads from them again.
///
/// - Thread-safety: thread-compatible; owner-thread-only.
/// - Ownership: shares the blob bytes and the texture.
struct GpuTextureAsset
{
    TextureView view;                 ///< The blob, read in place.
    SharedBytesPtr bytes;             ///< The region `view` reads.
    renderer::GpuTexturePtr texture;  ///< The texture, shared with the materials that bind it.
};

/// A material in the material system, with its textures bound by owner and its samplers. The material system holds a
/// share of every texture and sampler the material binds, so none can go while the material uses it; this holder is
/// shared itself: a spawned level's entities name its slot in the material system, so the level holds a share of the
/// holder and a reload of the resource does not destroy the material under them.
///
/// - Thread-safety: thread-compatible; owner-thread-only.
/// - Ownership: owns the material (destroyed with the last share).
struct GpuMaterial
{
    GpuMaterial() = default;
    GpuMaterial(const GpuMaterial&) = delete;
    GpuMaterial& operator=(const GpuMaterial&) = delete;
    /// Destroys the material.
    ~GpuMaterial();

    renderer::MaterialSystem* materials = nullptr;  ///< The system that owns the material.
    renderer::MaterialId id;                        ///< The material.
    u32 flags = 0;                                  ///< [[asset::MaterialFlags]].
};

/// How a [[asset::GpuMaterial]] is held: shared.
using GpuMaterialPtr = SharedPtr<GpuMaterial>;

/// What a material blob is once the GPU loaders are registered: a share of its [[asset::GpuMaterial]].
///
/// - Thread-safety: thread-compatible; owner-thread-only.
/// - Ownership: shares the material.
struct GpuMaterialAsset
{
    GpuMaterialPtr material;  ///< The material, shared with the levels that draw with it.
};

/// A GPU mesh is the object of a mesh blob once the GPU loaders are registered.
///
/// - Thread-safety: thread-safe, a compile-time constant.
template <> struct ResourceTraits<GpuMeshAsset>
{
    static constexpr ResourceType kType = types::kMesh;                              ///< `MESH`.
    static constexpr u32 kKind = ResourceType::FromChars('g', 'M', 'S', 'H').Tag();  ///< The code of the C++ type.
};
/// A GPU texture is the object of a texture blob once the GPU loaders are registered.
///
/// - Thread-safety: thread-safe, a compile-time constant.
template <> struct ResourceTraits<GpuTextureAsset>
{
    static constexpr ResourceType kType = types::kTexture;                           ///< `TEXR`.
    static constexpr u32 kKind = ResourceType::FromChars('g', 'T', 'E', 'X').Tag();  ///< The code of the C++ type.
};
/// A GPU material is the object of a material blob once the GPU loaders are registered.
///
/// - Thread-safety: thread-safe, a compile-time constant.
template <> struct ResourceTraits<GpuMaterialAsset>
{
    static constexpr ResourceType kType = types::kMaterial;                          ///< `MATL`.
    static constexpr u32 kKind = ResourceType::FromChars('g', 'M', 'A', 'T').Tag();  ///< The code of the C++ type.
};

/// Makes the height texture of a height map (`R16_UNorm`, all mip levels) straight from the blob's bytes.
///
/// The texture shares the view's bytes, so a device reset uploads it again from the same samples.
///
/// - Param device: the device.
/// - Param view: an open height map.
/// - Param resources: the registry the texture registers with, or null.
/// - Param out: set to the texture on success.
/// - Returns: true on success.
/// - Fails: returns false, leaving `*out` empty, for an empty view or when the device refuses the texture.
/// - Ownership: shares the view's bytes; `*out` is written.
bool MakeHeightTexture(rhi::Device* device, const HeightMapView& view, renderer::DeviceResources* resources,
                       renderer::GpuTexturePtr* out);

/// The ids of the fallback resources ([[asset::RegisterGpuFallbacks]]); they are in no mount.
inline constexpr u64 kFallbackMeshId = 0xfa11bac000000001ull;      ///< The unit cube.
inline constexpr u64 kFallbackTextureId = 0xfa11bac000000002ull;   ///< The checker texture.
inline constexpr u64 kFallbackMaterialId = 0xfa11bac000000003ull;  ///< The magenta material.

/// Registers the fallbacks of the GPU types: a unit cube for meshes, a magenta and black checker for textures and a
/// magenta material, made in memory (they are in no mount and never reach a dependency). They stand in for a missing or
/// invalid resource of the type only when `assets.allow_fallbacks` is on, each use logged and counted in
/// [[asset::ResourceStats::fallbacks_used]].
///
/// - Param manager: the manager; its type registry is the one the blobs are built against.
/// - Fails: the failures of the blob builders (not expected).
/// - Ownership: borrows `manager` for the call; the blobs are copied into it.
Status RegisterGpuFallbacks(ResourceManager* manager);

/// Registers the loaders of meshes ([[asset::GpuMeshAsset]], built from the blob's own bytes, nothing copied; a skinned
/// mesh is checked against its skeleton and needs a skinned material), textures ([[asset::GpuTextureAsset]]) and
/// materials ([[asset::GpuMaterialAsset]], textures bound by owner), replacing the CPU loaders of those types.
///
/// - Param manager: the manager.
/// - Param context: the device, registry and material system; it must stay alive and unchanged until the manager is
///   destroyed.
/// - Ownership: borrows both; nothing is copied.
void RegisterGpuLoaders(ResourceManager* manager, GpuAssetContext* context);

}  // namespace vse::asset
