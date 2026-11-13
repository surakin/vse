// The Metal backend's resources (Phase 21 Day 301): storage modes by memory location, the forced-Managed round trip
// (this Mac is unified: the hook is the Managed path's only coverage), uploads and readbacks of buffers and of every
// texture format, row pitches, mips and layers, deferred destruction and the counts going back to zero. macOS only.
#include "metal_test_support.h"  // first: it imports AppKit before any engine name is in scope

#include "cstr/mem_ops.h"
#include "sync/atomic.h"

#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "rhi/backends/backends.h"
#include "rhi/format.h"
#include "rhi/metal/metal_device.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::rhi::metal;
using namespace engine_placeholder::rhi::metal::test;

namespace
{

DevicePtr MakeMetal(const char* name = "metal_resource_test")
{
    return CreateDevice(MetalInfo(name));
}

// Forces the Managed storage mode for the devices made while it lives.
struct ForcedManaged
{
    ForcedManaged() { ForceManagedStorageForTest(true); }
    ~ForcedManaged() { ForceManagedStorageForTest(false); }
};

Vector<u8> TestPattern(usize size, u32 seed)
{
    Vector<u8> v;
    v.Resize(size);
    u32 x = seed * 2654435761u + 1;
    for (usize i = 0; i < size; ++i)
    {
        x = x * 1664525u + 1013904223u;
        v[i] = static_cast<u8>(x >> 24);
    }
    return v;
}

bool Same(const Vector<u8>& a, const Vector<u8>& b)
{
    return a.Size() == b.Size() && MemCompare(a.Data(), b.Data(), a.Size()) == 0;
}

BufferHandle MakeBuffer(Device& device, u64 size, BufferUsage usage, MemoryLocation location)
{
    BufferDesc d;
    d.size = size;
    d.usage = usage;
    d.location = location;
    d.debug_name = "metal_resource_test buffer";
    return device.CreateBuffer(d);
}

// The buffer paths of one device: a GPU-only buffer through the staging ring (larger than one chunk), a CpuToGpu buffer
// written through its mapping and by UploadBuffer, and a GpuToCpu buffer filled by a GPU copy and read through its
// mapping and ReadbackBuffer.
void BufferRoundTrips(Device& device)
{
    const u64 big = (16ull << 20) + 4096 + 12;  // more than one upload chunk
    const Vector<u8> data = TestPattern(static_cast<usize>(big), 1);
    const BufferHandle gpu = MakeBuffer(device, big, BufferUsage::Storage | BufferUsage::CopySrc | BufferUsage::CopyDst,
                                        MemoryLocation::GpuOnly);
    REQUIRE(gpu.IsValid());
    const UploadTicket up = device.UploadBuffer(gpu, 0, data.Data(), big);
    CHECK(up.ok);
    CHECK(up.fence != 0);
    Vector<u8> back;
    back.Resize(static_cast<usize>(big));
    REQUIRE(device.ReadbackBuffer(gpu, 0, back.Data(), big));
    CHECK(Same(back, data));
    // A range in the middle, at an odd offset.
    Vector<u8> part;
    part.Resize(1000);
    REQUIRE(device.ReadbackBuffer(gpu, 4097, part.Data(), 1000));
    CHECK(MemCompare(part.Data(), data.Data() + 4097, 1000) == 0);

    // An unaligned write (Metal copies buffers in 4-byte units) changes only its own bytes.
    const u8 three[3] = {0xA1, 0xB2, 0xC3};
    CHECK(device.UploadBuffer(gpu, 4097, three, 3).ok);
    Vector<u8> around;
    around.Resize(20);
    REQUIRE(device.ReadbackBuffer(gpu, 4090, around.Data(), 20));
    Vector<u8> expect;
    expect.Resize(20);
    MemCopy(expect.Data(), data.Data() + 4090, 20);
    MemCopy(expect.Data() + 7, three, 3);
    CHECK(Same(around, expect));
    // A buffer whose size is not a multiple of 4, written and read whole.
    const BufferHandle odd = MakeBuffer(device, 13, BufferUsage::Vertex | BufferUsage::CopySrc | BufferUsage::CopyDst,
                                        MemoryLocation::GpuOnly);
    REQUIRE(odd.IsValid());
    const Vector<u8> thirteen = TestPattern(13, 9);
    CHECK(device.UploadBuffer(odd, 0, thirteen.Data(), 13).ok);
    Vector<u8> odd_back;
    odd_back.Resize(13);
    REQUIRE(device.ReadbackBuffer(odd, 0, odd_back.Data(), 13));
    CHECK(Same(odd_back, thirteen));
    device.DestroyBuffer(odd);

    const BufferHandle upload = MakeBuffer(
        device, 4096, BufferUsage::Uniform | BufferUsage::CopySrc | BufferUsage::CopyDst, MemoryLocation::CpuToGpu);
    REQUIRE(upload.IsValid());
    const Vector<u8> words = TestPattern(4096, 2);
    u8* mapped = static_cast<u8*>(device.MapBuffer(upload));
    REQUIRE(mapped != nullptr);
    MemCopy(mapped, words.Data(), 2048);
    device.FlushMappedBuffer(upload, 0, 2048);
    device.UnmapBuffer(upload);
    CHECK(device.UploadBuffer(upload, 2048, words.Data() + 2048, 2048).ok);  // through the mapping

    // The GPU copies the CpuToGpu buffer into a GpuToCpu one: the CPU's writes reached the GPU, and the GPU's write
    // reaches the CPU.
    const BufferHandle readback = MakeBuffer(device, 4096, BufferUsage::CopyDst, MemoryLocation::GpuToCpu);
    REQUIRE(readback.IsValid());
    {
        Vector<u8> staged;
        staged.Resize(4096);
        REQUIRE(device.ReadbackBuffer(upload, 0, staged.Data(), 4096));  // CpuToGpu memory is CPU-visible
        CHECK(Same(staged, words));
        const UploadTicket t = device.UploadBuffer(readback, 0, staged.Data(), 4096);  // a GPU copy into it
        CHECK(t.ok);
    }
    Vector<u8> got;
    got.Resize(4096);
    REQUIRE(device.ReadbackBuffer(readback, 0, got.Data(), 4096));
    CHECK(Same(got, words));
    const u8* read_map = static_cast<const u8*>(device.MapBuffer(readback));
    REQUIRE(read_map != nullptr);
    CHECK(MemCompare(read_map, words.Data(), 4096) == 0);
    device.UnmapBuffer(readback);

    device.DestroyBuffer(gpu);
    device.DestroyBuffer(upload);
    device.DestroyBuffer(readback);
    device.WaitIdle();
}

}  // namespace

TEST_CASE(
    "Metal storage modes follow the memory location: Private on the GPU, Shared readback, Shared or Managed uploads",
    "[rhi][metal][gpu]")
{
    DevicePtr device = MakeMetal();
    REQUIRE(device);
    MetalDevice& metal = AsMetal(*device);
    CHECK(metal.ManagedStorage() == !metal.Mtl().hasUnifiedMemory);
    CHECK(metal.StorageModeFor(MemoryLocation::GpuOnly) == MTLStorageModePrivate);
    const MTLStorageMode visible = metal.ManagedStorage() ? MTLStorageModeManaged : MTLStorageModeShared;
    CHECK(metal.StorageModeFor(MemoryLocation::CpuToGpu) == visible);
    CHECK(metal.StorageModeFor(MemoryLocation::GpuToCpu) == MTLStorageModeShared);  // readback memory, always
    const BufferHandle gpu = MakeBuffer(*device, 256, BufferUsage::Vertex, MemoryLocation::GpuOnly);
    const BufferHandle cpu = MakeBuffer(*device, 256, BufferUsage::Uniform, MemoryLocation::CpuToGpu);
    REQUIRE(gpu.IsValid());
    REQUIRE(cpu.IsValid());
    CHECK(metal.BufferObject(gpu).storageMode == MTLStorageModePrivate);
    CHECK(metal.BufferObject(cpu).storageMode == visible);
    TextureDesc td;
    td.width = 16;
    td.height = 16;
    td.format = Format::RGBA8_UNorm;
    td.usage = TextureUsage::Sampled | TextureUsage::CopyDst;
    const TextureHandle tex = device->CreateTexture(td);
    REQUIRE(tex.IsValid());
    CHECK(metal.TextureObject(tex).storageMode == MTLStorageModePrivate);
    CHECK((metal.TextureObject(tex).usage & MTLTextureUsageShaderRead) != 0);
    device->DestroyBuffer(gpu);
    device->DestroyBuffer(cpu);
    device->DestroyTexture(tex);
    device->WaitIdle();
    {
        ForcedManaged forced;
        DevicePtr managed = MakeMetal("metal_resource_test managed");
        REQUIRE(managed);
        CHECK(AsMetal(*managed).ManagedStorage());
        CHECK(AsMetal(*managed).StorageModeFor(MemoryLocation::CpuToGpu) == MTLStorageModeManaged);
        CHECK(AsMetal(*managed).StorageModeFor(MemoryLocation::GpuToCpu) == MTLStorageModeShared);
        // CPU-written memory the GPU may write stays Shared (Day 312's review), so a GPU copy into it reads back.
        CHECK(AsMetal(*managed).StorageModeFor(MemoryLocation::CpuToGpu, BufferUsage::CopyDst) == MTLStorageModeShared);
        CHECK(AsMetal(*managed).StorageModeFor(MemoryLocation::CpuToGpu, BufferUsage::Storage) == MTLStorageModeShared);
        BufferDesc src_desc;
        src_desc.size = 64;
        src_desc.usage = BufferUsage::CopySrc | BufferUsage::CopyDst;
        const BufferHandle src = managed->CreateBuffer(src_desc);
        BufferDesc dst_desc;
        dst_desc.size = 64;
        dst_desc.usage = BufferUsage::CopyDst | BufferUsage::CopySrc | BufferUsage::Uniform;
        dst_desc.location = MemoryLocation::CpuToGpu;
        const BufferHandle dst = managed->CreateBuffer(dst_desc);
        REQUIRE((src.IsValid() && dst.IsValid()));
        Array<u8, 64> pattern = {};
        for (usize i = 0; i < pattern.Size(); ++i)
        {
            pattern[i] = static_cast<u8>(i * 3 + 1);
        }
        REQUIRE(managed->UploadBuffer(src, 0, pattern.Data(), pattern.Size()).ok);
        managed->BeginFrame();
        CommandList* list = managed->CreateCommandList();
        REQUIRE(list != nullptr);
        list->Begin();
        list->CopyBuffer(src, dst, 0, 0, 64);
        list->End();
        REQUIRE(managed->Submit(&list, 1) != 0);
        managed->EndFrame();
        managed->WaitIdle();
        Array<u8, 64> got = {};
        REQUIRE(managed->ReadbackBuffer(dst, 0, got.Data(), got.Size()));
        CHECK(MemCompare(got.Data(), pattern.Data(), pattern.Size()) == 0);  // the GPU's write, not the CPU's copy
        managed->DestroyBuffer(src);
        managed->DestroyBuffer(dst);
    }
    DevicePtr after = MakeMetal("metal_resource_test after");
    REQUIRE(after);
    CHECK(AsMetal(*after).ManagedStorage() == !AsMetal(*after).Mtl().hasUnifiedMemory);  // the hook is off again
}

TEST_CASE("Metal buffers round-trip through uploads, mappings and readbacks", "[rhi][metal][gpu]")
{
    DevicePtr device = MakeMetal();
    REQUIRE(device);
    BufferRoundTrips(*device);
    CHECK(device->Resources().buffers == 0);
}

TEST_CASE("The forced Managed storage mode gives the same bytes on every buffer path", "[rhi][metal][gpu]")
{
    ForcedManaged forced;
    DevicePtr device = MakeMetal("metal_resource_test managed");
    REQUIRE(device);
    REQUIRE(AsMetal(*device).ManagedStorage());
    BufferRoundTrips(*device);
    CHECK(device->Resources().buffers == 0);
}

TEST_CASE("Every Metal texture format round-trips a level through upload and readback", "[rhi][metal][gpu]")
{
    DevicePtr device = MakeMetal();
    REQUIRE(device);
    u32 tried = 0;
    for (usize f = 1; f < static_cast<usize>(Format::Count); ++f)
    {
        const Format format = static_cast<Format>(f);
        const FormatProperties& fp = FormatInfo(format);
        const TextureUsage usage = TextureUsage::Sampled | TextureUsage::CopyDst | TextureUsage::CopySrc;
        INFO(fp.name);
        if (fp.has_stencil || !device->FormatSupport(format, usage))
        {
            CHECK((fp.has_stencil || format == Format::RGB32_Float ||
                   (fp.is_compressed && !AsMetal(*device).Mtl().supportsBCTextureCompression)));
            continue;  // stencil formats are not uploaded (the RHI's rule); RGB32 is vertex data only
        }
        ++tried;
        TextureDesc td;
        td.width = 20;  // not a multiple of the 4x4 blocks: the last block column and row are partial
        td.height = 12;
        td.mip_levels = 3;
        td.format = format;
        td.usage = usage;
        const TextureHandle tex = device->CreateTexture(td);
        REQUIRE(tex.IsValid());
        for (u32 mip = 0; mip < 3; ++mip)
        {
            const u32 w = td.width >> mip;
            const u32 h = td.height >> mip;
            const usize bytes = static_cast<usize>(FormatSliceBytes(format, w, h));
            Vector<u8> data = TestPattern(bytes, static_cast<u32>(f * 7 + mip));
            if (fp.is_depth)
            {
                // Depth: floats in [0, 1] (a blit may canonicalize other bit patterns).
                for (usize i = 0; i + 4 <= bytes; i += 4)
                {
                    const f32 v = static_cast<f32>(data[i]) / 255.0f;
                    MemCopy(data.Data() + i, &v, 4);
                }
            }
            const UploadTicket t = device->UploadTexture(tex, mip, 0, data.Data(), 0);
            CHECK(t.ok);
            Vector<u8> back;
            back.Resize(bytes);
            REQUIRE(device->ReadbackTexture(tex, mip, 0, back.Data(), bytes, ResourceState::ShaderRead));
            CHECK(Same(back, data));
        }
        device->DestroyTexture(tex);
    }
    CHECK(tried >= 15);
    device->WaitIdle();
    CHECK(device->Resources().textures == 0);
}

TEST_CASE("A Metal texture upload honours the caller's row pitch, and array layers and 3D slices stay apart",
          "[rhi][metal][gpu]")
{
    DevicePtr device = MakeMetal();
    REQUIRE(device);
    TextureDesc td;
    td.width = 13;
    td.height = 7;
    td.array_layers = 3;
    td.format = Format::RGBA8_UNorm;
    td.usage = TextureUsage::Sampled | TextureUsage::CopyDst | TextureUsage::CopySrc;
    const TextureHandle tex = device->CreateTexture(td);
    REQUIRE(tex.IsValid());
    const u32 row = 13 * 4;
    const u32 pitch = 64;  // padded rows
    for (u32 layer = 0; layer < 3; ++layer)
    {
        Vector<u8> padded = TestPattern(pitch * 7, 100 + layer);
        CHECK(device->UploadTexture(tex, 0, layer, padded.Data(), pitch).ok);
        Vector<u8> back;
        back.Resize(row * 7);
        REQUIRE(device->ReadbackTexture(tex, 0, layer, back.Data(), back.Size(), ResourceState::ShaderRead));
        for (u32 y = 0; y < 7; ++y)
        {
            CHECK(MemCompare(back.Data() + y * row, padded.Data() + y * pitch, row) == 0);
        }
    }
    // Layer 0 is still layer 0's after the others were written.
    Vector<u8> first = TestPattern(pitch * 7, 100);
    Vector<u8> back;
    back.Resize(row * 7);
    REQUIRE(device->ReadbackTexture(tex, 0, 0, back.Data(), back.Size(), ResourceState::ShaderRead));
    CHECK(MemCompare(back.Data(), first.Data(), row) == 0);

    TextureDesc vd;
    vd.dimension = TextureDimension::Tex3D;
    vd.width = 8;
    vd.height = 4;
    vd.depth = 5;
    vd.format = Format::R16_Float;
    vd.usage = TextureUsage::Sampled | TextureUsage::CopyDst | TextureUsage::CopySrc;
    const TextureHandle volume = device->CreateTexture(vd);
    REQUIRE(volume.IsValid());
    const Vector<u8> slices = TestPattern(8 * 2 * 4 * 5, 7);
    CHECK(device->UploadTexture(volume, 0, 0, slices.Data(), 0).ok);
    Vector<u8> volume_back;
    volume_back.Resize(slices.Size());
    REQUIRE(device->ReadbackTexture(volume, 0, 0, volume_back.Data(), volume_back.Size(), ResourceState::ShaderRead));
    CHECK(Same(volume_back, slices));
    device->DestroyTexture(tex);
    device->DestroyTexture(volume);
}

TEST_CASE("A texture larger than one staging chunk uploads in bands", "[rhi][metal][gpu]")
{
    DevicePtr device = MakeMetal();
    REQUIRE(device);
    TextureDesc td;
    td.width = 2048;
    td.height = 2600;  // 2048 x 4 x 2600 = 21 MB: two bands
    td.format = Format::RGBA8_UNorm;
    td.usage = TextureUsage::Sampled | TextureUsage::CopyDst | TextureUsage::CopySrc;
    const TextureHandle tex = device->CreateTexture(td);
    REQUIRE(tex.IsValid());
    const Vector<u8> data = TestPattern(2048 * 4 * 2600, 5);
    CHECK(device->UploadTexture(tex, 0, 0, data.Data(), 0).ok);
    Vector<u8> back;
    back.Resize(data.Size());
    REQUIRE(device->ReadbackTexture(tex, 0, 0, back.Data(), back.Size(), ResourceState::ShaderRead));
    CHECK(Same(back, data));
    device->DestroyTexture(tex);
}

TEST_CASE("Destroyed Metal resources go when their frames finish, and the counts return to zero", "[rhi][metal][gpu]")
{
    DevicePtr device = MakeMetal();
    REQUIRE(device);
    const ResourceCounts before = device->Resources();
    CHECK(before.internal_objects == 1);  // the staging ring
    CHECK(before.device_memory_bytes >= (64ull << 20));
    device->BeginFrame();
    const BufferHandle b = MakeBuffer(*device, 1 << 20, BufferUsage::Vertex | BufferUsage::CopyDst,
                                      MemoryLocation::GpuOnly);
    REQUIRE(b.IsValid());
    TextureDesc td;
    td.width = 64;
    td.height = 64;
    td.format = Format::D32_Float;
    td.usage = TextureUsage::DepthAttachment | TextureUsage::Sampled;
    const TextureHandle depth = device->CreateTexture(td);
    REQUIRE(depth.IsValid());
    CHECK((AsMetal(*device).TextureObject(depth).usage & MTLTextureUsageRenderTarget) != 0);
    const ResourceCounts during = device->Resources();
    CHECK(during.buffers == 1);
    CHECK(during.textures == 1);
    CHECK(during.internal_objects == 3);
    device->DestroyBuffer(b);
    device->DestroyTexture(depth);
    CHECK(AsMetal(*device).BufferObject(b) == nil);    // the handle is dead at once
    CHECK(device->Resources().internal_objects == 3);  // the objects wait for the frame
    device->EndFrame();
    for (int f = 0; f < 4; ++f)
    {
        device->BeginFrame();
        device->EndFrame();
    }
    device->WaitIdle();
    const ResourceCounts after = device->Resources();
    CHECK(after.buffers == 0);
    CHECK(after.textures == 0);
    CHECK(after.internal_objects == before.internal_objects);
    CHECK(after.device_memory_bytes == before.device_memory_bytes);
}

TEST_CASE("An injected Metal allocation failure gives invalid handles, and a leak is counted at destruction",
          "[rhi][metal][gpu][fault]")
{
    DeviceCreateInfo info = MetalInfo("metal_resource_test faults");
    info.faults.fail_allocations_from = 3;  // the staging ring is the first allocation
    DevicePtr device = CreateDevice(info);
    REQUIRE(device);
    const bool was = LogRhi.Enabled();
    LogRhi.SetEnabled(false);
    const BufferHandle ok = MakeBuffer(*device, 256, BufferUsage::Vertex, MemoryLocation::GpuOnly);
    const BufferHandle refused = MakeBuffer(*device, 256, BufferUsage::Vertex, MemoryLocation::GpuOnly);
    TextureDesc td;
    td.width = 4;
    td.height = 4;
    td.format = Format::RGBA8_UNorm;
    td.usage = TextureUsage::Sampled;
    const TextureHandle tex = device->CreateTexture(td);
    CHECK(ok.IsValid());
    CHECK_FALSE(refused.IsValid());
    CHECK_FALSE(tex.IsValid());
    MetalDevice::ResetLeakedUserObjects();
    device.Reset();  // `ok` is leaked on purpose
    LogRhi.SetEnabled(was);
    CHECK(MetalDevice::LeakedUserObjects() == 1);
    MetalDevice::ResetLeakedUserObjects();
}
