// Phase 8 Day 108: the device-resource registry and the recreatable owners (meshes, textures, samplers).
#include <catch2/catch_test_macros.hpp>

#include "byte_vector.h"
#include "renderer/device_resources.h"
#include "renderer/mesh.h"
#include "renderer/renderer_log.h"
#include "renderer/texture.h"
#include "rhi/device_host.h"
#include "rhi/null/null_device.h"
#include "rhi/rhi_log.h"
#include "cstr/cstr.h"
#include "cstr/mem_ops.h"
#include "test_text.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

namespace
{

struct ResQuietLog
{
    bool saved_rhi = rhi::LogRhi.Enabled();
    ResQuietLog() { rhi::LogRhi.SetEnabled(false); }
    ~ResQuietLog() { rhi::LogRhi.SetEnabled(saved_rhi); }
};

rhi::DeviceCreateInfo NullCreate()
{
    rhi::DeviceCreateInfo info;
    info.backend = rhi::Backend::Null;
    return info;
}

// An owner that records what the registry asks of it.
struct FakeOwner final : Recreatable
{
    engine_test::TextList* log = nullptr;
    const char* name = "fake";
    bool fail_recreate = false;
    bool live = true;
    int releases = 0;
    int recreates = 0;
    ScopedRegistration registration;
    DeviceResources* register_during_reset = nullptr;
    bool register_refused = false;

    void Release() override
    {
        ++releases;
        live = false;
        log->Push(engine_test::Concat("release ", name).View());
    }
    bool Recreate(rhi::Device*) override
    {
        if (register_during_reset != nullptr)
        {
            FakeOwner other;
            other.log = log;
            register_refused = !register_during_reset->Register(&other, 0);
            if (!register_refused)
            {
                register_during_reset->Unregister(&other);
            }
        }
        if (fail_recreate)
        {
            log->Push(engine_test::Concat("fail ", name).View());
            return false;
        }
        ++recreates;
        live = true;
        log->Push(engine_test::Concat("recreate ", name).View());
        return true;
    }
    bool Live() const override { return live; }
    const char* RecreatableName() const override { return name; }
};

struct ResetRig
{
    engine_test::TextList log;
    DeviceResources registry;
    rhi::DeviceHost host;
    ResetRig() { host.AddObserver(&registry, 0); }
    bool Create() { return host.Create(NullCreate(), rhi::SwapchainDesc{}); }
    bool Reset() { return host.Recreate(NullCreate(), rhi::SwapchainDesc{}); }
};

}  // namespace

TEST_CASE("Owners register and unregister, also by scope", "[device_resources]")
{
    ResetRig rig;
    REQUIRE(rig.Create());
    CHECK(rig.registry.Count() == 0);
    {
        FakeOwner a;
        a.log = &rig.log;
        REQUIRE(a.registration.Attach(&rig.registry, &a, 1));
        CHECK(rig.registry.Count() == 1);
        CHECK(a.registration.Registered());
    }
    CHECK(rig.registry.Count() == 0);  // a destroyed owner never gets a callback
    CHECK(rig.Reset());
    CHECK(rig.log.Empty());
}

TEST_CASE("A reset releases from the highest tier down and recreates from the lowest up", "[device_resources]")
{
    ResetRig rig;
    FakeOwner t0;
    FakeOwner t1a;
    FakeOwner t1b;
    FakeOwner t4;
    for (FakeOwner* o : {&t0, &t1a, &t1b, &t4})
    {
        o->log = &rig.log;
    }
    t0.name = "t0";
    t1a.name = "t1a";
    t1b.name = "t1b";
    t4.name = "t4";
    REQUIRE(rig.Create());
    // Registered out of order on purpose.
    REQUIRE(t4.registration.Attach(&rig.registry, &t4, tiers::kPasses));
    REQUIRE(t1a.registration.Attach(&rig.registry, &t1a, tiers::kMeshes));
    REQUIRE(t0.registration.Attach(&rig.registry, &t0, tiers::kTextures));
    REQUIRE(t1b.registration.Attach(&rig.registry, &t1b, tiers::kMeshes));

    REQUIRE(rig.Reset());
    CHECK(rig.log.SameAs({"release t4", "release t1b", "release t1a", "release t0", "recreate t0", "recreate t1a",
                          "recreate t1b", "recreate t4"}));
    CHECK(rig.registry.CurrentDevice() == rig.host.GetDevice());

    rig.log.Clear();
    REQUIRE(rig.Reset());  // twice: each owner is released and recreated once per reset
    CHECK(rig.log.Size() == 8);
    CHECK(t0.recreates == 2);
}

TEST_CASE("A failing recreate stops the sequence, releases everything and names the owner", "[device_resources]")
{
    ResQuietLog quiet;
    ResetRig rig;
    FakeOwner low;
    FakeOwner bad;
    FakeOwner high;
    low.log = bad.log = high.log = &rig.log;
    low.name = "low";
    bad.name = "bad";
    high.name = "high";
    REQUIRE(rig.Create());
    low.registration.Attach(&rig.registry, &low, 0);
    bad.registration.Attach(&rig.registry, &bad, 1);
    high.registration.Attach(&rig.registry, &high, 2);

    bad.fail_recreate = true;
    rig.log.Clear();
    CHECK_FALSE(rig.Reset());
    CHECK(CStrCompare(rig.registry.FailedOwner(), "bad") == 0);
    CHECK(rig.host.GetDevice() == nullptr);
    CHECK_FALSE(low.live);       // low was built, then released again: a half-built set is never used
    CHECK(high.recreates == 0);  // never reached
    CHECK(rig.log.Back().View() == StringView("release low"));

    // Retry after the cause is gone: starts from released and succeeds.
    bad.fail_recreate = false;
    REQUIRE(rig.Create());
    CHECK(rig.registry.FailedOwner() == nullptr);
    CHECK(low.live);
    CHECK(bad.live);
    CHECK(high.live);
}

TEST_CASE("Registering during a reset is rejected", "[device_resources]")
{
    ResQuietLog quiet;
    ResetRig rig;
    FakeOwner owner;
    owner.log = &rig.log;
    REQUIRE(rig.Create());
    owner.registration.Attach(&rig.registry, &owner, 0);
    owner.register_during_reset = &rig.registry;
    REQUIRE(rig.Reset());
    CHECK(owner.register_refused);
    CHECK(rig.registry.Count() == 1);
}

TEST_CASE("A mesh survives a device change with identical content and the same sort id", "[device_resources][mesh]")
{
    ResQuietLog quiet;
    ResetRig rig;
    REQUIRE(rig.Create());
    MeshData data;
    REQUIRE(MakeSphereMesh(1.0f, 12, 6, &data) == nullptr);
    renderer_test::ByteVector vertices;
    vertices.Resize(static_cast<usize>(data.VertexBytes()));
    MemCopy(vertices.Data(), data.Vertices(), vertices.Size());

    SECTION("a copy of the geometry is the source")
    {
        GpuMesh mesh;
        REQUIRE(mesh.Create(rig.host.GetDevice(), data, "sphere"));
        REQUIRE(mesh.MakeRecreatable(&rig.registry, data, "sphere"));
        const u32 sort_id = mesh.SortId();
        const rhi::BufferHandle old_vb = mesh.VertexBuffer();
        REQUIRE(rig.Reset());
        REQUIRE(mesh.Valid());
        CHECK(mesh.VertexBuffer() != old_vb);  // the handle changed ...
        CHECK(mesh.SortId() == sort_id);       // ... the identity did not
        CHECK(mesh.VertexCount() == data.VertexCount());
        renderer_test::ByteVector after;
        after.Resize(vertices.Size());
        auto* null_device = static_cast<rhi::NullDevice*>(rig.host.GetDevice());
        REQUIRE(null_device->PeekBuffer(mesh.VertexBuffer(), 0, after.Data(), after.Size()));
        CHECK(renderer_test::SameBytes(after, vertices));
        CHECK(rig.host.GetDevice()->Resources().buffers == 2);
    }
    SECTION("owned source, moved around")
    {
        GpuMesh original;
        REQUIRE(original.Create(rig.host.GetDevice(), data, "owned"));
        REQUIRE(original.MakeRecreatable(&rig.registry, data, "owned"));
        GpuMesh moved = static_cast<GpuMesh&&>(original);  // the registration follows the move
        CHECK(rig.registry.Count() == 1);
        REQUIRE(rig.Reset());
        REQUIRE(moved.Valid());
        CHECK_FALSE(original.Valid());
        renderer_test::ByteVector after;
        after.Resize(vertices.Size());
        auto* null_device = static_cast<rhi::NullDevice*>(rig.host.GetDevice());
        REQUIRE(null_device->PeekBuffer(moved.VertexBuffer(), 0, after.Data(), after.Size()));
        CHECK(renderer_test::SameBytes(after, vertices));
    }
    SECTION("a point list survives a reset like any other topology")
    {
        const u16 idx[3] = {0, 1, 2};
        MeshData points;
        REQUIRE(points.Set(data.Layout(), 0, data.Vertices(), data.VertexCount(), rhi::IndexType::UInt16, idx, 3,
                           rhi::PrimitiveTopology::PointList) == nullptr);
        GpuMesh mesh;
        REQUIRE(mesh.Create(rig.host.GetDevice(), points, "points"));
        REQUIRE(mesh.MakeRecreatable(&rig.registry, points, "points"));
        REQUIRE(rig.Reset());
        CHECK(mesh.Valid());
        CHECK(mesh.Topology() == rhi::PrimitiveTopology::PointList);
    }
    SECTION("a mesh with no source fails the reset and says which")
    {
        GpuMesh mesh;
        REQUIRE(mesh.Create(rig.host.GetDevice(), data, "bare"));
        // Registered through a tiny wrapper that has no source: simulate by making it recreatable then losing the
        // source.
        REQUIRE(mesh.MakeRecreatable(&rig.registry, data, "bare"));
        MeshData small;
        REQUIRE(MakeCubeMesh(1.0f, &small) == nullptr);
        mesh.MakeRecreatable(&rig.registry, small, "bare");  // wrong geometry
        CHECK_FALSE(rig.Reset());
        CHECK(CStrCompare(rig.registry.FailedOwner(), "bare") == 0);
    }
}

TEST_CASE("Destroying meshes and textures leaves nothing on the device", "[device_resources][mesh]")
{
    ResetRig rig;
    REQUIRE(rig.Create());
    {
        MeshData data;
        REQUIRE(MakeCubeMesh(1.0f, &data) == nullptr);
        GpuMesh mesh;
        REQUIRE(mesh.Create(rig.host.GetDevice(), data, "cube"));
        REQUIRE(mesh.MakeRecreatable(&rig.registry, data, "cube"));
        GpuTexture texture;
        TextureImage image;
        image.desc.width = image.desc.height = 4;
        image.desc.format = rhi::Format::RGBA8_UNorm;
        image.desc.usage = rhi::TextureUsage::Sampled;
        image.pixels.Resize(64);
        REQUIRE(texture.Create(rig.host.GetDevice(), static_cast<TextureImage&&>(image), &rig.registry, "tex"));
        GpuSampler sampler;
        REQUIRE(sampler.Create(rig.host.GetDevice(), rhi::SamplerDesc{}, &rig.registry, "smp"));
        CHECK(rig.registry.Count() == 3);
    }
    CHECK(rig.registry.Count() == 0);
    rig.host.GetDevice()->Quiesce();
    CHECK(rig.host.GetDevice()->Resources().UserObjects() == 0);
}

TEST_CASE("Textures and samplers are rebuilt from their pixels or a loader", "[device_resources][texture]")
{
    ResQuietLog quiet;
    ResetRig rig;
    REQUIRE(rig.Create());
    GpuTexture from_pixels;
    TextureImage image;
    image.desc.width = image.desc.height = 8;
    image.desc.mip_levels = 4;
    image.desc.format = rhi::Format::RGBA8_UNorm;
    image.desc.usage = rhi::TextureUsage::Sampled;
    image.pixels.Resize(8 * 8 * 4);
    image.generate_mips = true;
    REQUIRE(from_pixels.Create(rig.host.GetDevice(), static_cast<TextureImage&&>(image), &rig.registry, "pixels"));
    CHECK(from_pixels.Desc().mip_levels == 4);

    struct Loader
    {
        int calls = 0;
        static bool Load(void* user, TextureImage* out)
        {
            static_cast<Loader*>(user)->calls++;
            out->desc.width = out->desc.height = 4;
            out->desc.format = rhi::Format::RGBA8_UNorm;
            out->desc.usage = rhi::TextureUsage::Sampled;
            out->pixels.Resize(64);
            return true;
        }
    } loader;
    GpuTexture from_loader;
    REQUIRE(from_loader.CreateFromLoader(rig.host.GetDevice(), &Loader::Load, &loader, &rig.registry, "loaded"));
    GpuSampler sampler;
    REQUIRE(sampler.Create(rig.host.GetDevice(), rhi::SamplerDesc{}, &rig.registry, "smp"));
    CHECK(loader.calls == 1);

    const rhi::TextureHandle old_handle = from_pixels.Handle();
    REQUIRE(rig.Reset());
    CHECK(from_pixels.Valid());
    CHECK(from_pixels.Handle() != old_handle);
    CHECK(from_pixels.Desc().mip_levels == 4);
    CHECK(from_loader.Valid());
    CHECK(loader.calls == 2);  // the loader runs at every recreate
    CHECK(sampler.Valid());
    CHECK(rig.host.GetDevice()->Resources().textures == 2);
    CHECK(rig.host.GetDevice()->Resources().samplers == 1);
}

TEST_CASE("A device with smaller limits fails the recreate with a reason instead of crashing",
          "[device_resources][texture]")
{
    ResQuietLog quiet;
    DeviceResources registry;
    rhi::NullDeviceOptions big;
    rhi::NullDevicePtr first = rhi::CreateNullDevice(big);
    REQUIRE(registry.OnDeviceCreated(first.Get(), first->Epoch()));  // the registry learns the device
    GpuTexture texture;
    TextureImage image;
    image.desc.width = image.desc.height = 64;
    image.desc.format = rhi::Format::RGBA8_UNorm;
    image.desc.usage = rhi::TextureUsage::Sampled;
    image.pixels.Resize(64 * 64 * 4);
    REQUIRE(texture.Create(first.Get(), static_cast<TextureImage&&>(image), &registry, "big"));
    registry.OnDeviceReleasing(first->Epoch());
    CHECK_FALSE(texture.Valid());

    rhi::NullDeviceOptions small;
    small.caps.max_texture_dimension_2d = 32;
    rhi::NullDevicePtr second = rhi::CreateNullDevice(small);
    CHECK_FALSE(registry.OnDeviceCreated(second.Get(), second->Epoch()));
    CHECK(CStrCompare(registry.FailedOwner(), "big") == 0);
    CHECK_FALSE(texture.Valid());
    CHECK(second->Resources().UserObjects() == 0);

    rhi::NullDevicePtr third = rhi::CreateNullDevice(big);
    CHECK(registry.OnDeviceCreated(third.Get(), third->Epoch()));  // retry on a device that can hold it
    CHECK(texture.Valid());
    texture.Destroy();  // before the devices go
}

TEST_CASE("Texture content is identical after a reset on a real device", "[device_resources][texture][vulkan][gpu]")
{
    ResQuietLog quiet;
    DeviceResources registry;
    rhi::DeviceHost host;
    host.AddObserver(&registry, 0);
    rhi::DeviceCreateInfo info;
    if (!host.Create(info, rhi::SwapchainDesc{}))
    {
        SKIP("no GPU backend can make a device here (Vulkan or Direct3D 12; none on macOS before Metal)");
    }
    GpuTexture texture;
    TextureImage image;
    image.desc.width = image.desc.height = 8;
    image.desc.format = rhi::Format::RGBA8_UNorm;
    image.desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopySrc;
    image.pixels.Resize(8 * 8 * 4);
    for (usize i = 0; i < image.pixels.Size(); ++i)
    {
        image.pixels[i] = static_cast<u8>(i * 7 + 3);
    }
    Vector<u8> expected;
    expected.Resize(image.pixels.Size());
    for (usize i = 0; i < expected.Size(); ++i)
    {
        expected[i] = image.pixels[i];
    }
    REQUIRE(texture.Create(host.GetDevice(), static_cast<TextureImage&&>(image), &registry, "pattern"));
    REQUIRE(host.Recreate(info, rhi::SwapchainDesc{}));
    REQUIRE(texture.Valid());
    Vector<u8> got;
    got.Resize(expected.Size());
    REQUIRE(host.GetDevice()->ReadbackTexture(texture.Handle(), 0, 0, got.Data(), got.Size(),
                                              rhi::ResourceState::ShaderRead));
    CHECK(MemCompare(got.Data(), expected.Data(), expected.Size()) == 0);
    texture.Destroy();
    host.Destroy();
}

TEST_CASE("A mesh that was ready before a reset is ready after it", "[device_resources][mesh]")
{
    ResQuietLog quiet;
    ResetRig rig;
    REQUIRE(rig.Create());
    MeshData data;
    REQUIRE(MakeCubeMesh(1.0f, &data) == nullptr);
    GpuMesh mesh;
    REQUIRE(mesh.Create(rig.host.GetDevice(), data, "cube"));
    REQUIRE(mesh.MakeRecreatable(&rig.registry, data, "cube"));
    rig.host.GetDevice()->BeginFrame();
    rhi::CommandList* list = rig.host.GetDevice()->CreateCommandList();
    list->Begin();
    mesh.RecordPrepare(list);
    list->End();
    rhi::CommandList* lists[] = {list};
    REQUIRE(rig.host.GetDevice()->Submit(lists, 1) != 0);
    rig.host.GetDevice()->EndFrame();
    mesh.MarkReady();

    REQUIRE(rig.Reset());
    CHECK(mesh.Ready());  // the rebuilt buffers were moved to their read states, so Draw's precondition holds
}

TEST_CASE("A moved sampler or texture rebuilds with a valid description", "[device_resources][texture]")
{
    ResQuietLog quiet;
    ResetRig rig;
    REQUIRE(rig.Create());
    Vector<GpuSampler> samplers;
    samplers.Resize(1);
    REQUIRE(samplers[0].Create(rig.host.GetDevice(), rhi::SamplerDesc{}, &rig.registry, "moving sampler"));
    GpuSampler moved = static_cast<GpuSampler&&>(samplers[0]);
    samplers.Resize(0);
    GpuTexture texture;
    TextureImage image;
    image.desc.width = image.desc.height = 4;
    image.desc.format = rhi::Format::RGBA8_UNorm;
    image.desc.usage = rhi::TextureUsage::Sampled;
    image.pixels.Resize(64);
    REQUIRE(texture.Create(rig.host.GetDevice(), static_cast<TextureImage&&>(image), &rig.registry, "moving texture"));
    GpuTexture moved_texture = static_cast<GpuTexture&&>(texture);
    REQUIRE(rig.Reset());  // under ASan this reads the names the descriptions point at
    CHECK(moved.Valid());
    CHECK(moved_texture.Valid());
    CHECK(CStrCompare(moved_texture.Desc().debug_name, "moving texture") == 0);
}

TEST_CASE("An owner may unregister itself or another from inside Release or Recreate", "[device_resources]")
{
    ResQuietLog quiet;
    ResetRig rig;
    struct Suicidal final : Recreatable
    {
        DeviceResources* registry = nullptr;
        Recreatable* victim = nullptr;
        bool live = true;
        void Release() override
        {
            live = false;
            registry->Unregister(this);
            if (victim != nullptr)
            {
                registry->Unregister(victim);
            }
        }
        bool Recreate(rhi::Device*) override
        {
            live = true;
            return true;
        }
        bool Live() const override { return live; }
        const char* RecreatableName() const override { return "suicidal"; }
    };
    FakeOwner other;
    other.log = &rig.log;
    Suicidal first;
    first.registry = &rig.registry;
    first.victim = &other;
    REQUIRE(rig.Create());
    REQUIRE(rig.registry.Register(&first, 3));
    REQUIRE(rig.registry.Register(&other, 1));
    REQUIRE(rig.Reset());
    CHECK(rig.registry.Count() == 0);
    CHECK(other.releases == 0);  // released-after-unregistered owners are not called
}

TEST_CASE("An owner registered before the registry saw a device is released at the next reset", "[device_resources]")
{
    ResQuietLog quiet;
    DeviceResources registry;
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    GpuSampler sampler;
    REQUIRE(sampler.Create(device.Get(), rhi::SamplerDesc{}, &registry, "early"));  // the registry has no device yet
    registry.OnDeviceReleasing(device->Epoch());
    CHECK_FALSE(sampler.Valid());  // it was live, so it was released rather than skipped
    CHECK(device->Resources().samplers == 0);
    sampler.Destroy();
}

TEST_CASE("A texture with GPU mips can be made inside an open frame", "[device_resources][texture]")
{
    ResQuietLog quiet;
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    GpuTexture texture;
    TextureImage image;
    image.desc.width = image.desc.height = 8;
    image.desc.mip_levels = 4;
    image.desc.format = rhi::Format::RGBA8_UNorm;
    image.desc.usage = rhi::TextureUsage::Sampled;
    image.pixels.Resize(8 * 8 * 4);
    image.generate_mips = true;
    device->BeginFrame();  // the caller's frame is open
    REQUIRE(texture.Create(device.Get(), static_cast<TextureImage&&>(image), nullptr, "streamed"));
    CHECK(device->InFrame());  // the texture did not close the caller's frame
    device->EndFrame();
    CHECK(device->ErrorCount() == 0);
    texture.Destroy();
}
