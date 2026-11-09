#include <memory>
#include "sync/thread.h"
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gpu_fixture.h"
#include "jobs/parallel_for.h"
#include "jobs/scheduler.h"
#include "rhi/null/null_device.h"
#include "rhi/rhi.h"
#include "rhi/vulkan/vk_barrier.h"
#include "rhi/vulkan/vulkan_device.h"
#include "cstr/mem_ops.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::rhi::vulkan;

namespace {

std::vector<unsigned char> VulkanCommandsTestPattern(usize size, unsigned seed) {
    std::vector<unsigned char> bytes(size);
    unsigned state = seed * 2654435761u + 12345u;
    for (usize i = 0; i < size; ++i) {
        state = state * 1664525u + 1013904223u;
        bytes[i] = static_cast<unsigned char>(state >> 24);
    }
    return bytes;
}

VulkanDevicePtr VulkanCommandsTestMakeDevice() {
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

constexpr BufferUsage kCopyBoth = BufferUsage::CopySrc | BufferUsage::CopyDst;

BufferHandle MakeBuffer(Device* device, u64 size) {
    BufferDesc desc;
    desc.size = size;
    desc.usage = kCopyBoth;
    return device->CreateBuffer(desc);
}

// Records "dst = copy of src" with the barriers the states require; src was filled by UploadBuffer (state CopyDst).
void RecordCopy(CommandList* list, BufferHandle src, BufferHandle dst, u64 size) {
    list->Begin();
    list->PushDebugLabel("copy");
    list->Barrier(src, ResourceState::CopyDst, ResourceState::CopySrc);
    list->Barrier(dst, ResourceState::Undefined, ResourceState::CopyDst);
    list->CopyBuffer(src, dst, 0, 0, size);
    list->Barrier(dst, ResourceState::CopyDst, ResourceState::CopySrc);
    list->PopDebugLabel();
    list->End();
}

} // namespace

TEST_CASE("GetBarrierScopes: the state table matches hand-derived stages, accesses and layouts", "[rhi][barrier]") {
    BarrierScopes s;

    SECTION("upload then read as a copy source (buffer)") {
        REQUIRE(GetBarrierScopes(ResourceState::CopyDst, ResourceState::CopySrc, false, &s));
        CHECK(s.src.stage == VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT);
        CHECK(s.src.access == VK_ACCESS_2_TRANSFER_WRITE_BIT);
        CHECK(s.dst.stage == VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT);
        CHECK(s.dst.access == VK_ACCESS_2_TRANSFER_READ_BIT);
        CHECK(s.src.layout == VK_IMAGE_LAYOUT_UNDEFINED); // buffers carry no layout
        CHECK(s.dst.layout == VK_IMAGE_LAYOUT_UNDEFINED);
    }
    SECTION("the same transition for an image carries layouts") {
        REQUIRE(GetBarrierScopes(ResourceState::CopyDst, ResourceState::ShaderRead, true, &s));
        CHECK(s.src.layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        CHECK(s.dst.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        CHECK(s.dst.access == VK_ACCESS_2_SHADER_READ_BIT);
        CHECK((s.dst.stage & VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT) != 0);
    }
    SECTION("Undefined source waits on everything and discards an image") {
        REQUIRE(GetBarrierScopes(ResourceState::Undefined, ResourceState::ColorAttachment, true, &s));
        CHECK(s.src.stage == VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
        CHECK(s.src.layout == VK_IMAGE_LAYOUT_UNDEFINED);
        CHECK(s.dst.stage == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
        CHECK(s.dst.layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }
    SECTION("depth and present") {
        REQUIRE(GetBarrierScopes(ResourceState::DepthAttachment, ResourceState::DepthRead, true, &s));
        CHECK(s.src.layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        CHECK(s.dst.layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        CHECK((s.src.access & VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) != 0);
        CHECK((s.dst.access & VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) == 0);
        REQUIRE(GetBarrierScopes(ResourceState::ColorAttachment, ResourceState::Present, true, &s));
        CHECK(s.dst.layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        CHECK(s.dst.stage == VK_PIPELINE_STAGE_2_NONE);
        CHECK(s.dst.access == 0);
    }
    SECTION("shader write is read-write in the general layout") {
        REQUIRE(GetBarrierScopes(ResourceState::ShaderWrite, ResourceState::ShaderRead, true, &s));
        CHECK(s.src.layout == VK_IMAGE_LAYOUT_GENERAL);
        CHECK(s.src.access == (VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT));
    }
    SECTION("buffer-only states") {
        REQUIRE(GetBarrierScopes(ResourceState::CopyDst, ResourceState::VertexBuffer, false, &s));
        CHECK(s.dst.stage == VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT);
        CHECK(s.dst.access == VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
        REQUIRE(GetBarrierScopes(ResourceState::CopyDst, ResourceState::IndexBuffer, false, &s));
        CHECK(s.dst.stage == VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT);
        REQUIRE(GetBarrierScopes(ResourceState::CopyDst, ResourceState::UniformBuffer, false, &s));
        CHECK(s.dst.access == VK_ACCESS_2_UNIFORM_READ_BIT);
    }
    SECTION("invalid transitions are rejected and leave the output alone") {
        BarrierScopes untouched;
        untouched.src.stage = 77;
        s = untouched;
        CHECK_FALSE(GetBarrierScopes(ResourceState::CopyDst, ResourceState::Undefined, true, &s));
        CHECK_FALSE(GetBarrierScopes(ResourceState::CopyDst, ResourceState::ColorAttachment, false, &s)); // not a buffer state
        CHECK_FALSE(GetBarrierScopes(ResourceState::VertexBuffer, ResourceState::CopyDst, true, &s));     // not an image state
        CHECK_FALSE(GetBarrierScopes(ResourceState::Count, ResourceState::CopyDst, true, &s));
        CHECK(s.src.stage == 77);
    }
    SECTION("every state is reachable from Undefined for the kind of resource it supports") {
        for (u32 i = 1; i < static_cast<u32>(ResourceState::Count); ++i) {
            const ResourceState state = static_cast<ResourceState>(i);
            const bool as_image = GetBarrierScopes(ResourceState::Undefined, state, true, &s);
            const bool as_buffer = GetBarrierScopes(ResourceState::Undefined, state, false, &s);
            CHECK((as_image || as_buffer));
        }
    }
}

TEST_CASE("Vulkan: record, submit and verify a buffer copy", "[rhi][vulkan][gpu][commands]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    VulkanDevicePtr device = VulkanCommandsTestMakeDevice();

    constexpr u64 kSize = 64 * 1024;
    BufferHandle src = MakeBuffer(device.Get(), kSize);
    BufferHandle dst = MakeBuffer(device.Get(), kSize);
    const std::vector<unsigned char> data = VulkanCommandsTestPattern(kSize, 1);
    REQUIRE(device->UploadBuffer(src, 0, data.data(), kSize).ok);

    device->BeginFrame();
    CommandList* list = device->CreateCommandList();
    REQUIRE(list != nullptr);
    RecordCopy(list, src, dst, kSize);
    CommandList* lists[] = {list};
    const FenceValue fence = device->Submit(lists, 1);
    REQUIRE(fence != 0);
    CHECK(device->WaitFence(fence, 10ull * 1000 * 1000 * 1000));
    CHECK(device->CompletedFence() >= fence);
    device->EndFrame();

    std::vector<unsigned char> back(kSize);
    REQUIRE(device->ReadbackBuffer(dst, 0, back.data(), kSize));
    CHECK(back == data);

    device->DestroyBuffer(src);
    device->DestroyBuffer(dst);
    device.Reset();
}

TEST_CASE("Vulkan fences: values increase, waiting times out when never signaled", "[rhi][vulkan][gpu][commands][fence]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    VulkanDevicePtr device = VulkanCommandsTestMakeDevice();

    BufferHandle a = MakeBuffer(device.Get(), 4096);
    BufferHandle b = MakeBuffer(device.Get(), 4096);
    const std::vector<unsigned char> data = VulkanCommandsTestPattern(4096, 2);
    device->UploadBuffer(a, 0, data.data(), data.size());

    device->BeginFrame();
    FenceValue previous = 0;
    for (int i = 0; i < 5; ++i) {
        CommandList* list = device->CreateCommandList();
        REQUIRE(list != nullptr);
        list->Begin();
        list->Barrier(a, i == 0 ? ResourceState::CopyDst : ResourceState::CopySrc, ResourceState::CopySrc);
        list->Barrier(b, i == 0 ? ResourceState::Undefined : ResourceState::CopyDst, ResourceState::CopyDst);
        list->CopyBuffer(a, b, 0, 0, 4096);
        list->Barrier(b, ResourceState::CopyDst, ResourceState::CopyDst);
        list->End();
        CommandList* lists[] = {list};
        const FenceValue fence = device->Submit(lists, 1);
        CHECK(fence > previous);
        previous = fence;
    }
    CHECK(device->WaitFence(previous, 10ull * 1000 * 1000 * 1000));
    CHECK(device->WaitFence(previous, 0)); // already reached: a poll succeeds
    CHECK_FALSE(device->WaitFence(previous + 1000, 1000 * 1000)); // never signaled: times out after 1 ms
    CHECK_FALSE(device->DeviceLost());
    device->EndFrame();

    device->DestroyBuffer(a);
    device->DestroyBuffer(b);
    device.Reset();
}

TEST_CASE("Vulkan: many frames reuse the per-thread pools", "[rhi][vulkan][gpu][commands][frames]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    VulkanDevicePtr device = VulkanCommandsTestMakeDevice();

    BufferHandle src = MakeBuffer(device.Get(), 1024);
    BufferHandle dst = MakeBuffer(device.Get(), 1024);
    const std::vector<unsigned char> data = VulkanCommandsTestPattern(1024, 3);
    device->UploadBuffer(src, 0, data.data(), data.size());

    FenceValue last = 0;
    for (int frame = 0; frame < 20; ++frame) {
        device->BeginFrame();
        CommandList* list = device->CreateCommandList();
        REQUIRE(list != nullptr);
        list->Begin();
        list->Barrier(src, frame == 0 ? ResourceState::CopyDst : ResourceState::CopySrc, ResourceState::CopySrc);
        list->Barrier(dst, frame == 0 ? ResourceState::Undefined : ResourceState::CopyDst, ResourceState::CopyDst);
        list->CopyBuffer(src, dst, 0, 0, 1024);
        list->End();
        CommandList* lists[] = {list};
        last = device->Submit(lists, 1);
        REQUIRE(last != 0);
        device->EndFrame();
    }
    // One thread recorded in every frame: one pool per frame slot, never one per frame.
    CHECK(device->CommandPoolCount() == VulkanDevice::kFramePools);
    device->WaitIdle();
    CHECK(device->CompletedFence() >= last);

    std::vector<unsigned char> back(1024);
    // dst is left in CopyDst; the readback path orders itself after the earlier work.
    REQUIRE(device->ReadbackBuffer(dst, 0, back.data(), 1024));
    CHECK(back == data);
    device->DestroyBuffer(src);
    device->DestroyBuffer(dst);
    device.Reset();
}

TEST_CASE("Vulkan: command lists are recorded on many job threads and submitted in order", "[rhi][vulkan][gpu][commands][mt]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    VulkanDevicePtr device = VulkanCommandsTestMakeDevice();

    constexpr usize kLists = 64;
    constexpr u64 kSize = 16 * 1024;
    BufferHandle src[kLists];
    BufferHandle dst[kLists];
    std::vector<unsigned char> data[kLists];
    for (usize i = 0; i < kLists; ++i) {
        src[i] = MakeBuffer(device.Get(), kSize);
        dst[i] = MakeBuffer(device.Get(), kSize);
        data[i] = VulkanCommandsTestPattern(kSize, static_cast<unsigned>(10 + i));
        REQUIRE(device->UploadBuffer(src[i], 0, data[i].data(), kSize).ok);
    }

    jobs::Scheduler::Config cfg;
    cfg.participants = 4;
    cfg.pin = false;
    jobs::Scheduler scheduler(cfg);

    device->BeginFrame();
    CommandList* lists[kLists] = {};
    auto record = [&](usize begin, usize end) {
        for (usize i = begin; i < end; ++i) {
            lists[i] = device->CreateCommandList();
            if (lists[i] != nullptr) {
                RecordCopy(lists[i], src[i], dst[i], kSize);
            }
        }
    };
    jobs::ParallelForBlocking(scheduler, kLists, 1, record);
    for (usize i = 0; i < kLists; ++i) {
        REQUIRE(lists[i] != nullptr);
        for (usize j = 0; j < i; ++j) {
            REQUIRE(lists[j] != lists[i]); // each thread got its own lists
        }
    }
    // Recording used at most one pool per participant (plus the calling thread's slot) in this frame slot.
    CHECK(device->CommandPoolCount() <= 5);

    const FenceValue fence = device->Submit(lists, kLists);
    REQUIRE(fence != 0);
    CHECK(device->WaitFence(fence, 10ull * 1000 * 1000 * 1000));
    device->EndFrame();

    for (usize i = 0; i < kLists; ++i) {
        std::vector<unsigned char> back(kSize);
        REQUIRE(device->ReadbackBuffer(dst[i], 0, back.data(), kSize));
        CHECK(back == data[i]);
        device->DestroyBuffer(src[i]);
        device->DestroyBuffer(dst[i]);
    }
    device.Reset();
}

TEST_CASE("Vulkan: plain threads can record, and thread slots are recycled", "[rhi][vulkan][gpu][commands][mt]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    VulkanDevicePtr device = VulkanCommandsTestMakeDevice();

    BufferHandle src = MakeBuffer(device.Get(), 1024);
    const std::vector<unsigned char> data = VulkanCommandsTestPattern(1024, 4);
    device->UploadBuffer(src, 0, data.data(), data.size());
    constexpr int kThreads = 3;
    BufferHandle dst[kThreads];
    for (int t = 0; t < kThreads; ++t) {
        dst[t] = MakeBuffer(device.Get(), 1024);
    }

    device->BeginFrame();
    CommandList* lists[kThreads] = {};
    Thread threads[kThreads];
    for (int t = 0; t < kThreads; ++t) {
        threads[t] = Thread([&, t] {
            lists[t] = device->CreateCommandList();
            lists[t]->Begin();
            lists[t]->Barrier(dst[t], ResourceState::Undefined, ResourceState::CopyDst);
            lists[t]->End();
        });
    }
    for (Thread& th : threads) {
        th.Join();
    }
    for (int t = 0; t < kThreads; ++t) {
        REQUIRE(lists[t] != nullptr);
    }
    const FenceValue fence = device->Submit(lists, kThreads);
    CHECK(fence != 0);
    device->EndFrame();
    device->WaitIdle();

    // Far more short-lived threads than there are slots: each gives its slot back when it exits.
    device->BeginFrame();
    for (usize i = 0; i < VulkanDevice::kThreadSlots * 2; ++i) {
        CommandList* got = nullptr;
        Thread([&] { got = device->CreateCommandList(); }).Join();
        REQUIRE(got != nullptr);
    }
    // The lists above were never begun or submitted, which is fine: the pools are reset when their frame slot comes round.
    device->EndFrame();

    device->DestroyBuffer(src);
    for (int t = 0; t < kThreads; ++t) {
        device->DestroyBuffer(dst[t]);
    }
    device.Reset();
}

TEST_CASE("Vulkan: a lost device makes submission and list creation fail without crashing", "[rhi][vulkan][gpu][commands][lost]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    VulkanDevicePtr device = VulkanCommandsTestMakeDevice();

    BufferHandle a = MakeBuffer(device.Get(), 1024);
    BufferHandle b = MakeBuffer(device.Get(), 1024);
    const std::vector<unsigned char> data = VulkanCommandsTestPattern(1024, 5);
    device->UploadBuffer(a, 0, data.data(), data.size());

    device->BeginFrame();
    CommandList* list = device->CreateCommandList();
    REQUIRE(list != nullptr);
    RecordCopy(list, a, b, 1024);
    CommandList* lists[] = {list};
    device->InjectSubmitFailure(VK_ERROR_DEVICE_LOST);
    CHECK(device->Submit(lists, 1) == 0);
    CHECK(device->DeviceLost());

    // Everything after is a no-op that reports failure.
    CHECK(device->CreateCommandList() == nullptr);
    CHECK(device->UploadBuffer(a, 0, data.data(), data.size()).ok == false);
    std::vector<unsigned char> back(1024);
    CHECK_FALSE(device->ReadbackBuffer(a, 0, back.data(), 1024));
    device->EndFrame();
    device->BeginFrame(); // a frame still begins and ends
    device->EndFrame();
    CHECK(device->DeviceLost()); // sticky

    device->DestroyBuffer(a);
    device->DestroyBuffer(b);
    device.Reset();
}

TEST_CASE("Vulkan: an empty submit returns the latest fence", "[rhi][vulkan][gpu][commands]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    VulkanDevicePtr device = VulkanCommandsTestMakeDevice();
    device->BeginFrame();
    CHECK(device->Submit(nullptr, 0) == 0); // nothing was ever submitted
    BufferHandle a = MakeBuffer(device.Get(), 64);
    const UploadTicket ticket = device->UploadBuffer(a, 0, "0123456789abcdef", 16);
    CHECK(device->Submit(nullptr, 0) == ticket.fence);
    device->EndFrame();
    device->DestroyBuffer(a);
    device.Reset();
}

TEST_CASE("Null device: command lists recorded on job threads submit in order (runs under TSan)", "[rhi][null][commands][mt]") {
    NullDevicePtr device = CreateNullDevice();
    constexpr usize kLists = 64;
    constexpr u64 kSize = 256;
    BufferHandle src[kLists];
    BufferHandle dst[kLists];
    std::vector<unsigned char> data[kLists];
    for (usize i = 0; i < kLists; ++i) {
        src[i] = MakeBuffer(device.Get(), kSize);
        dst[i] = MakeBuffer(device.Get(), kSize);
        data[i] = VulkanCommandsTestPattern(kSize, static_cast<unsigned>(50 + i));
        REQUIRE(device->UploadBuffer(src[i], 0, data[i].data(), kSize).ok);
    }

    jobs::Scheduler::Config cfg;
    cfg.participants = 4;
    cfg.pin = false;
    jobs::Scheduler scheduler(cfg);

    device->BeginFrame();
    CommandList* lists[kLists] = {};
    auto record = [&](usize begin, usize end) {
        for (usize i = begin; i < end; ++i) {
            lists[i] = device->CreateCommandList();
            if (lists[i] != nullptr) {
                RecordCopy(lists[i], src[i], dst[i], kSize);
            }
        }
    };
    jobs::ParallelForBlocking(scheduler, kLists, 1, record);
    for (usize i = 0; i < kLists; ++i) {
        REQUIRE(lists[i] != nullptr);
    }
    REQUIRE(device->Submit(lists, kLists) != 0);
    device->EndFrame();
    CHECK(device->ErrorCount() == 0);
    for (usize i = 0; i < kLists; ++i) {
        unsigned char back[kSize];
        REQUIRE(device->PeekBuffer(dst[i], 0, back, kSize));
        CHECK(MemCompare(back, data[i].data(), kSize) == 0);
    }
    device.Reset();
}

TEST_CASE("Null device: a command list cannot be created outside a frame", "[rhi][null][commands]") {
    NullDevicePtr device = CreateNullDevice();
    CHECK(device->CreateCommandList() == nullptr);
    CHECK(device->ErrorCount() == 1);
    device->BeginFrame();
    CHECK(device->CreateCommandList() != nullptr);
    CHECK(device->ErrorCount() == 1);
    device->EndFrame();
    CHECK(device->CreateCommandList() == nullptr);
    CHECK(device->ErrorCount() == 2);
    device.Reset();
}

// Phase 13 Day 160: WaitIdle's promise, checked where it was found broken. AMD's Windows driver reads the timeline one value short for
// about 100 us after vkDeviceWaitIdle (79 of 80 runs without validation, where the layer's own work does not hide the gap), so WaitIdle
// also waits on the timeline. Without validation on purpose.
TEST_CASE("Vulkan: after WaitIdle the completed fence is the last submitted one", "[rhi][vulkan][gpu][commands]") {
    DeviceCreateInfo info;
    info.validation = ValidationSetting::Off;
    VulkanDevicePtr device = VulkanDevice::Create(info);
    if (device == nullptr) {
        if (gpu_test::GpuRequired()) FAIL("ENGINE_RHI_REQUIRE_GPU=1 but no Vulkan device could be created");
        SKIP("no suitable Vulkan device");
    }
    BufferHandle src = MakeBuffer(device.Get(), 1024);
    BufferHandle dst = MakeBuffer(device.Get(), 1024);
    for (int round = 0; round < 10; ++round) {
        FenceValue last = 0;
        for (int frame = 0; frame < 20; ++frame) {
            device->BeginFrame();
            CommandList* list = device->CreateCommandList();
            REQUIRE(list != nullptr);
            list->Begin();
            const bool first = round == 0 && frame == 0;
            list->Barrier(src, first ? ResourceState::Undefined : ResourceState::CopySrc, ResourceState::CopySrc);
            list->Barrier(dst, first ? ResourceState::Undefined : ResourceState::CopyDst, ResourceState::CopyDst);
            list->CopyBuffer(src, dst, 0, 0, 1024);
            list->End();
            CommandList* lists[] = {list};
            last = device->Submit(lists, 1);
            REQUIRE(last != 0);
            device->EndFrame();
        }
        device->WaitIdle();
        INFO("round " << round);
        CHECK(device->CompletedFence() >= last);
    }
    device->DestroyBuffer(src);
    device->DestroyBuffer(dst);
    device.Reset();
}
