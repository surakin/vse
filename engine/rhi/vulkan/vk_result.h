#pragma once

#include "rhi/vulkan/vk_loader.h"

//! `VkResult` naming, classification and a logging check helper.

namespace engine_placeholder::rhi::vulkan {

/// What a `VkResult` means for the caller.
enum class VkResultClass : u8 {
    Success = 0,   ///< The call succeeded (this includes the non-error statuses such as `VK_NOT_READY` and `VK_TIMEOUT`).
    Suboptimal,    ///< `VK_SUBOPTIMAL_KHR`: the call worked and the result is usable, but the swapchain should be recreated soon.
    Recoverable,   ///< The call did not complete but the owner can recreate the object and retry: `VK_ERROR_OUT_OF_DATE_KHR`, `VK_ERROR_SURFACE_LOST_KHR`.
    DeviceLost,    ///< The device is gone (`VK_ERROR_DEVICE_LOST`); every object of that device is unusable.
    Fatal,         ///< Any other error: out of memory, a driver fault, or a usage error; code that can react keeps the raw value.
};

/// Returns the enumerator name of `result` (`"VK_ERROR_DEVICE_LOST"`), or `"VK_RESULT_UNKNOWN"` for a value it does not know.
/// Ownership: the string is static.
const char* VkResultName(VkResult result);

/// Classifies a `VkResult`; see [[rhi::vulkan::VkResultClass]].
///
/// Unknown negative values are `Fatal`, unknown non-negative ones `Success`. A caller that needs the exact
/// value (say `VK_ERROR_OUT_OF_DEVICE_MEMORY`) stores the `VkResult` first and classifies that.
VkResultClass ClassifyVkResult(VkResult result);

/// Classifies `result` and logs anything that is not `Success` on `LogRhi`, with the call text and source location.
///
/// `Suboptimal` and `Recoverable` results are logged at `Debug` (they are routine on resize), the rest at `Error`.
/// Normally invoked through `ENGINE_VK_CHECK`.
///
/// Param result: the value returned by a Vulkan call.
/// Param call_text: the call as source text, for the log; must not be null.
/// Param file: source file; must not be null.
/// Param line: source line.
/// Returns: the classification of `result`.
VkResultClass CheckVkResult(VkResult result, const char* call_text, const char* file, int line);

} // namespace engine_placeholder::rhi::vulkan

/// Evaluates a Vulkan call and returns its [[rhi::vulkan::VkResultClass]], logging failures with file and line.
#define ENGINE_VK_CHECK(call) \
    ::engine_placeholder::rhi::vulkan::CheckVkResult((call), #call, __FILE__, __LINE__)
