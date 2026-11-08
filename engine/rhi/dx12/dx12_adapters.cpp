#include "rhi/dx12/dx12_adapters.h"

#include <cstdio>

#include "os/windows/utf16.h"

namespace engine_placeholder::rhi::dx12 {

AdapterInfo MakeAdapterInfo(const AdapterFacts& facts, bool presentable) {
    AdapterInfo out;
    std::snprintf(out.id, sizeof(out.id), "dx12-%04x-%04x-%08x-%02x", facts.vendor_id & 0xFFFFu, facts.device_id & 0xFFFFu, facts.subsystem_id,
                  facts.revision & 0xFFu);
    std::snprintf(out.luid, sizeof(out.luid), "%08x%08x", static_cast<u32>(facts.luid_high), facts.luid_low);
    std::snprintf(out.name, sizeof(out.name), "%s", facts.name);
    out.kind = facts.software ? AdapterKind::Software : facts.uma ? AdapterKind::Integrated : AdapterKind::Discrete;
    out.dedicated_memory_bytes = facts.dedicated_video_memory;
    if (facts.driver_version != 0) {
        const u64 v = facts.driver_version;
        std::snprintf(out.driver, sizeof(out.driver), "%u.%u.%u.%u", static_cast<u32>((v >> 48) & 0xFFFF), static_cast<u32>((v >> 32) & 0xFFFF),
                      static_cast<u32>((v >> 16) & 0xFFFF), static_cast<u32>(v & 0xFFFF));
    }
    if (!facts.feature_level_ok) {
        out.unsuitable_reason = "no Direct3D 12 device at feature level 12_0";
    } else if (facts.binding_tier < 2) {
        out.unsuitable_reason = "resource binding tier 2 is not supported";
    }
    out.suitable = out.unsuitable_reason == nullptr;
    out.can_present = presentable && out.suitable;
    return out;
}

namespace {

void ReadFacts(IDXGIAdapter1* adapter, AdapterFacts* f) {
    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED(adapter->GetDesc1(&desc))) {
        return;
    }
    usize length = 0;
    while (length < 128 && desc.Description[length] != 0) {
        ++length;
    }
    const usize n = os::windows::Utf16ToUtf8(desc.Description, length, f->name, sizeof(f->name));
    f->name[n < sizeof(f->name) ? n : sizeof(f->name) - 1] = '\0';
    f->vendor_id = desc.VendorId;
    f->device_id = desc.DeviceId;
    f->subsystem_id = desc.SubSysId;
    f->revision = desc.Revision;
    f->luid_low = desc.AdapterLuid.LowPart;
    f->luid_high = desc.AdapterLuid.HighPart;
    f->dedicated_video_memory = desc.DedicatedVideoMemory;
    f->software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
    LARGE_INTEGER umd{};
    if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd))) {
        f->driver_version = static_cast<u64>(umd.QuadPart);
    }
    ComRef<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), device.PutVoid()))) {
        return;
    }
    f->feature_level_ok = true;
    D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)))) {
        f->binding_tier = static_cast<u32>(options.ResourceBindingTier);
    }
    D3D12_FEATURE_DATA_ARCHITECTURE architecture{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &architecture, sizeof(architecture)))) {
        f->uma = architecture.UMA != FALSE;
    }
}

} // namespace

bool ListAdapters(IDXGIFactory6* factory, bool presentable, AdapterSet* out) {
    for (u32 i = 0; i < kMaxAdapters; ++i) {
        out->adapters[i].Reset();
        out->facts[i] = AdapterFacts{};
    }
    out->list = AdapterList{};
    if (factory == nullptr) {
        return false;
    }
    bool have_software = false;
    u32 count = 0;
    for (UINT i = 0; count < kMaxAdapters; ++i) {
        ComRef<IDXGIAdapter1> adapter;
        if (FAILED(factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, __uuidof(IDXGIAdapter1), adapter.PutVoid()))) {
            break; // DXGI_ERROR_NOT_FOUND past the last one
        }
        ReadFacts(adapter.Get(), &out->facts[count]);
        have_software = have_software || out->facts[count].software;
        out->adapters[count] = static_cast<ComRef<IDXGIAdapter1>&&>(adapter);
        ++count;
    }
    if (!have_software && count < kMaxAdapters) {
        ComRef<IDXGIAdapter1> warp;
        if (SUCCEEDED(factory->EnumWarpAdapter(__uuidof(IDXGIAdapter1), warp.PutVoid()))) {
            ReadFacts(warp.Get(), &out->facts[count]);
            out->facts[count].software = true;
            out->adapters[count] = static_cast<ComRef<IDXGIAdapter1>&&>(warp);
            ++count;
        }
    }
    for (u32 i = 0; i < count; ++i) {
        out->list.items[i] = MakeAdapterInfo(out->facts[i], presentable);
    }
    out->list.count = count;
    return true;
}

int PickAdapter(const AdapterSet& set, const char* request, char* reason, usize reason_size) {
    const AdapterList& list = set.list;
    if (request != nullptr && request[0] != '\0') {
        const int match = MatchAdapter(list.items, list.count, request);
        if (match < 0) {
            std::snprintf(reason, reason_size, "DeviceCreateInfo::adapter='%s' matches none of the %u Direct3D 12 adapters", request, list.count);
            return -1;
        }
        if (!list.items[match].suitable) {
            std::snprintf(reason, reason_size, "DeviceCreateInfo::adapter='%s' selects '%s', which is unsuitable: %s", request, list.items[match].name,
                          list.items[match].unsuitable_reason != nullptr ? list.items[match].unsuitable_reason : "unknown");
            return -1;
        }
        return match;
    }
    for (u32 i = 0; i < list.count; ++i) {
        if (list.items[i].suitable && list.items[i].kind != AdapterKind::Software) {
            return static_cast<int>(i);
        }
    }
    for (u32 i = 0; i < list.count; ++i) {
        if (list.items[i].suitable) {
            return static_cast<int>(i);
        }
    }
    std::snprintf(reason, reason_size, "none of the %u Direct3D 12 adapters is suitable", list.count);
    return -1;
}

} // namespace engine_placeholder::rhi::dx12
