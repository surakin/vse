// The Direct3D 12 backend's device (Phase 13 Day 164): COM ownership, adapters, devices on every adapter with the debug layer silent
// and nothing left alive, capabilities, and bounded fence waits. Windows only.
#include <cstdio>
#include <cstdlib>
#include "sync/atomic.h"
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
#include "test_env.h"

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

TEST_CASE("The dx12 backend is registered, takes DXIL, and is what Auto picks on Windows", "[rhi][dx12]") {
    const BackendDescriptor* d = FindBackend(Backend::Dx12);
    REQUIRE(d != nullptr);
    CHECK(StringView(d->name) == "dx12");
    CHECK(d->shader_format == ShaderFormat::Dxil);
    const BackendDescriptor* vulkan = FindBackend(Backend::Vulkan);
    REQUIRE(vulkan != nullptr);
    CHECK(d->auto_rank > vulkan->auto_rank); // since Phase 13 Day 172
    char why[160] = {};
    const BackendDescriptor* picked = PickAutoBackend(why, sizeof(why));
    INFO(why);
    REQUIRE(picked != nullptr);
    CHECK(picked->backend == Backend::Dx12);
    // Alone in the registry it is still picked: Vulkan is not needed.
    ClearBackendRegistryForTest();
    REQUIRE(RegisterDx12Backend());
    picked = PickAutoBackend(why, sizeof(why));
    CHECK((picked != nullptr && picked->backend == Backend::Dx12));
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

TEST_CASE("ENGINE_RHI_GPU picks the DX12 adapter, and the descriptor's adapter beats it, as on Vulkan", "[rhi][dx12][gpu]") {
    // Phase 13 Day 170: the DX12 backend ignored the variable, so the golden run meant for WARP ran on the hardware adapter.
    const char* before = std::getenv("ENGINE_RHI_GPU");
    char saved[256] = {};
    if (before != nullptr) std::snprintf(saved, sizeof(saved), "%s", before);
    test_support::SetEnv("ENGINE_RHI_GPU", "Basic Render");
    DeviceCreateInfo info;
    info.backend = Backend::Dx12;
    info.application_name = "dx12-env-adapter";
    {
        Dx12DevicePtr device = Dx12Device::Create(info);
        REQUIRE(device);
        CHECK(StringView(device->Adapter().name).Contains(StringView("Basic Render")));
    }
    // An index in the descriptor wins over the variable: adapter 0 is the first hardware adapter (WARP comes last).
    info.adapter = "0";
    {
        Dx12DevicePtr device = Dx12Device::Create(info);
        REQUIRE(device);
        CHECK(device->Adapter().kind != AdapterKind::Software);
    }
    if (before != nullptr) {
        test_support::SetEnv("ENGINE_RHI_GPU", saved);
    } else {
        test_support::UnsetEnv("ENGINE_RHI_GPU");
    }
}

// Phase 13 Day 172: `fail_allocations_from` on Direct3D 12, where each committed resource is one allocation.
TEST_CASE("An injected DX12 allocation failure gives invalid handles and the device keeps working", "[rhi][dx12][gpu][fault]") {
    // Find how many resources creation itself makes (the staging ring and friends), then fail from the next one on.
    u32 creation_allocations = 0;
    bool found = false;
    for (u32 n = 1; n <= 16 && !found; ++n) {
        DeviceCreateInfo info = Dx12Info();
        info.faults.fail_allocations_from = n;
        DevicePtr probe = CreateDevice(info);
        if (probe != nullptr) {
            found = true;
            creation_allocations = n - 1;
        }
    }
    if (!found) SKIP("no Direct3D 12 device");
    CHECK(creation_allocations > 0); // creation does allocate: a device that could not would hide the failure path

    DeviceCreateInfo info = Dx12Info();
    info.faults.fail_allocations_from = creation_allocations + 1;
    DevicePtr device = CreateDevice(info);
    REQUIRE(device != nullptr);
    const ResourceCounts before = device->Resources();
    BufferDesc bd;
    bd.size = 256;
    bd.usage = BufferUsage::Uniform;
    bd.location = MemoryLocation::CpuToGpu;
    CHECK_FALSE(device->CreateBuffer(bd).IsValid());
    TextureDesc td;
    td.width = 16;
    td.height = 16;
    td.format = Format::RGBA8_UNorm;
    td.usage = TextureUsage::Sampled | TextureUsage::CopyDst;
    CHECK_FALSE(device->CreateTexture(td).IsValid());
    const ResourceCounts after = device->Resources();
    CHECK(after.buffers == before.buffers);
    CHECK(after.textures == before.textures);
    CHECK(after.internal_objects == before.internal_objects);
    CHECK(after.device_memory_bytes == before.device_memory_bytes);
    device->BeginFrame(); // frames still run
    device->EndFrame();
    device->WaitIdle();
    CHECK_FALSE(device->DeviceLost());
}

// Phase 13 Day 172 review: the explicit loss (a real RemoveDevice) honors the shared strike limit, as every injected loss does.
TEST_CASE("InjectDeviceLoss on DX12 removes the device only while strikes are left", "[rhi][dx12][gpu][fault]") {
    Atomic<u32> strikes{1};
    DeviceCreateInfo info = Dx12Info();
    info.faults.strikes_left = &strikes;
    {
        DevicePtr device = CreateDevice(info);
        if (device == nullptr) SKIP("no Direct3D 12 device");
        device->InjectDeviceLoss();
        CHECK(device->DeviceLost());
        CHECK(strikes.Load() == 0);
        device->WaitIdle(); // returns on a removed device
    }
    DevicePtr second = CreateDevice(info); // every device of the adapter is released: a new one, not the removed one
    REQUIRE(second != nullptr);
    second->InjectDeviceLoss(); // no strike left: nothing happens
    CHECK_FALSE(second->DeviceLost());
    BufferDesc bd;
    bd.size = 256;
    bd.usage = BufferUsage::Uniform;
    bd.location = MemoryLocation::CpuToGpu;
    const BufferHandle buffer = second->CreateBuffer(bd);
    CHECK(buffer.IsValid());
    if (buffer.IsValid()) second->DestroyBuffer(buffer);
}
