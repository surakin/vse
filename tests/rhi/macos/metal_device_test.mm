// The Metal backend's device (Phase 21 Day 300): the registration, the adapters, a device on every suitable adapter
// with validation silent, the capabilities, the timeline and frames, bounded waits, loss, and nothing left alive. macOS
// only; Objective-C++ for the device's own accessors (metal_device.h).
#include "metal_test_support.h"  // first: it imports AppKit before any engine name is in scope
#include "log/output.h"

#include "cstr/cstr.h"
#include "cstr/format.h"
#include "sync/atomic.h"

#include <catch2/catch_test_macros.hpp>

#include "os/console.h"
#include "os/os.h"
#include "renderer/gbuffer.h"
#include "rhi/backend_registry.h"
#include "rhi/backends/backends.h"
#include "rhi/metal/metal_adapters.h"
#include "rhi/metal/metal_backend.h"
#include "rhi/metal/metal_device.h"
#include "rhi/metal/metal_format.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"
#include "string_view.h"

using namespace vse;
using namespace vse::rhi;
using namespace vse::rhi::metal;

using namespace vse::rhi::metal::test;

TEST_CASE("The metal backend is registered, takes Metal libraries, and is what Auto picks on macOS", "[rhi][metal]")
{
    RegisterCompiledBackends();
    const BackendDescriptor* d = FindBackend(Backend::Metal);
    REQUIRE(d != nullptr);
    CHECK(StringView(d->name) == "metal");
    CHECK(d->shader_format == ShaderFormat::MetalLib);
    CHECK(d->auto_rank > 0);
    char why[160] = {};
    const BackendDescriptor* picked = PickAutoBackend(why, sizeof(why));
    INFO(why);
    REQUIRE(picked != nullptr);
    CHECK(picked->backend == Backend::Metal);  // from Phase 21 Day 309
    char reason[160] = {};
    INFO(reason);
    CHECK(BackendAvailable(*d, reason, sizeof(reason)));
}

TEST_CASE("Metal lists every device with a hex registry id, the same on every enumeration, and creates no queue",
          "[rhi][metal][gpu]")
{
    AdapterList first;
    REQUIRE(EnumerateAdapters(MetalInfo("metal_device_test"), &first));
    REQUIRE(first.count >= 1);
    AdapterList second;
    REQUIRE(EnumerateAdapters(MetalInfo("metal_device_test"), &second));
    REQUIRE(second.count == first.count);
    u32 suitable = 0;
    for (u32 i = 0; i < first.count; ++i)
    {
        const AdapterInfo& a = first.items[i];
        INFO(a.name);
        CHECK(CStrLength(a.id) == 16);
        for (usize c = 0; a.id[c] != '\0'; ++c)
        {
            CHECK(((a.id[c] >= '0' && a.id[c] <= '9') || (a.id[c] >= 'a' && a.id[c] <= 'f')));
        }
        CHECK(StringView(a.id) == StringView(second.items[i].id));
        CHECK(a.name[0] != '\0');
        CHECK(a.driver[0] != '\0');
        CHECK((a.suitable == (a.unsuitable_reason == nullptr)));
        suitable += a.suitable ? 1 : 0;
    }
    CHECK(suitable >= 1);
    // The properties of each device, for the day file's record (Day 300: what this Mac reports).
    @autoreleasepool
    {
        NSArray<id<MTLDevice>>* devices = MTLCopyAllDevices();
        for (id<MTLDevice> device in devices)
        {
            out::Print("metal adapter: %s unified=%d low_power=%d removable=%d headless=%d working_set=%llu "
                       "argument_buffers_tier=%ld read_write_textures_tier=%ld\n",
                       device.name.UTF8String, device.hasUnifiedMemory ? 1 : 0, device.isLowPower ? 1 : 0,
                       device.isRemovable ? 1 : 0, device.isHeadless ? 1 : 0,
                       static_cast<unsigned long long>(device.recommendedMaxWorkingSetSize),
                       static_cast<long>(device.argumentBuffersSupport),
                       static_cast<long>(device.readWriteTextureSupport));
        }
    }
}

TEST_CASE("A Metal device is created and destroyed on every suitable adapter, with its capabilities and nothing alive",
          "[rhi][metal][gpu]")
{
    AdapterList adapters;
    REQUIRE(EnumerateAdapters(MetalInfo("metal_device_test"), &adapters));
    u32 made = 0;
    for (u32 i = 0; i < adapters.count; ++i)
    {
        if (!adapters.items[i].suitable)
        {
            continue;
        }
        INFO(adapters.items[i].name);
        DeviceCreateInfo info = MetalInfo("metal_device_test");
        info.adapter = adapters.items[i].id;
        info.validation = ValidationSetting::On;
        DevicePtr device = CreateDevice(info);
        METAL_REQUIRE_DEVICE(device);
        ++made;
        CHECK(StringView(device->BackendName()) == "metal");
        CHECK(StringView(device->Adapter().id) == StringView(adapters.items[i].id));
        CHECK(device->Epoch() != 0);
        // The GPU ctests run with Metal's API validation on (tests/rhi/CMakeLists.txt), and say so: under ctest the
        // variable must be there and the device must see it; by hand, the device reports what the process has.
        const char* layer = os::GetEnvironment("MTL_DEBUG_LAYER");
        const bool layer_on = layer != nullptr && layer[0] == '1';
        const char* expect = os::GetEnvironment("ENGINE_RHI_EXPECT_METAL_VALIDATION");
        if (expect != nullptr && expect[0] == '1')
        {
            REQUIRE(layer_on);
        }
        CHECK(AsMetal(*device).ValidationEnabled() == layer_on);
        const DeviceCaps& caps = device->Caps();
        CHECK(caps.shader_format == ShaderFormat::MetalLib);
        CHECK(caps.object_index_stream);
        CHECK(caps.max_color_attachments == 8);
        CHECK(caps.buffer_texture_copy_row_pitch_alignment == 1);
        CHECK(caps.max_texture_dimension_2d == 16384);
        CHECK(caps.depth_format == Format::D32_Float);
        CHECK(device->FormatSupport(Format::RGBA8_UNorm, TextureUsage::Sampled | TextureUsage::ColorAttachment));
        CHECK(device->FormatSupport(Format::D32_Float, TextureUsage::DepthAttachment));
        CHECK_FALSE(device->FormatSupport(Format::D32_Float, TextureUsage::ColorAttachment));
        CHECK_FALSE(device->FormatSupport(Format::RGBA8_SRGB, TextureUsage::Storage));
        char error[256] = {};
        CHECK(renderer::CheckGBufferFormats(device.Get(), error, sizeof(error)));
        INFO(error);
        AdapterList listed;
        CHECK(device->ListAdapters(&listed));
        CHECK(listed.count == adapters.count);
        for (int f = 0; f < 3; ++f)
        {
            device->BeginFrame();
            CHECK(device->InFrame());
            CHECK(device->Submit(nullptr, 0) == 0);  // nothing submitted yet: the fence for everything so far
            device->EndFrame();
        }
        CHECK(device->FrameNumber() == 3);
        device->WaitIdle();
        CHECK_FALSE(device->DeviceLost());
        const ResourceCounts counts = device->Resources();
        CHECK(counts.buffers == 0);
        CHECK(counts.textures == 0);
        device.Reset();
    }
    CHECK(made >= 1);
}

TEST_CASE("The Metal timeline counts the signals, frames wait for their fences, and waits are bounded",
          "[rhi][metal][gpu]")
{
    DeviceCreateInfo info = MetalInfo("metal_device_test");
    info.frames_in_flight = 2;
    DevicePtr device = CreateDevice(info);
    METAL_REQUIRE_DEVICE(device);
    MetalDevice& metal = AsMetal(*device);
    CHECK(metal.FramesInFlight() == 2);
    CHECK(device->WaitFence(0, 0));  // 0 is always reached
    for (int f = 1; f <= 20; ++f)
    {
        device->BeginFrame();
        u64 value = 0;
        {
            LockGuard<Mutex> lock(metal.SubmitMutex());
            value = metal.SignalLocked();
        }
        CHECK(value == static_cast<u64>(f));
        device->EndFrame();
        // At most two frames ahead: the frame two back has finished.
        CHECK(metal.CompletedFrame() + 2 >= static_cast<u64>(f));
    }
    device->WaitIdle();
    CHECK(device->CompletedFence() == 20);
    CHECK(metal.CompletedFrame() == 20);
    CHECK(device->WaitFence(20, 0));

    const u64 start = os::MonotonicTimeNanos();
    CHECK_FALSE(device->WaitFence(1000, 50ull * 1000 * 1000));  // nothing signals 1000
    const u64 waited = os::MonotonicTimeNanos() - start;
    CHECK(waited >= 40ull * 1000 * 1000);
    CHECK(waited < 2000ull * 1000 * 1000);    // bounded: no hang
    CHECK_FALSE(device->WaitFence(1000, 0));  // a poll
}

TEST_CASE("A lost Metal device answers waits at once and ignores submissions; strikes limit the injected losses",
          "[rhi][metal][gpu][fault]")
{
    Atomic<u32> strikes{1};
    DeviceCreateInfo info = MetalInfo("metal_device_test");
    info.faults.strikes_left = &strikes;
    {
        DevicePtr device = CreateDevice(info);
        METAL_REQUIRE_DEVICE(device);
        const bool was = LogRhi.Enabled();
        LogRhi.SetEnabled(false);
        device->InjectDeviceLoss();
        LogRhi.SetEnabled(was);
        CHECK(device->DeviceLost());
        CHECK(strikes.Load() == 0);
        const u64 start = os::MonotonicTimeNanos();
        CHECK_FALSE(device->WaitFence(1, ~0ull));  // a lost device answers at once
        CHECK(os::MonotonicTimeNanos() - start < 1000ull * 1000 * 1000);
        device->BeginFrame();
        CHECK(device->Submit(nullptr, 0) == 0);
        {
            LockGuard<Mutex> lock(AsMetal(*device).SubmitMutex());
            CHECK(AsMetal(*device).SignalLocked() == 0);  // ignored
        }
        device->EndFrame();
        device->WaitIdle();  // returns on a lost device
    }
    DevicePtr second = CreateDevice(info);
    REQUIRE(second);
    second->InjectDeviceLoss();  // no strike left: nothing happens
    CHECK_FALSE(second->DeviceLost());
}

TEST_CASE("A frame lost at a set number marks the device lost at that BeginFrame", "[rhi][metal][gpu][fault]")
{
    DeviceCreateInfo info = MetalInfo("metal_device_test");
    info.faults.lose_device_at_frame = 3;
    DevicePtr device = CreateDevice(info);
    METAL_REQUIRE_DEVICE(device);
    const bool was = LogRhi.Enabled();
    LogRhi.SetEnabled(false);
    for (int f = 1; f <= 4; ++f)
    {
        device->BeginFrame();
        CHECK(device->DeviceLost() == (f >= 3));
        device->EndFrame();
    }
    LogRhi.SetEnabled(was);
    device->WaitIdle();
}

TEST_CASE("A requested Metal adapter that does not exist fails without falling back", "[rhi][metal][gpu]")
{
    DeviceCreateInfo info = MetalInfo("metal_device_test");
    info.adapter = "no-such-adapter-anywhere";
    const bool was = LogRhi.Enabled();
    LogRhi.SetEnabled(false);
    CHECK_FALSE(CreateDevice(info));
    LogRhi.SetEnabled(was);
}

TEST_CASE("Metal pixel formats: depth is never a color target, sRGB never storage, RGB32 has none", "[rhi][metal]")
{
    @autoreleasepool
    {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        REQUIRE(device != nil);
        CHECK(ToMtlPixelFormat(Format::RGBA8_UNorm, device) == MTLPixelFormatRGBA8Unorm);
        CHECK(ToMtlPixelFormat(Format::BGRA8_SRGB, device) == MTLPixelFormatBGRA8Unorm_sRGB);
        CHECK(ToMtlPixelFormat(Format::D32_Float, device) == MTLPixelFormatDepth32Float);
        CHECK(ToMtlPixelFormat(Format::RGB32_Float, device) == MTLPixelFormatInvalid);
        CHECK(ToMtlPixelFormat(Format::Undefined, device) == MTLPixelFormatInvalid);
        CHECK_FALSE(MtlFormatSupports(Format::D32_Float, TextureUsage::ColorAttachment, device));
        CHECK_FALSE(MtlFormatSupports(Format::RGBA8_SRGB, TextureUsage::Storage, device));
        CHECK(MtlFormatSupports(Format::RGBA16_Float, TextureUsage::Storage | TextureUsage::Sampled, device));
    }
}

TEST_CASE("Metal generates mips only for filterable color formats", "[rhi][metal][gpu]")
{
    DevicePtr device = CreateDevice(MetalInfo("metal_device_test"));
    METAL_REQUIRE_DEVICE(device);
    CHECK(device->CanGenerateMips(Format::RGBA8_UNorm));
    CHECK(device->CanGenerateMips(Format::RGBA16_Float));
    CHECK_FALSE(device->CanGenerateMips(Format::R32_UInt));  // integers are not filterable
    CHECK_FALSE(device->CanGenerateMips(Format::D32_Float));
    CHECK_FALSE(device->CanGenerateMips(Format::BC7_UNorm));
    CHECK_FALSE(device->CanGenerateMips(Format::Undefined));
    CHECK(device->CanGenerateMips(Format::RGBA32_Float) == AsMetal(*device).Mtl().supports32BitFloatFiltering);
}

TEST_CASE("A Metal device destroyed right after its signals outlives none of their completion handlers",
          "[rhi][metal][gpu]")
{
    // The handlers run on Metal's threads after the timeline value the destruction waits for: they hold the device's
    // shared state, never the device (the ASan flavor catches a handler touching a freed device).
    for (int round = 0; round < 20; ++round)
    {
        DevicePtr device = CreateDevice(MetalInfo("metal_device_test"));
        METAL_REQUIRE_DEVICE(device);
        MetalDevice& metal = AsMetal(*device);
        device->BeginFrame();
        {
            LockGuard<Mutex> lock(metal.SubmitMutex());
            for (int i = 0; i < 16; ++i)
            {
                metal.SignalLocked();
            }
        }
        device->EndFrame();
        device.Reset();
    }
}
