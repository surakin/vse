#include "rhi/vulkan/vk_device_select.h"

#include <vulkan/vulkan_core.h>

#include <cstdio>
#include <cstring>

#include "rhi/adapter.h"
#include "rhi/rhi_log.h"

namespace engine_placeholder::rhi::vulkan {

namespace {

const char* KindName(GpuKind kind) {
    switch (kind) {
    case GpuKind::Discrete: return "discrete";
    case GpuKind::Integrated: return "integrated";
    case GpuKind::Virtual: return "virtual";
    case GpuKind::Cpu: return "cpu";
    case GpuKind::Other: break;
    }
    return "other";
}

bool FamilyUsable(const QueueFamilyInfo& f, bool presentable) {
    return f.queue_count > 0 && f.graphics && f.compute && (!presentable || f.present);
}

} // namespace

const char* RejectionReason(const PhysicalDeviceInfo& info, bool presentable) {
    if (info.api_version < VK_API_VERSION_1_3) {
        return "supports less than Vulkan 1.3";
    }
    if (!info.dynamic_rendering) {
        return "lacks dynamic rendering";
    }
    if (!info.synchronization2) {
        return "lacks synchronization2";
    }
    if (!info.timeline_semaphore) {
        return "lacks timeline semaphores";
    }
    if (presentable && !info.swapchain_extension) {
        return "lacks VK_KHR_swapchain";
    }
    if (PlanQueues(info, presentable).graphics_family < 0) {
        return presentable ? "has no graphics+compute queue family that can present"
                           : "has no graphics+compute queue family";
    }
    return nullptr;
}

QueuePlan PlanQueues(const PhysicalDeviceInfo& info, bool presentable) {
    QueuePlan plan;
    const u32 count = info.queue_family_count < kMaxQueueFamilies ? info.queue_family_count : static_cast<u32>(kMaxQueueFamilies);
    for (u32 i = 0; i < count; ++i) {
        if (FamilyUsable(info.families[i], presentable)) {
            plan.graphics_family = static_cast<int>(i);
            break;
        }
    }
    if (plan.graphics_family < 0) {
        return plan;
    }
    plan.transfer_family = plan.graphics_family;
    for (u32 i = 0; i < count; ++i) {
        const QueueFamilyInfo& f = info.families[i];
        if (f.queue_count > 0 && f.transfer && !f.graphics && !f.compute) {
            plan.transfer_family = static_cast<int>(i);
            break;
        }
    }
    return plan;
}

void MakeAdapterId(const PhysicalDeviceInfo& info, char* out, usize capacity) {
    static const char kHex[] = "0123456789abcdef";
    char uuid[33];
    for (usize i = 0; i < 16; ++i) {
        uuid[i * 2] = kHex[info.uuid[i] >> 4];
        uuid[i * 2 + 1] = kHex[info.uuid[i] & 15];
    }
    uuid[32] = '\0';
    std::snprintf(out, capacity, "vk-%04x-%04x-%s", info.vendor_id & 0xFFFFu, info.device_id & 0xFFFFu, uuid);
}

AdapterInfo MakeAdapterInfo(const PhysicalDeviceInfo& info, bool presentable) {
    AdapterInfo out;
    MakeAdapterId(info, out.id, sizeof(out.id));
    if (info.luid_valid) {
        // A LUID is { DWORD LowPart; LONG HighPart; }: the text is the high part, then the low part.
        const u32 low = static_cast<u32>(info.luid[0]) | (static_cast<u32>(info.luid[1]) << 8) | (static_cast<u32>(info.luid[2]) << 16) |
                        (static_cast<u32>(info.luid[3]) << 24);
        const u32 high = static_cast<u32>(info.luid[4]) | (static_cast<u32>(info.luid[5]) << 8) | (static_cast<u32>(info.luid[6]) << 16) |
                         (static_cast<u32>(info.luid[7]) << 24);
        std::snprintf(out.luid, sizeof(out.luid), "%08x%08x", high, low);
    }
    std::strncpy(out.name, info.name, sizeof(out.name) - 1);
    switch (info.kind) {
    case GpuKind::Discrete: out.kind = AdapterKind::Discrete; break;
    case GpuKind::Integrated: out.kind = AdapterKind::Integrated; break;
    case GpuKind::Virtual: out.kind = AdapterKind::Virtual; break;
    case GpuKind::Cpu: out.kind = AdapterKind::Software; break;
    case GpuKind::Other: out.kind = AdapterKind::Other; break;
    }
    out.dedicated_memory_bytes = info.dedicated_memory_bytes;
    std::strncpy(out.driver, info.driver, sizeof(out.driver) - 1);
    out.unsuitable_reason = RejectionReason(info, presentable);
    out.suitable = out.unsuitable_reason == nullptr;
    out.can_present = presentable && PlanQueues(info, true).graphics_family >= 0 && info.swapchain_extension;
    return out;
}

int SelectPhysicalDevice(const PhysicalDeviceInfo* devices, usize count, bool presentable, const char* override_text) {
    return SelectPhysicalDeviceFor(devices, count, presentable, override_text, "ENGINE_RHI_GPU");
}

int SelectPhysicalDeviceFor(const PhysicalDeviceInfo* devices, usize count, bool presentable, const char* override_text,
                            const char* source) {
    if (count > kMaxPhysicalDevices) {
        count = kMaxPhysicalDevices;
    }
    const char* reasons[kMaxPhysicalDevices];
    int best = -1;
    for (usize i = 0; i < count; ++i) {
        reasons[i] = RejectionReason(devices[i], presentable);
        if (reasons[i] == nullptr && (best < 0 || devices[i].kind > devices[best].kind)) {
            best = static_cast<int>(i);
        }
    }

    for (usize i = 0; i < count; ++i) {
        if (reasons[i] != nullptr) {
            RhiLog(LogVerbosity::Info, "Vulkan: GPU %zu '%s' (%s) rejected: %s", i, devices[i].name,
                   KindName(devices[i].kind), reasons[i]);
        } else {
            RhiLog(LogVerbosity::Info, "Vulkan: GPU %zu '%s' (%s) is suitable", i, devices[i].name, KindName(devices[i].kind));
        }
    }

    int chosen = best;
    if (override_text != nullptr && override_text[0] != '\0') {
        AdapterInfo adapters[kMaxPhysicalDevices];
        for (usize i = 0; i < count; ++i) {
            adapters[i] = MakeAdapterInfo(devices[i], presentable);
        }
        const int match = MatchAdapter(adapters, count, override_text);
        if (match < 0) {
            RhiLog(LogVerbosity::Error, "Vulkan: %s='%s' matches none of the %zu GPUs", source, override_text, count);
            return -1;
        }
        if (reasons[match] != nullptr) {
            RhiLog(LogVerbosity::Error, "Vulkan: %s='%s' selects GPU %d '%s', which %s", source, override_text, match,
                   devices[match].name, reasons[match]);
            return -1;
        }
        chosen = match;
    }

    if (chosen < 0) {
        RhiLog(LogVerbosity::Error, "Vulkan: no suitable GPU among %zu (need Vulkan 1.3, dynamic rendering, synchronization2, "
               "timeline semaphores and a graphics+compute queue)", count);
        return -1;
    }
    RhiLog(LogVerbosity::Info, "Vulkan: selected GPU %d '%s'", chosen, devices[chosen].name);
    return chosen;
}

} // namespace engine_placeholder::rhi::vulkan
