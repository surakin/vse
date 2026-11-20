#pragma once

//! The G-buffer geometry pass: draws every visible object once into the G-buffer with its material, depth-tested.
//!
//! The G-buffer holds four color targets and a depth target (renderer spec R11): albedo (`RGBA8_SRGB`: rgb albedo, a
//! baked occlusion), the world-space normal (`RG16_Float`, octahedral-encoded by [[renderer::OctEncode]]), the material
//! (`RGBA8_UNorm`: roughness, metallic, a free channel written 0, flags / 255), the emitted radiance (`RGBA16_Float`,
//! linear rgb, not limited to 1; glTF coverage Day 2) and `D32_Float` depth (0 at the near plane, 1 at the far plane,
//! no reverse-Z). The shaders are `gbuffer.vert` and `gbuffer.frag`; the fragment shader has
//! permutations for a normal map, an alpha test and per-vertex colors, and [[renderer::CreateGBufferMaterial]] picks
//! the matching vertex and fragment modules so the two never disagree.
//!
//! A frame uses [[renderer::GBufferPass]] in two steps: `Prepare` (on the frame's thread) culls the scene, sorts the
//! visible objects by [[renderer::MakeDrawSortKey]], writes their per-object data in that order and builds the draw
//! list; `AddToGraph` declares the pass whose callback records it. The record step creates no object and issues a
//! handful of RHI calls per draw: bind the pipeline and material group only when they change, one instanced draw per
//! run of the same mesh and material.
#include "containers/array.h"
#include "containers/vector.h"
#include "debug/compiler.h"
#include "math/matrix.h"
#include "math/vector.h"
#include "renderer/camera.h"
#include "renderer/frame_data.h"
#include "renderer/frustum.h"
#include "renderer/material.h"
#include "renderer/render_graph.h"
#include "renderer/render_scene.h"
#include "rhi/rhi.h"
#include "types.h"

namespace vse::renderer
{

/// The albedo target's format (sRGB-encoded on write, decoded on sample).
inline constexpr rhi::Format kGBufferAlbedoFormat = rhi::Format::RGBA8_SRGB;
/// The normal target's format (two octahedral coordinates in [-1, 1]).
inline constexpr rhi::Format kGBufferNormalFormat = rhi::Format::RG16_Float;
/// The material target's format (roughness, metallic, a free channel, flags / 255).
inline constexpr rhi::Format kGBufferMaterialFormat = rhi::Format::RGBA8_UNorm;
/// The emissive target's format: the radiance a surface emits, linear and above 1 where it is bright.
inline constexpr rhi::Format kGBufferEmissiveFormat = rhi::Format::RGBA16_Float;
/// The depth target's format.
inline constexpr rhi::Format kGBufferDepthFormat = rhi::Format::D32_Float;
/// The bit of a G-buffer material's `flags` parameter that makes it unlit: the pixel shows its base colour (albedo)
/// with no ambient, light or emissive term, and the point lights add nothing to it (glTF's `KHR_materials_unlit`). The
/// other bits are free.
inline constexpr u32 kMaterialFlagUnlit = 1u;

/// A G-buffer layer, for the debug view.
enum class GBufferLayer : u8
{
    Albedo = 0,     ///< The albedo target as it is.
    Normal,         ///< The decoded normal shown as `n * 0.5 + 0.5`.
    Material,       ///< The material target's rgb (roughness, metallic, the free channel).
    Depth,          ///< Linear view-space depth divided by the far plane, as grey.
    WorldPosition,  ///< The world position rebuilt from depth, shown as `position * 0.05 + 0.5` (background black);
                    ///< needs `SetInverseViewProjection`.
    Emissive,       ///< The emissive target's rgb (radiance above 1 shows as 1).
};

/// Checks that the device supports every G-buffer format for the usage the pass needs, and enough simultaneous color
/// attachments (the geometry pass writes four). Call it once at start-up, before creating templates and passes: the
/// passes themselves do not repeat it, and on a device that fails it every object would be left out with a log line per
/// pipeline.
///
/// - Param device: the device to ask.
/// - Param error: receives a message naming the first unsupported format and usage, or the attachment limit; may be
/// null.
/// - Param error_capacity: size of `error` in bytes.
/// - Returns: true when all formats are supported.
/// - Fails: returns false and fills `error`; nothing is logged or aborted.
/// - Ownership: `error` is borrowed for the call.
bool CheckGBufferFormats(const rhi::Device* device, char* error, usize error_capacity);

/// Returns the attachment formats a G-buffer pipeline renders to (four colors and depth).
///
/// - Fails: never.
PipelineTarget GBufferPipelineTarget();

/// Encodes a unit vector as two octahedral coordinates in [-1, 1]; the CPU twin of `OctEncode` in
/// `gbuffer_common.slang`.
///
/// - Param n: a unit vector (a longer or shorter one is projected as it is, so the result still lies in [-1, 1]).
/// - Returns: the coordinates.
/// - Fails: never. A zero vector gives NaN.
Vec2 OctEncode(const Vec3& n);

/// Decodes two octahedral coordinates to a unit vector; the CPU twin of `OctDecode` in `gbuffer_common.slang`.
///
/// - Param f: coordinates in [-1, 1].
/// - Returns: a unit vector.
/// - Fails: never.
Vec3 OctDecode(const Vec2& f);

/// Converts an sRGB-encoded channel in [0, 1] to linear light; the CPU twin of `SrgbToLinear` in
/// `gbuffer_common.slang`.
///
/// - Param c: the encoded value.
/// - Returns: the linear value.
/// - Fails: never.
f32 SrgbToLinear(f32 c);

/// Converts a depth-buffer value (0 at the near plane, 1 at the far plane) to the distance along the view axis.
///
/// - Param depth: a depth value in [0, 1].
/// - Param near_plane: the camera's near distance, positive.
/// - Param far_plane: the camera's far distance, greater than `near_plane`.
/// - Returns: the view-space distance in [near_plane, far_plane].
/// - Fails: never.
f32 LinearizeDepth(f32 depth, f32 near_plane, f32 far_plane);

/// Sets a camera's near and far planes to enclose every visible-or-not object of the scene.
///
/// The planes come from the world-space bounding spheres projected on the camera's forward axis, with the near plane
/// never below `min_near` and the far plane at least twice the near one. A scene without objects leaves the camera as
/// it was.
///
/// - Param scene: the scene.
/// - Param camera: the camera to change; its position and rotation must be set.
/// - Param min_near: the smallest near distance allowed, positive.
/// - Ownership: `camera` is borrowed for the call.
/// - Aborts: `camera` is null or `min_near` is not positive.
void FitCameraDepthRange(const RenderScene& scene, Camera* camera, f32 min_near);

/// The texture shape a material slot accepts (the asset minter checks a referenced texture against it).
enum class SlotShape : u8
{
    Texture2D = 0,  ///< A single 2D image.
    Cube,           ///< A cube map.
    Array2D,        ///< An array of 2D images.
};

/// The color space a slot expects its texture in.
enum class SlotColorSpace : u8
{
    Any = 0,  ///< Either.
    Srgb,     ///< Color data, stored sRGB-encoded.
    Linear,   ///< Data (normals, masks), stored linear.
};

/// One texture slot of a built-in template, as the asset minter checks it.
///
/// - Thread-safety: immutable static data.
/// - Ownership: `name` is a string literal.
struct TemplateTextureSlot
{
    const char* name = "";                             ///< The shader's texture variable name.
    SlotShape shape = SlotShape::Texture2D;            ///< The shape accepted.
    SlotColorSpace color_space = SlotColorSpace::Any;  ///< The color space accepted.
};

/// The initial value of a parameter-block member when a material does not set it (the values
/// [[renderer::CreateGBufferMaterial]] writes).
///
/// - Thread-safety: immutable static data.
/// - Ownership: `name` is a string literal.
struct TemplateParamDefault
{
    const char* name = "";        ///< The member name.
    f32 value[4] = {0, 0, 0, 0};  ///< The value (first `columns * rows` entries, or the integer bits for `flags`).
};

/// What an offline tool needs to know about one of the built-in G-buffer templates: its name, whether its vertex shader
/// skins, the fragment permutations a material chooses from, and the slots and defaults of the material block.
///
/// - Thread-safety: immutable static data.
/// - Ownership: every pointer is to static data.
struct GBufferTemplateInfo
{
    const char* name = "";      ///< `gbuffer`, `gbuffer_color`, `gbuffer_skinned` or `gbuffer_skinned_color`.
    bool skinned = false;       ///< Whether the vertex shader has the `SKINNED` permutation.
    bool vertex_color = false;  ///< Whether the vertex shader has `VERTEX_COLOR`.
    const ShaderPermutations* fragment_permutations = nullptr;  ///< The fragment shader's permutations.
    const TemplateTextureSlot* slots = nullptr;                 ///< The texture slots.
    u32 slot_count = 0;                                         ///< Number of slots.
    const TemplateParamDefault* defaults = nullptr;             ///< Parameter defaults.
    u32 default_count = 0;                                      ///< Number of defaults.
};

/// Finds a built-in G-buffer template by name.
///
/// - Param name: a template name; null gives null.
/// - Returns: the description, or null when no template has that name.
/// - Fails: returns null for an unknown name.
/// - Ownership: the result is static data.
const GBufferTemplateInfo* FindGBufferTemplateInfo(const char* name);

/// Options of a G-buffer material, which select the shader modules.
///
/// - Thread-safety: thread-compatible plain data.
struct GBufferOptions
{
    /// Perturb the normal with the `normal_tex` slot (tangent space).
    bool normal_map = false;
    /// Discard fragments whose alpha (`base_color.a`, times the albedo texture's and the vertex color's) is below
    /// `alpha_cutoff`.
    bool alpha_test = false;
    /// Multiply the albedo by the mesh's per-vertex color (sRGB bytes, converted to linear in the shader). The mesh
    /// must have one.
    bool vertex_color = false;
    /// Skin the mesh's vertices with the joint palette: for meshes with a skin stream, and only for them (a rigid
    /// mesh with a skinned material, or the reverse, is rejected with a message naming both).
    bool skinned = false;
};

/// The two material templates of the G-buffer shaders: for meshes without and with per-vertex colors.
///
/// - Thread-safety: thread-compatible plain data.
struct GBufferTemplates
{
    /// The template for standard-layout meshes.
    TemplateId plain;
    /// The template for meshes in the [[renderer::ColorVertexLayout]].
    TemplateId colored;
    /// The template for skinned meshes of the standard layout (`SkinnedVertexLayout(StandardVertexLayout())`).
    TemplateId skinned;
    /// The template for skinned meshes of the color layout.
    TemplateId skinned_colored;
};

/// Creates the G-buffer material templates.
///
/// - Param materials: the system to create them in; must be valid.
/// - Returns: both template ids; invalid ids when the shaders did not match the material conventions (logged).
/// - Fails: reported through invalid ids.
/// - Ownership: the templates belong to `materials`.
/// - Aborts: `materials` is null.
GBufferTemplates CreateGBufferTemplates(MaterialSystem* materials);

/// Creates a G-buffer material with the options' shader modules and sensible parameters.
///
/// The parameters start as base color white, roughness 0.5, metallic 0, emissive colour black, alpha cutoff 0.5 and
/// occlusion 1, `albedo_uv` (1, 1, 0, 0), `albedo_uv_outside` (0, 0, 0, 0) and occlusion strength 1; change them with
/// the material system's setters (`base_color`, `roughness`, `metallic`, `emissive_color`, `alpha_cutoff`, `occlusion`,
/// `flags`, `albedo_uv`, `albedo_uv_outside`, `occlusion_strength`), and the textures `albedo_tex`, `normal_tex`,
/// `metallic_roughness_tex`, `occlusion_tex` and `emissive_tex` (glTF's maps: the first two linear, roughness times the
/// green channel, metallic times the blue, occlusion times `1 + occlusion_strength * (red - 1)`; the emissive map sRGB,
/// the emitted radiance `emissive_color.rgb * emissive_color.a * emissive_tex.rgb`, with the strength in `a`; unset
/// they are white, so the parameters alone decide; all three read the mesh's coordinates untransformed, as the normal
/// map does). `albedo_uv` is (scale u, scale v, offset u, offset v): the albedo is sampled at uv * scale + offset. When
/// `albedo_uv_outside` has alpha above 0, coordinates outside 0..1 show its rgb (linear) instead of the texture, and
/// coordinates inside are kept half a texel from the edge so that filtering never reads the other side; with alpha 0
/// the sampler's address mode applies as before.
///
/// - Param materials: the system.
/// - Param templates: from [[renderer::CreateGBufferTemplates]].
/// - Param options: which permutation to use.
/// - Param name: a debug name; may be null.
/// - Returns: the material, or an invalid id when the template is invalid or the permutation does not exist (logged).
/// - Fails: reported through the invalid id.
/// - Ownership: the material belongs to `materials`.
/// - Aborts: `materials` is null.
MaterialId CreateGBufferMaterial(MaterialSystem* materials, const GBufferTemplates& templates,
                                 const GBufferOptions& options, const char* name);

/// Sets every parameter of a G-buffer material to its default (the ones [[renderer::CreateGBufferMaterial]] starts
/// with). A material made straight from a template starts with zeros, which would make `occlusion` 0 and the ambient
/// term black; the asset loader calls this before it applies the parameters a blob names.
///
/// - Param materials: the system the material belongs to.
/// - Param id: a G-buffer material of `materials`.
/// - Fails: a parameter the material lacks is skipped.
/// - Ownership: borrows `materials` for the call only.
/// - Aborts: `materials` is null.
void ApplyGBufferDefaults(MaterialSystem* materials, MaterialId id);

/// The graph resources of the G-buffer targets.
///
/// - Thread-safety: thread-compatible plain data.
struct GBufferTargets
{
    /// Albedo and baked occlusion.
    GraphResource albedo;
    /// Octahedral world-space normal.
    GraphResource normal;
    /// Roughness, metallic, a free channel, flags.
    GraphResource material;
    /// The emitted radiance (linear rgb); the fullscreen lighting pass adds it once.
    GraphResource emissive;
    /// Depth; left in the `DepthAttachment` state; a later pass reads it as `Sampled` (through a sampler) or
    /// `DepthRead` (depth test).
    GraphResource depth;
};

/// What a prepared G-buffer pass will draw, and what went wrong.
///
/// - Thread-safety: thread-compatible plain data.
struct GBufferStats
{
    /// Objects inside the frustum.
    u32 visible = 0;
    /// Objects that will be drawn (visible minus rejected).
    u32 drawn_objects = 0;
    /// Draw calls (runs of the same mesh and material are one instanced call).
    u32 draws = 0;
    /// Visible objects left out because of a named error (see `LastError`).
    u32 rejected = 0;
    /// Pipeline binds the pass will record.
    u32 pipeline_binds = 0;
    /// Material bind-group binds the pass will record.
    u32 material_binds = 0;
    /// Skinned objects not drawn because the frame's joint palette had no room for their matrices (the budget is
    /// `renderer.joint_palette_bytes`); each is skipped whole, never drawn with a stale or partial pose.
    u32 skinned_dropped = 0;
    /// Objects drawn with their morph targets applied this frame (glTF coverage Day 7).
    ///
    /// One per mesh and weights, so an entity whose mesh has several material slots counts once.
    u32 morphed_objects = 0;
    /// Non-zero morph weights left out this frame.
    ///
    /// An object with more than [[renderer::kMaxActiveMorphTargets]] keeps the largest; when the frame's morph budget
    /// is full, the object draws its base shape and all its weights count.
    u32 morph_targets_dropped = 0;
};

/// Draws the scene's visible objects into the G-buffer.
///
/// - Thread-safety: thread-compatible. `Prepare` and `AddToGraph` run on the frame's thread; the recorded callback
/// reads the draw list only, so it may run on a scheduler participant while nothing prepares.
/// - Ownership: borrows the device, the material system and the frame resources, which must outlive it.
class GBufferPass
{
public:
    /// Creates the pass.
    ///
    /// - Param device: the device.
    /// - Param materials: the material system the scene's materials live in.
    /// - Param frame: the frame resources the per-frame and per-object data are written to.
    /// - Ownership: all three are borrowed.
    /// - Aborts: `device`, `materials` or `frame` is null.
    GBufferPass(rhi::Device* device, MaterialSystem* materials, FrameResources* frame);
    /// Frees the draw list.
    ~GBufferPass();
    GBufferPass(const GBufferPass&) = delete;
    GBufferPass& operator=(const GBufferPass&) = delete;

    /// Culls the scene, sorts the visible objects and writes their per-object data.
    ///
    /// Call it after `FrameResources::BeginFrame` and `MaterialSystem::BeginFrame`/`FlushMaterials` and before
    /// `FrameResources::Flush`. The objects are sorted by [[renderer::MakeDrawSortKey]] (pipeline, material, mesh) with
    /// the scene slot as tie-break, so the result does not depend on the order objects were added. An object is left
    /// out and named in `LastError` when its mesh is missing or not prepared, its material does not exist, a pipeline
    /// cannot be built for the mesh, or the mesh and the material disagree about per-vertex colors.
    ///
    /// - Param scene: the scene.
    /// - Param frustum: the camera's frustum.
    /// - Returns: true when every visible object was accepted.
    /// - Fails: returns false when any object was rejected (the rest is still drawn); the first message is in
    /// `LastError`.
    /// - Ownership: `scene` is borrowed until the pass has been recorded.
    bool Prepare(const RenderScene& scene, const Frustum& frustum);

    /// Declares the G-buffer targets as transients and the pass that renders them.
    ///
    /// - Param graph: the frame's graph.
    /// - Param width: the target width in pixels.
    /// - Param height: the target height in pixels.
    /// - Returns: the targets; the pass writes all four.
    /// - Aborts: called twice since the last `Prepare` (one pass object serves one view per frame; make one per view).
    /// - Ownership: `graph` is borrowed for the call; the pass keeps a pointer to this object until the graph is
    /// executed.
    GBufferTargets AddToGraph(RenderGraph* graph, u32 width, u32 height);

    /// Adds usage flags to the G-buffer textures that the next `AddToGraph` declares.
    ///
    /// For tools and tests, for example `CopySrc` to read a layer back. The default is none beyond what the pass and
    /// the lighting need.
    ///
    /// - Param usage: flags to add to the four textures.
    /// - Fails: never.
    void SetExtraTargetUsage(rhi::TextureUsage usage) { extra_usage_ = usage; }

    /// Returns the first error of the last `Prepare`, or an empty string.
    /// - Fails: never.
    /// - Ownership: the string belongs to the pass and stays valid until the next `Prepare` or the pass's destruction.
    const char* LastError() const { return error_; }
    /// Returns the counts of the last `Prepare`.
    /// - Fails: never.
    GBufferStats Stats() const { return stats_; }

private:
    struct SortEntry;  // one visible object on its way into the draw list; defined in gbuffer.cpp
    struct Draw
    {
        rhi::PipelineHandle pipeline;
        rhi::BindGroupHandle material_group;
        const GpuMesh* mesh = nullptr;
        u32 first_instance = 0;
        u32 instance_count = 0;
        u32 first_index = 0;  // the index range drawn (0, 0: every index)
        u32 index_count = 0;
        bool skinned = false;
        u32 morph_job = kNoMorphJob;  // the morph pass's output this draw reads, or none
    };
    // One object's morph work: the mesh, the weights it was given (to share the work between the slots of one
    // entity), where its vertices go in the frame's morph buffer, and the push constants of shaders/morph.comp.
    struct MorphJob
    {
        const GpuMesh* mesh = nullptr;
        const f32* weights = nullptr;
        u64 out_offset = 0;
        Array<u32, 32> push = {};
    };
    static constexpr u32 kNoMorphJob = 0xFFFFFFFFu;
    static void Record(const PassExecuteContext& context, void* user);
    static void RecordMorph(const PassExecuteContext& context, void* user);
    void Reject(const char* fmt, ...) ENGINE_PRINTF_FORMAT(2, 3);

    MaterialSystem* materials_ = nullptr;
    FrameResources* frame_ = nullptr;
    Vector<Draw> draws_;
    u32 draw_count_ = 0;
    Vector<u32> slots_;
    Vector<u32> joint_offsets_;  // parallel to slots_: the palette offset of each drawn object, kNoJoints if rigid
    Vector<u32> cull_slots_;     // grown when the scene has more slots than the frame resources hold objects
    bool added_ = false;
    bool storage_ok_ = false;  // the draw, slot and sort arrays were allocated
    Vector<SortEntry> sort_scratch_;
    // The draw order: one small proxy per entry, sorted instead of the entries.
    struct DrawOrder
    {
        u64 key = 0;
        u64 tie = 0;  // first_index above index_count
        u32 slot = 0;
        u32 entry = 0;  // the entry in sort_scratch_
    };
    Vector<DrawOrder> order_;
    Vector<DrawOrder> order_scratch_;  // the merge buffer, reserved at construction
    rhi::BindGroupHandle frame_group_;
    rhi::BindGroupHandle object_group_;
    rhi::BindGroupHandle joint_group_;       // the frame's joint palette (set 3), bound for skinned draws only
    Vector<MorphJob> morph_jobs_;            // this frame's morphed objects (glTF coverage Day 7)
    u64 morph_bytes_ = 0;                    // the size of the frame's morph buffer
    GraphResource morph_buffer_;             // the morphed vertices, written by the morph pass
    rhi::BufferHandle object_index_stream_;  // MaterialSystem::ObjectIndexStream of this frame; invalid on Vulkan
    GBufferTargets targets_;
    rhi::TextureUsage extra_usage_ = rhi::TextureUsage::None;
    u32 width_ = 0;
    u32 height_ = 0;
    GBufferStats stats_;
    char error_[256] = {};
};

/// Draws one G-buffer layer into a color target, for debugging and the demo.
///
/// It creates one pipeline per output format and a bind group per frame (a tool, not part of the per-frame cost
/// budget).
///
/// - Thread-safety: thread-compatible.
/// - Ownership: borrows the device, which must outlive it; owns its shaders, sampler, layout and pipeline.
class GBufferDebugView final : public Recreatable
{
public:
    /// Creates the view's shaders and sampler.
    ///
    /// - Param device: the device.
    /// - Ownership: borrowed.
    /// - Aborts: `device` is null.
    explicit GBufferDebugView(rhi::Device* device);
    /// Destroys the pipelines, shaders and sampler.
    ~GBufferDebugView() override;
    GBufferDebugView(const GBufferDebugView&) = delete;
    GBufferDebugView& operator=(const GBufferDebugView&) = delete;

    /// Registers the debug view with a registry so it survives a device reset.
    ///
    /// - Param resources: the registry; null does nothing.
    /// - Returns: true if registered or `resources` is null.
    /// - Fails: returns false if the registry refused.
    /// - Ownership: the registry is borrowed.
    bool RegisterWith(DeviceResources* resources) { return registration_.Attach(resources, this, tiers::kPasses); }

    /// Destroys the shaders, layout, sampler and pipelines; nothing else is kept on the device.
    void Release() override;

    /// Makes the shaders, layout, sampler and (on first use) pipelines again.
    ///
    /// - Param device: the new device.
    /// - Returns: true if the objects were made.
    /// - Fails: returns false, logging the reason, if the device refuses one of them.
    /// - Ownership: the device is borrowed.
    bool Recreate(rhi::Device* device) override;
    bool Live() const override { return device_ != nullptr && vertex_.IsValid(); }
    const char* RecreatableName() const override { return "gbuffer debug view"; }

    /// Returns whether the shaders and sampler were created.
    /// - Fails: never.
    bool Valid() const;

    /// Sets the matrix the `WorldPosition` layer uses to rebuild positions:
    /// [[renderer::FrameData::inverse_view_projection]].
    ///
    /// - Param inverse_view_projection: the inverse of the camera's view-projection (including the projection's Y
    /// flip).
    /// - Fails: never.
    void SetInverseViewProjection(const Mat4& inverse_view_projection)
    {
        params_.inverse_view_projection = inverse_view_projection;
        has_inverse_view_projection_ = true;
    }

    /// Declares a pass that shows `layer` in `output`.
    ///
    /// - Param graph: the frame's graph.
    /// - Param targets: the G-buffer targets the pass reads, all as `Sampled` (a depth texture read through a sampler
    /// is `Sampled`,
    ///   `DepthRead` is for the depth test without writes).
    /// - Param layer: what to show.
    /// - Param output: a color texture resource the pass writes (written as `ColorAttachment`, cleared); its extent
    /// must equal the G-buffer's.
    /// - Param output_format: the format of `output` (an import carries no description, so the pass cannot read it).
    /// - Param near_plane: the camera's near plane, for `GBufferLayer::Depth`.
    /// - Param far_plane: the camera's far plane, for `GBufferLayer::Depth`.
    /// - Fails: an invalid resource is reported by `RenderGraph::Compile`; a pipeline the device cannot create is
    /// logged and the
    ///   pass then draws nothing.
    /// - Aborts: `layer` is `WorldPosition` and `SetInverseViewProjection` was not called since the last `AddToGraph`.
    /// - Ownership: the pass keeps a pointer to this object until the graph is executed.
    void AddToGraph(RenderGraph* graph, const GBufferTargets& targets, GBufferLayer layer, GraphResource output,
                    rhi::Format output_format, f32 near_plane, f32 far_plane);

private:
    struct Params
    {
        Mat4 inverse_view_projection = Mat4::Identity();
        u32 layer = 0;
        f32 near_plane = 0.1f;
        f32 far_plane = 100.0f;
        u32 pad = 0;
    };
    static void Record(const PassExecuteContext& context, void* user);

    void Build();

    ScopedRegistration registration_;
    rhi::Device* device_ = nullptr;
    rhi::ShaderHandle vertex_;
    rhi::ShaderHandle fragment_;
    rhi::SamplerHandle sampler_;
    rhi::BindGroupLayoutHandle layout_;
    rhi::PipelineHandle pipeline_;
    rhi::Format pipeline_format_ = rhi::Format::Undefined;
    GBufferTargets targets_;
    GraphResource output_;
    rhi::Format output_format_ = rhi::Format::Undefined;
    bool has_inverse_view_projection_ = false;
    Params params_;
};

}  // namespace vse::renderer
