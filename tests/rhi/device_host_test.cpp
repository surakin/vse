#include <cstring>
#include <string>
#include "sync/thread.h"
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gpu_fixture.h"
#include "log/assert.h"
#include "rhi/device_host.h"
#include "rhi/handle_pool.h"
#include "rhi/null/null_device.h"
#include "rhi/rhi_log.h"
#include "rhi/vulkan/vulkan_device.h"
#include "cstr/cstr.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;

namespace {

struct HostQuietLog {
    bool saved = LogRhi.Enabled();
    HostQuietLog() { LogRhi.SetEnabled(false); }
    ~HostQuietLog() { LogRhi.SetEnabled(saved); }
};

DeviceCreateInfo NullCreate() {
    DeviceCreateInfo info;
    info.backend = Backend::Null;
    return info;
}

// Records observer calls into a shared log.
struct Recorder final : DeviceObserver {
    std::vector<std::string>* log = nullptr;
    const char* name = "";
    bool fail_create = false;
    u32 last_epoch = 0;
    int released = 0;
    int created = 0;
    Device* device = nullptr;
    BufferHandle buffer; // a device object this observer owns, to show the release/recreate cycle

    void OnDeviceReleasing(u32 epoch) override {
        (void)epoch;
        ++released;
        log->push_back(std::string("release ") + name);
        if (buffer.IsValid() && device != nullptr) {
            device->DestroyBuffer(buffer);
        }
        buffer = BufferHandle{};
        device = nullptr;
    }
    bool OnDeviceCreated(Device* created_device, u32 epoch) override {
        last_epoch = epoch;
        device = created_device;
        if (fail_create) {
            log->push_back(std::string("fail ") + name);
            return false;
        }
        ++created;
        log->push_back(std::string("create ") + name);
        BufferDesc d;
        d.size = 64;
        d.usage = BufferUsage::Uniform;
        d.location = MemoryLocation::CpuToGpu;
        buffer = device->CreateBuffer(d);
        return buffer.IsValid();
    }
    const char* ObserverName() const override { return name; }
};

struct AssertThrown {
    std::string message;
};

AssertAction ThrowingHandler(const AssertInfo& info) {
    throw AssertThrown{info.message != nullptr ? info.message : ""};
}

struct ThrowingAsserts {
    AssertHandler previous = SetAssertHandler(&ThrowingHandler);
    ~ThrowingAsserts() { SetAssertHandler(previous); }
};

} // namespace

TEST_CASE("Handle pools of different seeds never produce equal handles", "[rhi][host][handles]") {
    constexpr u32 kSeedA = GenerationSeed(1);
    constexpr u32 kSeedB = GenerationSeed(2);
    static_assert(kSeedA != kSeedB);
    static_assert((kSeedA & 0xFFFFF) == 1, "the low 20 bits start at 1");

    auto check = [](auto tag) {
        using Tag = decltype(tag);
        HandlePool<int, Tag> a;
        HandlePool<int, Tag> b;
        a.SeedGeneration(kSeedA);
        b.SeedGeneration(kSeedB);
        auto ha = a.Allocate(1);
        auto hb = b.Allocate(2);
        REQUIRE(ha.IsValid());
        REQUIRE(hb.IsValid());
        CHECK(ha.index == hb.index);
        CHECK(ha != hb);
        CHECK(b.Get(ha) == nullptr); // a handle of pool A is not found in pool B
        CHECK(a.Get(hb) == nullptr);
        CHECK(a.Get(ha) != nullptr);
    };
    check(BufferTag{});
    check(TextureTag{});
    check(SamplerTag{});
    check(ShaderTag{});
    check(PipelineTag{});
    check(BindGroupLayoutTag{});
    check(BindGroupTag{});
}

TEST_CASE("Generation seeding keeps zero invalid and wraps past it", "[rhi][host][handles]") {
    HandlePool<int, BufferTag> pool;
    pool.SeedGeneration(0xFFFFFFFFu);
    auto h = pool.Allocate(7);
    CHECK(h.generation == 0xFFFFFFFFu);
    REQUIRE(pool.Free(h));
    auto again = pool.Allocate(8); // the slot's generation wrapped past 0 to 1
    CHECK(again.IsValid());
    CHECK(again.generation == 1);
    CHECK(pool.Get(h) == nullptr);
    pool.Free(again);
    HandlePool<int, BufferTag> zero;
    zero.SeedGeneration(0);
    CHECK(zero.Allocate(1).generation == 1);
}

TEST_CASE("A handle from one null device is rejected by the next", "[rhi][host][handles]") {
    HostQuietLog quiet;
    DeviceCreateInfo info = NullCreate();
    DevicePtr a = CreateDevice(info);
    REQUIRE(a != nullptr);
    BufferDesc bd;
    bd.size = 64;
    bd.usage = BufferUsage::Uniform;
    bd.location = MemoryLocation::CpuToGpu;
    const BufferHandle from_a = a->CreateBuffer(bd);
    TextureDesc td;
    td.width = td.height = 4;
    td.format = Format::RGBA8_UNorm;
    td.usage = TextureUsage::Sampled | TextureUsage::CopyDst;
    const TextureHandle tex_a = a->CreateTexture(td);
    SamplerDesc sd;
    const SamplerHandle smp_a = a->CreateSampler(sd);
    REQUIRE(from_a.IsValid());
    REQUIRE(tex_a.IsValid());
    REQUIRE(smp_a.IsValid());
    const u32 epoch_a = a->Epoch();

    DevicePtr b = CreateDevice(info);
    REQUIRE(b != nullptr);
    CHECK(b->Epoch() != epoch_a);
    auto* null_b = static_cast<NullDevice*>(b.Get());
    const BufferHandle from_b = b->CreateBuffer(bd);
    CHECK(from_b.index == from_a.index);
    CHECK(from_b != from_a);
    CHECK((from_b.generation >> 20) != (from_a.generation >> 20));

    null_b->ResetErrors();
    b->DestroyBuffer(from_a);
    b->DestroyTexture(tex_a);
    b->DestroySampler(smp_a);
    CHECK(null_b->ErrorCount() == 3);
    CHECK(null_b->StateOf(tex_a) == ResourceState::Undefined);
    CHECK(b->Resources().buffers == 1); // the stale destroy did not touch B's buffer
    b->DestroyBuffer(from_b);
    a->DestroyBuffer(from_a);
    a->DestroyTexture(tex_a);
    a->DestroySampler(smp_a);
}

TEST_CASE("ResourceCounts follow creation, destruction and Quiesce", "[rhi][host]") {
    DevicePtr device = CreateDevice(NullCreate());
    REQUIRE(device != nullptr);
    CHECK(device->Resources().UserObjects() == 0);
    BufferDesc bd;
    bd.size = 64;
    bd.usage = BufferUsage::Uniform;
    bd.location = MemoryLocation::CpuToGpu;
    const BufferHandle buffer = device->CreateBuffer(bd);
    CHECK(device->Resources().buffers == 1);
    CHECK(device->Resources().UserObjects() == 1);
    device->DestroyBuffer(buffer);
    device->Quiesce();
    CHECK(device->Resources().UserObjects() == 0);
    CHECK_FALSE(device->InFrame());
    device->BeginFrame();
    CHECK(device->InFrame());
    device->EndFrame();
    CHECK_FALSE(device->InFrame());
}

TEST_CASE("DeviceHost creates, recreates 100 times and leaves nothing behind", "[rhi][host]") {
    HostQuietLog quiet;
    std::vector<std::string> log; // observers outlive the host
    Recorder observer;
    DeviceHost host;
    observer.log = &log;
    observer.name = "one";
    REQUIRE(host.AddObserver(&observer, 0));

    const DeviceCreateInfo info = NullCreate();
    REQUIRE(host.Create(info, SwapchainDesc{}));
    REQUIRE(host.GetDevice() != nullptr);
    CHECK(host.GetSwapchain() == nullptr); // no window
    CHECK(observer.created == 1);
    u32 last_epoch = host.Epoch();
    CHECK(last_epoch != 0);

    for (int i = 0; i < 100; ++i) {
        INFO("iteration " << i);
        REQUIRE(host.Recreate(info, SwapchainDesc{}));
        CHECK(host.Epoch() != last_epoch);
        last_epoch = host.Epoch();
        CHECK(observer.last_epoch == last_epoch);
        // Only the observer's one buffer is alive on the new device; the old device is gone.
        const ResourceCounts c = host.GetDevice()->Resources();
        CHECK(c.UserObjects() == 1);
        CHECK(c.buffers == 1);
    }
    CHECK(host.ResetCount() == 100);
    CHECK(observer.released == 100);
    CHECK(observer.created == 101);
    host.Destroy();
    CHECK(host.GetDevice() == nullptr);
    CHECK(host.Epoch() == 0);
    CHECK(observer.released == 101);
    host.RemoveObserver(&observer);
}

TEST_CASE("DeviceHost calls observers in tier order, once each", "[rhi][host]") {
    HostQuietLog quiet;
    std::vector<std::string> log;
    Recorder low;
    Recorder mid_first;
    Recorder mid_second;
    Recorder high;
    DeviceHost host;
    for (Recorder* r : {&low, &mid_first, &mid_second, &high}) {
        r->log = &log;
    }
    low.name = "low";
    mid_first.name = "mid1";
    mid_second.name = "mid2";
    high.name = "high";
    // Registered out of order on purpose.
    REQUIRE(host.AddObserver(&high, 4));
    REQUIRE(host.AddObserver(&mid_first, 2));
    REQUIRE(host.AddObserver(&low, 0));
    REQUIRE(host.AddObserver(&mid_second, 2));

    const DeviceCreateInfo info = NullCreate();
    REQUIRE(host.Create(info, SwapchainDesc{}));
    const std::vector<std::string> created = {"create low", "create mid1", "create mid2", "create high"};
    CHECK(log == created);

    log.clear();
    REQUIRE(host.Recreate(info, SwapchainDesc{}));
    const std::vector<std::string> expected = {"release high", "release mid2", "release mid1", "release low",
                                               "create low",   "create mid1",  "create mid2",  "create high"};
    CHECK(log == expected);
    host.Destroy();
}

TEST_CASE("A failing create leaves the host empty and says why", "[rhi][host]") {
    HostQuietLog quiet;
    std::vector<std::string> log; // observers outlive the host: its destructor notifies them
    Recorder observer;
    DeviceHost host;
    observer.log = &log;
    observer.name = "obs";
    host.AddObserver(&observer, 1);

    DeviceCreateInfo good = NullCreate();
    REQUIRE(host.Create(good, SwapchainDesc{}));

    DeviceCreateInfo bad = good;
    bad.adapter = "no-such-adapter";
    CHECK_FALSE(host.Recreate(bad, SwapchainDesc{}));
    CHECK(host.GetDevice() == nullptr);
    CHECK(host.Epoch() == 0);
    CHECK(CStrLength(host.LastError()) > 0);
    CHECK(host.ResetCount() == 0);

    // Rolling back is a Create with the old description.
    REQUIRE(host.Create(good, SwapchainDesc{}));
    CHECK(host.GetDevice() != nullptr);
    CHECK(CStrLength(host.LastError()) == 0);

    // An observer that cannot rebuild fails the whole call and everything is released again.
    observer.fail_create = true;
    log.clear();
    CHECK_FALSE(host.Recreate(good, SwapchainDesc{}));
    CHECK(host.GetDevice() == nullptr);
    CHECK(CStrFind(host.LastError(), "obs") != nullptr);
    observer.fail_create = false;
    REQUIRE(host.Create(good, SwapchainDesc{}));
    CHECK(host.GetDevice()->Resources().UserObjects() == 1);
}

TEST_CASE("DeviceHost refuses Recreate inside a frame or off the owning thread", "[rhi][host]") {
    HostQuietLog quiet;
    ThrowingAsserts asserts;
    DeviceHost host;
    const DeviceCreateInfo info = NullCreate();
    REQUIRE(host.Create(info, SwapchainDesc{}));
    const u32 epoch = host.Epoch();

    host.GetDevice()->BeginFrame();
    bool threw = false;
    try {
        host.Recreate(info, SwapchainDesc{});
    } catch (const AssertThrown& e) {
        threw = true;
        CHECK(e.message.find("BeginFrame") != std::string::npos);
    }
    CHECK(threw);
    CHECK(host.Epoch() == epoch); // untouched
    host.GetDevice()->EndFrame();

    bool other_threw = false;
    Thread other([&] {
        try {
            host.Recreate(info, SwapchainDesc{});
        } catch (const AssertThrown& e) {
            other_threw = e.message.find("thread") != std::string::npos;
        }
    });
    other.Join();
    CHECK(other_threw);
    CHECK(host.Epoch() == epoch);
    CHECK(host.Recreate(info, SwapchainDesc{}));
}

TEST_CASE("Adding or removing an observer during a reset is rejected", "[rhi][host]") {
    HostQuietLog quiet;
    ThrowingAsserts asserts;
    DeviceHost host;
    struct Meddler final : DeviceObserver {
        DeviceHost* host = nullptr;
        Recorder* other = nullptr;
        bool threw = false;
        void OnDeviceReleasing(u32) override {}
        bool OnDeviceCreated(Device*, u32) override {
            try {
                host->AddObserver(other, 0);
            } catch (const AssertThrown&) {
                threw = true;
            }
            return true;
        }
        const char* ObserverName() const override { return "meddler"; }
    } meddler;
    std::vector<std::string> log;
    Recorder other;
    other.log = &log;
    meddler.host = &host;
    meddler.other = &other;
    host.AddObserver(&meddler, 0);
    REQUIRE(host.Create(NullCreate(), SwapchainDesc{}));
    CHECK(meddler.threw);
    host.Destroy();
}

TEST_CASE("An observer added to a live host is told about the device at once", "[rhi][host]") {
    HostQuietLog quiet;
    std::vector<std::string> log;
    Recorder late;
    DeviceHost host;
    REQUIRE(host.Create(NullCreate(), SwapchainDesc{}));
    late.log = &log;
    late.name = "late";
    REQUIRE(host.AddObserver(&late, 3));
    CHECK(late.created == 1);
    CHECK(late.buffer.IsValid());
    host.Destroy(); // releases it
    CHECK(late.released == 1);
    CHECK_FALSE(late.buffer.IsValid());
}

TEST_CASE("Vulkan devices are destroyed and recreated with nothing left over", "[rhi][host][vulkan][gpu]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    vulkan::VulkanDevice::ResetLeakCounts();

    DeviceCreateInfo info;
    info.application_name = "host-test";
    info.validation = ValidationSetting::On;
    std::vector<std::string> log;
    Recorder observer;
    DeviceHost host;
    observer.log = &log;
    observer.name = "gpu";
    host.AddObserver(&observer, 0);
    if (!host.Create(info, SwapchainDesc{})) {
        if (gpu_test::GpuRequired()) {
            FAIL("ENGINE_RHI_REQUIRE_GPU=1 but no Vulkan device could be created");
        }
        SKIP("no suitable Vulkan device");
    }
    u32 last = host.Epoch();
    for (int i = 0; i < 20; ++i) {
        INFO("iteration " << i);
        REQUIRE(host.Recreate(info, SwapchainDesc{}));
        CHECK(host.Epoch() != last);
        last = host.Epoch();
        const ResourceCounts c = host.GetDevice()->Resources();
        CHECK(c.buffers == 1);
        CHECK(c.UserObjects() == 1);
    }
    host.Destroy();
    const auto leaks = vulkan::VulkanDevice::GetLeakCounts();
    CHECK(leaks.user_objects == 0);
    CHECK(leaks.internal_objects == 0);
}

TEST_CASE("A stale Vulkan buffer handle is not valid on the next device", "[rhi][host][vulkan][gpu]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    DeviceCreateInfo info;
    DevicePtr a = CreateDevice(info);
    if (a == nullptr) {
        SKIP("no suitable Vulkan device");
    }
    BufferDesc bd;
    bd.size = 64;
    bd.usage = BufferUsage::Uniform;
    bd.location = MemoryLocation::CpuToGpu;
    const BufferHandle from_a = a->CreateBuffer(bd);
    REQUIRE(from_a.IsValid());
    DevicePtr b = CreateDevice(info);
    REQUIRE(b != nullptr);
    const BufferHandle from_b = b->CreateBuffer(bd);
    REQUIRE(from_b.IsValid());
    CHECK(from_a != from_b);
    CHECK((from_a.generation >> 20) != (from_b.generation >> 20));
    // A stale handle given to the device is rejected by an assert, which cannot be caught here without unwinding
    // through the device lock; the null-device test above exercises the rejection.
    b->DestroyBuffer(from_b);
    a->DestroyBuffer(from_a);
}

TEST_CASE("Removing an observer that holds objects releases them first", "[rhi][host]") {
    HostQuietLog quiet;
    std::vector<std::string> log;
    Recorder observer;
    DeviceHost host;
    observer.log = &log;
    observer.name = "gone";
    REQUIRE(host.Create(NullCreate(), SwapchainDesc{}));
    REQUIRE(host.AddObserver(&observer, 0));
    CHECK(host.GetDevice()->Resources().buffers == 1);
    host.RemoveObserver(&observer);
    CHECK(observer.released == 1);
    CHECK(host.GetDevice()->Resources().buffers == 0);
}
