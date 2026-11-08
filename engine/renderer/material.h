#pragma once

//! A material is a shader permutation plus a parameter block and textures, with the bind groups and pipelines it needs.
//!
//! A [[renderer::MaterialSystem]] holds templates (the shaders and render state) and materials (the parameter values and
//! textures of one surface). Everything is validated against the shaders' reflection when it is created, and the system
//! owns every bind group and pipeline, so a draw creates no object. Descriptor sets follow one convention: set 0 is the
//! frame uniform ([[renderer::FrameData]]), set 1 the material, set 2 the per-object storage buffer.

#include "containers/vector.h"
#include "math/matrix.h"
#include "math/vector.h"
#include "renderer/device_resources.h"
#include "renderer/frame_data.h"
#include "renderer/shader_permutation.h"
#include "renderer/shader_reflection.h"
#include "rhi/rhi.h"
#include "types.h"

namespace engine_placeholder::renderer {

class GpuTexture;
class GpuSampler;

/// Descriptor set of the frame uniform.
inline constexpr u32 kFrameSet = 0;
/// Descriptor set of the material: its parameter block (uniform buffer), textures and samplers.
inline constexpr u32 kMaterialSet = 1;
/// Descriptor set of the per-object storage buffer.
inline constexpr u32 kObjectSet = 2;
/// Largest parameter block of a material, in bytes.
inline constexpr u32 kMaxMaterialParamBytes = 1024;
/// Most sampled textures in a material.
inline constexpr u32 kMaxMaterialTextures = 8;
/// Most samplers in a material.
inline constexpr u32 kMaxMaterialSamplers = 4;

/// A material template. The generation makes a stale id detectable; the all-zero id is invalid.
///
/// Thread-safety: thread-compatible plain data.
struct TemplateId {
    /// Slot in the system's template list.
    u32 index = 0;
    /// The slot's generation when the id was made; 0 means invalid.
    u32 generation = 0;

    /// Returns whether this is not the invalid (all-zero) id; a valid-looking id can still be stale.
    /// Fails: never.
    constexpr bool IsValid() const { return generation != 0; }
};

/// A material. The generation makes a stale id detectable; the all-zero id is invalid.
///
/// Thread-safety: thread-compatible plain data.
struct MaterialId {
    /// Slot in the system's material list.
    u32 index = 0;
    /// The slot's generation when the id was made; 0 means invalid.
    u32 generation = 0;

    /// Returns whether this is not the invalid (all-zero) id; a valid-looking id can still be stale.
    /// Fails: never.
    constexpr bool IsValid() const { return generation != 0; }
};

/// The fixed-function state a template draws with.
///
/// Thread-safety: thread-compatible plain data.
struct MaterialRenderState {
    /// Which faces are culled.
    rhi::CullMode cull = rhi::CullMode::Back;
    /// Whether the depth test runs (ignored when the target has no depth format).
    bool depth_test = true;
    /// Whether depth is written.
    bool depth_write = true;
    /// The depth comparison.
    rhi::CompareOp depth_compare = rhi::CompareOp::Less;
    /// Source-alpha blending on every color target.
    bool alpha_blend = false;
};

/// The kinds of default texture a system provides for slots a material leaves unset.
enum class DefaultTexture : u8 {
    White = 0,  ///< 1x1 opaque white: albedo, occlusion and anything multiplied in.
    Black,      ///< 1x1 opaque black: emissive.
    FlatNormal, ///< 1x1 (0.5, 0.5, 1): a tangent-space normal pointing straight out.
};

/// The vertex attribute location a material vertex shader reads the object index from on a device with
/// [[rhi::DeviceCaps::object_index_stream]] (semantic `TEXCOORD15`; `shaders/target_common.slang`).
inline constexpr u32 kObjectIndexLocation = 15;
/// How many object indices the object index stream holds: the most objects one pass draws by index.
inline constexpr u32 kMaxObjectIndices = 65536;

/// Names the default texture of one texture slot, overriding the name-based choice.
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: `slot` is borrowed for the call to [[renderer::MaterialSystem::CreateTemplate]].
struct TextureDefault {
    /// The shader's texture variable name.
    const char* slot = "";
    /// What an unset slot of that name shows.
    DefaultTexture texture = DefaultTexture::White;
};

/// The shaders and render state of a template.
///
/// The vertex stage is a single module. The fragment stage is either a single module (`fragment`) or a permuted shader
/// (`fragment_permutations`); each material then picks a [[renderer::PermutationKey]] for it.
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: the shader words, reflection blobs, permutation table and name are borrowed and must outlive the system
///   (they are normally static generated data).
struct MaterialTemplateDesc {
    /// Debug name, at most 47 characters.
    const char* name = "";
    /// The vertex module and its reflection.
    PermutationModule vertex;
    /// The fragment module and its reflection, used when `fragment_permutations` is null.
    PermutationModule fragment;
    /// The permuted fragment shader, or null.
    const ShaderPermutations* fragment_permutations = nullptr;
    /// Fixed-function state.
    MaterialRenderState state;
    /// Explicit defaults for texture slots, up to [[renderer::kMaxMaterialTextures]]; a slot not listed follows the
    /// name rule described at [[renderer::MaterialSystem]]. May be null with a count of 0.
    const TextureDefault* texture_defaults = nullptr;
    /// Number of entries in `texture_defaults`.
    u32 texture_default_count = 0;
};

/// What a pipeline renders into; part of the pipeline cache key.
///
/// Thread-safety: thread-compatible plain data.
struct PipelineTarget {
    /// Formats of the color attachments, the first `color_count` of them.
    rhi::Format color_formats[rhi::kMaxColorAttachments] = {};
    /// Number of color attachments.
    u32 color_count = 1;
    /// Depth format, or `Undefined` for no depth attachment.
    rhi::Format depth_format = rhi::Format::Undefined;
};

/// A pipeline found or created by [[renderer::MaterialSystem::GetPipeline]].
///
/// Thread-safety: thread-compatible plain data.
struct PipelineLookup {
    /// The pipeline, or invalid if it could not be created.
    rhi::PipelineHandle pipeline;
    /// Position in the system's pipeline cache; stable for the pipeline's life and used by the draw sort key.
    u32 index = 0;
};

/// What the system has done, for tests and tools.
///
/// Thread-safety: thread-compatible plain data.
struct MaterialStats {
    /// Pipeline lookups served from the cache.
    u32 pipeline_hits = 0;
    /// Pipeline lookups that had to create a pipeline (or failed to).
    u32 pipeline_misses = 0;
    /// Pipelines alive in the cache.
    u32 pipelines = 0;
    /// Times a material's bind groups were rebuilt after a texture or sampler change.
    u32 bind_group_rebuilds = 0;
    /// Times a material's parameter block was copied to the GPU buffer.
    u32 parameter_flushes = 0;
    /// Times the system released its device objects and rebuilt them after a device change.
    u32 rebuilds = 0;
};

/// Capacities of a [[renderer::MaterialSystem]].
///
/// Thread-safety: thread-compatible plain data.
struct MaterialSystemDesc {
    /// Most live templates.
    u32 max_templates = 32;
    /// Most live materials.
    u32 max_materials = 1024;
    /// Most pipelines in the cache.
    u32 max_pipelines = 256;
    /// The registry to register with so the system survives a device reset; null for none.
    DeviceResources* resources = nullptr;
    /// Whether a recreate also rebuilds every pipeline the system had (eager); otherwise a pipeline is rebuilt the first time
    /// it is asked for (lazy). Eager moves the compile cost into the reset; the pipeline cache file makes both cheap.
    bool prewarm_pipelines = false;
};

/// Packs the draw order into one integer: pipeline first, then material, then mesh.
///
/// Sorting by this key groups draws that share a pipeline and, within it, a material, so pipeline and material bind
/// groups change as rarely as possible.
///
/// Param pipeline_index: [[renderer::PipelineLookup::index]]; below 65536.
/// Param material_index: [[renderer::MaterialId::index]]; below 2^24.
/// Param mesh_index: any stable small number for the mesh (for example its slot); below 2^24.
/// Returns: the key; compare keys with `<`.
/// Aborts: an argument is out of range.
u64 MakeDrawSortKey(u32 pipeline_index, u32 material_index, u32 mesh_index);

/// Templates, materials, default textures, bind groups and the pipeline cache.
///
/// Creation validates against reflection and returns an invalid id (after logging) on any mismatch; a stale or invalid
/// id passed to another function is a programming error. Parameter writes go to a CPU shadow and reach the GPU in one
/// batch per frame in [[renderer::MaterialSystem::FlushMaterials]]. Use is: `BeginFrame`, change materials,
/// `FlushMaterials`, then record draws with the bind groups and pipelines the system hands out.
///
/// Parameter names are the member names of the fragment (or vertex) shader's set-1 uniform block; texture and sampler
/// names are the shader's variable names. A texture slot left unset uses a default texture: the one the template lists
/// for it in `texture_defaults`, otherwise one chosen by name (a name starting with `normal` gets the flat normal, one
/// starting with `emissive` or `black` gets black, anything else white). Name your normal and emissive slots to match,
/// or list them.
/// Samplers left unset use a linear, repeating sampler.
///
/// Thread-safety: thread-compatible.
/// Ownership: borrows the device, which must outlive it; owns the shaders, layouts, buffers, bind groups, pipelines and
///   default textures it creates. Textures and samplers given to a material belong to the caller and must outlive it.
class MaterialSystem final : public Recreatable {
public:
    /// Creates the system, the frame and object layouts, the default textures and the default sampler.
    ///
    /// Param device: the device; must not be null.
    /// Param desc: capacities; each at least 1, at most 2^24 materials and 65536 pipelines (the draw sort key's range).
    /// Fails: creating a default resource fails; [[renderer::MaterialSystem::Valid]] then returns false.
    /// Aborts: `device` is null or a capacity is 0 or too large.
    /// Ownership: `device` is borrowed.
    MaterialSystem(rhi::Device* device, const MaterialSystemDesc& desc);
    /// Destroys every material, template, pipeline and default resource. The GPU may still be using them; the device
    /// defers the actual release.
    ~MaterialSystem() override;
    MaterialSystem(const MaterialSystem&) = delete;
    MaterialSystem& operator=(const MaterialSystem&) = delete;

    /// Returns whether the default resources were created.
    /// Fails: never.
    bool Valid() const;

    /// Creates a template.
    ///
    /// Param desc: the shaders and state.
    /// Returns: the template, or an invalid id after logging the reason.
    /// Fails: the reflection blobs do not parse or are for the wrong stage, the vertex shader uses a descriptor set other
    ///   than 0 to 2, set 0 is not one uniform buffer at binding 0, set 2 is not one storage buffer at binding 0, more
    ///   `texture_defaults` are listed than there are slots, or the template list is full.
    /// Aborts: the system is not valid (see [[renderer::MaterialSystem::Valid]]).
    TemplateId CreateTemplate(const MaterialTemplateDesc& desc);

    /// Destroys a template and its pipelines.
    ///
    /// Param id: a live template.
    /// Aborts: `id` is invalid or stale, or a material still uses the template.
    void DestroyTemplate(TemplateId id);

    /// Creates a material.
    ///
    /// The set-1 bindings of the chosen fragment module and the vertex module are merged into a layout; the parameter
    /// block starts zeroed and every texture and sampler slot starts on its default.
    ///
    /// Param tmpl: a live template.
    /// Param key: the fragment permutation; ignored (use the default key) for a template without a permuted fragment.
    /// Param name: debug name, at most 47 characters; may be null.
    /// Returns: the material, or an invalid id after logging the reason.
    /// Fails: the key was not built, the fragment shader breaks the set conventions, the set-1 bindings do not merge, the parameter block exceeds
    ///   [[renderer::kMaxMaterialParamBytes]], there are more textures or samplers than the limits allow, a binding is an
    ///   array or an unsupported type, the vertex and fragment shaders declare different parameter blocks, or the
    ///   material list is full or a GPU object could not be created.
    /// Aborts: `tmpl` is invalid or stale.
    /// Ownership: `name` is copied.
    MaterialId CreateMaterial(TemplateId tmpl, PermutationKey key, const char* name);

    /// Destroys a material and its GPU objects (the device defers the release).
    ///
    /// Param id: a live material.
    /// Aborts: `id` is invalid or stale.
    void DestroyMaterial(MaterialId id);

    /// Returns whether `id` names a live material.
    /// Fails: never.
    bool IsAlive(MaterialId id) const;
    /// Returns the live material that has slot `index`, or an invalid id when the slot is free or out of range.
    ///
    /// A scene object stores only the slot index (`RenderObject::material`); this recovers the id with its generation.
    /// Fails: never.
    /// Thread-safety: thread-compatible; concurrent calls are fine while nothing creates or destroys materials.
    MaterialId MaterialAtIndex(u32 index) const;
    /// Returns the bind group that binds `region` of a buffer as the set-0 frame uniform ([[renderer::FrameData]]).
    ///
    /// The group is created on first use and cached by buffer, offset and size, so the three regions of a
    /// [[renderer::FrameResources]] cost three creations in total. The system holds up to 8 such groups; past that the
    /// oldest is destroyed (through the device's deferred destruction), so recreating the frame resources does not exhaust it.
    ///
    /// Param region: the frame region from [[renderer::FrameResources::FrameRegion]].
    /// Returns: the group, or an invalid handle when the device cannot create it (logged).
    /// Ownership: the system owns the group; it lives until it is evicted or the system is destroyed, which is fine after
    ///   the buffer itself was destroyed.
    /// Thread-safety: thread-compatible; call from one thread (the one preparing the frame).
    rhi::BindGroupHandle FrameBindGroup(const BufferRegion& region);
    /// Returns the bind group that binds `region` of a buffer as the set-2 per-object storage buffer; cached like
    /// [[renderer::MaterialSystem::FrameBindGroup]].
    ///
    /// Param region: the object region from [[renderer::FrameResources::ObjectRegion]].
    /// Returns: the group, or an invalid handle when the device cannot create it (logged).
    /// Ownership: as for [[renderer::MaterialSystem::FrameBindGroup]].
    /// Thread-safety: thread-compatible; call from one thread (the one preparing the frame).
    rhi::BindGroupHandle ObjectBindGroup(const BufferRegion& region);
    /// Returns whether the material's vertex shader reads the vertex attribute at `location`.
    ///
    /// Param id: a live material.
    /// Param location: a vertex input location.
    /// Aborts: `id` is invalid or stale.
    /// Thread-safety: thread-compatible; concurrent calls are fine while nothing creates or destroys materials.
    bool ReadsVertexInput(MaterialId id, u32 location) const;

    /// Sets a scalar float member of the parameter block.
    ///
    /// Param id: a live material.
    /// Param name: the member name.
    /// Param value: the value.
    /// Returns: true if set.
    /// Fails: no such member, or it is not a single float; the reason is logged.
    /// Aborts: `id` is invalid or stale.
    bool SetFloat(MaterialId id, const char* name, f32 value);
    /// Sets a scalar signed integer member; see [[renderer::MaterialSystem::SetFloat]].
    ///
    /// Param id: a live material.
    /// Param name: the member name.
    /// Param value: the value.
    /// Returns: true if set.
    /// Fails: no such member, or it is not a single int; the reason is logged.
    /// Aborts: `id` is invalid or stale.
    bool SetInt(MaterialId id, const char* name, i32 value);
    /// Sets a scalar unsigned integer member; see [[renderer::MaterialSystem::SetFloat]].
    ///
    /// Param id: a live material.
    /// Param name: the member name.
    /// Param value: the value.
    /// Returns: true if set.
    /// Fails: no such member, or it is not a single uint; the reason is logged.
    /// Aborts: `id` is invalid or stale.
    bool SetUInt(MaterialId id, const char* name, u32 value);
    /// Sets a float2 member; see [[renderer::MaterialSystem::SetFloat]].
    ///
    /// Param id: a live material.
    /// Param name: the member name.
    /// Param value: the value.
    /// Returns: true if set.
    /// Fails: no such member, or it is not a float2; the reason is logged.
    /// Aborts: `id` is invalid or stale.
    bool SetVec2(MaterialId id, const char* name, const Vec2& value);
    /// Sets a float3 member; see [[renderer::MaterialSystem::SetFloat]].
    ///
    /// Param id: a live material.
    /// Param name: the member name.
    /// Param value: the value.
    /// Returns: true if set.
    /// Fails: no such member, or it is not a float3; the reason is logged.
    /// Aborts: `id` is invalid or stale.
    bool SetVec3(MaterialId id, const char* name, const Vec3& value);
    /// Sets a float4 member; see [[renderer::MaterialSystem::SetFloat]].
    ///
    /// Param id: a live material.
    /// Param name: the member name.
    /// Param value: the value.
    /// Returns: true if set.
    /// Fails: no such member, or it is not a float4; the reason is logged.
    /// Aborts: `id` is invalid or stale.
    bool SetVec4(MaterialId id, const char* name, const Vec4& value);
    /// Sets a float4x4 member; see [[renderer::MaterialSystem::SetFloat]].
    ///
    /// Param id: a live material.
    /// Param name: the member name.
    /// Param value: the value.
    /// Returns: true if set.
    /// Fails: no such member, or it is not a float4x4; the reason is logged.
    /// Aborts: `id` is invalid or stale.
    bool SetMat4(MaterialId id, const char* name, const Mat4& value);

    /// Sets a texture slot. The bind groups are rebuilt at the next flush.
    ///
    /// Param id: a live material.
    /// Param name: the shader's texture variable name.
    /// Param texture: a 2D texture in `ShaderRead` state while drawing, or an invalid handle to go back to the default.
    /// Returns: true if set.
    /// Fails: no such texture slot; logged.
    /// Aborts: `id` is invalid or stale.
    /// Ownership: `texture` is borrowed; it must outlive the material's use of it.
    bool SetTexture(MaterialId id, const char* name, rhi::TextureHandle texture);

    /// Binds a texture owner: the material follows the owner's handle through a device reset. Preferred over the raw handle.
    ///
    /// Param id: the material.
    /// Param name: the texture slot's name.
    /// Param texture: the owner, or null to unbind; borrowed, must outlive the material or be unbound first.
    /// Returns: true if the slot exists.
    /// Fails: returns false, logging the reason, if the material has no such slot.
    /// Aborts: `id` is invalid or stale.
    /// Ownership: the owner is borrowed.
    bool SetTexture(MaterialId id, const char* name, const GpuTexture* texture);

    /// Binds a sampler owner; see the texture overload.
    ///
    /// Param id: the material.
    /// Param name: the sampler slot's name.
    /// Param sampler: the owner, or null to unbind; borrowed.
    /// Returns: true if the slot exists.
    /// Fails: returns false, logging the reason, if the material has no such slot.
    /// Aborts: `id` is invalid or stale.
    /// Ownership: the owner is borrowed.
    bool SetSampler(MaterialId id, const char* name, const GpuSampler* sampler);

    /// Sets a sampler slot. The bind groups are rebuilt at the next flush.
    ///
    /// Param id: a live material.
    /// Param name: the shader's sampler variable name.
    /// Param sampler: the sampler, or an invalid handle to go back to the default.
    /// Returns: true if set.
    /// Fails: no such sampler slot; logged.
    /// Aborts: `id` is invalid or stale.
    /// Ownership: `sampler` is borrowed; it must outlive the material's use of it.
    bool SetSampler(MaterialId id, const char* name, rhi::SamplerHandle sampler);

    /// Starts a frame: selects the per-frame region the parameter blocks are written to.
    ///
    /// Param frame_index: the frame number; regions rotate every three frames, as in [[renderer::FrameResources]].
    void BeginFrame(u64 frame_index);

    /// Rebuilds the bind groups of materials whose textures or samplers changed and copies changed parameter blocks to
    /// the GPU. Call once per frame after `BeginFrame` and before recording draws.
    ///
    /// Aborts: `BeginFrame` was not called.
    void FlushMaterials();

    /// Returns the bind group (set 1) to bind for a material.
    ///
    /// Param id: a live material.
    /// Returns: the group of the current frame region.
    /// Aborts: `id` is invalid or stale, or the material changed since the last flush.
    /// Ownership: borrowed from the system.
    rhi::BindGroupHandle MaterialBindGroup(MaterialId id) const;

    /// Returns the GPU buffer range holding a material's parameter block for the current frame (for tests and tools).
    ///
    /// Param id: a live material.
    /// Returns: the range, or an invalid buffer if the material has no parameter block.
    /// Aborts: `id` is invalid or stale, or `BeginFrame` was not called.
    BufferRegion ParameterRegion(MaterialId id) const;

    /// Returns the layout of the frame uniform group (set 0).
    /// Fails: never.
    rhi::BindGroupLayoutHandle FrameLayout() const;
    /// Returns the layout of the object storage group (set 2).
    /// Fails: never.
    rhi::BindGroupLayoutHandle ObjectLayout() const;

    /// Returns the object index stream: a vertex buffer holding the indices `0 .. kMaxObjectIndices - 1` as `u32`, which a pass binds
    /// at [[renderer::MaterialSystem::ObjectIndexSlot]] once before drawing objects, on a device whose shaders read the index from an
    /// instance-rate attribute ([[rhi::DeviceCaps::object_index_stream]], Direct3D 12). Draw `i` with `first_instance = i` then reads `i`.
    ///
    /// Returns: the buffer, or an invalid handle on a device that does not need it (Vulkan reads `gl_InstanceIndex`).
    /// Fails: an invalid handle, as described.
    /// Ownership: the system owns the buffer and rebuilds it with the device.
    rhi::BufferHandle ObjectIndexStream() const { return object_index_stream_; }

    /// Returns the vertex-buffer slot the object index stream uses in the material pipelines: 1, after the mesh at slot 0.
    ///
    /// Fails: never.
    static constexpr u32 ObjectIndexSlot() { return 1; }

    /// Adds the object index attribute to a pipeline's vertex input when the device needs it: binding `slot`, instance rate, 4-byte
    /// stride, attribute location [[renderer::kObjectIndexLocation]] as `UInt1`. Does nothing when
    /// [[rhi::DeviceCaps::object_index_stream]] is false.
    ///
    /// Param device: the device the pipeline is for.
    /// Param bindings: the pipeline's vertex bindings; `*binding_count` of them are used, and one is appended at index `slot`.
    /// Param binding_count: in and out.
    /// Param binding_capacity: the size of `bindings`.
    /// Param attributes: the pipeline's attributes; `*attribute_count` used, one appended.
    /// Param attribute_count: in and out.
    /// Param attribute_capacity: the size of `attributes`.
    /// Param slot: the vertex-buffer slot, which must equal `*binding_count` (bindings are dense).
    /// Returns: true if the attribute was added or not needed.
    /// Fails: returns false when `slot` is not the next binding or an array is full.
    /// Ownership: the arrays belong to the caller.
    static bool AddObjectIndexAttribute(const rhi::Device& device, rhi::VertexBindingDesc* bindings, u32* binding_count, u32 binding_capacity,
                                        rhi::VertexAttributeDesc* attributes, u32* attribute_count, u32 attribute_capacity, u32 slot);
    /// Returns the set-1 layout of a material.
    ///
    /// Param id: a live material.
    /// Aborts: `id` is invalid or stale.
    rhi::BindGroupLayoutHandle MaterialLayout(MaterialId id) const;

    /// Returns the texture a material's slot currently shows: the one set, or the slot's default.
    ///
    /// Param id: a live material.
    /// Param name: the shader's texture variable name.
    /// Returns: the texture, or an invalid handle if there is no such slot.
    /// Fails: reported through the return value.
    /// Aborts: `id` is invalid or stale.
    /// Ownership: borrowed.
    rhi::TextureHandle EffectiveTexture(MaterialId id, const char* name) const;

    /// Returns a default texture.
    ///
    /// Param kind: which one.
    /// Returns: the texture, in `ShaderRead`.
    /// Fails: never.
    /// Ownership: borrowed from the system.
    rhi::TextureHandle GetDefaultTexture(DefaultTexture kind) const;

    /// Returns the pipeline a material draws with into a target and with a vertex layout, creating it on a miss.
    ///
    /// Pipelines are keyed by (template, permutation, target, vertex layout, topology, restart), so materials of one
    /// template and key share one; a strip mesh and a list mesh with the same material get separate pipelines. Creation happens here, never while recording: call this (or [[renderer::MaterialSystem::Warmup]]) before the
    /// frame's recording starts.
    ///
    /// Param id: a live material.
    /// Param target: the attachment formats.
    /// Param vertex_layout: the mesh layout; every vertex input of the vertex shader must be present with the same format.
    /// Param topology: the mesh's topology ([[renderer::MeshData::Topology]]); a triangle list by default.
    /// Param primitive_restart: whether the mesh has restart markers ([[renderer::MeshData::HasPrimitiveRestart]]).
    /// Returns: the pipeline and its cache index, or an invalid pipeline after logging.
    /// Fails: the layout lacks or mismatches a vertex shader input, the target is invalid, restart is asked for with a
    ///   topology that is not a strip, the cache is full, or the device cannot create the pipeline.
    /// Aborts: `id` is invalid or stale.
    PipelineLookup GetPipeline(MaterialId id, const PipelineTarget& target, const VertexLayoutData& vertex_layout,
                               rhi::PrimitiveTopology topology = rhi::PrimitiveTopology::TriangleList, bool primitive_restart = false);

    /// Creates the pipeline for a material ahead of use; the same as `GetPipeline` without the result.
    ///
    /// Param id: a live material.
    /// Param target: the attachment formats.
    /// Param vertex_layout: the mesh layout.
    /// Param topology: the mesh's topology.
    /// Param primitive_restart: whether the mesh has restart markers.
    /// Returns: whether the pipeline exists afterwards.
    /// Fails: as for [[renderer::MaterialSystem::GetPipeline]].
    /// Aborts: `id` is invalid or stale.
    bool Warmup(MaterialId id, const PipelineTarget& target, const VertexLayoutData& vertex_layout,
                rhi::PrimitiveTopology topology = rhi::PrimitiveTopology::TriangleList, bool primitive_restart = false);

    /// Returns how many times this material's bind groups were rebuilt after creation.
    ///
    /// Param id: a live material.
    /// Aborts: `id` is invalid or stale.
    u32 BindGroupRebuilds(MaterialId id) const;

    /// Returns the counters.
    /// Fails: never.
    MaterialStats Stats() const;

    /// Destroys every device object (layouts, shaders, default textures, parameter buffers, bind groups, pipelines) and keeps
    /// the CPU side: template descriptors and reflection, parameter values, owner bindings, and the pipeline descriptors.
    /// Raw texture and sampler handles set by the caller are forgotten (they belong to the old device).
    void Release() override;

    /// Rebuilds everything on `device` in dependency order; pipelines lazily, or eagerly with `prewarm_pipelines`.
    ///
    /// Param device: the new device.
    /// Returns: true on success.
    /// Fails: returns false, logging the reason, with nothing left allocated if any object cannot be made.
    /// Ownership: the device is borrowed.
    bool Recreate(rhi::Device* device) override;
    bool Live() const override { return valid_ && !released_; }
    const char* RecreatableName() const override { return "materials"; }

private:
    struct TemplateSlot;
    struct MaterialSlot;
    struct PipelineEntry;
    struct Variant;

    bool CreateDefaults();
    void DestroyDefaults();
    bool CreateParams(MaterialSlot& m);
    bool RebuildPipeline(PipelineEntry& entry);
    rhi::PipelineHandle MakePipeline(TemplateSlot& t, const Variant& v, const PipelineTarget& target, const VertexLayoutData& vertex_layout,
                                     rhi::PrimitiveTopology topology, bool primitive_restart);
    void DestroyMaterialObjects(MaterialSlot& m);
    bool BuildGroups(MaterialSlot& m);
    bool SetValue(MaterialId id, const char* name, MemberKind kind, ScalarType scalar, u32 rows, u32 columns, const void* data, usize size);
    const MaterialSlot& Get(MaterialId id) const;
    MaterialSlot& Get(MaterialId id);
    Variant* BuildVariant(TemplateSlot& t, PermutationKey key);
    void DestroyPipelines(u32 template_index);

    rhi::Device* device_ = nullptr;
    MaterialSystemDesc desc_;
    Vector<TemplateSlot> templates_;
    Vector<MaterialSlot> materials_;
    Vector<PipelineEntry> pipelines_;
    u32 pipeline_count_ = 0;
    u32 material_high_ = 0; // one past the highest material slot ever used
    rhi::BindGroupLayoutHandle frame_layout_;
    rhi::BindGroupLayoutHandle object_layout_;
    rhi::TextureHandle defaults_[3];
    rhi::SamplerHandle default_sampler_;
    rhi::BufferHandle object_index_stream_; // only on a device with DeviceCaps::object_index_stream
    u32 slot_ = 0;
    bool begun_ = false;
    bool valid_ = false;
    bool released_ = false;
    ScopedRegistration registration_;
    MaterialStats stats_;
    struct CachedGroup {
        rhi::BufferHandle buffer;
        u64 offset = 0;
        u64 size = 0;
        rhi::BindGroupHandle group;
    };
    static constexpr u32 kMaxCachedGroups = 8;
    rhi::BindGroupHandle CachedBindGroup(CachedGroup* cache, u32* count, rhi::BindGroupLayoutHandle layout, const BufferRegion& region);
    CachedGroup frame_groups_[kMaxCachedGroups];
    CachedGroup object_groups_[kMaxCachedGroups];
    u32 frame_group_count_ = 0;
    u32 object_group_count_ = 0;
};

} // namespace engine_placeholder::renderer
