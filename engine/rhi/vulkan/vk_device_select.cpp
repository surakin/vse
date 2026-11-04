#include "rhi/vulkan/vk_device_select.h"

#include <vulkan/vulkan_core.h>

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

char Lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool ContainsIgnoreCase(const char* haystack, const char* needle) {
    for (usize i = 0; haystack[i] != '\0'; ++i) {
        usize j = 0;
        while (needle[j] != '\0' && haystack[i + j] != '\0' && Lower(haystack[i + j]) == Lower(needle[j])) {
            ++j;
        }
        if (needle[j] == '\0') {
            return true;
        }
    }
    return false;
}

// Parses a non-empty all-digit string; returns -1 if it is not one.
long ParseIndex(const char* text) {
    long value = 0;
    if (text[0] == '\0') {
        return -1;
    }
    for (usize i = 0; text[i] != '\0'; ++i) {
        if (text[i] < '0' || text[i] > '9' || value > 100000) {
            return -1;
        }
        value = value * 10 + (text[i] - '0');
    }
    return value;
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

int SelectPhysicalDevice(const PhysicalDeviceInfo* devices, usize count, bool presentable, const char* override_text) {
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
        int match = -1;
        long index = ParseIndex(override_text);
        if (index >= 0 && static_cast<usize>(index) < count) {
            match = static_cast<int>(index);
        } else {
            for (usize i = 0; i < count && match < 0; ++i) {
                if (ContainsIgnoreCase(devices[i].name, override_text)) {
                    match = static_cast<int>(i);
                }
            }
        }
        if (match < 0) {
            RhiLog(LogVerbosity::Error, "Vulkan: ENGINE_RHI_GPU='%s' matches none of the %zu GPUs", override_text, count);
            return -1;
        }
        if (reasons[match] != nullptr) {
            RhiLog(LogVerbosity::Error, "Vulkan: ENGINE_RHI_GPU='%s' selects GPU %d '%s', which %s", override_text, match,
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
