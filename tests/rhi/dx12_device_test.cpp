// The Direct3D 12 backend's device (Phase 13 Day 164): COM ownership, adapters, devices on every adapter with the debug layer silent
// and nothing left alive, capabilities, and bounded fence waits. Windows only.
#include <cstring>

#include <catch2/catch_test_macros.hpp>

#include "os/os.h"
#include "rhi/backend_registry.h"
#include "rhi/backends/backends.h"
#include "rhi/dx12/dx12_backend.h"
#include "rhi/dx12/com_ref.h"
#include "rhi/dx12/dx12_adapters.h"
#include "rhi/dx12/dx12_debug.h"
#include "rhi/dx12/dx12_device.h"
#include "rhi/dx12/dx12_result.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"
#include "string_view.h"
#include "gpu_fixture.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::rhi::dx12;

namespace {

// A COM-shaped object that counts its references.
struct FakeCom {
    int refs = 1;
    int released_to_zero = 0;
    unsigned long AddRef() { return static_cast<unsigned long>(++refs); }
    unsigned long Release() {
        const int left = --refs;
        if (left == 0) ++released_to_zero;
        return static_cast<unsigned long>(left);
    }
};

// Fails the test on any debug-layer warning or error while it lives (the allow list starts empty, Day 164).
struct DebugLayerSilence {
    DebugLayerSilence() {
        ResetDebugMessageCounts();
        Dx12Device::ResetLeakCounts();
    }
    ~DebugLayerSilence() {
        const DebugMessageCounts c = GetDebugMessageCounts();
        CHECK(c.errors == 0);
        CHECK(c.warnings == 0);
        CHECK(c.live_objects == 0);
        CHECK(Dx12Device::GetLeakCounts().live_d3d12_objects == 0);
    }
};

DeviceCreateInfo Dx12Info() {
    DeviceCreateInfo info;
    info.backend = Backend::Dx12;
    info.application_name = "dx12-test";
    return info;
}

} // namespace

TEST_CASE("ComRef owns one reference: moves transfer it, Share adds one, Reset and destruction release it", "[rhi][dx12]") {
    FakeCom object;
    {
        ComRef<FakeCom> a(&object); // takes the creation reference
        CHECK(object.refs == 1);
        ComRef<FakeCom> b = a.Share();
        CHECK(object.refs == 2);
        ComRef<FakeCom> c(static_cast<ComRef<FakeCom>&&>(a));
        CHECK_FALSE(a);
        CHECK(c.Get() == &object);
        CHECK(object.refs == 2); // a move adds nothing
        b.Reset();
        CHECK(object.refs == 1);
        FakeCom* raw = c.Detach();
        CHECK(raw == &object);
        CHECK(object.refs == 1); // detaching releases nothing
        ComRef<FakeCom> d(raw);
        FakeCom** slot = d.Put(); // Put releases what was held
        CHECK(object.refs == 0);
        CHECK(*slot == nullptr);
    }
    CHECK(object.released_to_zero == 1); // released exactly once, never below zero
    CHECK(object.refs == 0);
}

TEST_CASE("The adapter rules: kind, ids in hex and the suitability reasons", "[rhi][dx12]") {
    AdapterFacts f;
    std::strcpy(f.name, "AMD Radeon(TM) 860M Graphics");
    f.vendor_id = 0x1002;
    f.device_id = 0x150e;
    f.subsystem_id = 0x8c9b103c;
    f.revision = 0xc1;
    f.luid_low = 0x12345678;
    f.luid_high = 2;
    f.feature_level_ok = true;
    f.binding_tier = 3;
    f.uma = true;
    f.driver_version = (u64{32} << 48) | (u64{0} << 32) | (u64{32015} << 16) | 7029;
    AdapterInfo a = MakeAdapterInfo(f, true);
    CHECK(StringView(a.id) == "dx12-1002-150e-8c9b103c-c1");
    CHECK(StringView(a.luid) == "0000000212345678");
    CHECK(a.kind == AdapterKind::Integrated);
    CHECK(StringView(a.driver) == "32.0.32015.7029");
    CHECK(a.suitable);
    CHECK(a.can_present);
    f.uma = false;
    CHECK(MakeAdapterInfo(f, false).kind == AdapterKind::Discrete);
    CHECK_FALSE(MakeAdapterInfo(f, false).can_present);
    f.software = true;
    CHECK(MakeAdapterInfo(f, false).kind == AdapterKind::Software);
    f.binding_tier = 1;
    CHECK_FALSE(MakeAdapterInfo(f, false).suitable);
    CHECK(StringView(MakeAdapterInfo(f, false).unsuitable_reason).Contains("binding tier 2"));
    f.feature_level_ok = false;
    CHECK(StringView(MakeAdapterInfo(f, false).unsuitable_reason).Contains("12_0"));
}

TEST_CASE("HRESULT names cover the device-loss codes", "[rhi][dx12]") {
    CHECK(StringView(HresultName(DXGI_ERROR_DEVICE_REMOVED)) == "DXGI_ERROR_DEVICE_REMOVED");
    CHECK(StringView(HresultName(E_OUTOFMEMORY)) == "E_OUTOFMEMORY");
    CHECK(StringView(HresultName(static_cast<HRESULT>(0x12345678))) == "unknown HRESULT");
    CHECK(IsDeviceLostResult(DXGI_ERROR_DEVICE_HUNG));
    CHECK(IsDeviceLostResult(DXGI_ERROR_DEVICE_RESET));
    CHECK_FALSE(IsDeviceLostResult(E_INVALIDARG));
}

TEST_CASE("The dx12 backend is registered, takes DXIL, and is never picked by Auto until it is complete", "[rhi][dx12]") {
    const BackendDescriptor* d = FindBackend(Backend::Dx12);
    REQUIRE(d != nullptr);
    CHECK(StringView(d->name) == "dx12");
    CHECK(d->shader_format == ShaderFormat::Dxil);
    CHECK(d->auto_rank == 0); // Phase 13 Day 172 raises it
    // With every other backend unavailable, Auto still does not fall back to it.
    ClearBackendRegistryForTest();
    REQUIRE(RegisterDx12Backend());
    char why[160] = {};
    CHECK(PickAutoBackend(why, sizeof(why)) == nullptr);
    RegisterCompiledBackends();
    char reason[160] = {};
    CHECK(BackendAvailable(*d, reason, sizeof(reason)));
}

TEST_CASE("DX12 lists the hardware adapters and WARP, with ids that are the same on every enumeration", "[rhi][dx12][gpu]") {
    AdapterList first;
    AdapterList second;
    REQUIRE(EnumerateAdapters(Dx12Info(), &first));
    REQUIRE(EnumerateAdapters(Dx12Info(), &second));
    REQUIRE(first.count == second.count);
    bool warp = false;
    bool hardware = false;
    for (u32 i = 0; i < first.count; ++i) {
        INFO(first.items[i].name);
        CHECK(StringView(first.items[i].id) == StringView(second.items[i].id));
        CHECK(StringView(first.items[i].luid) == StringView(second.items[i].luid));
        CHECK(std::strncmp(first.items[i].id, "dx12-", 5) == 0);
        CHECK(std::strlen(first.items[i].luid) == 16);
        warp = warp || (first.items[i].kind == AdapterKind::Software && first.items[i].suitable);
        hardware = hardware || (first.items[i].kind != AdapterKind::Software && first.items[i].suitable);
    }
    CHECK(warp); // WARP is part of Windows: always there
    if (!hardware) {
        if (gpu_test::GpuRequired()) FAIL("ENGINE_RHI_REQUIRE_GPU=1 but no hardware adapter supports Direct3D 12 at feature level 12_0");
        WARN("no hardware Direct3D 12 adapter on this machine");
    }
}

TEST_CASE("A DX12 device is created and destroyed on every suitable adapter with the debug layer silent", "[rhi][dx12][gpu]") {
    AdapterList adapters;
    REQUIRE(EnumerateAdapters(Dx12Info(), &adapters));
    DebugLayerSilence silence;
    u32 made = 0;
    for (u32 i = 0; i < adapters.count; ++i) {
        if (!adapters.items[i].suitable) continue;
        INFO(adapters.items[i].name);
        DeviceCreateInfo info = Dx12Info();
        info.adapter = adapters.items[i].id;
        info.validation = ValidationSetting::On;
        DevicePtr device = CreateDevice(info);
        REQUIRE(device);
        ++made;
        CHECK(StringView(device->BackendName()) == "dx12");
        CHECK(StringView(device->Adapter().id) == StringView(adapters.items[i].id));
        CHECK(device->Epoch() != 0);
        const DeviceCaps& caps = device->Caps();
        CHECK(caps.shader_format == ShaderFormat::Dxil);
        CHECK(caps.object_index_stream);
        CHECK(caps.buffer_texture_copy_row_pitch_alignment == 256);
        CHECK(caps.buffer_texture_copy_offset_alignment == 512);
        CHECK(caps.min_uniform_buffer_offset_alignment == 256);
        CHECK(caps.max_texture_dimension_2d == 16384);
        AdapterList listed;
        CHECK(device->ListAdapters(&listed));
        CHECK(listed.count == adapters.count);
        for (int f = 0; f < 3; ++f) {
            device->BeginFrame();
            CHECK(device->Submit(nullptr, 0) == 0); // nothing submitted yet: the fence for everything so far
            device->EndFrame();
        }
        CHECK(device->FrameNumber() == 3);
        device->WaitIdle();
        CHECK_FALSE(device->DeviceLost());
        device.Reset();
    }
    CHECK(made >= 1);
}

TEST_CASE("A DX12 fence wait on a value that is never signaled returns false within its timeout", "[rhi][dx12][gpu]") {
    DeviceCreateInfo info = Dx12Info();
    info.validation = ValidationSetting::Off;
    DevicePtr device = CreateDevice(info);
    REQUIRE(device);
    CHECK(device->WaitFence(0, 0));        // 0 is always reached
    const u64 start = os::MonotonicTimeNanos();
    CHECK_FALSE(device->WaitFence(1000, 50ull * 1000 * 1000)); // nothing signals 1000
    const u64 waited = os::MonotonicTimeNanos() - start;
    CHECK(waited >= 40ull * 1000 * 1000);
    CHECK(waited < 2000ull * 1000 * 1000); // bounded: no hang
    CHECK_FALSE(device->WaitFence(1000, 0)); // a poll
    CHECK(device->CompletedFence() == 0);
    device->InjectDeviceLoss();
    CHECK(device->DeviceLost());
    CHECK_FALSE(device->WaitFence(1, ~0ull)); // a lost device answers at once
    device->WaitIdle(); // and still waits for its real work (none) without hanging
}

TEST_CASE("A requested DX12 adapter that does not exist fails without falling back", "[rhi][dx12][gpu]") {
    DeviceCreateInfo info = Dx12Info();
    info.adapter = "no-such-adapter-anywhere";
    const bool was = LogRhi.Enabled();
    LogRhi.SetEnabled(false);
    CHECK_FALSE(CreateDevice(info));
    LogRhi.SetEnabled(was);
}

TEST_CASE("With validation on, a D3D12 error reaches the RHI log and is counted", "[rhi][dx12][gpu]") {
    DeviceCreateInfo info = Dx12Info();
    info.validation = ValidationSetting::On;
    Dx12DevicePtr device = Dx12Device::Create(info);
    REQUIRE(device);
    REQUIRE(device->ValidationEnabled());
    ResetDebugMessageCounts();
    struct Seen {
        u32 errors = 0;
    } seen;
    SetDebugMessageCapture([](void* user, LogVerbosity severity, u32, const char*) {
        if (severity == LogVerbosity::Error) ++static_cast<Seen*>(user)->errors;
    }, &seen);
    const bool was = LogRhi.Enabled();
    LogRhi.SetEnabled(false);
    // A zero-sized buffer is invalid: the debug layer reports it and the call fails.
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 0;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComRef<ID3D12Resource> resource;
    const HRESULT hr = device->D3D()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                             __uuidof(ID3D12Resource), resource.PutVoid());
    LogRhi.SetEnabled(was);
    SetDebugMessageCapture(nullptr, nullptr);
    CHECK(FAILED(hr));
    CHECK(seen.errors >= 1);
    CHECK(GetDebugMessageCounts().errors >= 1);
    ResetDebugMessageCounts();
}
