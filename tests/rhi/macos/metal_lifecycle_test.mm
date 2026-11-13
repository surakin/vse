// The Metal backend's lifecycle (Phase 26 Day 325): 100 device resets through rhi::DeviceHost with an observer that
// owns a buffer, the counts and memory unchanged after each and the reset time measured; a stale handle invalid on the
// next device; a loss that ignores every later submission and a destruction after it that does not wait; the device
// observer installed and removed with the device. The conformance suite's Metal case is in
// lifecycle_conformance_test.cpp. macOS only.
#include "metal_test_support.h"  // first: it imports AppKit before any engine name is in scope

#include "os/console.h"
#include "os/os.h"

#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "fill.comp.dxil.inc"
#include "rhi/device_host.h"
#include "temp_dir.h"
#include "test_text.h"
#include "rhi/metal/metal_device.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::rhi::metal;
using namespace engine_placeholder::rhi::metal::test;

namespace metal_lifecycle_test
{

// Owns one buffer per device, as a renderer's resources would: made on creation, destroyed on release.
// Owns a buffer and a compute pipeline per device, as a renderer's resources would: made on creation (the pipeline
// through the device's pipeline archive), destroyed on release.
struct BufferOwner final : DeviceObserver
{
    Device* device = nullptr;
    BufferHandle buffer;
    ShaderHandle shader;
    BindGroupLayoutHandle layout;
    PipelineHandle pipeline;
    u32 created = 0;
    u32 released = 0;
    u32 observers_seen = 0;  // the live device observers while a device lived

    void OnDeviceReleasing(u32) override
    {
        observers_seen = MetalDevice::LiveDeviceObservers();
        device->DestroyPipeline(pipeline);
        device->DestroyBindGroupLayout(layout);
        device->DestroyShader(shader);
        device->DestroyBuffer(buffer);
        buffer = {};
        device = nullptr;
        ++released;
    }
    bool OnDeviceCreated(Device* d, u32) override
    {
        device = d;
        BufferDesc bd;
        bd.size = 64 * 1024;
        bd.usage = BufferUsage::Uniform;
        bd.location = MemoryLocation::CpuToGpu;
        buffer = d->CreateBuffer(bd);
        shader = MetalShader(*d, shaders::kFillCompShader, ShaderStage::Compute);
        BindingDesc b;
        b.binding = 0;
        b.type = BindingType::StorageBuffer;
        b.stages = ShaderStage::Compute;
        b.stride = 4;
        BindGroupLayoutDesc ld;
        ld.entries = engine_placeholder::Span<BindingDesc>(&b, 1);
        layout = d->CreateBindGroupLayout(ld);
        ComputePipelineDesc cp;
        cp.shader = shader;
        cp.bind_group_layouts[0] = layout;
        cp.bind_group_layout_count = 1;
        cp.push_constant_bytes = 8;
        pipeline = d->CreateComputePipeline(cp);
        ++created;
        return buffer.IsValid() && pipeline.IsValid();
    }
    const char* ObserverName() const override { return "metal buffer owner"; }
};

}  // namespace metal_lifecycle_test

using namespace metal_lifecycle_test;

TEST_CASE("A hundred Metal device resets keep the counts and memory, change the epoch, and leave nothing behind",
          "[rhi][metal][gpu][reset]")
{
    MetalDevice::ResetLeakedUserObjects();
    const u32 observers_before = MetalDevice::LiveDeviceObservers();
    io_test::TempDir dir;
    const engine_test::Text cache = dir.Native();
    DeviceCreateInfo info = MetalInfo("metal_lifecycle_test");
    info.pipeline_cache = true;  // every device writes the archive as it goes, and the next reads it
    info.pipeline_cache_dir = cache.CStr();
    BufferOwner owner;
    DeviceHost host;
    REQUIRE(host.AddObserver(&owner, 0));
    REQUIRE(host.Create(info, SwapchainDesc{}));
    const ResourceCounts first = host.GetDevice()->Resources();
    CHECK(first.buffers == 1);
    CHECK(first.UserObjects() == 4);  // the buffer, the shader, the layout and the pipeline
    u32 last = host.Epoch();
    Vector<f64> reset_ms;
    for (int i = 0; i < 100; ++i)
    {
        INFO("reset " << i);
        const u64 start = os::MonotonicTimeNanos();
        REQUIRE(host.Recreate(info, SwapchainDesc{}));
        reset_ms.Push(static_cast<f64>(os::MonotonicTimeNanos() - start) / 1e6);
        CHECK(host.Epoch() != last);
        last = host.Epoch();
        const ResourceCounts c = host.GetDevice()->Resources();
        CHECK(c.buffers == 1);
        CHECK(c.UserObjects() == 4);
        CHECK(c.internal_objects == first.internal_objects);
        CHECK(c.device_memory_bytes == first.device_memory_bytes);
        CHECK(owner.observers_seen == observers_before + 1);  // one observer per device, the old one removed
        CHECK(AsMetal(*host.GetDevice()).PipelineCacheHits() ==
              1);  // the pipeline came from the archive the last device wrote
    }
    CHECK(owner.created == 101);
    CHECK(owner.released == 100);
    host.Destroy();
    CHECK(owner.released == 101);
    CHECK(MetalDevice::LeakedUserObjects() == 0);
    CHECK(MetalDevice::LiveDeviceObservers() == observers_before);
    // The reset time, for the day file (Linux: about 40 ms on its GPU).
    f64 total = 0;
    f64 worst = 0;
    for (usize i = 0; i < reset_ms.Size(); ++i)
    {
        total += reset_ms[i];
        worst = reset_ms[i] > worst ? reset_ms[i] : worst;
    }
    os::Print("metal reset: %zu resets, mean %.2f ms, worst %.2f ms\n", reset_ms.Size(),
              total / static_cast<f64>(reset_ms.Size()), worst);
}

TEST_CASE("A Metal handle from a destroyed device is not valid on the next one", "[rhi][metal][gpu][reset]")
{
    DeviceCreateInfo info = MetalInfo("metal_lifecycle_test");
    BufferHandle stale;
    {
        DevicePtr first = CreateDevice(info);
        REQUIRE(first);
        BufferDesc bd;
        bd.size = 256;
        bd.usage = BufferUsage::Vertex;
        stale = first->CreateBuffer(bd);
        REQUIRE(stale.IsValid());
        first->DestroyBuffer(stale);
    }
    DevicePtr second = CreateDevice(info);
    REQUIRE(second);
    BufferDesc bd;
    bd.size = 256;
    bd.usage = BufferUsage::Vertex;
    const BufferHandle fresh = second->CreateBuffer(bd);
    REQUIRE(fresh.IsValid());
    CHECK(fresh != stale);                               // the generation seed is the device's own
    CHECK(AsMetal(*second).BufferObject(stale) == nil);  // the old handle resolves to nothing
    CHECK(AsMetal(*second).BufferObject(fresh) != nil);
    second->DestroyBuffer(fresh);
}

TEST_CASE("A lost Metal device ignores later submissions and is destroyed without waiting", "[rhi][metal][gpu][fault]")
{
    DeviceCreateInfo info = MetalInfo("metal_lifecycle_test");
    DevicePtr device = CreateDevice(info);
    REQUIRE(device);
    BufferDesc bd;
    bd.size = 1024;
    bd.usage = BufferUsage::CopySrc | BufferUsage::CopyDst;
    const BufferHandle a = device->CreateBuffer(bd);
    const BufferHandle b = device->CreateBuffer(bd);
    REQUIRE((a.IsValid() && b.IsValid()));
    // Real work first: a copy in flight, so the fence has a value and the loss has something to cut short.
    device->BeginFrame();
    CommandList* before = device->CreateCommandList();
    REQUIRE(before != nullptr);
    before->Begin();
    before->CopyBuffer(a, b, 0, 0, 1024);
    before->End();
    const FenceValue fence = device->Submit(&before, 1);
    CHECK(fence != 0);
    CHECK(device->Submit(nullptr, 0) == fence);  // nothing new: the fence for everything so far
    device->EndFrame();
    // A list recorded before the loss is refused at its submission.
    device->BeginFrame();
    CommandList* recorded = device->CreateCommandList();
    REQUIRE(recorded != nullptr);
    recorded->Begin();
    recorded->CopyBuffer(b, a, 0, 0, 1024);
    recorded->End();
    const bool was = LogRhi.Enabled();
    LogRhi.SetEnabled(false);
    device->InjectDeviceLoss();
    CHECK(device->DeviceLost());
    CHECK(device->Submit(&recorded, 1) == 0);          // ignored
    CHECK(device->Submit(nullptr, 0) == 0);            // and no fence any more, not the old one
    CHECK(device->CreateCommandList() == nullptr);     // a lost device hands out no list
    CHECK_FALSE(device->WaitFence(fence + 1, ~0ull));  // a wait for what never comes returns at once
    device->EndFrame();
    Array<u8, 16> bytes = {};
    CHECK_FALSE(device->UploadBuffer(a, 0, bytes.Data(), 16).ok);
    CHECK_FALSE(device->ReadbackBuffer(a, 0, bytes.Data(), 16));
    device->DestroyBuffer(a);
    device->DestroyBuffer(b);
    const u64 start = os::MonotonicTimeNanos();
    device.Reset();  // waits for nothing it cannot have
    LogRhi.SetEnabled(was);
    CHECK(os::MonotonicTimeNanos() - start < 2000ull * 1000 * 1000);
}
