#include <cstring>

#include <catch2/catch_test_macros.hpp>

#include "gpu_fixture.h"
#include "rhi/rhi.h"
#include "rhi/vulkan/vk_device_select.h"
#include "rhi/vulkan/vk_format.h"
#include "rhi/vulkan/vulkan_device.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::rhi::vulkan;

namespace {

// A suitable device: Vulkan 1.3 with the required features and one graphics+compute(+present) family.
PhysicalDeviceInfo Good(const char* name, GpuKind kind) {
    PhysicalDeviceInfo info;
    std::strncpy(info.name, name, sizeof(info.name) - 1);
    info.kind = kind;
    info.api_version = VK_API_VERSION_1_3;
    info.dynamic_rendering = true;
    info.synchronization2 = true;
    info.timeline_semaphore = true;
    info.sampler_anisotropy = true;
    info.swapchain_extension = true;
    info.queue_family_count = 1;
    info.families[0] = {16, true, true, true, 64, true};
    return info;
}

} // namespace

TEST_CASE("RejectionReason names the first missing requirement", "[rhi][vulkan][select]") {
    PhysicalDeviceInfo d = Good("gpu", GpuKind::Discrete);
    CHECK(RejectionReason(d, false) == nullptr);
    CHECK(RejectionReason(d, true) == nullptr);

    PhysicalDeviceInfo old = d;
    old.api_version = VK_API_VERSION_1_2;
    CHECK(std::strstr(RejectionReason(old, false), "1.3") != nullptr);

    PhysicalDeviceInfo no_dr = d;
    no_dr.dynamic_rendering = false;
    CHECK(std::strstr(RejectionReason(no_dr, false), "dynamic rendering") != nullptr);

    PhysicalDeviceInfo no_sync2 = d;
    no_sync2.synchronization2 = false;
    CHECK(std::strstr(RejectionReason(no_sync2, false), "synchronization2") != nullptr);

    PhysicalDeviceInfo no_timeline = d;
    no_timeline.timeline_semaphore = false;
    CHECK(std::strstr(RejectionReason(no_timeline, false), "timeline") != nullptr);

    PhysicalDeviceInfo no_queue = d;
    no_queue.families[0].compute = false;
    CHECK(std::strstr(RejectionReason(no_queue, false), "graphics+compute") != nullptr);

    PhysicalDeviceInfo no_swapchain = d;
    no_swapchain.swapchain_extension = false;
    CHECK(RejectionReason(no_swapchain, false) == nullptr); // headless does not need it
    CHECK(std::strstr(RejectionReason(no_swapchain, true), "swapchain") != nullptr);

    PhysicalDeviceInfo no_present = d;
    no_present.families[0].present = false;
    CHECK(RejectionReason(no_present, false) == nullptr);
    CHECK(std::strstr(RejectionReason(no_present, true), "present") != nullptr);
}

TEST_CASE("SelectPhysicalDevice ranks discrete over integrated over virtual over software", "[rhi][vulkan][select]") {
    PhysicalDeviceInfo devices[4] = {Good("software", GpuKind::Cpu), Good("integrated", GpuKind::Integrated),
                                     Good("discrete", GpuKind::Discrete), Good("virtual", GpuKind::Virtual)};
    CHECK(SelectPhysicalDevice(devices, 4, false, nullptr) == 2);

    devices[2].dynamic_rendering = false; // the discrete GPU is no longer usable
    CHECK(SelectPhysicalDevice(devices, 4, false, nullptr) == 1);

    devices[1].api_version = VK_API_VERSION_1_1;
    CHECK(SelectPhysicalDevice(devices, 4, false, nullptr) == 3);
}

TEST_CASE("SelectPhysicalDevice: ties go to the earlier device", "[rhi][vulkan][select]") {
    PhysicalDeviceInfo devices[2] = {Good("first", GpuKind::Discrete), Good("second", GpuKind::Discrete)};
    CHECK(SelectPhysicalDevice(devices, 2, false, nullptr) == 0);
}

TEST_CASE("SelectPhysicalDevice honors the ENGINE_RHI_GPU override", "[rhi][vulkan][select]") {
    PhysicalDeviceInfo devices[3] = {Good("NVIDIA GeForce", GpuKind::Discrete), Good("AMD Radeon Graphics", GpuKind::Integrated),
                                     Good("llvmpipe", GpuKind::Cpu)};
    SECTION("by index") {
        CHECK(SelectPhysicalDevice(devices, 3, false, "2") == 2);
        CHECK(SelectPhysicalDevice(devices, 3, false, "0") == 0);
    }
    SECTION("by name substring, ignoring case") {
        CHECK(SelectPhysicalDevice(devices, 3, false, "radeon") == 1);
        CHECK(SelectPhysicalDevice(devices, 3, false, "LLVM") == 2);
    }
    SECTION("a number too big to be an index is matched against names") {
        PhysicalDeviceInfo named[2] = {Good("llvmpipe", GpuKind::Cpu), Good("NVIDIA GeForce RTX 3060", GpuKind::Discrete)};
        CHECK(SelectPhysicalDevice(named, 2, false, "3060") == 1);
        CHECK(SelectPhysicalDevice(named, 2, false, "0") == 0); // still an index when it is in range
    }
    SECTION("empty or null means no override") {
        CHECK(SelectPhysicalDevice(devices, 3, false, "") == 0);
        CHECK(SelectPhysicalDevice(devices, 3, false, nullptr) == 0);
    }
    SECTION("an override that matches nothing fails rather than falling back") {
        CHECK(SelectPhysicalDevice(devices, 3, false, "intel") == -1);
        CHECK(SelectPhysicalDevice(devices, 3, false, "7") == -1);
        CHECK(SelectPhysicalDevice(devices, 3, false, "3060") == -1);
    }
    SECTION("an override naming an unsuitable device fails") {
        devices[1].synchronization2 = false;
        CHECK(SelectPhysicalDevice(devices, 3, false, "radeon") == -1);
        CHECK(SelectPhysicalDevice(devices, 3, false, "1") == -1);
    }
}

TEST_CASE("SelectPhysicalDevice reports no suitable device", "[rhi][vulkan][select]") {
    PhysicalDeviceInfo devices[2] = {Good("a", GpuKind::Discrete), Good("b", GpuKind::Integrated)};
    devices[0].timeline_semaphore = false;
    devices[1].api_version = VK_API_VERSION_1_0;
    CHECK(SelectPhysicalDevice(devices, 2, false, nullptr) == -1);
    CHECK(SelectPhysicalDevice(devices, 0, false, nullptr) == -1);
}

TEST_CASE("PlanQueues picks a graphics+compute family and a dedicated transfer family when there is one", "[rhi][vulkan][select]") {
    PhysicalDeviceInfo d = Good("gpu", GpuKind::Discrete);
    d.queue_family_count = 3;
    d.families[0] = {1, false, false, true, 0, false};  // transfer only
    d.families[1] = {16, true, true, true, 64, false};   // graphics+compute, cannot present
    d.families[2] = {8, true, true, true, 64, true};     // graphics+compute+present

    QueuePlan headless = PlanQueues(d, false);
    CHECK(headless.graphics_family == 1);
    CHECK(headless.transfer_family == 0);

    QueuePlan present = PlanQueues(d, true);
    CHECK(present.graphics_family == 2);
    CHECK(present.transfer_family == 0);

    d.families[0] = {1, true, true, true, 64, false}; // no dedicated transfer family any more
    QueuePlan shared = PlanQueues(d, false);
    CHECK(shared.graphics_family == 0);
    CHECK(shared.transfer_family == 0);

    d.queue_family_count = 0;
    CHECK(PlanQueues(d, false).graphics_family == -1);
}

TEST_CASE("Every portable Format maps to a VkFormat and back, with matching flags", "[rhi][vulkan][format]") {
    for (usize i = 1; i < static_cast<usize>(Format::Count); ++i) {
        Format format = static_cast<Format>(i);
        VkFormat vk = ToVkFormat(format);
        INFO(FormatInfo(format).name);
        REQUIRE(vk != VK_FORMAT_UNDEFINED);
        CHECK(FromVkFormat(vk) == format);
        const FormatProperties& props = FormatInfo(format);
        const VkImageAspectFlags aspects = AspectsOf(format);
        CHECK(((aspects & VK_IMAGE_ASPECT_DEPTH_BIT) != 0) == props.is_depth);
        CHECK(((aspects & VK_IMAGE_ASPECT_STENCIL_BIT) != 0) == props.has_stencil);
        // sRGB-ness agrees with the VkFormat's name class.
        const bool vk_srgb = vk == VK_FORMAT_R8G8B8A8_SRGB || vk == VK_FORMAT_B8G8R8A8_SRGB || vk == VK_FORMAT_BC1_RGBA_SRGB_BLOCK ||
                             vk == VK_FORMAT_BC3_SRGB_BLOCK || vk == VK_FORMAT_BC7_SRGB_BLOCK;
        CHECK(vk_srgb == props.is_srgb);
        const bool vk_compressed = vk >= VK_FORMAT_BC1_RGB_UNORM_BLOCK && vk <= VK_FORMAT_BC7_SRGB_BLOCK;
        CHECK(vk_compressed == props.is_compressed);
    }
    CHECK(ToVkFormat(Format::Undefined) == VK_FORMAT_UNDEFINED);
    CHECK(ToVkFormat(Format::Count) == VK_FORMAT_UNDEFINED);
    CHECK(FromVkFormat(VK_FORMAT_A2B10G10R10_UNORM_PACK32) == Format::Undefined);
}

TEST_CASE("CreateDevice(Null) returns the null device", "[rhi][device]") {
    DeviceCreateInfo info;
    info.backend = Backend::Null;
    Device* device = CreateDevice(info);
    REQUIRE(device != nullptr);
    CHECK(std::strcmp(device->BackendName(), "null") == 0);
    DestroyDevice(device);
}

TEST_CASE("A headless Vulkan device is created through the factory with plausible caps", "[rhi][vulkan][gpu]") {
    gpu_test::GpuFixture fx;
    fx.Require();

    DeviceCreateInfo info;
    info.application_name = "device-test";
    info.validation = ValidationSetting::On;
    info.sync_validation = true;
    Device* device = CreateDevice(info);
    if (device == nullptr) {
        if (gpu_test::GpuRequired()) {
            FAIL("ENGINE_RHI_REQUIRE_GPU=1 but no Vulkan device could be created");
        }
        SKIP("no suitable Vulkan device");
    }
    CHECK(std::strcmp(device->BackendName(), "vulkan") == 0);
    CHECK_FALSE(device->DeviceLost());

    const DeviceCaps& caps = device->Caps();
    CHECK(caps.max_texture_dimension_2d >= 4096);
    CHECK(caps.max_texture_dimension_3d >= 256);
    CHECK(caps.max_texture_array_layers >= 256);
    CHECK(caps.max_bind_groups >= 4);
    CHECK(caps.max_color_attachments >= 4);
    CHECK(caps.max_push_constant_bytes >= 128);
    CHECK(caps.buffer_texture_copy_offset_alignment == 512); // the strict portable contract, even though Vulkan needs less
    CHECK(caps.buffer_texture_copy_row_pitch_alignment == 256);
    CHECK(caps.min_uniform_buffer_offset_alignment >= 16);
    CHECK(caps.min_uniform_buffer_offset_alignment <= 1024);
    CHECK(caps.max_sampler_anisotropy >= 1.0f);
    CHECK(caps.max_compute_work_group_count[0] >= 65535);
    CHECK(FormatInfo(caps.depth_format).is_depth);
    CHECK(device->CompletedFence() == 0);
    CHECK(device->WaitFence(0, 0)); // fence 0 means no work: already complete

    device->WaitIdle();
    CHECK_FALSE(device->DeviceLost());
    DestroyDevice(device); // destruction is validation-clean: the fixture fails the test otherwise
}

TEST_CASE("VulkanDevice exposes its GPU, queues and function table", "[rhi][vulkan][gpu]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    DeviceCreateInfo info;
    info.validation = ValidationSetting::On;
    VulkanDevice* device = VulkanDevice::Create(info);
    if (device == nullptr) {
        if (gpu_test::GpuRequired()) {
            FAIL("ENGINE_RHI_REQUIRE_GPU=1 but no Vulkan device could be created");
        }
        SKIP("no suitable Vulkan device");
    }
    CHECK(device->GpuName()[0] != '\0');
    CHECK(device->Queues().graphics_family >= 0);
    CHECK(device->Queues().transfer_family >= 0);
    CHECK(device->Handle() != nullptr);
    CHECK(device->Fns().QueueSubmit2 != nullptr);
    CHECK(device->Properties().apiVersion >= VK_API_VERSION_1_3);
    DestroyDevice(device);
}
