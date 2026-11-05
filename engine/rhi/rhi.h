#pragma once

#include "os/window.h"
#include "rhi/device.h"
#include "types.h"

//! Creating a device: the one entry point layers above the RHI use to get a backend.

namespace engine_placeholder::rhi {

/// Which backend [[rhi::CreateDevice]] creates.
enum class Backend : u8 {
    Null = 0, ///< The validating no-GPU device ([[rhi::NullDevice]]).
    Vulkan,   ///< The Vulkan 1.3 backend, loaded at run time from the system's Vulkan loader.
};

/// Whether the backend's validation layers are wanted (a development aid, never a requirement).
enum class ValidationSetting : u8 {
    Auto = 0, ///< Follow `ENGINE_RHI_VALIDATION`; on by default in Debug builds when the layer is installed.
    Off,      ///< Never enable validation.
    On,       ///< Enable validation if the layer is installed.
};

/// Faults a device injects on purpose, to exercise the error paths that real hardware rarely reaches.
///
/// A development and test aid; the defaults inject nothing. The Vulkan backend honors both faults; the null backend
/// honors `lose_device_at_frame` (it also has `SimulateDeviceLoss`) and ignores `fail_allocations_from`, since it has no
/// device memory.
///
/// Thread-safety: thread-compatible plain data.
struct FaultInjection {
    /// Report the device lost at the start of this frame (1 is the first `BeginFrame`), as if the driver returned
    /// `VK_ERROR_DEVICE_LOST`; 0 never does. From then on submissions are ignored and `Device::DeviceLost` is true.
    u32 lose_device_at_frame = 0;
    /// Make the Nth device-memory allocation, counted from device creation (1 is the first), and every one after it
    /// fail as if the device ran out of memory; 0 never does. Resource creation then returns an invalid handle.
    u32 fail_allocations_from = 0;
};

/// What [[rhi::CreateDevice]] needs to know.
///
/// The device is headless unless a presentation window is supplied (added with the swapchain on Day 56).
///
/// Thread-safety: thread-compatible plain data.
struct DeviceCreateInfo {
    /// The backend to create.
    Backend backend = Backend::Vulkan;
    /// The application name reported to the driver; a static string, must not be null.
    const char* application_name = "engine";
    /// Whether to enable the backend's validation layers; see [[rhi::ValidationSetting]].
    ValidationSetting validation = ValidationSetting::Auto;
    /// Also enable synchronization validation (barrier and hazard checks) when validation is on.
    bool sync_validation = false;
    /// The window the device will present to, or null for a device that only computes and renders offscreen.
    ///
    /// The Vulkan backend creates the presentation surface from the window's native handles and picks a GPU that can
    /// present to it. The window must outlive the device. The null backend ignores it.
    const os::Window* window = nullptr;
    /// How many frames the CPU may get ahead of the GPU, 1 to 3 (values outside are clamped). `BeginFrame` waits for the
    /// frame this many back to finish. 1 trades throughput for latency; 3 the other way. The null backend ignores it.
    u32 frames_in_flight = 2;
    /// Whether the Vulkan backend keeps a pipeline cache on disk, so pipelines compile faster on later runs.
    ///
    /// The file name carries the vendor, device and driver identity; a file that does not match or is damaged is
    /// ignored with a log line. The null backend ignores it.
    bool pipeline_cache = true;
    /// How many bind groups one descriptor pool holds, 16 to 65536 (clamped). The Vulkan backend adds a pool whenever
    /// one runs out, so this sets the granularity of growth, not a limit. The null backend ignores it.
    u32 descriptor_sets_per_pool = 1024;
    /// The directory for the pipeline cache file, or null to use `ENGINE_RHI_CACHE_DIR`, then
    /// `$XDG_CACHE_HOME/<application_name>`, then `$HOME/.cache/<application_name>`. Borrowed for the call.
    const char* pipeline_cache_dir = nullptr;
    /// Faults to inject on purpose; see [[rhi::FaultInjection]]. Nothing by default.
    FaultInjection faults;
};

/// Creates a device for the requested backend.
///
/// For the Vulkan backend the physical device is chosen by a ranking (discrete before integrated before virtual
/// before software), among those that have Vulkan 1.3, dynamic rendering, synchronization2, timeline semaphores
/// and a graphics+compute queue. `ENGINE_RHI_GPU=<index or name substring>` overrides the choice; the full ranking
/// is logged on [[rhi::LogRhi]].
///
/// Param info: what to create.
/// Returns: the device, or an empty pointer.
/// Fails: returns an empty pointer, logging the reason, if there is no Vulkan loader, no suitable GPU, the override matches
///   nothing suitable, or device creation fails.
/// Ownership: the caller owns the device through the returned [[rhi::DevicePtr]].
/// Thread-safety: thread-safe.
DevicePtr CreateDevice(const DeviceCreateInfo& info);

} // namespace engine_placeholder::rhi
