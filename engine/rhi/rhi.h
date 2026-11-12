#pragma once

#include "sync/atomic.h"

#include "platform/window.h"
#include "rhi/adapter.h"
#include "rhi/device.h"
#include "types.h"

//! Creating a device: the one entry point layers above the RHI use to get a backend.

namespace engine_placeholder::rhi
{

/// Which backend [[rhi::CreateDevice]] creates.
///
/// A backend exists in a program only once its library registered it ([[rhi::RegisterCompiledBackends]]); see
/// `rhi/backend_registry.h`.
enum class Backend : u8
{
    Null = 0,  ///< The validating no-GPU device ([[rhi::NullDevice]]).
    Vulkan,    ///< The Vulkan 1.3 backend, loaded at run time from the system's Vulkan loader.
    Dx12,      ///< The Direct3D 12 backend (Windows).
    Metal,     ///< The Metal backend (macOS, Phase 26 from Day 316; by name only until Day 325).
    Auto,      ///< The platform's preferred backend among those registered and available ([[rhi::PickAutoBackend]]).
};

/// Whether the backend's validation layers are wanted (a development aid, never a requirement).
enum class ValidationSetting : u8
{
    Auto = 0,  ///< Follow `ENGINE_RHI_VALIDATION`; on by default in Debug builds when the layer is installed.
    Off,       ///< Never enable validation.
    On,        ///< Enable validation if the layer is installed.
};

/// Faults a device injects on purpose, to exercise the error paths that real hardware rarely reaches.
///
/// A development and test aid; the defaults inject nothing. The Vulkan and Direct3D 12 backends honor both faults
/// (Direct3D 12 counts each committed resource as one allocation, Vulkan each memory block); the null backend honors
/// `lose_device_at_frame` (it also has `SimulateDeviceLoss`) and ignores `fail_allocations_from`, since it has no
/// device memory.
///
/// - Thread-safety: thread-compatible plain data.
struct FaultInjection
{
    /// Report the device lost at the start of this frame (1 is the first `BeginFrame`), as if the driver returned
    /// `VK_ERROR_DEVICE_LOST`; 0 never does. From then on submissions are ignored and `Device::DeviceLost` is true.
    u32 lose_device_at_frame = 0;
    /// Report the device lost at the Nth `Submit` that submits something (1 is the first), as if the driver returned
    /// `VK_ERROR_DEVICE_LOST` from the queue; that submit and every later one is ignored. 0 never does.
    u32 lose_device_at_submit = 0;
    /// Report the device lost at the Nth swapchain `Present` (1 is the first); that present returns `Lost`. 0 never
    /// does.
    u32 lose_device_at_present = 0;
    /// How many devices an injected loss may strike, counted across devices that share this pointer (a recreated device
    /// is built from the same description): each injected loss decrements it, and at 0 no loss is injected any more.
    /// Null means no limit. Lets a test lose the device twice and then let a recovery succeed. Borrowed; must outlive
    /// the devices.
    Atomic<u32>* strikes_left = nullptr;
    /// Make the Nth device-memory allocation, counted from device creation (1 is the first), and every one after it
    /// fail as if the device ran out of memory; 0 never does. Resource creation then returns an invalid handle.
    u32 fail_allocations_from = 0;
    /// Make `Device::RecreateSurface` fail, as if the adapter could not present to the new surface, so the caller's
    /// fallback to a full reset is exercised. The null backend honors it; the Vulkan backend ignores it.
    bool fail_surface_recreate = false;
    /// Make `Device::CreateSwapchain` fail (returns null), to exercise the rollback of a settings change. The null
    /// backend honors it; the Vulkan backend ignores it.
    bool fail_swapchain_create = false;
};

/// What [[rhi::CreateDevice]] needs to know.
///
/// The device is headless unless a presentation window is supplied (added with the swapchain on Day 56).
///
/// - Thread-safety: thread-compatible plain data.
struct DeviceCreateInfo
{
    /// The backend to create; `Auto` picks the preferred registered backend that is available here.
    Backend backend = Backend::Auto;
    /// The application name reported to the driver; a static string, must not be null.
    const char* application_name = "engine";
    /// Whether to enable the backend's validation layers; see [[rhi::ValidationSetting]].
    ValidationSetting validation = ValidationSetting::Auto;
    /// Also enable synchronization validation (barrier and hazard checks) when validation is on.
    bool sync_validation = false;
    /// Which adapter (GPU) to use: an id from [[rhi::EnumerateAdapters]], or text that [[rhi::MatchAdapter]] accepts
    /// (an index or part of the name). Null means the best one.
    ///
    /// The order of precedence is this field, then `ENGINE_RHI_GPU` (Vulkan only), then the ranking. A requested
    /// adapter that does not exist or is unsuitable makes [[rhi::CreateDevice]] fail with a log line naming the reason;
    /// it never falls back to another adapter, so the caller decides what to do. Borrowed for the call.
    const char* adapter = nullptr;
    /// The window the device will present to, or null for a device that only computes and renders offscreen.
    ///
    /// The Vulkan backend creates the presentation surface from the window's native handles and picks a GPU that can
    /// present to it. The window must outlive the device. The null backend ignores it.
    const os::Window* window = nullptr;
    /// How many frames the CPU may get ahead of the GPU, 1 to 3 (values outside are clamped). `BeginFrame` waits for
    /// the frame this many back to finish. 1 trades throughput for latency; 3 the other way. The null backend ignores
    /// it.
    u32 frames_in_flight = 2;
    /// Whether the device keeps a pipeline cache on disk, so pipelines compile faster on later runs (Vulkan, Direct3D
    /// 12 and Metal, which keeps an `MTLBinaryArchive`).
    ///
    /// The file name carries the adapter's identity, and the file records the driver it was made with; a file that
    /// does not match or is damaged is ignored with a log line. The null backend ignores it.
    bool pipeline_cache = true;
    /// How many bind groups one descriptor pool holds, 16 to 65536 (clamped). The Vulkan backend adds a pool whenever
    /// one runs out, so this sets the granularity of growth, not a limit. The null backend ignores it.
    u32 descriptor_sets_per_pool = 1024;
    /// The directory for the pipeline cache file, or null to use `ENGINE_RHI_CACHE_DIR`, then
    /// the user's cache directory for `application_name` (`$XDG_CACHE_HOME/<application_name>`, then
    /// `$HOME/.cache/<application_name>`). A directory set here or in the environment that cannot be used means no
    /// cache; it never falls back to another one. Borrowed for the call.
    const char* pipeline_cache_dir = nullptr;
    /// Faults to inject on purpose; see [[rhi::FaultInjection]]. Nothing by default.
    FaultInjection faults;
};

namespace detail
{
/// The null backend's adapter list; used by [[rhi::EnumerateAdapters]] and [[rhi::CreateDevice]].
void NullAdapters(AdapterList* out);
}  // namespace detail

/// Creates a device for the requested backend, through the backend registry (`rhi/backend_registry.h`).
///
/// `Auto` resolves to the available registered backend with the highest rank. For the Vulkan backend the physical
/// device is chosen by a ranking (discrete before integrated before virtual before software), among those that have
/// Vulkan 1.3, dynamic rendering, synchronization2, timeline semaphores and a graphics+compute queue.
/// `ENGINE_RHI_GPU=<index or name substring>` overrides the choice; the full ranking is logged on [[rhi::LogRhi]].
/// `DeviceCreateInfo::adapter` overrides both.
///
/// - Param info: what to create.
/// - Returns: the device, or an empty pointer.
/// - Fails: returns an empty pointer, logging the reason, if the backend is not registered in this program or not
/// available
///   here, there is no Vulkan loader, no suitable GPU, the override matches nothing suitable, or device creation fails.
/// - Ownership: the caller owns the device through the returned [[rhi::DevicePtr]].
/// - Thread-safety: thread-safe.
DevicePtr CreateDevice(const DeviceCreateInfo& info);

}  // namespace engine_placeholder::rhi
