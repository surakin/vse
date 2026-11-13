// The adapters of the Metal backend (Phase 26 Day 316; metal_adapters.h).
#include "rhi/metal/metal_adapters.h"

#include "cstr/cstr.h"
#include "cstr/format.h"
#include "sync/mutex.h"

#include "containers/array.h"

#import <Foundation/Foundation.h>

namespace engine_placeholder::rhi::metal
{

namespace
{

void CopyText(char* out, usize size, NSString* text)
{
    const char* utf8 = text != nil ? text.UTF8String : "";
    FormatTo(out, size, "%s", utf8 != nullptr ? utf8 : "");
}

u32 KindRank(AdapterKind kind)
{
    switch (kind)
    {
    case AdapterKind::Discrete: return 3;
    case AdapterKind::Integrated: return 2;
    case AdapterKind::Virtual: return 1;
    default: return 0;
    }
}

}  // namespace

AdapterInfo MetalAdapterInfo(id<MTLDevice> device)
{
    AdapterInfo info;
    FormatTo(info.id, sizeof(info.id), "%016llx", static_cast<unsigned long long>(device.registryID));
    CopyText(info.name, sizeof(info.name), device.name);
    // Unified memory (Intel integrated, Apple silicon) shares the system's; a removable GPU is an eGPU.
    info.kind = device.hasUnifiedMemory || device.isLowPower ? AdapterKind::Integrated : AdapterKind::Discrete;
    info.dedicated_memory_bytes = device.hasUnifiedMemory ? 0 : static_cast<u64>(device.recommendedMaxWorkingSetSize);
    CopyText(info.driver, sizeof(info.driver), [NSProcessInfo processInfo].operatingSystemVersionString);
    info.can_present = !device.isHeadless;
    info.suitable = [device supportsFamily:MTLGPUFamilyMac2] && [device supportsFamily:MTLGPUFamilyMetal3];
    info.unsuitable_reason = info.suitable ? nullptr : "the GPU lacks the Mac2 family or Metal 3";
    return info;
}

namespace
{

// The GPUs the system asked this process to let go of (an external GPU being ejected), by registry ID: left out of
// every later pick, so the reset after the loss does not take the same GPU again.
Mutex g_removal_mutex;
Array<u64, 16> g_removal_requested = {};
u32 g_removal_count = 0;

}  // namespace

void NoteMetalRemovalRequested(u64 registry_id)
{
    LockGuard<Mutex> lock(g_removal_mutex);
    for (u32 i = 0; i < g_removal_count; ++i)
    {
        if (g_removal_requested[i] == registry_id)
        {
            return;
        }
    }
    if (g_removal_count < g_removal_requested.Size())
    {
        g_removal_requested[g_removal_count++] = registry_id;
    }
}

bool MetalRemovalRequested(u64 registry_id)
{
    LockGuard<Mutex> lock(g_removal_mutex);
    for (u32 i = 0; i < g_removal_count; ++i)
    {
        if (g_removal_requested[i] == registry_id)
        {
            return true;
        }
    }
    return false;
}

bool ListMetalAdapters(MetalAdapterSet* out)
{
    *out = MetalAdapterSet{};
    NSArray<id<MTLDevice>>* devices = MTLCopyAllDevices();
    if (devices == nil || devices.count == 0)
    {
        return false;
    }
    NSMutableArray<id<MTLDevice>>* kept = [NSMutableArray array];
    for (id<MTLDevice> device in devices)
    {
        if (out->list.count == kMaxAdapters)
        {
            break;
        }
        AdapterInfo info = MetalAdapterInfo(device);
        if (info.suitable && MetalRemovalRequested(device.registryID))
        {
            info.suitable = false;
            info.unsuitable_reason = "the system asked to remove this GPU";
        }
        out->list.items[out->list.count++] = info;
        [kept addObject:device];
    }
    out->devices = kept;
    return true;
}

int PickMetalAdapter(const MetalAdapterSet& set, const char* request, char* reason, usize reason_size)
{
    if (request != nullptr && request[0] != '\0')
    {
        const int match = MatchAdapter(set.list.items, set.list.count, request);
        if (match < 0)
        {
            FormatTo(reason, reason_size, "no Metal GPU matches '%s'", request);
            return -1;
        }
        if (!set.list.items[match].suitable)
        {
            FormatTo(reason, reason_size, "the Metal GPU '%s' is not suitable: %s", set.list.items[match].name,
                     set.list.items[match].unsuitable_reason);
            return -1;
        }
        return match;
    }
    int best = -1;
    for (u32 i = 0; i < set.list.count; ++i)
    {
        const AdapterInfo& a = set.list.items[i];
        if (a.suitable && (best < 0 || KindRank(a.kind) > KindRank(set.list.items[best].kind)))
        {
            best = static_cast<int>(i);
        }
    }
    if (best < 0)
    {
        FormatTo(reason, reason_size, "no Metal GPU of this Mac supports the Mac2 family and Metal 3");
    }
    return best;
}

}  // namespace engine_placeholder::rhi::metal
