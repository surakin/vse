#pragma once

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
};

/// Creates a device for the requested backend.
///
/// For the Vulkan backend the physical device is chosen by a ranking (discrete before integrated before virtual
/// before software), among those that have Vulkan 1.3, dynamic rendering, synchronization2, timeline semaphores
/// and a graphics+compute queue. `ENGINE_RHI_GPU=<index or name substring>` overrides the choice; the full ranking
/// is logged on [[rhi::LogRhi]].
///
/// Param info: what to create.
/// Returns: the device, or null.
/// Fails: returns null, logging the reason, if there is no Vulkan loader, no suitable GPU, the override matches
///   nothing suitable, or device creation fails.
/// Ownership: the caller owns the device and releases it with [[rhi::DestroyDevice]].
/// Thread-safety: thread-safe.
Device* CreateDevice(const DeviceCreateInfo& info);

} // namespace engine_placeholder::rhi
