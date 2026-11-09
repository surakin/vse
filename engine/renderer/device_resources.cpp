#include "renderer/device_resources.h"

#include "cstr/format.h"

#include "log/assert.h"
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
    ReleaseLive(0);
    device_ = nullptr;
}

void DeviceResources::ReleaseFrom(u32 first_tier) {
    resetting_ = true;
    ReleaseLive(first_tier);
    resetting_ = false;
    Compact();
}

bool DeviceResources::RecreateFrom(u32 first_tier) {
    ENGINE_ASSERT(device_ != nullptr, "DeviceResources::RecreateFrom: there is no current device");
    resetting_ = true;
    failed_name_[0] = '\0';
    bool ok = true;
    for (;;) {
        Entry* pick = nullptr;
        for (usize i = 0; i < count_; ++i) {
            Entry& e = entries_[i];
            if (e.owner != nullptr && !e.live && e.tier >= first_tier &&
                (pick == nullptr || e.tier < pick->tier || (e.tier == pick->tier && e.sequence < pick->sequence))) {
                pick = &e;
            }
        }
        if (pick == nullptr) {
            break;
        }
        pick->live = true;
        if (!pick->owner->Recreate(device_)) {
            FormatTo(failed_name_, sizeof(failed_name_), "%s", pick->owner->RecreatableName());
            RenderLog(LogVerbosity::Error, "DeviceResources: '%s' could not be recreated", failed_name_);
            pick->live = false;
            ok = false;
            break;
        }
    }
    if (!ok) {
        ReleaseLive(first_tier);
    }
    resetting_ = false;
    Compact();
    return ok;
}

void DeviceResources::ReleaseLive(u32 first_tier) {
    // Highest tier first; within a tier the later registration first.
    for (;;) {
        Entry* pick = nullptr;
        for (usize i = 0; i < count_; ++i) {
            Entry& e = entries_[i];
            if (e.owner != nullptr && e.live && e.tier >= first_tier && (pick == nullptr || e.tier > pick->tier || (e.tier == pick->tier && e.sequence > pick->sequence))) {
                pick = &e;
            }
        }
        if (pick == nullptr) {
            break;
        }
        pick->live = false;
        pick->owner->Release();
    }
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
    device_ = device;
    if (!RecreateFrom(0)) {
        device_ = nullptr; // everything is released again; a half-built set is never used
        return false;
    }
    return true;
}

} // namespace engine_placeholder::renderer
