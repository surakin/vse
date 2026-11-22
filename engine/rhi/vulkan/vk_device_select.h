#pragma once

#include "rhi/adapter.h"
#include "types.h"

//! Choosing a physical device and planning its queues, as pure functions over plain data.

namespace vse::rhi::vulkan
{

/// The most physical devices considered; further ones are ignored.
inline constexpr usize kMaxPhysicalDevices = 16;
/// The most queue families recorded per device; further ones are ignored.
inline constexpr usize kMaxQueueFamilies = 16;

/// What kind of GPU a physical device is, in the order the ranking prefers.
enum class GpuKind : u8
{
    Other = 0,   ///< Unknown kind.
    Cpu,         ///< A software implementation.
    Virtual,     ///< A virtualized GPU.
    Integrated,  ///< A GPU sharing memory with the CPU.
    Discrete,    ///< A dedicated GPU.
};

/// One queue family of a physical device.
///
/// - Thread-safety: thread-compatible plain data.
struct QueueFamilyInfo
{
    /// Number of queues in the family.
    u32 queue_count = 0;
    /// Whether the family supports graphics.
    bool graphics = false;
    /// Whether the family supports compute.
    bool compute = false;
    /// Whether the family supports transfer (graphics and compute families always do, implicitly).
    bool transfer = false;
    /// Number of valid bits in timestamps written by the family; 0 means it cannot write timestamps.
    u32 timestamp_valid_bits = 0;
    /// Whether the family can present to the surface the device was queried with (false when headless).
    bool present = false;
};

/// Everything the selection policy needs to know about one physical device.
///
/// - Thread-safety: thread-compatible plain data.
struct PhysicalDeviceInfo
{
    /// The device name as reported by the driver, null-terminated.
    char name[256] = {};
    /// The kind of GPU.
    GpuKind kind = GpuKind::Other;
    /// The highest Vulkan version the device supports (`VK_MAKE_API_VERSION` encoding).
    u32 api_version = 0;
    /// Whether dynamic rendering is supported.
    bool dynamic_rendering = false;
    /// Whether synchronization2 is supported.
    bool synchronization2 = false;
    /// Whether timeline semaphores are supported.
    bool timeline_semaphore = false;
    /// Whether anisotropic filtering is supported.
    bool sampler_anisotropy = false;
    /// Whether `fillModeNonSolid` is supported (wireframe pipelines).
    bool fill_mode_non_solid = false;
    /// Whether the device supports the swapchain extension.
    bool swapchain_extension = false;
    /// Whether the device supports `VK_EXT_memory_budget`.
    bool memory_budget_extension = false;
    /// The PCI vendor id.
    u32 vendor_id = 0;
    /// The device id within the vendor.
    u32 device_id = 0;
    /// The device UUID (`VkPhysicalDeviceIDProperties::deviceUUID`); all zero when the driver gives none.
    u8 uuid[16] = {};
    /// The device LUID (`VkPhysicalDeviceIDProperties::deviceLUID`, Windows' 8-byte LUID: low part then high part,
    /// little-endian).
    u8 luid[8] = {};
    /// Whether the driver reported a LUID (`deviceLUIDValid`; Windows drivers only).
    bool luid_valid = false;
    /// The driver version, already decoded to text (for example `575.57.8`), null-terminated.
    char driver[64] = {};
    /// The size in bytes of the largest device-local memory heap; 0 if unknown.
    u64 dedicated_memory_bytes = 0;
    /// The queue families; only the first `queue_family_count` are valid.
    QueueFamilyInfo families[kMaxQueueFamilies];
    /// Number of valid entries in `families`.
    u32 queue_family_count = 0;
};

/// Returns why a device cannot be used, or null if it can.
///
/// A device is unusable if its Vulkan version is below 1.3, it lacks dynamic rendering, synchronization2 or
/// timeline semaphores, or has no queue family with graphics and compute (and, when `presentable`, one that
/// also presents and a swapchain extension).
///
/// - Param info: the device description.
/// - Param presentable: whether the device must be able to present to a window.
/// - Returns: a static explanation, or null if suitable.
/// - Ownership: the string is static.
const char* RejectionReason(const PhysicalDeviceInfo& info, bool presentable);

/// Builds the stable adapter id of a physical device: `vk-<vendor>-<device>-<uuid>`, lowercase hex.
///
/// The id does not depend on the enumeration order, so a saved setting survives another GPU being added.
///
/// - Param info: the device description.
/// - Param out: receives the null-terminated id.
/// - Param capacity: size of `out` in bytes; at least `kAdapterIdBytes`.
void MakeAdapterId(const PhysicalDeviceInfo& info, char* out, usize capacity);

/// Describes a physical device as an adapter.
///
/// - Param info: the device description.
/// - Param presentable: whether the device must be able to present (the enumeration was given a window).
/// - Returns: the adapter; `suitable` and `unsuitable_reason` come from [[rhi::vulkan::RejectionReason]].
AdapterInfo MakeAdapterInfo(const PhysicalDeviceInfo& info, bool presentable);

/// Chooses the physical device to use.
///
/// Unsuitable devices are skipped; the rest are ranked by [[rhi::vulkan::GpuKind]] (discrete best), ties going
/// to the earlier device. A non-empty `override_text` that is a decimal number below the device count picks that
/// device index; any other text (including a number too large to be an index, such as a model number) picks the first
/// device whose name contains it, ignoring case; the text is first tried as an adapter id (see
/// [[rhi::MatchAdapter]]). The override must match a suitable device or selection fails. The whole ranking and every
/// rejection reason is logged on `LogRhi`.
///
/// - Param devices: the candidate devices.
/// - Param count: number of candidates; at most `kMaxPhysicalDevices` are considered.
/// - Param presentable: whether the device must be able to present.
/// - Param override_text: the value of `ENGINE_RHI_GPU`, or null/empty for none.
/// - Returns: the index of the chosen device, or -1.
/// - Fails: returns -1 if no device is suitable or the override matches no suitable device.
int SelectPhysicalDevice(const PhysicalDeviceInfo* devices, usize count, bool presentable, const char* override_text);

/// As [[rhi::vulkan::SelectPhysicalDevice]] (see there), naming where the request came from in the log lines.
///
/// - Param source: a static label such as `"DeviceCreateInfo::adapter"` or `"ENGINE_RHI_GPU"`.
int SelectPhysicalDeviceFor(const PhysicalDeviceInfo* devices, usize count, bool presentable, const char* override_text,
                            const char* source);

/// The queue families chosen for a device.
///
/// - Thread-safety: thread-compatible plain data.
struct QueuePlan
{
    /// The family used for graphics, compute and presentation; `-1` if none is usable.
    int graphics_family = -1;
    /// A dedicated transfer-only family if the device has one, otherwise the same as `graphics_family`.
    ///
    /// All work, including uploads, is submitted to the graphics queue; this records where an asynchronous
    /// transfer queue would go.
    int transfer_family = -1;
};

/// Plans the queues for a device: the first family that supports graphics and compute (and presenting, when
/// `presentable`), and a dedicated transfer family if one exists.
///
/// - Param info: the device description.
/// - Param presentable: whether the graphics family must be able to present.
/// - Returns: the plan; `graphics_family` is -1 if the device has no usable family.
QueuePlan PlanQueues(const PhysicalDeviceInfo& info, bool presentable);

}  // namespace vse::rhi::vulkan
