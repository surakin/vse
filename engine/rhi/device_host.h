#pragma once

#include <thread>

#include "rhi/rhi.h"
#include "rhi/swapchain.h"
#include "types.h"

//! The owner of a device and its swapchain, which can destroy and re-create both at run time.

namespace engine_placeholder::rhi {

/// Something that owns device objects and must release and rebuild them when the device is replaced.
///
/// Register one with [[rhi::DeviceHost::AddObserver]] and a tier; the renderer's resource registry is the usual observer.
///
/// Thread-safety: owner-thread-only: called on the host's owning thread.
class DeviceObserver {
public:
    virtual ~DeviceObserver() = default;

    /// The device is about to be destroyed: destroy every object you made on it and keep your CPU-side source.
    ///
    /// Called in descending tier order, with the device still alive; the host has waited for it to go idle first.
    ///
    /// Param epoch: the serial of the device being released.
    virtual void OnDeviceReleasing(u32 epoch) = 0;

    /// A new device (and swapchain, if the host has a window) exists: recreate and re-upload what you released.
    ///
    /// Called in ascending tier order. Also called by `DeviceHost::Create` for observers registered before it.
    ///
    /// Param device: the new device.
    /// Param epoch: its serial.
    /// Ownership: the device belongs to the host; do not keep the pointer past `OnDeviceReleasing`.
    /// Returns: false if you could not rebuild; the host then releases every observer again and fails the call.
    virtual bool OnDeviceCreated(Device* device, u32 epoch) = 0;

    /// The swapchain was rebuilt with a different image format or color space (anything built against the old format, such
    /// as the tonemap pipeline, must be rebuilt). Called in ascending tier order, on the owning thread.
    ///
    /// The default does nothing.
    ///
    /// Param swapchain: the swapchain, with its new format.
    /// Ownership: the swapchain belongs to the host.
    virtual void OnSwapchainChanged(Swapchain* swapchain) { (void)swapchain; }

    /// Returns a short name for logs and failure reports.
    ///
    /// Returns: a static string.
    /// Fails: never.
    /// Ownership: the string is static.
    virtual const char* ObserverName() const = 0;
};

/// Where the time of the last `Recreate` went, in milliseconds.
///
/// Thread-safety: thread-compatible plain data.
struct ResetTimings {
    /// Observers releasing their device objects (`OnDeviceReleasing`, all of them).
    double release_ms = 0.0;
    /// Waiting for the GPU, destroying the swapchain and the device (the pipeline cache is saved here).
    double destroy_ms = 0.0;
    /// `CreateDevice`: instance, surface and device.
    double create_device_ms = 0.0;
    /// Creating the swapchain.
    double create_swapchain_ms = 0.0;
    /// Observers rebuilding (`OnDeviceCreated`, all of them: textures re-uploaded, meshes re-uploaded, layouts, shaders, ...).
    double rebuild_ms = 0.0;
};

/// Owns a [[rhi::Device]] and its [[rhi::Swapchain]] and replaces them on request.
///
/// `Recreate` runs: notify observers (`OnDeviceReleasing`, high tier first), `Quiesce`, destroy the swapchain, destroy the
/// device (which saves the pipeline cache), create the device and the swapchain, notify observers (`OnDeviceCreated`, low
/// tier first). A failed creation leaves the host empty and `LastError` says why; the caller can `Create` again (the
/// renderer rolls back to its previous settings that way). Handles of the old device are always invalid on the new one:
/// each device seeds its handle generations from its serial.
///
/// Thread-safety: owner-thread-only: every call comes from the thread that called `Create`, outside `BeginFrame`/`EndFrame`.
/// Ownership: owns the device and the swapchain; observers are borrowed and must outlive their registration (the
///   destructor still notifies them). Defined in the neutral `rhi` library; devices come from the backends the program
///   registered ([[rhi::RegisterCompiledBackends]]).
class DeviceHost {
public:
    /// The most observers.
    static constexpr usize kMaxObservers = 32;

    /// Creates an empty host.
    DeviceHost() = default;

    DeviceHost(const DeviceHost&) = delete;
    DeviceHost& operator=(const DeviceHost&) = delete;

    /// Destroys the swapchain and the device (observers are notified first).
    ~DeviceHost();

    /// Creates the device, and the swapchain when `info.window` is set.
    ///
    /// Param info: what to create; copied. `info.window` and `info.adapter` must stay valid as long as the host may
    ///   `Recreate` with the same info (the host does not keep the strings; pass them again to `Recreate`).
    /// Param swapchain: the swapchain description, used only with a window.
    /// Returns: true on success.
    /// Fails: returns false with `LastError` set and the host empty if the device, the swapchain or an observer fails.
    /// Aborts: the host already holds a device.
    bool Create(const DeviceCreateInfo& info, const SwapchainDesc& swapchain);

    /// Replaces the device and swapchain; see the class description for the sequence.
    ///
    /// Param info: the new device description.
    /// Param swapchain: the new swapchain description.
    /// Returns: true on success.
    /// Fails: returns false with `LastError` set and the host empty (observers have released); `Create` again to roll back.
    /// Aborts: called off the owning thread, or between `BeginFrame` and `EndFrame`.
    bool Recreate(const DeviceCreateInfo& info, const SwapchainDesc& swapchain);

    /// Changes the swapchain's present mode, size, format or HDR mode without touching the device.
    ///
    /// Observers hear `OnSwapchainChanged` when the image format or color space changed. A request the surface cannot
    /// grant falls back (see [[rhi::Swapchain::Reconfigure]]).
    ///
    /// Param swapchain: the new description.
    /// Returns: the status of [[rhi::Swapchain::Reconfigure]]; `Lost` if the host has no swapchain.
    /// Fails: `Lost` as described.
    /// Aborts: called off the owning thread or between `BeginFrame` and `EndFrame`.
    SwapchainStatus ReconfigureSwapchain(const SwapchainDesc& swapchain);

    /// Replaces the presentation surface, keeping the device when the adapter can present to the new one.
    ///
    /// Destroys the swapchain, asks the device for a new surface ([[rhi::Device::RecreateSurface]]) and creates the swapchain
    /// again; if the device cannot, it does a full `Recreate` with the description of the last `Create` or `Recreate`
    /// (`SurfaceFallbackCount` counts those).
    ///
    /// Param window: the window to present to; must outlive the host's device.
    /// Param swapchain: the swapchain description.
    /// Returns: true on success.
    /// Fails: returns false with `LastError` set and the host empty if even the full reset fails.
    /// Aborts: called off the owning thread or between `BeginFrame` and `EndFrame`.
    bool RecreateSurface(const os::Window* window, const SwapchainDesc& swapchain);

    /// Returns how many device objects were still alive, after every observer released, when devices were destroyed. Any is a leak
    /// in an owner that did not release; each is also logged as an error. Command lists are not counted: the last frame's lists are
    /// recycled by the next `BeginFrame` and die with the device.
    u32 LeakedObjects() const { return leaked_objects_; }

    /// Returns how many times `RecreateSurface` had to fall back to a full reset.
    u32 SurfaceFallbackCount() const { return surface_fallbacks_; }

    /// Notifies observers, then destroys the swapchain and the device; the host is empty afterwards.
    ///
    /// Aborts: called off the owning thread or between `BeginFrame` and `EndFrame`.
    void Destroy();

    /// Returns the device, or null when the host is empty.
    ///
    /// Fails: returns null when the host is empty.
    /// Ownership: the host owns the device; the pointer is invalid after `Recreate` or `Destroy`.
    Device* GetDevice() const { return device_.Get(); }
    /// Returns the swapchain, or null when there is none.
    ///
    /// Fails: returns null without a window or when the host is empty.
    /// Ownership: the host owns the swapchain; the pointer is invalid after `Recreate` or `Destroy`.
    Swapchain* GetSwapchain() const { return swapchain_; }
    /// Returns the current device's serial, or 0 when the host is empty.
    u32 Epoch() const { return device_ ? device_->Epoch() : 0; }
    /// Returns how many times `Recreate` succeeded.
    u32 ResetCount() const { return reset_count_; }
    /// Returns where the time of the last `Create` or `Recreate` went.
    /// Fails: never.
    const ResetTimings& LastTimings() const { return timings_; }

    /// Returns the reason the last `Create` or `Recreate` failed, or an empty string.
    ///
    /// Fails: never.
    /// Ownership: the string belongs to the host and changes on the next call.
    const char* LastError() const { return error_; }

    /// Registers an observer.
    ///
    /// Param observer: borrowed; must stay alive until `RemoveObserver`.
    /// Param tier: its order; 0 is created first and released last.
    /// Returns: true if registered.
    /// Fails: returns false when the table is full.
    /// Aborts: called while a reset is running.
    /// Ownership: the host borrows the observer.
    bool AddObserver(DeviceObserver* observer, u32 tier);

    /// Unregisters an observer; unknown observers are ignored. An observer that holds device objects is told
    /// `OnDeviceReleasing` first, so it never leaves objects behind.
    ///
    /// Aborts: called while a reset is running.
    /// Ownership: the host stops borrowing the observer.
    void RemoveObserver(DeviceObserver* observer);

private:
    struct Entry {
        DeviceObserver* observer = nullptr;
        u32 tier = 0;
        u32 sequence = 0;
        bool created = false; // got OnDeviceCreated and not yet OnDeviceReleasing
    };

    void NotifySwapchainChanged();
    bool CreateInternal(const DeviceCreateInfo& info, const SwapchainDesc& swapchain);
    void ReleaseObservers();
    void DestroyDevice();
    void SetError(const char* text);
    void PublishCrashState();

    DevicePtr device_;
    Swapchain* swapchain_ = nullptr;
    Entry entries_[kMaxObservers];
    usize entry_count_ = 0;
    u32 next_sequence_ = 0;
    u32 reset_count_ = 0;
    u32 surface_fallbacks_ = 0;
    u32 leaked_objects_ = 0;
    ResetTimings timings_;
    DeviceCreateInfo last_info_;
    bool resetting_ = false;
    bool has_owner_ = false;
    std::thread::id owner_;
    char error_[160] = {};
};

} // namespace engine_placeholder::rhi
