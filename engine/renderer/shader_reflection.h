#pragma once

//! What a compiled shader says about itself: its stage, bindings, uniform and push-constant block layouts, vertex
//! inputs and compute local size. The offline `shader_cook` tool extracts it from `slangc` output into a compact blob
//! committed as `shaders/<name>.refl.inc`; the renderer parses the blob and builds bind group layouts and vertex
//! layouts from it instead of writing them a second time by hand.

#include "rhi/desc.h"
#include "rhi/enums.h"
#include "rhi/limits.h"
#include "types.h"

namespace engine_placeholder::renderer {

/// The reflection blob format version this build reads and writes.
inline constexpr u32 kReflectionVersion = 1;
/// The capacity of every reflected name, including the terminator; longer names are rejected.
inline constexpr u32 kMaxReflectionName = 48;
/// The most bindings one shader module may reflect.
inline constexpr u32 kMaxReflectedBindings = 16;
/// The most uniform and push-constant blocks one shader module may reflect.
inline constexpr u32 kMaxReflectedBlocks = 8;
/// The most members one reflected block may have.
inline constexpr u32 kMaxBlockMembers = 16;
/// The most vertex inputs one vertex shader may reflect.
inline constexpr u32 kMaxReflectedVertexInputs = 16;
/// The block index of a binding that has no block (textures, samplers, storage buffers).
inline constexpr u32 kNoBlock = 0xffffffffu;
/// The capacity of the message in a [[renderer::ReflectionError]].
inline constexpr u32 kReflectionErrorLength = 192;

/// The scalar type of a reflected block member.
enum class ScalarType : u8 {
    Float32 = 0, ///< 32-bit float.
    Int32,       ///< 32-bit signed integer.
    UInt32,      ///< 32-bit unsigned integer.
    Count,       ///< Number of scalar types; not a type.
};

/// The shape of a reflected block member.
enum class MemberKind : u8 {
    Scalar = 0, ///< One scalar (`rows` and `columns` are 1).
    Vector,     ///< A vector of `rows` scalars (`columns` is 1).
    Matrix,     ///< A matrix of `rows` x `columns` scalars, stored as the block's layout says.
    Struct,     ///< A nested struct: only its offset and size are reflected (`rows` and `columns` are 0).
    Count,      ///< Number of kinds; not a kind.
};

/// One member of a reflected uniform or push-constant block.
///
/// Thread-safety: thread-compatible plain data.
struct ReflectedMember {
    /// The member's name as written in the shader.
    char name[kMaxReflectionName] = {};
    /// Byte offset from the start of the block.
    u32 offset = 0;
    /// Size in bytes, including every array element.
    u32 size = 0;
    /// Scalar, vector, matrix or struct.
    MemberKind kind = MemberKind::Scalar;
    /// The scalar type; for a struct, `Float32` and meaningless.
    ScalarType scalar = ScalarType::Float32;
    /// Rows of a matrix, elements of a vector, 1 for a scalar, 0 for a struct.
    u8 rows = 1;
    /// Columns of a matrix, 1 for a scalar or vector, 0 for a struct.
    u8 columns = 1;
    /// Element count if the member is an array, else 0.
    u32 array_count = 0;
    /// Bytes between array elements; 0 if the member is not an array.
    u32 array_stride = 0;
};

/// The layout of a uniform buffer or push-constant block as the shader sees it.
///
/// Thread-safety: thread-compatible plain data.
struct ReflectedBlock {
    /// The block's struct name in the shader (for example `Params`).
    char name[kMaxReflectionName] = {};
    /// Total size in bytes, as the buffer or push range must be at least.
    u32 size = 0;
    /// Number of valid entries in `members`.
    u32 member_count = 0;
    /// The members in declaration order.
    ReflectedMember members[kMaxBlockMembers] = {};
};

/// One resource binding of a shader module.
///
/// Thread-safety: thread-compatible plain data.
struct ReflectedBinding {
    /// The variable's name in the shader.
    char name[kMaxReflectionName] = {};
    /// The descriptor set (bind group slot).
    u32 set = 0;
    /// The binding index inside the set.
    u32 binding = 0;
    /// What kind of resource the shader expects.
    rhi::BindingType type = rhi::BindingType::UniformBuffer;
    /// Array size; 1 for a non-array binding.
    u32 count = 1;
    /// The stage that reads the binding (the module's own stage).
    rhi::ShaderStage stages = rhi::ShaderStage::None;
    /// Index into [[renderer::ShaderReflection]]'s `blocks` for a uniform buffer, else [[renderer::kNoBlock]].
    u32 block = kNoBlock;
};

/// One vertex shader input.
///
/// Thread-safety: thread-compatible plain data.
struct ReflectedVertexInput {
    /// The input's name in the shader (the struct member or parameter name).
    char name[kMaxReflectionName] = {};
    /// The shader input location.
    u32 location = 0;
    /// The data type the shader reads (a shader cannot tell whether bytes are normalized, so packed formats are not produced).
    rhi::VertexFormat format = rhi::VertexFormat::Float3;
};

/// Everything the engine needs to know about one compiled shader module.
///
/// Plain data with fixed capacities, so it lives on the stack and needs no allocator.
///
/// Thread-safety: thread-compatible plain data.
struct ShaderReflection {
    /// The one stage the module implements.
    rhi::ShaderStage stage = rhi::ShaderStage::None;
    /// The entry point name.
    char entry[kMaxReflectionName] = {};
    /// Compute local size (x, y, z); zeros for other stages.
    u32 local_size[3] = {0, 0, 0};
    /// Number of valid entries in `bindings`.
    u32 binding_count = 0;
    /// The resource bindings the entry point uses.
    ReflectedBinding bindings[kMaxReflectedBindings] = {};
    /// Number of valid entries in `blocks`.
    u32 block_count = 0;
    /// Uniform buffer blocks and the push-constant block.
    ReflectedBlock blocks[kMaxReflectedBlocks] = {};
    /// Whether the module has a push-constant block.
    bool has_push_constants = false;
    /// Index into `blocks` of the push-constant block; meaningful only if `has_push_constants`.
    u32 push_constant_block = 0;
    /// Number of valid entries in `vertex_inputs`.
    u32 vertex_input_count = 0;
    /// The vertex inputs, sorted by increasing location.
    ReflectedVertexInput vertex_inputs[kMaxReflectedVertexInputs] = {};
};

/// A failure message from the reflection builders, with room for names so conflicts can be explained.
///
/// Thread-safety: thread-compatible plain data.
struct ReflectionError {
    /// A nul-terminated message; empty until a builder fails.
    char message[kReflectionErrorLength] = {};
};

/// Checks that a reflection is internally consistent: known enum values, terminated names, counts within capacity,
/// block indices in range, a stage that matches the compute local size.
///
/// Param reflection: the reflection to check.
/// Returns: null if it is consistent, else a static string naming the first problem.
/// Fails: reported through the return value, never by aborting.
/// Ownership: the returned string is static and is never freed by the caller.
/// Thread-safety: thread-safe. A pure function of its argument.
const char* ValidateShaderReflection(const ShaderReflection& reflection);

/// Encodes a reflection as the little-endian blob stored in `*.refl.inc`.
///
/// Call it with `out` null to learn the size needed.
///
/// Param reflection: a reflection that passes [[renderer::ValidateShaderReflection]].
/// Param out: where to write the blob, or null to only measure.
/// Param capacity: size of `out` in bytes.
/// Returns: the size of the blob in bytes; 0 if the reflection is invalid or `out` is non-null and too small.
/// Fails: reported through the return value 0, never by aborting.
/// Ownership: `out` is borrowed for the call.
/// Thread-safety: thread-safe. A pure function of its arguments.
usize SerializeShaderReflection(const ShaderReflection& reflection, u8* out, usize capacity);

/// Decodes a reflection blob, rejecting anything malformed without reading outside `data`.
///
/// It refuses a wrong magic number or version, truncated or over-long data, names without room for a terminator,
/// counts above the fixed capacities, enum values it does not know, indices out of range and trailing bytes. On
/// failure `out` is left in an unspecified but valid state.
///
/// Param data: the blob bytes.
/// Param size: number of bytes in `data`.
/// Param out: receives the reflection; must not be null.
/// Returns: null on success, else a static string naming the first problem.
/// Fails: reported through the return value, never by aborting.
/// Ownership: `data` is borrowed for the call; nothing is retained.
/// Thread-safety: thread-safe. A pure function of its arguments.
const char* ParseShaderReflection(const u8* data, usize size, ShaderReflection* out);

/// Returns the uniform or push-constant block called `name`, or null.
///
/// Param reflection: the reflection to search.
/// Param name: the block's struct name; must not be null.
/// Returns: a pointer into `reflection`, or null if there is no such block.
/// Ownership: the pointer is valid as long as `reflection` is.
/// Thread-safety: thread-safe. A pure function of its arguments.
const ReflectedBlock* FindBlock(const ShaderReflection& reflection, const char* name);

/// Checks that a C++ struct has the size of the shader block it is copied into.
///
/// Param reflection: the reflection that holds the block.
/// Param block_name: the block's struct name in the shader.
/// Param cpp_size: `sizeof` the C++ struct.
/// Param error: receives a message naming both sizes on failure; may be null.
/// Returns: true if the block exists and its size equals `cpp_size`.
/// Fails: reported through the return value; the message goes to `error` if given.
/// Ownership: `error` is borrowed for the call.
/// Thread-safety: thread-safe. A pure function of its arguments.
bool CheckBlockSize(const ShaderReflection& reflection, const char* block_name, usize cpp_size, ReflectionError* error);

/// A bind group layout built from reflection: storage for the entries, with a view usable as an
/// [[rhi::BindGroupLayoutDesc]].
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: [[renderer::BindGroupLayoutData::Desc]] returns a descriptor that points into this object, so it must outlive the descriptor.
struct BindGroupLayoutData {
    /// The entries, sorted by binding index.
    rhi::BindingDesc entries[rhi::kMaxBindingsPerLayout] = {};
    /// Number of valid entries.
    u32 entry_count = 0;

    /// Returns a descriptor over `entries`.
    ///
    /// Param debug_name: optional name for debuggers and logs; borrowed, may be null.
    /// Returns: a descriptor valid as long as this object is.
    rhi::BindGroupLayoutDesc Desc(const char* debug_name = nullptr) const;
};

/// Builds the layout of descriptor set `set` from one or more shader modules, merging a vertex and a fragment shader
/// into one layout.
///
/// A binding used by several modules appears once with the stage masks OR-ed. The same (set, binding) with different
/// types or array sizes in two modules is reported with both variable names.
///
/// Param shaders: pointers to the reflections to merge.
/// Param shader_count: number of pointers in `shaders`; at least 1.
/// Param set: the descriptor set to build.
/// Param out: receives the layout; must not be null.
/// Param error: receives a message on failure; may be null.
/// Returns: true on success; false on a conflict or if the set holds more than [[rhi::kMaxBindingsPerLayout]] bindings.
/// Fails: reported through the return value and `error`.
/// Ownership: all pointers are borrowed for the call.
/// Thread-safety: thread-safe. A pure function of its arguments.
bool BuildBindGroupLayout(const ShaderReflection* const* shaders, u32 shader_count, u32 set, BindGroupLayoutData* out,
                          ReflectionError* error);

/// Builds the layout of descriptor set `set` from a single shader module (the one-shader form of [[renderer::BuildBindGroupLayout]]).
///
/// Param shader: the reflection to read.
/// Param set: the descriptor set to build.
/// Param out: receives the layout; must not be null.
/// Param error: receives a message on failure; may be null.
/// Returns: true on success.
/// Fails: reported through the return value and `error`.
/// Ownership: all pointers are borrowed for the call.
/// Thread-safety: thread-safe. A pure function of its arguments.
bool BuildBindGroupLayoutFor(const ShaderReflection& shader, u32 set, BindGroupLayoutData* out, ReflectionError* error);

/// A vertex layout built from a vertex shader's inputs: attributes packed into one interleaved binding.
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: the attributes are copied by value into the pipeline descriptor that uses them.
struct VertexLayoutData {
    /// The attributes, in location order, offsets packed with no padding.
    rhi::VertexAttributeDesc attributes[rhi::kMaxVertexAttributes] = {};
    /// Number of valid attributes.
    u32 attribute_count = 0;
    /// Bytes per vertex: the end of the last attribute.
    u32 stride = 0;
};

/// Builds a tightly packed, interleaved vertex layout for the inputs of a vertex shader.
///
/// Attributes go in increasing location order, each at the end of the previous one, all in vertex binding `binding`.
/// A mesh whose vertices are laid out differently builds its attributes by hand.
///
/// Param shader: a vertex shader's reflection.
/// Param binding: the vertex buffer binding all attributes read from.
/// Param out: receives the layout; must not be null.
/// Param error: receives a message on failure; may be null.
/// Returns: true on success; false if `shader` is not a vertex shader or has more inputs than the RHI allows.
/// Fails: reported through the return value and `error`.
/// Ownership: all pointers are borrowed for the call.
/// Thread-safety: thread-safe. A pure function of its arguments.
bool BuildVertexLayout(const ShaderReflection& shader, u32 binding, VertexLayoutData* out, ReflectionError* error);

} // namespace engine_placeholder::renderer
