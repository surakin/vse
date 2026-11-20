#pragma once

#include "rhi/adapter.h"
#include "rhi/dx12/com_ref.h"
#include "rhi/dx12/d3d12_api.h"
#include "types.h"

//! The DXGI adapters the Direct3D 12 backend can use, described as [[rhi::AdapterInfo]] (Phase 13 Day 164).

namespace vse::rhi::dx12
{

/// The facts about one DXGI adapter that [[rhi::dx12::MakeAdapterInfo]] turns into an [[rhi::AdapterInfo]]; plain data
/// so the rule is testable without a GPU.
///
/// - Thread-safety: thread-compatible plain data.
struct AdapterFacts
{
    /// The adapter's name from `DXGI_ADAPTER_DESC1::Description`, UTF-8.
    char name[kAdapterNameBytes] = {};
    /// PCI vendor, device, subsystem and revision ids.
    u32 vendor_id = 0;
    /// PCI device id.
    u32 device_id = 0;
    /// PCI subsystem id.
    u32 subsystem_id = 0;
    /// PCI revision.
    u32 revision = 0;
    /// The LUID's low and high parts.
    u32 luid_low = 0;
    /// The LUID's high part.
    i32 luid_high = 0;
    /// `DXGI_ADAPTER_DESC1::DedicatedVideoMemory`.
    u64 dedicated_video_memory = 0;
    /// `DXGI_ADAPTER_FLAG_SOFTWARE` (WARP, the Basic Render Driver).
    bool software = false;
    /// The user-mode driver version from `CheckInterfaceSupport`, 0 when unknown.
    u64 driver_version = 0;
    /// Whether a device at the baseline feature level (12_0) could be created on it.
    bool feature_level_ok = false;
    /// The resource binding tier (1 to 3), 0 when unknown.
    u32 binding_tier = 0;
    /// Whether the adapter shares memory with the CPU (`D3D12_FEATURE_DATA_ARCHITECTURE::UMA`).
    bool uma = false;
};

/// Turns the facts into the RHI's description.
///
/// The id is `dx12-<vendor>-<device>-<subsystem>-<revision>` in lowercase hex (stable across runs and enumeration
/// order; two identical cards in one machine would share it, as on Vulkan without a UUID), the LUID the second id form;
/// `Software` for WARP, `Integrated` for a UMA adapter, else `Discrete`; suitable when the baseline feature level and
/// resource binding tier 2 are available.
///
/// - Param facts: the adapter.
/// - Param presentable: whether a window was given (D3D12 presents through the queue, so every suitable adapter can
/// present).
/// - Returns: the description.
/// - Fails: never.
/// - Thread-safety: thread-safe.
AdapterInfo MakeAdapterInfo(const AdapterFacts& facts, bool presentable);

/// The adapters of one factory, in the order the device picks from: DXGI's high-performance order, WARP last.
///
/// - Thread-safety: thread-compatible.
struct AdapterSet
{
    /// The adapters; only the first `count` are valid.
    ComRef<IDXGIAdapter1> adapters[kMaxAdapters];
    /// Their facts.
    AdapterFacts facts[kMaxAdapters];
    /// Their descriptions.
    AdapterList list;
};

/// Lists the factory's adapters and reads their facts.
///
/// Uses `IDXGIFactory6::EnumAdapterByGpuPreference` (high performance) and appends WARP if DXGI did not list a software
/// adapter. Reading the binding tier and the memory architecture needs a device: a temporary one is created per adapter
/// at the baseline feature level (D3D12 hands back the existing device when the process already has one on that
/// adapter, so a live device is not disturbed) and released before returning.
///
/// - Param factory: a DXGI factory.
/// - Param presentable: whether a window was given.
/// - Param out: receives the set; cleared first.
/// - Returns: true if the enumeration ran (the list may be empty).
/// - Fails: returns false if the factory is null.
/// - Thread-safety: thread-safe.
bool ListAdapters(IDXGIFactory6* factory, bool presentable, AdapterSet* out);

/// Picks the adapter a device is created on: the request matched by [[rhi::MatchAdapter]] if there is one, else the
/// first suitable hardware adapter, else WARP if it is suitable.
///
/// - Param set: the adapters.
/// - Param request: `DeviceCreateInfo::adapter`, or null.
/// - Param reason: receives why none was picked; `reason_size` bytes.
/// - Param reason_size: the size of `reason`.
/// - Returns: the index, or -1.
/// - Fails: -1 with the reason when the request matches nothing or an unsuitable adapter, or nothing is suitable.
/// - Thread-safety: thread-safe.
int PickAdapter(const AdapterSet& set, const char* request, char* reason, usize reason_size);

}  // namespace vse::rhi::dx12
