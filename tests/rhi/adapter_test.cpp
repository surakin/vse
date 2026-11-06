#include <cstdlib>
#include <cstring>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "gpu_fixture.h"
#include "render_target.h"
#include "rhi/adapter.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"
#include "rhi/vulkan/vk_device_select.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using engine_placeholder::rhi::vulkan::GpuKind;
using engine_placeholder::rhi::vulkan::PhysicalDeviceInfo;

namespace {

struct AdapterQuietLog {
    bool saved = LogRhi.Enabled();
    AdapterQuietLog() { LogRhi.SetEnabled(false); }
    ~AdapterQuietLog() { LogRhi.SetEnabled(saved); }
};

// Restores the default null adapter list however a test ends.
struct NullAdaptersGuard {
    ~NullAdaptersGuard() { SetNullAdapters(nullptr); }
};

DeviceCreateInfo NullInfo() {
    DeviceCreateInfo info;
    info.backend = Backend::Null;
    return info;
}

PhysicalDeviceInfo GoodGpu(const char* name, GpuKind kind, u32 vendor, u32 device, u8 uuid_seed) {
    PhysicalDeviceInfo info;
    std::strncpy(info.name, name, sizeof(info.name) - 1);
    info.kind = kind;
    info.api_version = (1u << 22) | (3u << 12);
    info.dynamic_rendering = info.synchronization2 = info.timeline_semaphore = info.swapchain_extension = true;
    info.vendor_id = vendor;
    info.device_id = device;
    for (u8 i = 0; i < 16; ++i) {
        info.uuid[i] = static_cast<u8>(uuid_seed + i);
    }
    info.queue_family_count = 1;
    info.families[0].queue_count = 1;
    info.families[0].graphics = info.families[0].compute = info.families[0].transfer = info.families[0].present = true;
    return info;
}

} // namespace

TEST_CASE("The null backend lists two fake adapters", "[rhi][adapter]") {
    AdapterList list;
    REQUIRE(EnumerateAdapters(NullInfo(), &list));
    REQUIRE(list.count == 2);
    CHECK(std::strcmp(list.items[0].id, "null-discrete") == 0);
    CHECK(list.items[0].kind == AdapterKind::Discrete);
    CHECK(list.items[0].dedicated_memory_bytes > 0);
    CHECK(list.items[0].suitable);
    CHECK(list.items[0].unsuitable_reason == nullptr);
    CHECK(std::strcmp(list.items[1].id, "null-integrated") == 0);
    CHECK(list.items[1].kind == AdapterKind::Integrated);
}

TEST_CASE("Adapter ids are identical across enumerations", "[rhi][adapter]") {
    AdapterList a;
    AdapterList b;
    REQUIRE(EnumerateAdapters(NullInfo(), &a));
    REQUIRE(EnumerateAdapters(NullInfo(), &b));
    REQUIRE(a.count == b.count);
    for (u32 i = 0; i < a.count; ++i) {
        CHECK(std::strcmp(a.items[i].id, b.items[i].id) == 0);
    }
}

TEST_CASE("MatchAdapter finds an adapter by id, index or name", "[rhi][adapter]") {
    AdapterList list;
    REQUIRE(EnumerateAdapters(NullInfo(), &list));
    CHECK(MatchAdapter(list.items, list.count, "null-integrated") == 1);
    CHECK(MatchAdapter(list.items, list.count, "null-discrete") == 0);
    CHECK(MatchAdapter(list.items, list.count, "1") == 1);
    CHECK(MatchAdapter(list.items, list.count, "0") == 0);
    CHECK(MatchAdapter(list.items, list.count, "INTEGRATED gpu") == 1);
    CHECK(MatchAdapter(list.items, list.count, "discrete") == 0);
    CHECK(MatchAdapter(list.items, list.count, "2") == -1); // out of range as an index, no such name
    CHECK(MatchAdapter(list.items, list.count, "nothing") == -1);
    CHECK(MatchAdapter(list.items, list.count, "") == -1);
    CHECK(MatchAdapter(list.items, list.count, nullptr) == -1);
}

TEST_CASE("The null backend fails when no adapter is suitable", "[rhi][adapter]") {
    NullAdaptersGuard guard;
    AdapterQuietLog quiet;
    AdapterList list;
    DefaultNullAdapters(&list);
    list.items[0].suitable = list.items[1].suitable = false;
    SetNullAdapters(&list);
    CHECK(CreateDevice(NullInfo()) == nullptr);
}

TEST_CASE("An id wins over an index that spells the same digits", "[rhi][adapter]") {
    NullAdaptersGuard guard;
    AdapterList list;
    DefaultNullAdapters(&list);
    std::strcpy(list.items[0].id, "1"); // an id that looks like the index of the second adapter
    SetNullAdapters(&list);
    AdapterList got;
    REQUIRE(EnumerateAdapters(NullInfo(), &got));
    CHECK(MatchAdapter(got.items, got.count, "1") == 0);
}

TEST_CASE("CreateDevice honors DeviceCreateInfo::adapter on the null backend", "[rhi][adapter]") {
    DeviceCreateInfo info = NullInfo();
    SECTION("no request picks the first suitable adapter") {
        DevicePtr device = CreateDevice(info);
        REQUIRE(device != nullptr);
        CHECK(std::strcmp(device->Adapter().id, "null-discrete") == 0);
    }
    SECTION("by id") {
        info.adapter = "null-integrated";
        DevicePtr device = CreateDevice(info);
        REQUIRE(device != nullptr);
        CHECK(std::strcmp(device->Adapter().id, "null-integrated") == 0);
        CHECK(device->Adapter().kind == AdapterKind::Integrated);
    }
    SECTION("by name substring") {
        info.adapter = "integrated";
        DevicePtr device = CreateDevice(info);
        REQUIRE(device != nullptr);
        CHECK(std::strcmp(device->Adapter().id, "null-integrated") == 0);
    }
    SECTION("by index") {
        info.adapter = "1";
        DevicePtr device = CreateDevice(info);
        REQUIRE(device != nullptr);
        CHECK(std::strcmp(device->Adapter().id, "null-integrated") == 0);
    }
    SECTION("an unknown adapter fails and does not fall back") {
        AdapterQuietLog quiet;
        info.adapter = "no-such-gpu";
        CHECK(CreateDevice(info) == nullptr);
    }
}

TEST_CASE("The null backend rejects an unsuitable adapter by name", "[rhi][adapter]") {
    NullAdaptersGuard guard;
    AdapterQuietLog quiet;
    AdapterList list;
    DefaultNullAdapters(&list);
    list.items[0].suitable = false;
    list.items[0].unsuitable_reason = "lacks everything";
    SetNullAdapters(&list);

    DeviceCreateInfo info = NullInfo();
    info.adapter = "null-discrete";
    CHECK(CreateDevice(info) == nullptr);

    info.adapter = nullptr; // "best" skips the unsuitable one
    DevicePtr device = CreateDevice(info);
    REQUIRE(device != nullptr);
    CHECK(std::strcmp(device->Adapter().id, "null-integrated") == 0);
}

TEST_CASE("Vulkan adapter ids come from vendor, device and UUID, not from the order", "[rhi][adapter][vulkan][select]") {
    PhysicalDeviceInfo a = GoodGpu("GPU A", GpuKind::Discrete, 0x10de, 0x2504, 1);
    PhysicalDeviceInfo b = GoodGpu("GPU B", GpuKind::Integrated, 0x8086, 0x9a49, 40);
    const AdapterInfo ia = vulkan::MakeAdapterInfo(a, false);
    const AdapterInfo ib = vulkan::MakeAdapterInfo(b, false);
    CHECK(std::strncmp(ia.id, "vk-10de-2504-", 13) == 0);
    CHECK(std::strncmp(ib.id, "vk-8086-9a49-", 13) == 0);
    CHECK(std::strcmp(ia.id, ib.id) != 0);
    CHECK(std::strlen(ia.id) == 13 + 32);
    // The same GPU described again gives the same id; a different UUID (two identical cards) gives a different one.
    CHECK(std::strcmp(vulkan::MakeAdapterInfo(a, false).id, ia.id) == 0);
    PhysicalDeviceInfo twin = a;
    twin.uuid[15] ^= 0xFF;
    CHECK(std::strcmp(vulkan::MakeAdapterInfo(twin, false).id, ia.id) != 0);

    // Selection by id is independent of the position: a GPU added in front does not change what the id selects.
    PhysicalDeviceInfo before[2] = {a, b};
    PhysicalDeviceInfo after[3] = {GoodGpu("GPU C", GpuKind::Discrete, 0x1002, 0x73bf, 90), a, b};
    CHECK(vulkan::SelectPhysicalDeviceFor(before, 2, false, ib.id, "test") == 1);
    CHECK(vulkan::SelectPhysicalDeviceFor(after, 3, false, ib.id, "test") == 2);
}

TEST_CASE("Unsuitable Vulkan adapters carry a reason, and presentability follows the window", "[rhi][adapter][vulkan][select]") {
    PhysicalDeviceInfo gpu = GoodGpu("GPU", GpuKind::Discrete, 1, 2, 3);
    AdapterInfo info = vulkan::MakeAdapterInfo(gpu, false);
    CHECK(info.suitable);
    CHECK(info.unsuitable_reason == nullptr);
    CHECK_FALSE(info.can_present); // no window, nothing to present to

    info = vulkan::MakeAdapterInfo(gpu, true);
    CHECK(info.suitable);
    CHECK(info.can_present);

    PhysicalDeviceInfo old = gpu;
    old.api_version = (1u << 22) | (1u << 12);
    info = vulkan::MakeAdapterInfo(old, false);
    CHECK_FALSE(info.suitable);
    REQUIRE(info.unsuitable_reason != nullptr);
    CHECK(std::strstr(info.unsuitable_reason, "1.3") != nullptr);

    PhysicalDeviceInfo no_present = gpu;
    no_present.families[0].present = false;
    info = vulkan::MakeAdapterInfo(no_present, true);
    CHECK_FALSE(info.suitable);
    CHECK_FALSE(info.can_present);
    REQUIRE(info.unsuitable_reason != nullptr);
    CHECK(std::strstr(info.unsuitable_reason, "present") != nullptr);
    CHECK(vulkan::MakeAdapterInfo(no_present, false).suitable);

    PhysicalDeviceInfo cpu = GoodGpu("llvmpipe", GpuKind::Cpu, 0x10005, 0, 0);
    CHECK(vulkan::MakeAdapterInfo(cpu, false).kind == AdapterKind::Software);
}

TEST_CASE("An explicit request does not fall back when it names an unsuitable GPU", "[rhi][adapter][vulkan][select]") {
    AdapterQuietLog quiet;
    PhysicalDeviceInfo devices[2] = {GoodGpu("fast", GpuKind::Discrete, 1, 1, 1), GoodGpu("slow", GpuKind::Integrated, 2, 2, 2)};
    devices[0].timeline_semaphore = false;
    char id[kAdapterIdBytes];
    vulkan::MakeAdapterId(devices[0], id, sizeof(id));
    CHECK(vulkan::SelectPhysicalDeviceFor(devices, 2, false, id, "test") == -1);
    CHECK(vulkan::SelectPhysicalDeviceFor(devices, 2, false, nullptr, "test") == 1); // "best" still works
}

TEST_CASE("Real adapters are listed and a device is created on each presentable one", "[rhi][adapter][vulkan][gpu]") {
    gpu_test::GpuFixture fx;
    fx.Require();

    DeviceCreateInfo info;
    info.application_name = "adapter-test";
    AdapterList list;
    REQUIRE(EnumerateAdapters(info, &list));
    REQUIRE(list.count >= 1);
    AdapterList again;
    REQUIRE(EnumerateAdapters(info, &again));
    REQUIRE(again.count == list.count);

    u32 tried = 0;
    for (u32 i = 0; i < list.count; ++i) {
        INFO("adapter " << list.items[i].name << " (" << list.items[i].id << ")");
        CHECK(std::strcmp(list.items[i].id, again.items[i].id) == 0);
        CHECK(list.items[i].id[0] != '\0');
        if (!list.items[i].suitable) {
            REQUIRE(list.items[i].unsuitable_reason != nullptr);
            continue;
        }
        DeviceCreateInfo create = info;
        create.adapter = list.items[i].id;
        // The explicit request wins over the environment.
        const char* saved = std::getenv("ENGINE_RHI_GPU");
        const std::string saved_copy = saved != nullptr ? saved : "";
        setenv("ENGINE_RHI_GPU", "no-such-gpu-name", 1);
        DevicePtr device = CreateDevice(create);
        if (saved != nullptr) {
            setenv("ENGINE_RHI_GPU", saved_copy.c_str(), 1);
        } else {
            unsetenv("ENGINE_RHI_GPU");
        }
        REQUIRE(device != nullptr);
        CHECK(std::strcmp(device->Adapter().id, list.items[i].id) == 0);

        gpu_test::RenderTarget target(device.Get(), 16, 16);
        REQUIRE(target.Valid());
        device->BeginFrame();
        CommandList* cl = device->CreateCommandList();
        cl->Begin();
        cl->Barrier(target.Texture(), ResourceState::Undefined, ResourceState::ColorAttachment);
        RenderingDesc pass;
        pass.color_count = 1;
        pass.color[0].texture = target.Texture();
        pass.color[0].clear_color[0] = 1.0f;
        pass.color[0].clear_color[3] = 1.0f;
        cl->BeginRendering(pass);
        cl->EndRendering();
        cl->End();
        CommandList* lists[] = {cl};
        CHECK(device->Submit(lists, 1) != 0);
        device->EndFrame();
        device->WaitIdle();
        gpu_test::Image image;
        REQUIRE(target.Readback(&image));
        CHECK(image.rgba[0] == 255);
        CHECK(image.rgba[1] == 0);
        ++tried;
    }
    REQUIRE(tried >= 1);
    if (list.count == 1) {
        WARN("single-adapter machine: the adapter test ran once");
    }
}

TEST_CASE("A requested Vulkan adapter that does not exist fails", "[rhi][adapter][vulkan][gpu]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    AdapterQuietLog quiet;
    DeviceCreateInfo info;
    info.adapter = "vk-0000-0000-00000000000000000000000000000000";
    CHECK(CreateDevice(info) == nullptr);
}
