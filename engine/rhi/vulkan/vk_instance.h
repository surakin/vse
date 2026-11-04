#pragma once

#include "log/log.h"
#include "rhi/vulkan/vk_functions.h"
#include "rhi/vulkan/vk_loader.h"

//! The Vulkan instance, the validation layer policy and the validation message counters.

namespace engine_placeholder::rhi::vulkan {

/// Whether to enable the Khronos validation layer for an instance.
enum class ValidationMode : u8 {
    Auto = 0, ///< Follow `ENGINE_RHI_VALIDATION` if set, otherwise on in Debug builds and off in Release.
    Off,      ///< Never enable validation, whatever the environment says.
    On,       ///< Enable validation if the layer is installed.
};

/// What to create a [[rhi::vulkan::VulkanInstance]] for.
///
/// Thread-safety: thread-compatible plain data.
struct VulkanInstanceDesc {
    /// Application name reported to the driver; a static string. Must not be null.
    const char* application_name = "engine";
    /// Application version reported to the driver (`VK_MAKE_VERSION` encoding).
    u32 application_version = 0;
    /// Enable the surface extensions (`VK_KHR_surface` and the Wayland/Xlib ones) so windows can be presented to.
    bool presentable = false;
    /// Whether the validation layer is wanted; see [[rhi::vulkan::ValidationMode]].
    ValidationMode validation = ValidationMode::Auto;
    /// Also enable synchronization validation (barriers and hazards); `ENGINE_RHI_VALIDATION_SYNC` overrides it.
    bool sync_validation = false;
    /// Also enable best-practices validation; `ENGINE_RHI_VALIDATION_BEST_PRACTICES` overrides it.
    bool best_practices = false;
};

/// The outcome of resolving a [[rhi::vulkan::VulkanInstanceDesc]] against the environment.
///
/// Thread-safety: thread-compatible plain data.
struct ValidationPolicy {
    /// Whether to ask for the validation layer.
    bool enable = false;
    /// Whether synchronization validation is requested.
    bool sync = false;
    /// Whether best-practices validation is requested.
    bool best_practices = false;
};

/// Decides the validation settings from the descriptor and the three environment values, without touching the environment.
///
/// `ENGINE_RHI_VALIDATION` is consulted only for `ValidationMode::Auto`; a value of `"1"` means on and `"0"` off.
/// The sync and best-practices variables override the descriptor whenever they are set to `"1"` or `"0"`.
///
/// Param desc: the instance descriptor.
/// Param env_validation: value of `ENGINE_RHI_VALIDATION`, or null if unset.
/// Param env_sync: value of `ENGINE_RHI_VALIDATION_SYNC`, or null if unset.
/// Param env_best_practices: value of `ENGINE_RHI_VALIDATION_BEST_PRACTICES`, or null if unset.
/// Param debug_build: whether this is a Debug build, the default for `Auto` with no environment value.
/// Returns: the resolved policy. Sync and best-practices are false when validation is not enabled.
ValidationPolicy ResolveValidationPolicy(const VulkanInstanceDesc& desc, const char* env_validation, const char* env_sync,
                                         const char* env_best_practices, bool debug_build);

/// Running totals of validation messages since process start or the last [[rhi::vulkan::ResetValidationCounts]].
///
/// Thread-safety: thread-compatible plain data (a snapshot).
struct ValidationCounts {
    /// Messages at `ERROR` severity.
    u64 errors = 0;
    /// Messages at `WARNING` severity.
    u64 warnings = 0;
};

/// Returns a snapshot of the validation message counters. The counters are process-wide.
/// Thread-safety: thread-safe.
ValidationCounts GetValidationCounts();

/// Sets both validation counters to zero. Thread-safe.
void ResetValidationCounts();

/// Receives every validation message (after it is counted and logged); see [[rhi::vulkan::SetValidationCapture]].
///
/// The arguments are the pointer given to `SetValidationCapture`, the message severity as a log verbosity
/// (`Error`, `Warn`, `Debug` or `Trace`), the validation message id (possibly empty) and the full message text.
/// For a few messages the loader reports the error itself and the id is the generic `Loader Message`, with the
/// `VUID-...` in the text.
using ValidationCaptureFn = void (*)(void* user, LogVerbosity severity, const char* message_id_name, const char* message);

/// Installs a hook that sees every validation message, or removes it when `fn` is null.
///
/// The hook may run on any thread that calls Vulkan. It is called without any internal lock held, so it may log or
/// call Vulkan; but a call already in flight on another thread can still finish after this function returns, so
/// remove the hook only once no Vulkan call is running. Tests use it to assert on message ids.
///
/// Param fn: the hook, or null to remove it.
/// Param user: passed through to the hook.
void SetValidationCapture(ValidationCaptureFn fn, void* user);

/// A Vulkan instance with the extensions its purpose needs and, when wanted and installed, the validation layer.
///
/// Move-only; the destructor destroys the debug messenger and the instance.
///
/// Thread-safety: thread-compatible. The tables it exposes are immutable after creation.
/// Ownership: owns the `VkInstance` and debug messenger. It keeps a copy of the loader it was created from (the
///   library itself is never unloaded), so the loader passed to `Create` need not outlive it.
class VulkanInstance {
public:
    /// Constructs an invalid instance; use [[rhi::vulkan::VulkanInstance::Create]].
    VulkanInstance() = default;
    /// Destroys the debug messenger and the instance, if valid.
    ~VulkanInstance();
    /// Takes ownership from `other`, which becomes invalid.
    VulkanInstance(VulkanInstance&& other);
    /// Destroys this instance, then takes ownership from `other`, which becomes invalid.
    VulkanInstance& operator=(VulkanInstance&& other);
    VulkanInstance(const VulkanInstance&) = delete;
    VulkanInstance& operator=(const VulkanInstance&) = delete;

    /// Creates an instance for Vulkan 1.3.
    ///
    /// Validation is enabled per [[rhi::vulkan::ResolveValidationPolicy]] and the process environment. If it is
    /// wanted but `VK_LAYER_KHRONOS_validation` is not installed, one warning is logged for the process and the
    /// instance is created without it. `ENGINE_RHI_VALIDATION_FATAL=1` makes any validation error abort the process.
    ///
    /// Param loader: an opened loader; copied.
    /// Param desc: what the instance is for.
    /// Returns: the instance; check [[rhi::vulkan::VulkanInstance::Valid]].
    /// Fails: returns an invalid instance, logging the reason, if the loader is older than 1.3, a required
    ///   extension (the surface extensions when `presentable`) is missing, or `vkCreateInstance` fails.
    static VulkanInstance Create(const VulkanLoader& loader, const VulkanInstanceDesc& desc);

    /// Returns whether this object holds a live instance.
    bool Valid() const { return instance_ != nullptr; }

    /// Returns the raw `VkInstance`, or null if invalid. Ownership: borrowed.
    VkInstance Handle() const { return instance_; }

    /// Returns the resolved instance-level function table; all-null if invalid.
    const VkInstanceFns& Fns() const { return fns_; }

    /// Returns this instance's copy of the loader it was created from.
    const VulkanLoader& Loader() const { return loader_; }

    /// Returns whether the validation layer is active and its messages are being routed (the debug messenger exists).
    bool ValidationEnabled() const { return validation_enabled_; }

    /// Returns whether the surface extensions are enabled, so presentation surfaces can be created.
    bool Presentable() const { return presentable_; }

private:
    void Destroy();

    VulkanLoader loader_;
    VkInstance instance_ = nullptr;
    VkInstanceFns fns_;
    VkDebugUtilsMessengerEXT messenger_ = nullptr;
    bool validation_enabled_ = false;
    bool presentable_ = false;
};

} // namespace engine_placeholder::rhi::vulkan
