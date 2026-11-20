// The GPU-backed loaders (Phase 22 Day 329): meshes, textures and materials loaded through the manager, the fallbacks,
// and a device reset with all of them loaded.

#include <catch2/catch_test_macros.hpp>

#include "asset/gpu_loaders.h"
#include "asset/async_loader.h"
#include "asset/memory_backend.h"
#include "asset/pak.h"
#include "asset/pak_backend.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "log/log.h"
#include "material_source.h"
#include "mesh_source.h"
#include "rhi/null/null_device.h"
#include "skeleton_source.h"
#include "support.h"
#include "texture_source.h"

namespace vse::asset
{

namespace
{

using test::AsBytes;
using test::Rid;
using test::TempDir;

class GpResolver final : public MaterialReferenceResolver
{
public:
    Status Resolve(StringView path, ResourceId* id, ByteSpan* blob) const override
    {
        if (path != StringView("albedo"))
        {
            return Status::Make(ErrorCode::NotFound, "no such texture");
        }
        *id = Rid(0x101);
        *blob = AsBytes(texture);
        return Status::Success();
    }
    Vector<byte> texture;
};

// The blobs of a small project: a texture, a material that uses it, a static mesh, a skeleton and a skinned mesh.
struct Blobs
{
    TypeRegistry types;
    Vector<byte> texture;
    Vector<byte> material;
    Vector<byte> mesh;
    Vector<byte> skeleton;
    Vector<byte> skinned_mesh;
    Vector<byte> other_skeleton;  // the same id, other joints
};

Blobs MakeBlobs()
{
    Blobs b;
    Vector<byte> pixels;
    pixels.Resize(4 * 4 * 4);
    for (usize i = 0; i < pixels.Size(); ++i)
    {
        pixels[i] = static_cast<byte>(i * 7);
    }
    Vector<byte> raw;
    REQUIRE(BuildRawImage(4, 4, AsBytes(pixels), 0, &raw).Ok());
    const ByteSpan image = AsBytes(raw);
    const char* texture_source = "id 0000000000000101\nsource a.raw\nformat rgba8\nsrgb true\n";
    TextureSourceError texture_error;
    REQUIRE(MintTexture(StringView(texture_source, CStrLength(texture_source)), Span<const ByteSpan>(&image, 1),
                        b.types, &b.texture, &texture_error)
                .Ok());
    GpResolver resolver;
    for (usize i = 0; i < b.texture.Size(); ++i)
    {
        resolver.texture.Push(b.texture[i]);
    }
    const char* material_source =
        "id 00000000000002a1\ntemplate gbuffer\nparam roughness float 0.25\nparam base_color color 1 0.5 0.25 1\n"
        "texture albedo_tex albedo\nsampler linear_sampler filter nearest address clamp anisotropy 4\n";
    MaterialSourceError material_error;
    const Status material_status = MintMaterial(StringView(material_source, CStrLength(material_source)), resolver,
                                                b.types, &b.material, &material_error);
    INFO(material_error.message.Data());
    REQUIRE(material_status.Ok());
    const char* mesh_source =
        "id 00000000000003a1\nv 0 0 0  0 0 1  1 0 0 1  0 0\nv 1 0 0  0 0 1  1 0 0 1  1 0\nv 0 1 0  0 0 1  1 0 0 1  0 1\n"
        "i 0 1 2\n";
    MeshSourceError mesh_error;
    REQUIRE(MintMesh(StringView(mesh_source, CStrLength(mesh_source)), b.types, nullptr, &b.mesh, &mesh_error).Ok());

    auto skeleton_blob = [&](const char* prefix, Vector<byte>* out)
    {
        char text[400];
        usize at = static_cast<usize>(FormatTo(text, sizeof(text), "id 00000000000000aa\n"));
        for (u32 i = 0; i < 3; ++i)
        {
            char parent[24];
            FormatTo(parent, sizeof(parent), i == 0 ? "-" : "%s%u", prefix, i - 1);
            at += static_cast<usize>(
                FormatTo(text + at, sizeof(text) - at, "joint %s%u %s 0 1 0  0 0 0 1  1 1 1\n", prefix, i, parent));
        }
        SkeletonSourceError error;
        REQUIRE(MintSkeleton(StringView(text, at), b.types, out, &error).Ok());
    };
    skeleton_blob("j", &b.skeleton);
    skeleton_blob("k", &b.other_skeleton);
    SkeletonView view;
    REQUIRE(view.Open(AsBytes(b.skeleton), b.types).Ok());
    const char* skinned_source =
        "id 00000000000003b1\nskeleton 00000000000000aa\n"
        "v 0 0 0  0 0 1  1 0 0 1  0 0\nv 1 0 0  0 0 1  1 0 0 1  1 0\nv 0 1 0  0 0 1  1 0 0 1  0 1\ni 0 1 2\n"
        "skin 0 0 1\nskin 1 1 1\nskin 2 2 1\n";
    REQUIRE(
        MintMesh(StringView(skinned_source, CStrLength(skinned_source)), b.types, &view, &b.skinned_mesh, &mesh_error)
            .Ok());
    return b;
}

void Silence(bool on)
{
    LogRender.SetEnabled(!on);
}

// A null device with a registry, a material system and the manager with the GPU loaders.
struct GpuRig
{
    Blobs blobs;
    renderer::DeviceResources registry;
    rhi::NullDevicePtr device;
    UniquePtr<renderer::MaterialSystem> materials;
    GpuAssetContext context;
    HostMemoryBackend backend;
    UniquePtr<ResourceManager> manager;

    explicit GpuRig(bool allow_fallbacks = false)
        : blobs(MakeBlobs()), device(rhi::CreateNullDevice(rhi::NullDeviceOptions{})),
          backend(blobs.types, u64{8} << 20)
    {
        Silence(true);
        REQUIRE(registry.OnDeviceCreated(device.Get(), device->Epoch()));
        renderer::MaterialSystemDesc desc;
        desc.resources = &registry;
        materials = MakeUnique<renderer::MaterialSystem>(device.Get(), desc);
        context.device = device.Get();
        context.resources = &registry;
        context.materials = materials.Get();
        context.templates = renderer::CreateGBufferTemplates(materials.Get());
        ManagerConfig config;
        config.buffer_pool_bytes = u64{16} << 20;
        config.allow_fallbacks = allow_fallbacks;
        manager = MakeUnique<ResourceManager>(blobs.types, config);
        RegisterCpuLoaders(manager.Get());
        RegisterGpuLoaders(manager.Get(), &context);
        REQUIRE(manager->Mount(StringView("/"), &backend, 0).Ok());
        REQUIRE(backend.Add(Rid(0x101), AsBytes(blobs.texture)).Ok());
        REQUIRE(backend.Add(Rid(0x2a1), AsBytes(blobs.material)).Ok());
        REQUIRE(backend.Add(Rid(0x3a1), AsBytes(blobs.mesh)).Ok());
        REQUIRE(backend.Add(Rid(0xaa), AsBytes(blobs.skeleton)).Ok());
        REQUIRE(backend.Add(Rid(0x3b1), AsBytes(blobs.skinned_mesh)).Ok());
    }
    ~GpuRig()
    {
        manager.Reset();
        materials.Reset();
        device.Reset();
        Silence(false);
    }
};

bool SameCounts(const rhi::ResourceCounts& a, const rhi::ResourceCounts& b)
{
    return a.buffers == b.buffers && a.textures == b.textures && a.samplers == b.samplers &&
           a.bind_group_layouts == b.bind_group_layouts && a.shaders == b.shaders;
}

}  // namespace

TEST_CASE("gpu loaders: a mesh is uploaded from the blob's own bytes, nothing copied", "[asset][gpu]")
{
    GpuRig rig;
    const rhi::ResourceCounts before = rig.device->Resources();
    Handle<GpuMeshAsset> mesh;
    REQUIRE(rig.manager->Load<GpuMeshAsset>(Rid(0x3a1), &mesh).Ok());
    const GpuMeshAsset* asset = mesh.Get();
    REQUIRE(asset != nullptr);
    CHECK(asset->mesh->Valid());
    CHECK(asset->geometry.vertex_count == 3);
    CHECK(asset->geometry.index_count == 3);
    CHECK(asset->geometry.layout.skin_stride == 0);
    // The renderer's geometry is the blob's bytes, not a copy of them.
    CHECK(asset->geometry.bytes->Bytes().Data() + asset->geometry.vertex_offset == asset->view.VertexBytes().Data());
    CHECK(rig.device->Resources().buffers == before.buffers + 2);  // vertices and indices
    CHECK(rig.manager->Stats().load_conversions == 0);
    mesh.Reset();
    CHECK(SameCounts(rig.device->Resources(), before));
}

TEST_CASE("gpu loaders: a holder of the mesh keeps it, and its bytes, after the manager lets go of the resource",
          "[asset][gpu]")
{
    GpuRig rig;
    renderer::GpuMeshPtr held;
    renderer::GpuTexturePtr held_texture;
    {
        Handle<GpuMeshAsset> mesh;
        REQUIRE(rig.manager->Load<GpuMeshAsset>(Rid(0x3a1), &mesh).Ok());
        held = mesh.Get()->mesh;
        Handle<GpuTextureAsset> texture;
        REQUIRE(rig.manager->Load<GpuTextureAsset>(Rid(0x101), &texture).Ok());
        held_texture = texture.Get()->texture;
    }
    CHECK(rig.manager->Stats().resident == 0);    // unloaded: the manager's buffers went back to the pool
    CHECK(rig.manager->Pool().BytesInUse() > 0);  // but the mesh and the texture still hold their bytes
    CHECK(held->Valid());
    CHECK(held_texture->Valid());

    // A device reset uploads from those bytes, though the resource is gone from the manager.
    const rhi::ResourceCounts before = rig.device->Resources();
    rig.registry.OnDeviceReleasing(rig.device->Epoch());
    rhi::NullDevicePtr second = rhi::CreateNullDevice(rhi::NullDeviceOptions{});
    REQUIRE(rig.registry.OnDeviceCreated(second.Get(), second->Epoch()));
    CHECK(held->Valid());
    CHECK(held_texture->Valid());
    CHECK(second->Resources().buffers == before.buffers);
    CHECK(second->Resources().textures == before.textures);
    held.Reset();
    held_texture.Reset();
    CHECK(rig.manager->Pool().BytesInUse() == 0);  // the last holder returned the bytes
    CHECK(rig.manager->Stats().bytes_resident == 0);
    rig.registry.OnDeviceReleasing(second->Epoch());
}

TEST_CASE("gpu loaders: a device reset between an asynchronous read and its build uploads to the new device",
          "[asset][gpu][async]")
{
    GpuRig rig;
    jobs::Scheduler::Config config;
    config.participants = 3;
    config.pin = false;
    jobs::Scheduler scheduler(config);
    AsyncLoader loader(*rig.manager, scheduler);
    LoadTicket ticket = loader.LoadAsync<GpuMeshAsset>(Rid(0x3a1));
    for (u32 i = 0; i < 10000 && loader.Stats().ready == 0; ++i)
    {
        ThisThread::SleepForMilliseconds(1);  // the blob is read; nothing is on the device yet
    }
    REQUIRE(loader.Stats().ready == 1);
    rig.registry.OnDeviceReleasing(rig.device->Epoch());
    rhi::NullDevicePtr second = rhi::CreateNullDevice(rhi::NullDeviceOptions{});
    REQUIRE(rig.registry.OnDeviceCreated(second.Get(), second->Epoch()));
    rig.context.device = second.Get();  // the application points the loaders at the device that exists now
    CHECK(loader.Wait(ticket).Ok());
    Handle<GpuMeshAsset> mesh;
    REQUIRE(ticket.Finish(&mesh).Ok());
    CHECK(mesh.Get()->mesh->Valid());
    CHECK(second->Resources().buffers == 2);  // vertices and indices, on the device that exists now
    // Another reset with the mesh loaded: it follows.
    rig.registry.OnDeviceReleasing(second->Epoch());
    rhi::NullDevicePtr third = rhi::CreateNullDevice(rhi::NullDeviceOptions{});
    REQUIRE(rig.registry.OnDeviceCreated(third.Get(), third->Epoch()));
    CHECK(mesh.Get()->mesh->Valid());
    CHECK(third->Resources().buffers == 2);
    mesh.Reset();
    rig.registry.OnDeviceReleasing(third->Epoch());
}

TEST_CASE("gpu loaders: a skinned mesh needs its skeleton, adds the skin stream and is refused with another",
          "[asset][gpu]")
{
    GpuRig rig;
    const rhi::ResourceCounts before = rig.device->Resources();
    Handle<GpuMeshAsset> mesh;
    REQUIRE(rig.manager->Load<GpuMeshAsset>(Rid(0x3b1), &mesh).Ok());
    CHECK(mesh.Get()->geometry.layout.skin_stride == 8);
    CHECK(mesh.Get()->geometry.skin_joint_count == 3);
    CHECK(rig.device->Resources().buffers == before.buffers + 3);  // vertices, indices and the skin stream
    CHECK(rig.manager->Stats().resident == 2);                     // the skeleton came with it
    mesh.Reset();
    CHECK(SameCounts(rig.device->Resources(), before));

    // The same skeleton id with other joints: refused naming the mismatch, nothing left on the device.
    GpuRig other;
    REQUIRE(other.backend.Replace(Rid(0xaa), AsBytes(other.blobs.other_skeleton)).Ok());
    const rhi::ResourceCounts other_before = other.device->Resources();
    Handle<GpuMeshAsset> refused;
    CHECK(other.manager->Load<GpuMeshAsset>(Rid(0x3b1), &refused).Is(ErrorCode::DependencyTypeMismatch));
    CHECK(SameCounts(other.device->Resources(), other_before));
}

TEST_CASE("gpu loaders: a material loads its texture by owner and a device reset recreates all of it", "[asset][gpu]")
{
    GpuRig rig;
    Handle<GpuMaterialAsset> material;
    Handle<GpuMeshAsset> mesh;
    Handle<GpuMeshAsset> skinned;
    REQUIRE(rig.manager->Load<GpuMaterialAsset>(Rid(0x2a1), &material).Ok());
    REQUIRE(rig.manager->Load<GpuMeshAsset>(Rid(0x3a1), &mesh).Ok());
    REQUIRE(rig.manager->Load<GpuMeshAsset>(Rid(0x3b1), &skinned).Ok());
    CHECK(rig.materials->IsAlive(material.Get()->material->id));
    Handle<GpuTextureAsset> texture;
    REQUIRE(rig.manager->Load<GpuTextureAsset>(Rid(0x101), &texture).Ok());  // held by the material: a cache hit
    CHECK(texture.Get()->texture->Valid());
    CHECK(texture.Get()->texture->Desc().mip_levels == 3);
    ResourceStats stats = rig.manager->Stats();
    CHECK(stats.load_conversions == 0);
    CHECK(stats.fallbacks_used == 0);
    CHECK(stats.resident == 5);  // material, texture, mesh, skinned mesh, skeleton

    const rhi::ResourceCounts before = rig.device->Resources();
    const u32 epoch = rig.device->Epoch();
    rig.registry.OnDeviceReleasing(epoch);
    rhi::NullDevicePtr second = rhi::CreateNullDevice(rhi::NullDeviceOptions{});
    REQUIRE(rig.registry.OnDeviceCreated(second.Get(), second->Epoch()));
    const rhi::ResourceCounts after = second->Resources();
    CHECK(after.textures == before.textures);
    CHECK(after.samplers == before.samplers);
    CHECK(after.buffers == before.buffers);
    CHECK(after.bind_group_layouts == before.bind_group_layouts);
    CHECK(texture.Get()->texture->Valid());
    CHECK(mesh.Get()->mesh->Valid());
    CHECK(skinned.Get()->mesh->Valid());
    CHECK(rig.materials->IsAlive(material.Get()->material->id));

    // Everything goes when the handles do; the new device ends where it began.
    rig.context.device = second.Get();
    material.Reset();
    texture.Reset();
    mesh.Reset();
    skinned.Reset();
    CHECK(rig.manager->Stats().resident == 0);
    rig.registry.OnDeviceReleasing(second->Epoch());
    second.Reset();
}

TEST_CASE("gpu loaders: a pak and a memory backend give the same device objects", "[asset][gpu]")
{
    GpuRig memory;
    Handle<GpuMaterialAsset> from_memory;
    REQUIRE(memory.manager->Load<GpuMaterialAsset>(Rid(0x2a1), &from_memory).Ok());
    const rhi::ResourceCounts memory_counts = memory.device->Resources();

    TempDir dir;
    const io::Path pak_path = dir.Path().Join("gpu.pak");
    {
        io::FilePtr file;
        REQUIRE(io::File::Open(pak_path, io::OpenMode::Write, 0, &file).Ok());
        PakWriter writer(memory.blobs.types, PakWriterOptions());
        REQUIRE(writer.Add(Rid(0x101), AsBytes(memory.blobs.texture), StringView("/t")).Ok());
        REQUIRE(writer.Add(Rid(0x2a1), AsBytes(memory.blobs.material), StringView("/m")).Ok());
        REQUIRE(writer.Finish(*file).Ok());
        REQUIRE(file->Close().Ok());
    }
    GpuRig paked;
    paked.manager.Reset();
    ManagerConfig config;
    config.buffer_pool_bytes = u64{16} << 20;
    paked.manager = MakeUnique<ResourceManager>(paked.blobs.types, config);
    RegisterCpuLoaders(paked.manager.Get());
    RegisterGpuLoaders(paked.manager.Get(), &paked.context);
    REQUIRE(paked.manager->MountPath(StringView("/"), pak_path, 0).Ok());
    Handle<GpuMaterialAsset> from_pak;
    REQUIRE(paked.manager->Load<GpuMaterialAsset>(Rid(0x2a1), &from_pak).Ok());
    CHECK(SameCounts(paked.device->Resources(), memory_counts));
    CHECK(paked.manager->Stats().bytes_resident == memory.manager->Stats().bytes_resident);
}

TEST_CASE("gpu loaders: fallbacks stand in only when allowed, and are counted", "[asset][gpu]")
{
    {
        GpuRig strict;
        REQUIRE(RegisterGpuFallbacks(strict.manager.Get()).Ok());
        Handle<GpuMeshAsset> mesh;
        CHECK(strict.manager->Load<GpuMeshAsset>(Rid(0x999), &mesh).Is(ErrorCode::NotFound));
        CHECK(strict.manager->Stats().fallbacks_used == 0);
    }
    GpuRig rig(true);
    REQUIRE(RegisterGpuFallbacks(rig.manager.Get()).Ok());
    Handle<GpuMeshAsset> mesh;
    Handle<GpuTextureAsset> texture;
    Handle<GpuMaterialAsset> material;
    REQUIRE(rig.manager->Load<GpuMeshAsset>(Rid(0x999), &mesh).Ok());
    REQUIRE(rig.manager->Load<GpuTextureAsset>(Rid(0x998), &texture).Ok());
    REQUIRE(rig.manager->Load<GpuMaterialAsset>(Rid(0x997), &material).Ok());
    CHECK(mesh.Get()->geometry.vertex_count == 24);  // the unit cube
    CHECK(texture.Get()->texture->Valid());
    CHECK(rig.materials->IsAlive(material.Get()->material->id));
    CHECK(rig.manager->Stats().fallbacks_used == 3);
    // A type that is found loads as itself.
    Handle<GpuMeshAsset> real;
    REQUIRE(rig.manager->Load<GpuMeshAsset>(Rid(0x3a1), &real).Ok());
    CHECK(real.Get()->geometry.vertex_count == 3);
    CHECK(rig.manager->Stats().fallbacks_used == 3);
}

namespace
{

class GpBytes final : public SharedBytes
{
public:
    ByteSpan Bytes() const override { return ByteSpan(data.Data(), data.Size()); }
    Vector<byte> data;
};

}  // namespace

TEST_CASE("gpu loaders: a texture over shared bytes refuses levels that are not inside them", "[asset][gpu]")
{
    GpuRig rig;
    SharedPtr<GpBytes> region = MakeShared<GpBytes>();
    region->data.Resize(64);
    rhi::TextureDesc desc;
    desc.width = 4;
    desc.height = 4;
    desc.format = rhi::Format::RGBA8_SRGB;
    desc.usage = rhi::TextureUsage::Sampled;
    renderer::TextureLevelSpan good;
    good.offset = 0;
    good.bytes = 64;
    good.row_pitch = 16;
    const rhi::ResourceCounts before = rig.device->Resources();
    renderer::GpuTexture ok;
    REQUIRE(ok.CreateFromSharedLevels(rig.device.Get(), desc, region, Span<const renderer::TextureLevelSpan>(&good, 1),
                                      &rig.registry, "ok"));
    CHECK(ok.Valid());
    CHECK(rig.device->Resources().textures == before.textures + 1);

    renderer::GpuTexture refused;
    renderer::TextureLevelSpan short_level = good;
    short_level.offset = 63;
    short_level.bytes = 1;  // inside the region, but the device would read 64 bytes
    CHECK_FALSE(refused.CreateFromSharedLevels(rig.device.Get(), desc, region,
                                               Span<const renderer::TextureLevelSpan>(&short_level, 1), nullptr, "s"));
    renderer::TextureLevelSpan outside = good;
    outside.offset = 1;  // 64 bytes from offset 1 end past the region
    CHECK_FALSE(refused.CreateFromSharedLevels(rig.device.Get(), desc, region,
                                               Span<const renderer::TextureLevelSpan>(&outside, 1), nullptr, "a"));
    renderer::TextureLevelSpan empty = good;
    empty.bytes = 0;
    CHECK_FALSE(refused.CreateFromSharedLevels(rig.device.Get(), desc, region,
                                               Span<const renderer::TextureLevelSpan>(&empty, 1), nullptr, "b"));
    renderer::TextureLevelSpan wrapped = good;
    wrapped.offset = ~u64{0};  // an offset that overflows when the size is added
    CHECK_FALSE(refused.CreateFromSharedLevels(rig.device.Get(), desc, region,
                                               Span<const renderer::TextureLevelSpan>(&wrapped, 1), nullptr, "c"));
    CHECK_FALSE(refused.CreateFromSharedLevels(rig.device.Get(), desc, SharedBytesPtr(),
                                               Span<const renderer::TextureLevelSpan>(&good, 1), nullptr, "d"));
    CHECK_FALSE(refused.Valid());
    CHECK(rig.device->Resources().textures == before.textures + 1);  // nothing left behind
}

}  // namespace vse::asset
