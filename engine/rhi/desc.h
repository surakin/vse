#pragma once

#include "rhi/format.h"
#include "rhi/handle.h"
#include "rhi/limits.h"
#include "rhi/enums.h"
#include "types.h"

//! Descriptors passed to the device's creation functions, each with a `Validate()` that checks it without a device.
//!
//! `Validate()` reports the first problem as a static string starting with the descriptor name (for
//! example `"BufferDesc: size is zero"`) and returns null when the descriptor is acceptable. It checks
//! only what can be known without a device, against the RHI-wide ceilings in `rhi/limits.h`; the device's
//! own limits and handle liveness are checked at creation.

namespace engine_placeholder::rhi {

/// Returns the number of mip levels in a full chain for an image of the given extent: `floor(log2(max)) + 1`.
///
/// Param width: width in texels; must be at least 1.
/// Param height: height in texels; must be at least 1.
/// Param depth: depth in texels (1 for 2D); must be at least 1.
/// Returns: the full chain length, at least 1; 0 if any extent is 0.
u32 MaxMipLevels(u32 width, u32 height, u32 depth = 1);

/// Describes a buffer to create.
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: `debug_name` is borrowed for the duration of the call; the device copies it.
struct BufferDesc {
    /// Size in bytes; must be non-zero.
    u64 size = 0;
    /// What the buffer will be used for; at least one flag.
    BufferUsage usage = BufferUsage::None;
    /// Where the memory lives.
    MemoryLocation location = MemoryLocation::GpuOnly;
    /// Optional name shown in debuggers and logs; null for none.
    const char* debug_name = nullptr;

    /// Checks the descriptor without a device.
    ///
    /// Returns: null if the descriptor is acceptable, else the first problem.
    /// Fails: reported through the return value, never by aborting.
    /// Ownership: the returned string is static and is never freed by the caller.
    ///
    /// A `GpuToCpu` buffer may only be a copy destination, because Direct3D 12 readback memory cannot be
    /// used for anything else.
    const char* Validate() const;
};

/// Describes a texture to create.
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: `debug_name` is borrowed for the duration of the call; the device copies it.
struct TextureDesc {
    /// 2D, 3D or (reserved) cube.
    TextureDimension dimension = TextureDimension::Tex2D;
    /// Width in texels; must be non-zero.
    u32 width = 0;
    /// Height in texels; must be non-zero.
    u32 height = 0;
    /// Depth in texels; must be 1 unless the dimension is 3D.
    u32 depth = 1;
    /// Number of mip levels; 1 to the full chain length.
    u32 mip_levels = 1;
    /// Number of array layers; must be 1 for 3D textures.
    u32 array_layers = 1;
    /// Texel format; not `Undefined`.
    Format format = Format::Undefined;
    /// What the texture will be used for; at least one flag.
    TextureUsage usage = TextureUsage::None;
    /// Samples per texel; only 1 is supported in Phase 3.
    u32 samples = 1;
    /// Optional name shown in debuggers and logs; null for none.
    const char* debug_name = nullptr;

    /// Checks the descriptor without a device.
    ///
    /// Returns: null if the descriptor is acceptable, else the first problem.
    /// Fails: reported through the return value, never by aborting.
    /// Ownership: the returned string is static and is never freed by the caller.
    const char* Validate() const;
};

/// Describes a sampler to create.
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: `debug_name` is borrowed for the duration of the call; the device copies it.
struct SamplerDesc {
    /// Filter used when magnifying.
    FilterMode mag_filter = FilterMode::Linear;
    /// Filter used when minifying.
    FilterMode min_filter = FilterMode::Linear;
    /// Filter used between mip levels.
    FilterMode mip_filter = FilterMode::Linear;
    /// Addressing along U.
    AddressMode address_u = AddressMode::Repeat;
    /// Addressing along V.
    AddressMode address_v = AddressMode::Repeat;
    /// Addressing along W.
    AddressMode address_w = AddressMode::Repeat;
    /// Border color for `ClampToBorder`.
    BorderColor border_color = BorderColor::TransparentBlack;
    /// Maximum anisotropy, 1 to 16; 1 disables anisotropic filtering. Clamped to the device limit at creation.
    f32 max_anisotropy = 1.0f;
    /// Whether the sampler performs a depth comparison.
    bool compare_enable = false;
    /// The comparison used when `compare_enable` is set.
    CompareOp compare_op = CompareOp::Less;
    /// Lowest mip level that can be sampled.
    f32 min_lod = 0.0f;
    /// Highest mip level that can be sampled.
    f32 max_lod = 1000.0f;
    /// Optional name shown in debuggers and logs; null for none.
    const char* debug_name = nullptr;

    /// Checks the descriptor without a device.
    ///
    /// Returns: null if the descriptor is acceptable, else the first problem.
    /// Fails: reported through the return value, never by aborting.
    /// Ownership: the returned string is static and is never freed by the caller.
    const char* Validate() const;
};

/// Describes a shader module to create from SPIR-V words.
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: `words` and `entry` are borrowed for the duration of the call; the device copies what it keeps.
struct ShaderDesc {
    /// The single stage this module's entry point implements.
    ShaderStage stage = ShaderStage::None;
    /// The SPIR-V module as 32-bit words.
    const u32* words = nullptr;
    /// Number of 32-bit words in `words`.
    usize word_count = 0;
    /// Name of the entry point.
    const char* entry = "main";
    /// Optional name shown in debuggers and logs; null for none.
    const char* debug_name = nullptr;

    /// Checks the descriptor without a device.
    ///
    /// Returns: null if the descriptor is acceptable, else the first problem.
    /// Fails: reported through the return value, never by aborting.
    /// Ownership: the returned string is static and is never freed by the caller.
    ///
    /// Checks the stage, the entry name and that the module is at least as large as a SPIR-V header; the
    /// module's contents are checked when the shader is created.
    const char* Validate() const;
};

/// One binding in a bind group layout.
///
/// Thread-safety: thread-compatible plain data.
struct BindingDesc {
    /// Binding index within the group; unique in the layout.
    u32 binding = 0;
    /// What kind of resource is bound.
    BindingType type = BindingType::UniformBuffer;
    /// Stages that can access the binding; at least one.
    ShaderStage stages = ShaderStage::None;
    /// Array size; 1 for a non-array binding.
    u32 count = 1;
};

/// Describes a bind group layout: the shape of a set of resource bindings.
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: `entries` is borrowed for the duration of the call; the device copies it.
struct BindGroupLayoutDesc {
    /// The bindings; at most [[rhi::kMaxBindingsPerLayout]].
    Span<BindingDesc> entries;
    /// Optional name shown in debuggers and logs; null for none.
    const char* debug_name = nullptr;

    /// Checks the descriptor without a device.
    ///
    /// Returns: null if the descriptor is acceptable, else the first problem.
    /// Fails: reported through the return value, never by aborting.
    /// Ownership: the returned string is static and is never freed by the caller.
    const char* Validate() const;
};

/// One resource bound at a binding of a bind group; set the field that matches the binding's type.
///
/// Thread-safety: thread-compatible plain data.
struct BindGroupEntry {
    /// The binding index in the layout.
    u32 binding = 0;
    /// The element index for array bindings; 0 otherwise.
    u32 array_element = 0;
    /// The buffer, for uniform and storage buffer bindings.
    BufferHandle buffer;
    /// Byte offset of the bound range in `buffer`; a multiple of the device's offset alignment.
    u64 offset = 0;
    /// Byte size of the bound range; 0 binds from `offset` to the end of the buffer.
    u64 size = 0;
    /// The texture, for sampled and storage texture bindings.
    TextureHandle texture;
    /// The sampler, for sampler bindings.
    SamplerHandle sampler;
};

/// Describes a bind group: concrete resources for every binding of a layout.
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: `entries` and `debug_name` are borrowed for the duration of the call. The group does not
///   keep the bound resources alive; destroying one invalidates the group.
struct BindGroupDesc {
    /// The layout this group follows; required.
    BindGroupLayoutHandle layout;
    /// One entry per binding element of the layout.
    Span<BindGroupEntry> entries;
    /// Optional name shown in debuggers and logs; null for none.
    const char* debug_name = nullptr;

    /// Checks the descriptor without a device; whether the entries match the layout is checked at creation.
    ///
    /// Returns: null if the descriptor is acceptable, else the first problem.
    /// Fails: reported through the return value, never by aborting.
    /// Ownership: the returned string is static and is never freed by the caller.
    const char* Validate() const;
};

/// One vertex buffer binding: how fast the buffer advances.
///
/// Thread-safety: thread-compatible plain data.
struct VertexBindingDesc {
    /// Distance in bytes between consecutive elements; must be non-zero.
    u32 stride = 0;
    /// Whether the buffer advances per instance rather than per vertex.
    bool per_instance = false;
};

/// One vertex attribute: where a shader input comes from.
///
/// Thread-safety: thread-compatible plain data.
struct VertexAttributeDesc {
    /// The shader input location.
    u32 location = 0;
    /// Index into the pipeline's vertex bindings.
    u32 binding = 0;
    /// Data type and width.
    VertexFormat format = VertexFormat::Float3;
    /// Byte offset of the attribute inside one element of its binding.
    u32 offset = 0;
};

/// Blending for one color attachment.
///
/// Thread-safety: thread-compatible plain data.
struct BlendState {
    /// Whether blending is enabled; when false the other fields are ignored.
    bool enable = false;
    /// Factor for the source color.
    BlendFactor src_color = BlendFactor::One;
    /// Factor for the destination color.
    BlendFactor dst_color = BlendFactor::Zero;
    /// Operation combining the color terms.
    BlendOp color_op = BlendOp::Add;
    /// Factor for the source alpha.
    BlendFactor src_alpha = BlendFactor::One;
    /// Factor for the destination alpha.
    BlendFactor dst_alpha = BlendFactor::Zero;
    /// Operation combining the alpha terms.
    BlendOp alpha_op = BlendOp::Add;
};

/// One color attachment a pipeline writes.
///
/// Thread-safety: thread-compatible plain data.
struct ColorTargetDesc {
    /// Attachment format; a color format.
    Format format = Format::Undefined;
    /// Blending for this attachment.
    BlendState blend;
    /// Channels that are written.
    ColorWriteMask write_mask = ColorWriteMask::All;
};

/// Describes a graphics pipeline using dynamic rendering: attachment formats instead of a render pass.
///
/// Viewport and scissor are dynamic state set on the command list.
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: spans and `debug_name` are borrowed for the duration of the call; the device copies what it keeps.
///   The shader and layout handles are borrowed, not consumed.
struct GraphicsPipelineDesc {
    /// The vertex shader; required.
    ShaderHandle vertex_shader;
    /// The fragment shader; required unless the pipeline has no color targets (depth-only).
    ShaderHandle fragment_shader;
    /// Vertex buffer bindings; binding `i` of an attribute indexes this span.
    Span<VertexBindingDesc> vertex_bindings;
    /// Vertex attributes.
    Span<VertexAttributeDesc> vertex_attributes;
    /// How vertices form primitives.
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    /// Which faces are culled.
    CullMode cull_mode = CullMode::Back;
    /// Which winding is the front face.
    FrontFace front_face = FrontFace::CounterClockwise;
    /// Whether to rasterize triangles as lines.
    bool wireframe = false;
    /// Whether the depth test is enabled; requires `depth_format`.
    bool depth_test = false;
    /// Whether depth writes are enabled; requires `depth_format`.
    bool depth_write = false;
    /// The depth comparison.
    CompareOp depth_compare = CompareOp::Less;
    /// Color attachments written, in attachment order; the first `color_target_count` are used.
    ColorTargetDesc color_targets[kMaxColorAttachments];
    /// Number of valid entries in `color_targets`.
    u32 color_target_count = 0;
    /// Depth(-stencil) attachment format, or `Undefined` for none.
    Format depth_format = Format::Undefined;
    /// Layouts of bind groups 0..`bind_group_layout_count`-1.
    BindGroupLayoutHandle bind_group_layouts[kMaxBindGroups];
    /// Number of valid entries in `bind_group_layouts`.
    u32 bind_group_layout_count = 0;
    /// Size of the push-constant range in bytes; 0 for none; a multiple of 4.
    u32 push_constant_bytes = 0;
    /// Stages that read the push constants; required when `push_constant_bytes` is non-zero.
    ShaderStage push_constant_stages = ShaderStage::None;
    /// Samples per texel; only 1 is supported in Phase 3.
    u32 samples = 1;
    /// Optional name shown in debuggers and logs; null for none.
    const char* debug_name = nullptr;

    /// Checks the descriptor without a device.
    ///
    /// Returns: null if the descriptor is acceptable, else the first problem.
    /// Fails: reported through the return value, never by aborting.
    /// Ownership: the returned string is static and is never freed by the caller.
    const char* Validate() const;
};

/// Describes a compute pipeline. Its push constants are visible to the compute stage.
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: `debug_name` is borrowed for the duration of the call. The shader and layout handles are
///   borrowed, not consumed.
struct ComputePipelineDesc {
    /// The compute shader; required.
    ShaderHandle shader;
    /// Layouts of bind groups 0..`bind_group_layout_count`-1.
    BindGroupLayoutHandle bind_group_layouts[kMaxBindGroups];
    /// Number of valid entries in `bind_group_layouts`.
    u32 bind_group_layout_count = 0;
    /// Size of the push-constant range in bytes; 0 for none; a multiple of 4.
    u32 push_constant_bytes = 0;
    /// Optional name shown in debuggers and logs; null for none.
    const char* debug_name = nullptr;

    /// Checks the descriptor without a device.
    ///
    /// Returns: null if the descriptor is acceptable, else the first problem.
    /// Fails: reported through the return value, never by aborting.
    /// Ownership: the returned string is static and is never freed by the caller.
    const char* Validate() const;
};

} // namespace engine_placeholder::rhi
