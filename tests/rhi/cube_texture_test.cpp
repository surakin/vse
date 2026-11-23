// Cube textures: the description rules on the null device, and a cube sampled by direction on every GPU backend that
// has them (a compute shader samples the six axis directions at a chosen mip level and writes the colors to a buffer).

#include <catch2/catch_test_macros.hpp>

#include "backend_fixture.h"
#include "containers/vector.h"
#include "cstr/mem_ops.h"
#include "cube_sample.comp.dxil.inc"
#include "rhi/null/null_device.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"

using namespace vse;
using namespace vse::rhi;

namespace
{

struct CubeQuietLog
{
    bool saved = LogRhi.Enabled();
    CubeQuietLog() { LogRhi.SetEnabled(false); }
    ~CubeQuietLog() { LogRhi.SetEnabled(saved); }
};

TextureDesc CubeDesc(u32 size, u32 mips)
{
    TextureDesc td;
    td.dimension = TextureDimension::Cube;
    td.width = size;
    td.height = size;
    td.array_layers = 6;
    td.mip_levels = mips;
    td.format = Format::RGBA8_UNorm;
    td.usage = TextureUsage::Sampled | TextureUsage::CopyDst | TextureUsage::CopySrc;
    return td;
}

// The color of face `face` at mip level `mip`: distinct per face, and per level so the level sampled is visible.
void CubeFaceColor(u32 face, u32 mip, u8 out[4])
{
    out[0] = static_cast<u8>(40 * face + 20);
    out[1] = static_cast<u8>(255 - 40 * face - 60 * mip);
    out[2] = static_cast<u8>(30 + 70 * mip);
    out[3] = 255;
}

bool CubeNear(u8 got, u8 want)
{
    return got + 1 >= want && got <= want + 1;
}

}  // namespace

TEST_CASE("A cube texture description needs square faces, 6 layers and sampled or copy usage", "[rhi][null][cube]")
{
    CubeQuietLog quiet;
    CHECK(CubeDesc(16, 5).Validate() == nullptr);

    TextureDesc td = CubeDesc(16, 1);
    td.height = 8;
    CHECK(td.Validate() != nullptr);  // not square

    td = CubeDesc(16, 1);
    td.array_layers = 5;
    CHECK(td.Validate() != nullptr);
    td.array_layers = 12;  // a cube array
    CHECK(td.Validate() != nullptr);

    td = CubeDesc(16, 1);
    td.depth = 2;
    CHECK(td.Validate() != nullptr);

    td = CubeDesc(16, 1);
    td.usage = TextureUsage::Sampled | TextureUsage::Storage;
    CHECK(td.Validate() != nullptr);
    td.usage = TextureUsage::ColorAttachment;
    CHECK(td.Validate() != nullptr);

    td = CubeDesc(16, 6);  // more than the full chain of 16 (5 levels)
    CHECK(td.Validate() != nullptr);
}

TEST_CASE("The null device makes cube textures and a device without them refuses", "[rhi][null][cube]")
{
    CubeQuietLog quiet;
    NullDevicePtr dev = CreateNullDevice();
    REQUIRE(dev);
    CHECK(dev->Caps().cube_textures);
    const TextureHandle cube = dev->CreateTexture(CubeDesc(8, 4));
    CHECK(cube.IsValid());
    CHECK(dev->ErrorCount() == 0);
    dev->DestroyTexture(cube);

    NullDeviceOptions options;
    options.caps.cube_textures = false;
    NullDevicePtr without = CreateNullDevice(options);
    REQUIRE(without);
    CHECK_FALSE(without->CreateTexture(CubeDesc(8, 4)).IsValid());
    CHECK(without->ErrorCount() == 1);

    const TextureHandle again = dev->CreateTexture(CubeDesc(8, 4));  // the first device still works
    CHECK(again.IsValid());
    CHECK(dev->ErrorCount() == 0);
    dev->DestroyTexture(again);
}

TEST_CASE("A cube texture is sampled by direction, per face and per mip level", "[rhi][gpu][cube]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        DevicePtr device = gpu_test::CreateTestDevice(backend);
        if (!device->Caps().cube_textures)
        {
            continue;  // a backend that has no cube textures yet refuses them (checked on the null device)
        }
        constexpr u32 kCubeSize = 4;
        constexpr u32 kMips = 3;
        const TextureHandle cube = device->CreateTexture(CubeDesc(kCubeSize, kMips));
        REQUIRE(cube.IsValid());
        for (u32 face = 0; face < 6; ++face)
        {
            for (u32 mip = 0; mip < kMips; ++mip)
            {
                const u32 extent = kCubeSize >> mip;
                Vector<u8> texels;
                texels.Resize(extent * extent * 4);
                u8 color[4];
                CubeFaceColor(face, mip, color);
                for (u32 i = 0; i < extent * extent; ++i)
                {
                    for (u32 c = 0; c < 4; ++c)
                    {
                        texels[i * 4 + c] = color[c];
                    }
                }
                REQUIRE(device->UploadTexture(cube, mip, face, texels.Data(), extent * 4).ok);
            }
        }

        SamplerDesc sd;
        sd.mag_filter = sd.min_filter = FilterMode::Nearest;
        sd.mip_filter = FilterMode::Nearest;
        sd.address_u = sd.address_v = sd.address_w = AddressMode::ClampToEdge;
        const SamplerHandle sampler = device->CreateSampler(sd);
        REQUIRE(sampler.IsValid());

        BufferDesc bd;
        bd.size = 6 * 16;
        bd.usage = BufferUsage::Storage | BufferUsage::CopySrc;
        bd.location = MemoryLocation::GpuOnly;
        const BufferHandle out = device->CreateBuffer(bd);
        REQUIRE(out.IsValid());

        const ShaderHandle cs = CreateShaderFromSet(*device, shaders::kCubeSampleCompShader, ShaderStage::Compute);
        const BindingDesc entries[] = {{0, BindingType::SampledTexture, ShaderStage::Compute, 1, 0},
                                       {1, BindingType::Sampler, ShaderStage::Compute, 1, 0},
                                       {2, BindingType::StorageBuffer, ShaderStage::Compute, 1, 16}};
        BindGroupLayoutDesc ld;
        ld.entries = rhi::Span<BindingDesc>(const_cast<BindingDesc*>(entries), 3);
        const BindGroupLayoutHandle layout = device->CreateBindGroupLayout(ld);
        ComputePipelineDesc cp;
        cp.shader = cs;
        cp.bind_group_layouts[0] = layout;
        cp.bind_group_layout_count = 1;
        cp.push_constant_bytes = 4;
        cp.debug_name = "cube sample";
        const PipelineHandle pipeline = device->CreateComputePipeline(cp);
        REQUIRE(pipeline.IsValid());
        BindGroupEntry e[3];
        e[0].binding = 0;
        e[0].texture = cube;
        e[1].binding = 1;
        e[1].sampler = sampler;
        e[2].binding = 2;
        e[2].buffer = out;
        BindGroupDesc gd;
        gd.layout = layout;
        gd.entries = rhi::Span<BindGroupEntry>(e, 3);
        const BindGroupHandle group = device->CreateBindGroup(gd);
        REQUIRE(group.IsValid());

        for (u32 mip = 0; mip < kMips; ++mip)
        {
            device->BeginFrame();
            CommandList* list = device->CreateCommandList();
            list->Begin();
            list->Barrier(out, ResourceState::Undefined, ResourceState::ShaderWrite);
            list->SetPipeline(pipeline);
            list->SetBindGroup(0, group);
            const f32 lod = static_cast<f32>(mip);
            list->SetPushConstants(0, &lod, sizeof(lod));
            list->Dispatch(1, 1, 1);
            list->Barrier(out, ResourceState::ShaderWrite, ResourceState::CopySrc);
            list->End();
            CommandList* lists[] = {list};
            REQUIRE(device->Submit(lists, 1) != 0);
            device->EndFrame();

            f32 got[24];
            REQUIRE(device->ReadbackBuffer(out, 0, got, sizeof(got)));
            for (u32 face = 0; face < 6; ++face)
            {
                u8 want[4];
                CubeFaceColor(face, mip, want);
                INFO("mip " << mip << " face " << face);
                for (u32 c = 0; c < 4; ++c)
                {
                    CHECK(CubeNear(static_cast<u8>(got[face * 4 + c] * 255.0f + 0.5f), want[c]));
                }
            }
        }

        device->WaitIdle();
        device->DestroyBindGroup(group);
        device->DestroyPipeline(pipeline);
        device->DestroyBindGroupLayout(layout);
        device->DestroyShader(cs);
        device->DestroyBuffer(out);
        device->DestroySampler(sampler);
        device->DestroyTexture(cube);
    }
}

TEST_CASE("Every face of a cube texture is copied from a buffer and back", "[rhi][gpu][cube]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        DevicePtr device = gpu_test::CreateTestDevice(backend);
        if (!device->Caps().cube_textures)
        {
            continue;
        }
        constexpr u32 kFaceSize = 64;  // a 256-byte row meets every backend's pitch alignment
        constexpr u32 kFaceBytes = kFaceSize * kFaceSize * 4;
        Vector<u8> pattern;
        pattern.Resize(6 * kFaceBytes);
        for (u32 i = 0; i < pattern.Size(); ++i)
        {
            pattern[i] = static_cast<u8>((i / kFaceBytes) * 40 + (i * 7) % 31);
        }
        const TextureHandle cube = device->CreateTexture(CubeDesc(kFaceSize, 1));
        REQUIRE(cube.IsValid());
        BufferDesc bd;
        bd.size = pattern.Size();
        bd.usage = BufferUsage::CopySrc | BufferUsage::CopyDst;
        bd.location = MemoryLocation::GpuOnly;
        const BufferHandle in = device->CreateBuffer(bd);
        const BufferHandle out = device->CreateBuffer(bd);
        REQUIRE(in.IsValid());
        REQUIRE(out.IsValid());
        REQUIRE(device->UploadBuffer(in, 0, pattern.Data(), pattern.Size()).ok);

        device->BeginFrame();
        CommandList* list = device->CreateCommandList();
        list->Begin();
        list->Barrier(in, ResourceState::Undefined, ResourceState::CopySrc);
        list->Barrier(out, ResourceState::Undefined, ResourceState::CopyDst);
        list->Barrier(cube, ResourceState::Undefined, ResourceState::CopyDst);
        for (u32 face = 0; face < 6; ++face)
        {
            BufferTextureCopy region;
            region.buffer_offset = static_cast<u64>(face) * kFaceBytes;
            region.array_layer = face;
            list->CopyBufferToTexture(in, cube, region);
        }
        list->Barrier(cube, ResourceState::CopyDst, ResourceState::CopySrc);
        for (u32 face = 0; face < 6; ++face)
        {
            BufferTextureCopy region;
            region.buffer_offset = static_cast<u64>(face) * kFaceBytes;
            region.array_layer = face;
            list->CopyTextureToBuffer(cube, out, region);
        }
        list->Barrier(out, ResourceState::CopyDst, ResourceState::CopySrc);
        list->End();
        CommandList* lists[] = {list};
        REQUIRE(device->Submit(lists, 1) != 0);
        device->EndFrame();

        Vector<u8> got;
        got.Resize(pattern.Size());
        REQUIRE(device->ReadbackBuffer(out, 0, got.Data(), got.Size()));
        for (u32 face = 0; face < 6; ++face)
        {
            INFO("face " << face);
            CHECK(MemCompare(got.Data() + face * kFaceBytes, pattern.Data() + face * kFaceBytes, kFaceBytes) == 0);
        }
        device->WaitIdle();
        device->DestroyBuffer(in);
        device->DestroyBuffer(out);
        device->DestroyTexture(cube);
    }
}
