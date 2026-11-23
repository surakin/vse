#pragma once

#include "containers/span.h"
#include "types.h"

//! Enumerations and type-safe flag sets shared by every RHI descriptor and command.
//!
//! Flag enums are `u32` scoped enums whose `|`, `&` and `Has` operators are enabled by specializing
//! [[rhi::EnableFlagOperators]]; there are no raw integer masks in the interface.

namespace vse::rhi
{

/// Opt-in trait: specialize with `value = true` to give a `u32` scoped enum the flag operators.
///
/// - Thread-safety: immutable. A compile-time trait.
template <typename E> struct EnableFlagOperators
{
    /// Whether `E` is a flag enum.
    static constexpr bool value = false;
};

/// Returns the union of two flag values.
///
/// - Param a: first flag set.
/// - Param b: second flag set.
template <typename E>
    requires(EnableFlagOperators<E>::value)
constexpr E operator|(E a, E b)
{
    return static_cast<E>(static_cast<u32>(a) | static_cast<u32>(b));
}

/// Returns the flags set in both operands.
///
/// - Param a: first flag set.
/// - Param b: second flag set.
template <typename E>
    requires(EnableFlagOperators<E>::value)
constexpr E operator&(E a, E b)
{
    return static_cast<E>(static_cast<u32>(a) & static_cast<u32>(b));
}

/// Returns whether every flag in `flags` is also set in `value`.
///
/// - Param value: the flag set to test.
/// - Param flags: the flags that must all be present; an empty `flags` is trivially contained.
/// - Returns: true when every bit of `flags` is set in `value`.
template <typename E>
    requires(EnableFlagOperators<E>::value)
constexpr bool Has(E value, E flags)
{
    return (static_cast<u32>(value) & static_cast<u32>(flags)) == static_cast<u32>(flags);
}

/// Returns whether any bit is set in `value`.
///
/// - Param value: the flag set to test.
/// - Returns: true when `value` is non-zero.
template <typename E>
    requires(EnableFlagOperators<E>::value)
constexpr bool Any(E value)
{
    return static_cast<u32>(value) != 0;
}

/// What a buffer may be used for. Combine with `|`; at least one flag is required.
enum class BufferUsage : u32
{
    None = 0,            ///< No usage; invalid for a buffer.
    Vertex = 1u << 0,    ///< Bound as a vertex buffer.
    Index = 1u << 1,     ///< Bound as an index buffer.
    Uniform = 1u << 2,   ///< Bound as a uniform (constant) buffer.
    Storage = 1u << 3,   ///< Bound as a read/write storage buffer.
    CopySrc = 1u << 4,   ///< Source of a copy.
    CopyDst = 1u << 5,   ///< Destination of a copy.
    Indirect = 1u << 6,  ///< Source of indirect draw/dispatch arguments. Reserved.
};
/// Enables the flag operators for [[rhi::BufferUsage]].
///
/// - Thread-safety: immutable. A compile-time trait.
template <> struct EnableFlagOperators<BufferUsage>
{
    /// Always true.
    static constexpr bool value = true;
};

/// What a texture may be used for. Combine with `|`; at least one flag is required.
enum class TextureUsage : u32
{
    None = 0,                   ///< No usage; invalid for a texture.
    Sampled = 1u << 0,          ///< Read by shaders through a sampler.
    Storage = 1u << 1,          ///< Read/written by shaders as a storage image.
    ColorAttachment = 1u << 2,  ///< Rendered to as a color attachment.
    DepthAttachment = 1u << 3,  ///< Rendered to as a depth(-stencil) attachment.
    CopySrc = 1u << 4,          ///< Source of a copy.
    CopyDst = 1u << 5,          ///< Destination of a copy.
};
/// Enables the flag operators for [[rhi::TextureUsage]].
///
/// - Thread-safety: immutable. A compile-time trait.
template <> struct EnableFlagOperators<TextureUsage>
{
    /// Always true.
    static constexpr bool value = true;
};

/// The shader stages a binding or push-constant range is visible to. Combine with `|`.
enum class ShaderStage : u32
{
    None = 0,                                 ///< No stage.
    Vertex = 1u << 0,                         ///< Vertex shader.
    Fragment = 1u << 1,                       ///< Fragment (pixel) shader.
    Compute = 1u << 2,                        ///< Compute shader.
    AllGraphics = (1u << 0) | (1u << 1),      ///< Vertex and fragment.
    All = (1u << 0) | (1u << 1) | (1u << 2),  ///< Every stage.
};
/// Enables the flag operators for [[rhi::ShaderStage]].
///
/// - Thread-safety: immutable. A compile-time trait.
template <> struct EnableFlagOperators<ShaderStage>
{
    /// Always true.
    static constexpr bool value = true;
};

/// The bytecode format of a shader module ([[rhi::ShaderDesc]]); each backend takes one (see
/// [[rhi::DeviceCaps::shader_format]]).
enum class ShaderFormat : u8
{
    Spirv = 0,  ///< SPIR-V words (Vulkan).
    Dxil,       ///< A DXIL container (Direct3D 12).
    MetalLib,   ///< A Metal library (`.metallib`, AIR, minted offline) with its entry name (Metal).
};

/// Returns the lowercase name of a shader format (`spirv`, `dxil`, `metallib`), for logs.
///
/// - Param format: the format.
/// - Returns: a static string; `unknown` for a value outside the enum.
/// - Fails: never.
/// - Ownership: the string is static.
/// - Thread-safety: thread-safe.
const char* ShaderFormatName(ShaderFormat format);

/// Which color channels a render target write updates. Combine with `|`.
enum class ColorWriteMask : u32
{
    None = 0,     ///< Write nothing.
    R = 1u << 0,  ///< Red.
    G = 1u << 1,  ///< Green.
    B = 1u << 2,  ///< Blue.
    A = 1u << 3,  ///< Alpha.
    All = 0xF,    ///< All four channels.
};
/// Enables the flag operators for [[rhi::ColorWriteMask]].
///
/// - Thread-safety: immutable. A compile-time trait.
template <> struct EnableFlagOperators<ColorWriteMask>
{
    /// Always true.
    static constexpr bool value = true;
};

/// Where a buffer's memory lives and who can access it.
enum class MemoryLocation : u8
{
    GpuOnly = 0,  ///< Device-local; not CPU-visible. Fill it by upload.
    CpuToGpu,     ///< CPU-writable and GPU-readable (uploads, per-frame uniforms); mappable.
    GpuToCpu,     ///< GPU-writable and CPU-readable (readback); mappable, copy destination only.
};

/// The state a buffer or texture is in between commands.
///
/// The portable common denominator of Vulkan layouts and access masks, Direct3D 12 resource states and
/// Metal's implicit tracking. Callers state the `before` and `after` states of every transition.
enum class ResourceState : u8
{
    Undefined = 0,    ///< Contents unknown; the initial state of a texture. Transitions from here discard contents.
    VertexBuffer,     ///< Read as vertex data.
    IndexBuffer,      ///< Read as index data.
    UniformBuffer,    ///< Read as a uniform buffer.
    ShaderRead,       ///< Read by shaders (sampled texture or read-only storage).
    ShaderWrite,      ///< Written (or read-written) by shaders as storage.
    CopySrc,          ///< Source of a copy.
    CopyDst,          ///< Destination of a copy.
    ColorAttachment,  ///< Rendered to as a color attachment.
    DepthAttachment,  ///< Rendered to as a depth(-stencil) attachment, writes allowed.
    DepthRead,        ///< Depth(-stencil) read-only: depth test without writes, or sampled.
    Present,          ///< Ready to be presented to a window.
    Count,            ///< Number of states; not a state.
};

/// How vertices are assembled into primitives.
enum class PrimitiveTopology : u8
{
    TriangleList = 0,  ///< Every three vertices form a triangle.
    TriangleStrip,     ///< Each vertex after the first two forms a triangle with the previous two.
    LineList,          ///< Every two vertices form a line.
    LineStrip,         ///< Connected line segments.
    PointList,         ///< Each vertex is a point.
};

/// Which triangle faces are discarded.
enum class CullMode : u8
{
    None = 0,  ///< Draw both faces.
    Front,     ///< Discard front-facing triangles.
    Back,      ///< Discard back-facing triangles.
};

/// Which winding order is the front face.
enum class FrontFace : u8
{
    CounterClockwise = 0,  ///< Counter-clockwise in framebuffer space is front-facing.
    Clockwise,             ///< Clockwise in framebuffer space is front-facing.
};

/// A comparison used by depth tests and comparison samplers.
enum class CompareOp : u8
{
    Never = 0,     ///< Never passes.
    Less,          ///< Passes if the new value is less than the stored one.
    Equal,         ///< Passes if equal.
    LessEqual,     ///< Passes if less than or equal.
    Greater,       ///< Passes if greater.
    NotEqual,      ///< Passes if not equal.
    GreaterEqual,  ///< Passes if greater than or equal.
    Always,        ///< Always passes.
};

/// A blend factor applied to the source or destination color or alpha.
enum class BlendFactor : u8
{
    Zero = 0,          ///< 0.
    One,               ///< 1.
    SrcColor,          ///< Source color.
    OneMinusSrcColor,  ///< 1 - source color.
    DstColor,          ///< Destination color.
    OneMinusDstColor,  ///< 1 - destination color.
    SrcAlpha,          ///< Source alpha.
    OneMinusSrcAlpha,  ///< 1 - source alpha.
    DstAlpha,          ///< Destination alpha.
    OneMinusDstAlpha,  ///< 1 - destination alpha.
};

/// How blended source and destination terms are combined.
enum class BlendOp : u8
{
    Add = 0,          ///< src + dst.
    Subtract,         ///< src - dst.
    ReverseSubtract,  ///< dst - src.
    Min,              ///< Component-wise minimum (factors ignored).
    Max,              ///< Component-wise maximum (factors ignored).
};

/// Texture filtering.
enum class FilterMode : u8
{
    Nearest = 0,  ///< Nearest texel.
    Linear,       ///< Linear interpolation.
};

/// Behavior of texture coordinates outside [0, 1].
enum class AddressMode : u8
{
    Repeat = 0,     ///< Tile the texture.
    MirrorRepeat,   ///< Tile, mirroring every other repeat.
    ClampToEdge,    ///< Clamp to the edge texel.
    ClampToBorder,  ///< Use the sampler's border color.
};

/// Border color used by [[rhi::AddressMode]] `ClampToBorder`.
enum class BorderColor : u8
{
    TransparentBlack = 0,  ///< (0, 0, 0, 0).
    OpaqueBlack,           ///< (0, 0, 0, 1).
    OpaqueWhite,           ///< (1, 1, 1, 1).
};

/// What happens to an attachment's contents when a rendering pass begins.
enum class LoadOp : u8
{
    Load = 0,  ///< Keep the existing contents.
    Clear,     ///< Clear to the attachment's clear value.
    DontCare,  ///< Contents are undefined; the pass will overwrite everything it reads.
};

/// What happens to an attachment's contents when a rendering pass ends.
enum class StoreOp : u8
{
    Store = 0,  ///< Keep the results.
    DontCare,   ///< The results may be discarded (for example a depth buffer used only inside the pass).
};

/// The width of indices in an index buffer.
enum class IndexType : u8
{
    UInt16 = 0,  ///< 16-bit unsigned indices.
    UInt32,      ///< 32-bit unsigned indices.
};

/// The shape of a texture.
enum class TextureDimension : u8
{
    Tex2D = 0,  ///< A 2D image, optionally an array.
    Tex3D,      ///< A volume.
    Cube,       ///< A cube map: six square faces (`array_layers` is 6), sampled and copied only; see
                ///< `DeviceCaps::cube_textures`.
};

/// The data type and width of one vertex attribute.
enum class VertexFormat : u8
{
    Float1 = 0,  ///< One 32-bit float.
    Float2,      ///< Two 32-bit floats.
    Float3,      ///< Three 32-bit floats.
    Float4,      ///< Four 32-bit floats.
    UInt1,       ///< One 32-bit unsigned integer.
    UInt2,       ///< Two 32-bit unsigned integers.
    UInt3,       ///< Three 32-bit unsigned integers.
    UInt4,       ///< Four 32-bit unsigned integers.
    UNorm8x4,    ///< Four 8-bit unsigned normalized values (for example packed colors).
    Float16x2,   ///< Two 16-bit floats (half precision), read by the shader as a float2.
    Float16x4,   ///< Four 16-bit floats, read as a float4.
    UNorm16x2,   ///< Two 16-bit unsigned normalized values in [0, 1], read as a float2.
    UNorm16x4,   ///< Four 16-bit unsigned normalized values in [0, 1], read as a float4.
    SNorm16x2,   ///< Two 16-bit signed normalized values in [-1, 1], read as a float2.
    SNorm16x4,   ///< Four 16-bit signed normalized values in [-1, 1], read as a float4.
    SNorm8x4,    ///< Four 8-bit signed normalized values in [-1, 1] (packed normals and tangents), read as a float4.
    UInt8x4,     ///< Four 8-bit unsigned integers (joint indices), read as a uint4.
    UInt16x4,    ///< Four 16-bit unsigned integers, read as a uint4.
    Count,       ///< Number of vertex formats; not a format.
};

/// Returns the size in bytes of one attribute of `format`.
///
/// - Param format: a value below `VertexFormat::Count`; any other value returns 0.
constexpr u32 VertexFormatSize(VertexFormat format)
{
    switch (format)
    {
    case VertexFormat::Float1:
    case VertexFormat::UInt1:
    case VertexFormat::UNorm8x4:
    case VertexFormat::SNorm8x4:
    case VertexFormat::UInt8x4:
    case VertexFormat::Float16x2:
    case VertexFormat::UNorm16x2:
    case VertexFormat::SNorm16x2: return 4;
    case VertexFormat::Float2:
    case VertexFormat::UInt2:
    case VertexFormat::Float16x4:
    case VertexFormat::UNorm16x4:
    case VertexFormat::SNorm16x4:
    case VertexFormat::UInt16x4: return 8;
    case VertexFormat::Float3:
    case VertexFormat::UInt3: return 12;
    case VertexFormat::Float4:
    case VertexFormat::UInt4: return 16;
    case VertexFormat::Count: break;
    }
    return 0;
}

/// Returns the number of components the shader sees for one attribute of `format` (1 to 4).
///
/// - Param format: a value below `VertexFormat::Count`; any other value returns 0.
constexpr u32 VertexFormatComponents(VertexFormat format)
{
    switch (format)
    {
    case VertexFormat::Float1:
    case VertexFormat::UInt1: return 1;
    case VertexFormat::Float2:
    case VertexFormat::UInt2:
    case VertexFormat::Float16x2:
    case VertexFormat::UNorm16x2:
    case VertexFormat::SNorm16x2: return 2;
    case VertexFormat::Float3:
    case VertexFormat::UInt3: return 3;
    case VertexFormat::Float4:
    case VertexFormat::UInt4:
    case VertexFormat::UNorm8x4:
    case VertexFormat::SNorm8x4:
    case VertexFormat::UInt8x4:
    case VertexFormat::Float16x4:
    case VertexFormat::UNorm16x4:
    case VertexFormat::SNorm16x4:
    case VertexFormat::UInt16x4: return 4;
    case VertexFormat::Count: break;
    }
    return 0;
}

/// Returns whether the shader reads `format` as an unsigned integer vector (otherwise as a float vector).
///
/// - Param format: a value below `VertexFormat::Count`.
/// - Fails: never.
constexpr bool VertexFormatIsInteger(VertexFormat format)
{
    return format == VertexFormat::UInt1 || format == VertexFormat::UInt2 || format == VertexFormat::UInt3 ||
           format == VertexFormat::UInt4 || format == VertexFormat::UInt8x4 || format == VertexFormat::UInt16x4;
}

/// Returns the byte alignment an attribute of `format` needs inside its vertex: the smaller of its size and 4, which is
/// 4 for every format today. Direct3D 12 refuses an input layout whose attribute offset is not a multiple of that (a
/// 16-bit or 8-bit format aligned only to its component size fails `CreateInputLayout`, measured on Windows), and Metal
/// documents a multiple of 4 for an attribute offset (not yet run on the Mac), so the RHI demands it on every backend.
///
/// - Param format: a value below `VertexFormat::Count`; any other value returns 0.
constexpr u32 VertexFormatAlignment(VertexFormat format)
{
    switch (format)
    {
    case VertexFormat::UNorm8x4:
    case VertexFormat::SNorm8x4:
    case VertexFormat::UInt8x4:
    case VertexFormat::Float16x2:
    case VertexFormat::Float16x4:
    case VertexFormat::UNorm16x2:
    case VertexFormat::UNorm16x4:
    case VertexFormat::SNorm16x2:
    case VertexFormat::SNorm16x4:
    case VertexFormat::UInt16x4:
    case VertexFormat::Float1:
    case VertexFormat::Float2:
    case VertexFormat::Float3:
    case VertexFormat::Float4:
    case VertexFormat::UInt1:
    case VertexFormat::UInt2:
    case VertexFormat::UInt3:
    case VertexFormat::UInt4: return 4;
    case VertexFormat::Count: break;
    }
    return 0;
}

/// Returns whether a vertex attribute stored as `stored` can feed a shader input that reads `input` (a float or uint
/// vector of the same width): a normalized, half or float attribute feeds a float input, an integer attribute feeds a
/// uint input, and the component counts must match.
///
/// - Param stored: the attribute format in the mesh layout.
/// - Param input: the format the shader reads, as reflection reports it (`Float1`-`Float4` or `UInt1`-`UInt4`).
/// - Fails: never.
constexpr bool VertexFormatFeedsInput(VertexFormat stored, VertexFormat input)
{
    return stored < VertexFormat::Count && input < VertexFormat::Count &&
           VertexFormatIsInteger(stored) == VertexFormatIsInteger(input) &&
           VertexFormatComponents(stored) == VertexFormatComponents(input);
}

/// The kind of resource a binding refers to.
enum class BindingType : u8
{
    UniformBuffer = 0,  ///< A uniform buffer range.
    StorageBuffer,      ///< A storage buffer range.
    SampledTexture,     ///< A texture read through a sampler.
    Sampler,            ///< A sampler.
    StorageTexture,     ///< A texture read/written as a storage image.
    /// A storage buffer range the shaders only read (`StructuredBuffer` in Slang): a storage buffer to Vulkan, a shader
    /// resource view (not an unordered access view) to Direct3D 12, so the buffer is in `ShaderRead` state when bound.
    /// Last before `Count` so the values of the older types, which minted reflection stores, do not change.
    ReadOnlyStorageBuffer,
    Count,  ///< Number of binding types; not a type.
};

/// A read-only view of consecutive `T` owned by the caller: the foundation [[Span]] of const elements.
///
/// Used in descriptors so they need no allocation. The memory must stay valid for the duration of the
/// call the descriptor is passed to; backends copy what they keep.
template <typename T> using Span = vse::Span<const T>;

}  // namespace vse::rhi
