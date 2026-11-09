#include "rhi/device_host.h"

#include <cstdio>
#include <cstring>

#include "debug/crash_state.h"
#include "log/assert.h"
#include "os/os.h"
#include "rhi/rhi_log.h"
#include "sync/thread.h"

namespace engine_placeholder::rhi {

DeviceHost::~DeviceHost() {
    if (device_) {
        resetting_ = true;
        ReleaseObservers();
        DestroyDevice();
    }
}

void DeviceHost::SetError(const char* text) {
    std::snprintf(error_, sizeof(error_), "%s", text);
    RhiLog(LogVerbosity::Error, "DeviceHost: %s", error_);
}

void DeviceHost::PublishCrashState() {
    char text[64];
    std::snprintf(text, sizeof(text), "epoch %u, %u reset(s)", Epoch(), reset_count_);
    debug::SetCrashStateText("rhi_device", device_ ? text : "none");
}

void DeviceHost::ReleaseObservers() {
    if (device_) {
        device_->Quiesce(); // observers see an idle device
    }
    // Descending tier; among equal tiers, the later registration releases first.
    for (;;) {
        Entry* pick = nullptr;
        for (usize i = 0; i < entry_count_; ++i) {
            Entry& e = entries_[i];
            if (!e.created) {
                continue;
            }
            if (pick == nullptr || e.tier > pick->tier || (e.tier == pick->tier && e.sequence > pick->sequence)) {
                pick = &e;
            }
        }
        if (pick == nullptr) {
            break;
        }
        pick->created = false;
        pick->observer->OnDeviceReleasing(device_ ? device_->Epoch() : 0);
    }
}

void DeviceHost::DestroyDevice() {
    if (device_) {
        device_->Quiesce();
        if (swapchain_ != nullptr) {
            device_->DestroySwapchain(swapchain_);
            swapchain_ = nullptr;
        }
        const ResourceCounts counts = device_->Resources();
        // Command lists of the last frame are recycled by the next BeginFrame and die with the device; they are not leaks.
        const usize leaked = counts.UserObjects() - counts.command_lists;
        if (leaked != 0) {
            leaked_objects_ += static_cast<u32>(leaked);
            RhiLog(LogVerbosity::Error,
                   "DeviceHost: the device still holds %zu object(s) (buffers %zu, textures %zu, samplers %zu, shaders %zu, "
                   "layouts %zu, bind groups %zu, pipelines %zu) after the observers released",
                   leaked, counts.buffers, counts.textures, counts.samplers, counts.shaders,
                   counts.bind_group_layouts, counts.bind_groups, counts.pipelines);
        }
        device_.Reset();
    }
    swapchain_ = nullptr;
    PublishCrashState();
}

namespace {
double NowMs() { return static_cast<double>(os::MonotonicTimeNanos()) / 1.0e6; }
} // namespace

bool DeviceHost::CreateInternal(const DeviceCreateInfo& info, const SwapchainDesc& swapchain) {
    error_[0] = '\0';
    last_info_ = info;
    double t = NowMs();
    device_ = CreateDevice(info);
    timings_.create_device_ms = NowMs() - t;
    if (!device_) {
        SetError("the device could not be created");
        return false;
    }
    // A window is needed for a real swapchain; the null backend can make a windowless one when a size is given.
    t = NowMs();
    if (info.window != nullptr || (info.backend == Backend::Null && swapchain.width != 0)) {
        swapchain_ = device_->CreateSwapchain(swapchain);
        if (swapchain_ == nullptr) {
            SetError("the swapchain could not be created");
            DestroyDevice();
            return false;
        }
    }
    timings_.create_swapchain_ms = NowMs() - t;
    t = NowMs();
    // Ascending tier; among equal tiers, registration order.
    for (;;) {
        Entry* pick = nullptr;
        for (usize i = 0; i < entry_count_; ++i) {
            Entry& e = entries_[i];
            if (e.created) {
                continue;
            }
            if (pick == nullptr || e.tier < pick->tier || (e.tier == pick->tier && e.sequence < pick->sequence)) {
                pick = &e;
            }
        }
        if (pick == nullptr) {
            break;
        }
        pick->created = true;
        if (!pick->observer->OnDeviceCreated(device_.Get(), device_->Epoch())) {
            char text[128];
            std::snprintf(text, sizeof(text), "'%s' could not rebuild its device objects", pick->observer->ObserverName());
            SetError(text);
            // The failing observer released whatever it made; mark it created so it is released once more (harmless).
            ReleaseObservers();
            DestroyDevice();
            return false;
        }
    }
    timings_.rebuild_ms = NowMs() - t;
    PublishCrashState();
    return true;
}

void DeviceHost::NotifySwapchainChanged() {
    bool done[kMaxObservers] = {};
    for (;;) {
        usize pick = kMaxObservers;
        for (usize i = 0; i < entry_count_; ++i) {
            if (done[i] || !entries_[i].created) {
                continue;
            }
            if (pick == kMaxObservers || entries_[i].tier < entries_[pick].tier ||
                (entries_[i].tier == entries_[pick].tier && entries_[i].sequence < entries_[pick].sequence)) {
                pick = i;
            }
        }
        if (pick == kMaxObservers) {
            break;
        }
        done[pick] = true;
        entries_[pick].observer->OnSwapchainChanged(swapchain_);
    }
}

SwapchainStatus DeviceHost::ReconfigureSwapchain(const SwapchainDesc& swapchain) {
    ENGINE_ASSERT(!has_owner_ || owner_ == ThisThread::Id(), "DeviceHost::ReconfigureSwapchain: called off the owning thread");
    ENGINE_ASSERT(!device_ || !device_->InFrame(), "DeviceHost::ReconfigureSwapchain: called between BeginFrame and EndFrame");
    if (!device_ || swapchain_ == nullptr) {
        return SwapchainStatus::Lost;
    }
    device_->Quiesce();
    const Format old_format = swapchain_->ImageFormat();
    const ColorSpace old_space = swapchain_->GetColorSpace();
    const SwapchainStatus status = swapchain_->Reconfigure(swapchain);
    // A minimized window rebuilds later, but the swapchain already reports the format it will have: tell the observers now.
    if ((status == SwapchainStatus::Ok || status == SwapchainStatus::Minimized) &&
        (swapchain_->ImageFormat() != old_format || swapchain_->GetColorSpace() != old_space)) {
        NotifySwapchainChanged();
    }
    return status;
}

bool DeviceHost::RecreateSurface(const os::Window* window, const SwapchainDesc& swapchain) {
    ENGINE_ASSERT(!has_owner_ || owner_ == ThisThread::Id(), "DeviceHost::RecreateSurface: called off the owning thread");
    ENGINE_ASSERT(!device_ || !device_->InFrame(), "DeviceHost::RecreateSurface: called between BeginFrame and EndFrame");
    DeviceCreateInfo info = last_info_;
    info.window = window;
    Format old_format = Format::Undefined;
    ColorSpace old_space = ColorSpace::SrgbNonlinear;
    if (device_) {
        device_->Quiesce();
        if (swapchain_ != nullptr) {
            old_format = swapchain_->ImageFormat();
            old_space = swapchain_->GetColorSpace();
            device_->DestroySwapchain(swapchain_);
            swapchain_ = nullptr;
        }
        if (device_->RecreateSurface(window)) {
            swapchain_ = device_->CreateSwapchain(swapchain);
            if (swapchain_ != nullptr) {
                last_info_ = info;
                if (swapchain_->ImageFormat() != old_format || swapchain_->GetColorSpace() != old_space) {
                    NotifySwapchainChanged();
                }
                return true;
            }
        }
    }
    ++surface_fallbacks_;
    return Recreate(info, swapchain);
}

bool DeviceHost::Create(const DeviceCreateInfo& info, const SwapchainDesc& swapchain) {
    ENGINE_ASSERT(!device_, "DeviceHost::Create: the host already holds a device");
    timings_ = ResetTimings{};
    owner_ = ThisThread::Id();
    has_owner_ = true;
    resetting_ = true;
    const bool ok = CreateInternal(info, swapchain);
    resetting_ = false;
    return ok;
}

bool DeviceHost::Recreate(const DeviceCreateInfo& info, const SwapchainDesc& swapchain) {
    ENGINE_ASSERT(!has_owner_ || owner_ == ThisThread::Id(), "DeviceHost::Recreate: called off the owning thread");
    ENGINE_ASSERT(!device_ || !device_->InFrame(), "DeviceHost::Recreate: called between BeginFrame and EndFrame");
    if (!has_owner_) {
        owner_ = ThisThread::Id();
        has_owner_ = true;
    }
    resetting_ = true;
    timings_ = ResetTimings{};
    if (device_) {
        double t = NowMs();
        ReleaseObservers();
        timings_.release_ms = NowMs() - t;
        t = NowMs();
        DestroyDevice();
        timings_.destroy_ms = NowMs() - t;
    }
    const bool ok = CreateInternal(info, swapchain);
    if (ok) {
        ++reset_count_;
        PublishCrashState();
    }
    resetting_ = false;
    return ok;
}

void DeviceHost::Destroy() {
    ENGINE_ASSERT(!has_owner_ || owner_ == ThisThread::Id(), "DeviceHost::Destroy: called off the owning thread");
    ENGINE_ASSERT(!device_ || !device_->InFrame(), "DeviceHost::Destroy: called between BeginFrame and EndFrame");
    resetting_ = true;
    if (device_) {
        ReleaseObservers();
        DestroyDevice();
    }
    resetting_ = false;
}

bool DeviceHost::AddObserver(DeviceObserver* observer, u32 tier) {
    ENGINE_ASSERT(!resetting_, "DeviceHost::AddObserver: called while a reset is running");
    if (entry_count_ >= kMaxObservers) {
        return false;
    }
    Entry& e = entries_[entry_count_++];
    e.observer = observer;
    e.tier = tier;
    e.sequence = next_sequence_++;
    // An observer added to a host that already has a device is told about it at once.
    e.created = false;
    if (device_) {
        e.created = true;
        if (!observer->OnDeviceCreated(device_.Get(), device_->Epoch())) {
            e.created = false;
            --entry_count_;
            return false;
        }
    }
    return true;
}

void DeviceHost::RemoveObserver(DeviceObserver* observer) {
    ENGINE_ASSERT(!resetting_, "DeviceHost::RemoveObserver: called while a reset is running");
    for (usize i = 0; i < entry_count_; ++i) {
        if (entries_[i].observer == observer) {
            if (entries_[i].created) {
                if (device_) {
                    device_->Quiesce();
                }
                entries_[i].created = false;
                observer->OnDeviceReleasing(device_ ? device_->Epoch() : 0); // it must not leave objects behind
            }
            entries_[i] = entries_[entry_count_ - 1];
            --entry_count_;
            return;
        }
    }
}

} // namespace engine_placeholder::rhi
