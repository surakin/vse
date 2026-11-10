#include "sync/thread.h"
#include "containers/vector.h"

#include <catch2/catch_test_macros.hpp>

#include "gpu_fixture.h"
#include "test_text.h"
#include "rhi/null/null_device.h"
#include "rhi/rhi.h"
#include "rhi/staging_ring.h"
#include "rhi/vulkan/vulkan_device.h"
#include "cstr/mem_ops.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::rhi::vulkan;

namespace {

Vector<byte> Pattern(usize size, unsigned seed) {
    Vector<byte> bytes = engine_test::ByteBuffer(size);
    unsigned state = seed * 2654435761u + 12345u;
    for (usize i = 0; i < size; ++i) {
        state = state * 1664525u + 1013904223u;
        bytes[i] = static_cast<byte>(state >> 24);
    }
    return bytes;
}

// Creates the device under the validation fixture, or skips/fails as the fixture's policy says.
VulkanDevicePtr MakeDevice() {
    DeviceCreateInfo info;
    info.validation = ValidationSetting::On;
    info.sync_validation = true;
    VulkanDevicePtr device = VulkanDevice::Create(info);
    if (device == nullptr) {
        if (gpu_test::GpuRequired()) {
            FAIL("ENGINE_RHI_REQUIRE_GPU=1 but no Vulkan device could be created");
        }
        SKIP("no suitable Vulkan device");
    }
    return device;
}

} // namespace

TEST_CASE("RingAllocator hands out aligned ranges and wraps", "[rhi][ring]") {
    RingAllocator ring(1024);
    u64 a = 0, b = 0, c = 0;
    REQUIRE(ring.Allocate(100, 16, &a));
    CHECK(a == 0);
    REQUIRE(ring.Allocate(100, 64, &b));
    CHECK(b == 128);
    ring.Retire(1);
    REQUIRE(ring.Allocate(700, 1, &c)); // 228 .. 928
    CHECK(c == 228);
    CHECK_FALSE(ring.Allocate(200, 1, &c)); // 96 bytes left at the end, nothing free at the front yet
    ring.Retire(2);
    ring.Reclaim(1); // frees [0, 228)
    REQUIRE(ring.Allocate(200, 1, &c));
    CHECK(c == 0); // wrapped to the front
    CHECK(ring.OldestPendingFence() == 2);
    ring.Retire(3);
    ring.Reclaim(3);
    CHECK(ring.UsedBytes() == 0);
    CHECK(ring.OldestPendingFence() == 0);
}

TEST_CASE("RingAllocator rejects oversized requests and recovers from full", "[rhi][ring]") {
    RingAllocator ring(256);
    u64 off = 0;
    CHECK_FALSE(ring.Allocate(257, 1, &off));
    REQUIRE(ring.Allocate(256, 1, &off));
    CHECK_FALSE(ring.Allocate(1, 1, &off));
    ring.Retire(1);
    CHECK_FALSE(ring.Allocate(1, 1, &off)); // retired but not yet reclaimed
    ring.Reclaim(1);
    REQUIRE(ring.Allocate(256, 1, &off));
    CHECK(off == 0);
}

TEST_CASE("RingAllocator never hands out space still held, over 100k random operations", "[rhi][ring][stress]") {
    constexpr u64 kCap = 4096;
    RingAllocator ring(kCap);
    struct Held {
        u64 offset, size, fence;
    };
    Vector<Held> held;
    u64 fence = 0, completed = 0;
    u64 state = 88172645463325252ull;
    auto next = [&]() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    for (int step = 0; step < 100000; ++step) {
        u64 op = next() % 10;
        if (op < 6) {
            u64 size = 1 + next() % 300;
            u64 alignment = u64(1) << (next() % 6);
            u64 off = 0;
            if (ring.Allocate(size, alignment, &off)) {
                REQUIRE(off % alignment == 0);
                REQUIRE(off + size <= kCap);
                for (usize hi = 0; hi < held.Size(); ++hi) {
                    const Held& h = held[hi];
                    bool disjoint = off + size <= h.offset || h.offset + h.size <= off;
                    REQUIRE(disjoint);
                }
                held.Push(Held{off, size, 0});
            }
        } else if (op < 8) {
            ++fence;
            ring.Retire(fence);
            for (usize hi = 0; hi < held.Size(); ++hi) {
                if (held[hi].fence == 0) {
                    held[hi].fence = fence;
                }
            }
        } else if (fence > completed) {
            completed += 1 + next() % (fence - completed);
            ring.Reclaim(completed);
            for (usize i = 0; i < held.Size();) {
                if (held[i].fence != 0 && held[i].fence <= completed) {
                    held[i] = held[held.Size() - 1];
                    held.Pop();
                } else {
                    ++i;
                }
            }
        }
    }
}

TEST_CASE("NullDevice upload and readback round-trip and leave the buffer in CopyDst", "[rhi][null][upload]") {
    NullDevicePtr device = CreateNullDevice();
    BufferDesc desc;
    desc.size = 1024;
    desc.usage = BufferUsage::CopyDst | BufferUsage::CopySrc | BufferUsage::Vertex;
    BufferHandle buffer = device->CreateBuffer(desc);
    REQUIRE(buffer.IsValid());

    Vector<byte> data = Pattern(300, 7);
    UploadTicket ticket = device->UploadBuffer(buffer, 100, data.Data(), data.Size());
    CHECK(ticket.fence == 0);
    CHECK(device->StateOf(buffer) == ResourceState::CopyDst);
    Vector<byte> back = engine_test::ByteBuffer(300);
    REQUIRE(device->ReadbackBuffer(buffer, 100, back.Data(), back.Size()));
    CHECK(engine_test::SameBytes(back, data));
    CHECK(device->ErrorCount() == 0);

    // Misuse is counted, not fatal, on the null device.
    UploadTicket bad = device->UploadBuffer(buffer, 900, data.Data(), data.Size()); // past the end
    CHECK_FALSE(bad.ok);
    CHECK(device->ErrorCount() == 1);
    device.Reset();
}

TEST_CASE("Vulkan buffers: create, map, write and destroy, validation-clean", "[rhi][vulkan][gpu][buffer]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    VulkanDevicePtr device = MakeDevice();

    BufferDesc desc;
    desc.size = 4096;
    desc.usage = BufferUsage::Uniform | BufferUsage::CopySrc;
    desc.location = MemoryLocation::CpuToGpu;
    desc.debug_name = "mapped uniform";
    BufferHandle buffer = device->CreateBuffer(desc);
    REQUIRE(buffer.IsValid());
    auto* p = static_cast<byte*>(device->MapBuffer(buffer));
    REQUIRE(p != nullptr);
    MemSet(p, 0x5A, 4096);
    device->UnmapBuffer(buffer);

    BufferDesc bad;
    bad.size = 0;
    bad.usage = BufferUsage::Vertex;
    CHECK_FALSE(device->CreateBuffer(bad).IsValid()); // invalid description: invalid handle, logged

    VulkanDevice::ObjectCounts counts = device->Counts();
    CHECK(counts.buffers == 1);
    device->DestroyBuffer(buffer);
    CHECK(device->Counts().buffers == 0);
    CHECK(device->Counts().pending_destruction == 1); // deferred
    device->WaitIdle();
    CHECK(device->Counts().pending_destruction == 0);
    CHECK(device->Counts().raw_buffers == 1); // only the staging ring remains
    device.Reset();
}

TEST_CASE("Vulkan buffer uploads round-trip at every size, including larger than the staging ring", "[rhi][vulkan][gpu][buffer]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    VulkanDevicePtr device = MakeDevice();

    for (usize size : {usize(1), usize(4096), usize(1) << 20, usize(40) << 20}) {
        INFO("size " << size);
        BufferDesc desc;
        desc.size = size;
        desc.usage = BufferUsage::CopyDst | BufferUsage::CopySrc;
        desc.location = MemoryLocation::GpuOnly;
        BufferHandle buffer = device->CreateBuffer(desc);
        REQUIRE(buffer.IsValid());
        Vector<byte> data = Pattern(size, static_cast<unsigned>(size));
        UploadTicket ticket = device->UploadBuffer(buffer, 0, data.Data(), data.Size());
        CHECK(ticket.ok);
        CHECK(ticket.fence != 0);
        CHECK(device->WaitFence(ticket.fence, 10ull * 1000 * 1000 * 1000));
        Vector<byte> back = engine_test::ByteBuffer(size);
        REQUIRE(device->ReadbackBuffer(buffer, 0, back.Data(), back.Size()));
        CHECK(engine_test::SameBytes(back, data));
        device->DestroyBuffer(buffer);
    }
    device->WaitIdle();
    CHECK(device->Counts().raw_buffers == 1);
    device.Reset();
}

TEST_CASE("Vulkan partial uploads leave the rest of the buffer alone", "[rhi][vulkan][gpu][buffer]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    VulkanDevicePtr device = MakeDevice();
    BufferDesc desc;
    desc.size = 1024;
    desc.usage = BufferUsage::CopyDst | BufferUsage::CopySrc;
    BufferHandle buffer = device->CreateBuffer(desc);
    Vector<byte> base = Pattern(1024, 1);
    device->UploadBuffer(buffer, 0, base.Data(), base.Size());
    Vector<byte> patch = Pattern(100, 2);
    device->UploadBuffer(buffer, 500, patch.Data(), patch.Size());
    MemCopy(base.Data() + 500, patch.Data(), patch.Size());
    Vector<byte> back = engine_test::ByteBuffer(1024);
    REQUIRE(device->ReadbackBuffer(buffer, 0, back.Data(), back.Size()));
    CHECK(engine_test::SameBytes(back, base)); // the second upload is ordered after the first
    device->DestroyBuffer(buffer);
    device.Reset();
}

TEST_CASE("Vulkan CPU-visible buffers upload and read back through their mapping", "[rhi][vulkan][gpu][buffer]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    VulkanDevicePtr device = MakeDevice();
    BufferDesc up;
    up.size = 256;
    up.usage = BufferUsage::CopyDst | BufferUsage::CopySrc;
    up.location = MemoryLocation::CpuToGpu;
    BufferHandle buffer = device->CreateBuffer(up);
    Vector<byte> data = Pattern(256, 3);
    UploadTicket direct = device->UploadBuffer(buffer, 0, data.Data(), data.Size());
    CHECK(direct.fence == 0); // no GPU copy needed
    CHECK(direct.ok);
    auto* p = static_cast<byte*>(device->MapBuffer(buffer));
    CHECK(MemCompare(p, data.Data(), 256) == 0);
    device->UnmapBuffer(buffer);
    Vector<byte> back = engine_test::ByteBuffer(256);
    REQUIRE(device->ReadbackBuffer(buffer, 0, back.Data(), back.Size()));
    CHECK(engine_test::SameBytes(back, data));
    device->DestroyBuffer(buffer);
    device.Reset();
}

TEST_CASE("Vulkan uploads from two threads serialize correctly", "[rhi][vulkan][gpu][buffer][threads]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    VulkanDevicePtr device = MakeDevice();
    constexpr int kBuffers = 8;
    constexpr usize kSize = 3 << 20; // several uploads exceed the ring together
    BufferHandle buffers[2][kBuffers];
    Vector<byte> data[2][kBuffers];
    for (int t = 0; t < 2; ++t) {
        for (int i = 0; i < kBuffers; ++i) {
            BufferDesc desc;
            desc.size = kSize;
            desc.usage = BufferUsage::CopyDst | BufferUsage::CopySrc;
            buffers[t][i] = device->CreateBuffer(desc);
            REQUIRE(buffers[t][i].IsValid());
            data[t][i] = Pattern(kSize, static_cast<unsigned>(t * 100 + i));
        }
    }
    Thread threads[2];
    for (int t = 0; t < 2; ++t) {
        threads[t] = Thread([&, t] {
            for (int i = 0; i < kBuffers; ++i) {
                device->UploadBuffer(buffers[t][i], 0, data[t][i].Data(), kSize);
            }
        });
    }
    for (Thread& th : threads) {
        th.Join();
    }
    for (int t = 0; t < 2; ++t) {
        for (int i = 0; i < kBuffers; ++i) {
            Vector<byte> back = engine_test::ByteBuffer(kSize);
            REQUIRE(device->ReadbackBuffer(buffers[t][i], 0, back.Data(), kSize));
            CHECK(engine_test::SameBytes(back, data[t][i]));
            device->DestroyBuffer(buffers[t][i]);
        }
    }
    device.Reset();
}

TEST_CASE("Vulkan retired buffers are released only after their frame completes", "[rhi][vulkan][gpu][buffer][frames]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    VulkanDevicePtr device = MakeDevice();

    BufferDesc desc;
    desc.size = 4096;
    desc.usage = BufferUsage::CopyDst | BufferUsage::CopySrc;

    // Destroyed outside any frame: belongs to the next frame and survives frame 1 beginning.
    BufferHandle early = device->CreateBuffer(desc);
    device->DestroyBuffer(early);
    CHECK(device->Counts().pending_destruction == 1);

    device->BeginFrame(); // frame 1
    BufferHandle inside = device->CreateBuffer(desc);
    Vector<byte> data = Pattern(4096, 5);
    UploadTicket ticket = device->UploadBuffer(inside, 0, data.Data(), data.Size()); // in flight
    device->DestroyBuffer(inside);                                                    // destroy while the copy may run
    CHECK(device->Counts().pending_destruction == 2);
    device->EndFrame();
    CHECK(device->WaitFence(ticket.fence, 10ull * 1000 * 1000 * 1000));
    CHECK(device->Counts().pending_destruction == 2); // completing alone does not release; the next BeginFrame does

    device->BeginFrame(); // frame 2: frame 1 has completed, so both go
    CHECK(device->CompletedFrame() == 1);
    CHECK(device->Counts().pending_destruction == 0);
    CHECK(device->Counts().raw_buffers == 1);
    device->EndFrame();
    device.Reset();
}

TEST_CASE("Vulkan device destruction releases buffers the application forgot", "[rhi][vulkan][gpu][buffer]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    VulkanDevicePtr device = MakeDevice();
    BufferDesc desc;
    desc.size = 1 << 16;
    desc.usage = BufferUsage::Vertex | BufferUsage::CopyDst;
    device->CreateBuffer(desc);
    device->CreateBuffer(desc);
    device.Reset(); // logs a warning; validation must stay clean (no leaked VkBuffer)
}

TEST_CASE("RingAllocator reclaims many groups at once", "[rhi][ring]") {
    RingAllocator ring(1 << 20);
    u64 off = 0;
    for (u64 fence = 1; fence <= 1000; ++fence) {
        REQUIRE(ring.Allocate(64, 1, &off));
        ring.Retire(fence);
    }
    CHECK(ring.OldestPendingFence() == 1);
    ring.Reclaim(1000);
    CHECK(ring.UsedBytes() == 0);
    CHECK(ring.OldestPendingFence() == 0);
}
