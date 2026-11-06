#include "renderer/device_resources.h"

#include "renderer/renderer_log.h"

namespace engine_placeholder::renderer {

bool DeviceResources::Register(Recreatable* owner, u32 tier) {
    if (resetting_) {
        RenderLog(LogVerbosity::Error, "DeviceResources: '%s' cannot register during a reset", owner->RecreatableName());
        return false;
    }
    if (count_ >= kMaxOwners) {
        RenderLog(LogVerbosity::Error, "DeviceResources: the table of %zu owners is full", kMaxOwners);
        return false;
    }
    Entry& e = entries_[count_++];
    e.owner = owner;
    e.tier = tier;
    e.sequence = next_sequence_++;
    e.live = owner->Live();
    return true;
}

void DeviceResources::Compact() {
    usize out = 0;
    for (usize i = 0; i < count_; ++i) {
        if (entries_[i].owner != nullptr) {
            entries_[out++] = entries_[i];
        }
    }
    count_ = out;
    compact_pending_ = false;
}

void DeviceResources::Unregister(Recreatable* owner) {
    for (usize i = 0; i < count_; ++i) {
        if (entries_[i].owner == owner) {
            if (resetting_) {
                entries_[i].owner = nullptr; // removed when the reset step ends; the loops skip it
                entries_[i].live = false;
                compact_pending_ = true;
                return;
            }
            // Keep registration order for equal tiers: shift down instead of swapping.
            for (usize j = i + 1; j < count_; ++j) {
                entries_[j - 1] = entries_[j];
            }
            --count_;
            if (failed_name_ != nullptr && owner->RecreatableName() == failed_name_) {
                failed_name_ = nullptr; // the owner is going away; its name must not dangle
            }
            return;
        }
    }
}

void DeviceResources::Rebind(Recreatable* from, Recreatable* to) {
    for (usize i = 0; i < count_; ++i) {
        if (entries_[i].owner == from && from != nullptr) {
            entries_[i].owner = to;
            return;
        }
    }
}

void DeviceResources::ReleaseAllLive() {
    // Highest tier first; within a tier the later registration first.
    for (;;) {
        Entry* pick = nullptr;
        for (usize i = 0; i < count_; ++i) {
            Entry& e = entries_[i];
            if (e.owner != nullptr && e.live && (pick == nullptr || e.tier > pick->tier || (e.tier == pick->tier && e.sequence > pick->sequence))) {
                pick = &e;
            }
        }
        if (pick == nullptr) {
            break;
        }
        pick->live = false;
        pick->owner->Release();
    }
    device_ = nullptr;
}

void DeviceResources::OnDeviceReleasing(u32 epoch) {
    (void)epoch;
    resetting_ = true;
    ReleaseAllLive();
    resetting_ = false;
    Compact();
}

bool DeviceResources::OnDeviceCreated(rhi::Device* device, u32 epoch) {
    (void)epoch;
    resetting_ = true;
    failed_name_ = nullptr;
    device_ = device;
    bool ok = true;
    for (;;) {
        Entry* pick = nullptr;
        for (usize i = 0; i < count_; ++i) {
            Entry& e = entries_[i];
            if (e.owner != nullptr && !e.live && (pick == nullptr || e.tier < pick->tier || (e.tier == pick->tier && e.sequence < pick->sequence))) {
                pick = &e;
            }
        }
        if (pick == nullptr) {
            break;
        }
        pick->live = true;
        if (!pick->owner->Recreate(device)) {
            failed_name_ = pick->owner->RecreatableName();
            RenderLog(LogVerbosity::Error, "DeviceResources: '%s' could not be recreated", failed_name_);
            pick->live = false; // it cleaned up after itself
            ok = false;
            break;
        }
    }
    if (!ok) {
        ReleaseAllLive(); // a half-built set is never used
    }
    resetting_ = false;
    Compact();
    return ok;
}

} // namespace engine_placeholder::renderer
