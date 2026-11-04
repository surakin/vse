#pragma once

#include "rhi/desc.h"
#include "rhi/vulkan/vk_loader.h"

//! Pure conversions from RHI pipeline state to Vulkan enums, and the rules for the on-disk pipeline cache, as plain
//! functions so they can be tested without a GPU.

namespace engine_placeholder::rhi::vulkan {

/// Converts a primitive topology.
/// Param topology: the RHI topology.
/// Returns: the Vulkan topology.
VkPrimitiveTopology ToVkTopology(PrimitiveTopology topology);

/// Converts a cull mode.
/// Param mode: the RHI cull mode.
/// Returns: the Vulkan cull mode flags.
VkCullModeFlags ToVkCullMode(CullMode mode);

/// Converts a front-face winding.
/// Param face: the RHI winding.
/// Returns: the Vulkan front face.
VkFrontFace ToVkFrontFace(FrontFace face);

/// Converts a comparison.
/// Param op: the RHI comparison.
/// Returns: the Vulkan comparison.
VkCompareOp ToVkCompareOp(CompareOp op);

/// Converts a blend factor.
/// Param factor: the RHI factor.
/// Returns: the Vulkan factor.
VkBlendFactor ToVkBlendFactor(BlendFactor factor);

/// Converts a blend operation.
/// Param op: the RHI operation.
/// Returns: the Vulkan operation.
VkBlendOp ToVkBlendOp(BlendOp op);

/// Converts a vertex attribute format.
/// Param format: the RHI vertex format.
/// Returns: the Vulkan format.
VkFormat ToVkVertexFormat(VertexFormat format);

/// Converts an index width.
/// Param type: the RHI index type.
/// Returns: the Vulkan index type.
VkIndexType ToVkIndexType(IndexType type);

/// Converts shader stage flags.
/// Param stages: the RHI stage set.
/// Returns: the Vulkan stage flags.
VkShaderStageFlags ToVkShaderStages(ShaderStage stages);

/// Converts a binding type to a descriptor type.
/// Param type: the RHI binding type.
/// Returns: the Vulkan descriptor type.
VkDescriptorType ToVkDescriptorType(BindingType type);

/// The identity of the device a pipeline cache blob belongs to.
///
/// Thread-safety: thread-compatible plain data.
struct PipelineCacheIdentity {
    /// PCI vendor id.
    u32 vendor_id = 0;
    /// PCI device id.
    u32 device_id = 0;
    /// The driver's `pipelineCacheUUID`.
    u8 uuid[16] = {};
};

/// The size in bytes of a Vulkan pipeline cache header (version one), the part that names the device.
inline constexpr usize kPipelineCacheHeaderBytes = 32;

/// Checks the header of a pipeline cache blob read from disk against the device it would be loaded on.
///
/// A blob is acceptable when it is at least a header long, the header size field is at least 32, the header version
/// is 1, and the vendor, device and UUID equal those of `identity`. The driver validates the rest of the blob itself.
///
/// Param data: the blob.
/// Param size: its size in bytes.
/// Param identity: the device it must belong to.
/// Returns: null if acceptable, else a static string naming the first mismatch.
/// Fails: reported through the return value, never by aborting.
/// Ownership: the returned string is static and is never freed by the caller; `data` is borrowed.
/// Thread-safety: thread-safe. A pure function.
const char* CheckPipelineCacheHeader(const void* data, usize size, const PipelineCacheIdentity& identity);

/// Builds the cache file path `<dir>/pipeline_cache_<vendor>_<device>_<uuid>.bin`.
///
/// Param dir: the directory, without a trailing slash requirement (one is added if missing).
/// Param identity: the device the file is for.
/// Param out: receives the nul-terminated path.
/// Param capacity: size of `out` in bytes.
/// Returns: true if it fit.
/// Fails: returns false and leaves `out` empty if the path would not fit.
/// Thread-safety: thread-safe. A pure function.
bool BuildPipelineCachePath(const char* dir, const PipelineCacheIdentity& identity, char* out, usize capacity);

/// Chooses the directory for the pipeline cache.
///
/// In order: `explicit_dir` if non-empty, `$ENGINE_RHI_CACHE_DIR`, `$XDG_CACHE_HOME/<app>`, `$HOME/.cache/<app>`. Slashes
/// in `app` are replaced by underscores.
///
/// Param explicit_dir: a directory from the device descriptor, or null.
/// Param env_cache_dir: value of `ENGINE_RHI_CACHE_DIR`, or null.
/// Param xdg_cache_home: value of `XDG_CACHE_HOME`, or null.
/// Param home: value of `HOME`, or null.
/// Param app: the application name.
/// Param out: receives the nul-terminated directory.
/// Param capacity: size of `out` in bytes.
/// Returns: true if a directory was chosen and fit.
/// Fails: returns false, leaving `out` empty, if none of the sources is set or the path does not fit.
/// Thread-safety: thread-safe. A pure function; it does not read the environment itself.
bool ChoosePipelineCacheDir(const char* explicit_dir, const char* env_cache_dir, const char* xdg_cache_home, const char* home,
                            const char* app, char* out, usize capacity);

} // namespace engine_placeholder::rhi::vulkan
