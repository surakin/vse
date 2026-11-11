// Compute through the RHI on every GPU backend (Phase 13 Day 170, from vulkan_compute_test.cpp): a fill and a saxpy
// dispatch with a barrier between them, and a storage texture written and read back. The Vulkan-only checks (the SPIR-V
// module check) stay there.

#include <catch2/catch_test_macros.hpp>

#include "backend_fixture.h"
#include "containers/vector.h"
#include "fill.comp.dxil.inc"
#include "gradient.comp.dxil.inc"
#include "rhi/rhi.h"
#include "saxpy.comp.dxil.inc"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;

namespace compute_test
{

struct ComputeScene
{
    gpu_test::BackendValidation validation;
    DevicePtr device;

    explicit ComputeScene(Backend backend) : validation(backend) { device = gpu_test::CreateTestDevice(backend); }
    ~ComputeScene()
    {
        if (device)
        {
            device->WaitIdle();
        }
    }

    PipelineHandle Pipeline(ShaderHandle cs, BindGroupLayoutHandle layout, u32 push, const char* name)
    {
        ComputePipelineDesc cp;
        cp.shader = cs;
        cp.bind_group_layouts[0] = layout;
        cp.bind_group_layout_count = 1;
        cp.push_constant_bytes = push;
        cp.debug_name = name;
        return device->CreateComputePipeline(cp);
    }

    BindGroupLayoutHandle Layout(const BindingDesc* entries, usize count)
    {
        BindGroupLayoutDesc ld;
        ld.entries = rhi::Span<BindingDesc>(const_cast<BindingDesc*>(entries), count);
        return device->CreateBindGroupLayout(ld);
    }

    BufferHandle Storage(u64 bytes)
    {
        BufferDesc bd;
        bd.size = bytes;
        bd.usage = BufferUsage::Storage | BufferUsage::CopySrc | BufferUsage::CopyDst;
        bd.location = MemoryLocation::GpuOnly;
        return device->CreateBuffer(bd);
    }

    BindGroupHandle Group(BindGroupLayoutHandle layout, BufferHandle a, BufferHandle b = {})
    {
        BindGroupEntry e[2];
        e[0].binding = 0;
        e[0].buffer = a;
        e[1].binding = 1;
        e[1].buffer = b;
        BindGroupDesc gd;
        gd.layout = layout;
        gd.entries = rhi::Span<BindGroupEntry>(e, b.IsValid() ? 2 : 1);
        return device->CreateBindGroup(gd);
    }
};

}  // namespace compute_test

using namespace compute_test;

TEST_CASE("Compute shaders fill a buffer and a second dispatch reads it after a barrier", "[rhi][gpu][compute]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        ComputeScene t(backend);
        constexpr u32 kCount = 1u << 20;  // 1M elements
        constexpr u64 kBytes = static_cast<u64>(kCount) * 4;
        const ShaderHandle fill_cs = CreateShaderFromSet(*t.device, shaders::kFillCompShader, ShaderStage::Compute);
        const ShaderHandle saxpy_cs = CreateShaderFromSet(*t.device, shaders::kSaxpyCompShader, ShaderStage::Compute);
        REQUIRE((fill_cs.IsValid() && saxpy_cs.IsValid()));
        // fill writes out[] (RWStructuredBuffer<uint>); saxpy reads x (StructuredBuffer<uint>) and writes y.
        const BindingDesc one_b[] = {{0, BindingType::StorageBuffer, ShaderStage::Compute, 1, 4}};
        const BindingDesc two_b[] = {{0, BindingType::ReadOnlyStorageBuffer, ShaderStage::Compute, 1, 4},
                                     {1, BindingType::StorageBuffer, ShaderStage::Compute, 1, 4}};
        const BindGroupLayoutHandle one = t.Layout(one_b, 1);
        const BindGroupLayoutHandle two = t.Layout(two_b, 2);
        const PipelineHandle fill = t.Pipeline(fill_cs, one, 4, "fill");
        const PipelineHandle saxpy = t.Pipeline(saxpy_cs, two, 4, "saxpy");
        REQUIRE((fill.IsValid() && saxpy.IsValid()));
        const BufferHandle x = t.Storage(kBytes);
        const BufferHandle y = t.Storage(kBytes);
        REQUIRE((x.IsValid() && y.IsValid()));
        Vector<u32> y_init;
        y_init.Resize(kCount);
        for (u32 i = 0; i < kCount; ++i)
        {
            y_init[i] = i;
        }
        REQUIRE(t.device->UploadBuffer(y, 0, y_init.Data(), kBytes).ok);  // leaves y in CopyDst
        const BindGroupHandle fill_group = t.Group(one, x);
        const BindGroupHandle saxpy_group = t.Group(two, x, y);
        REQUIRE((fill_group.IsValid() && saxpy_group.IsValid()));

        t.device->BeginFrame();
        CommandList* list = t.device->CreateCommandList();
        list->Begin();
        list->Barrier(x, ResourceState::Undefined, ResourceState::ShaderWrite);
        list->Barrier(y, ResourceState::CopyDst, ResourceState::ShaderWrite);
        // x[i] = 3 * i
        list->SetPipeline(fill);
        list->SetBindGroup(0, fill_group);
        const u32 k = 3;
        list->SetPushConstants(0, &k, sizeof(k));
        list->Dispatch(kCount / 64, 1, 1);
        // The saxpy dispatch reads what fill wrote: a compute-to-compute hazard needs the barrier.
        list->Barrier(x, ResourceState::ShaderWrite, ResourceState::ShaderRead);
        // y[i] = 2 * x[i] + y[i] = 7 * i
        list->SetPipeline(saxpy);
        list->SetBindGroup(0, saxpy_group);
        const u32 a = 2;
        list->SetPushConstants(0, &a, sizeof(a));
        list->Dispatch(kCount / 64, 1, 1);
        list->Barrier(x, ResourceState::ShaderRead, ResourceState::CopySrc);
        list->Barrier(y, ResourceState::ShaderWrite, ResourceState::CopySrc);
        list->End();
        CommandList* lists[] = {list};
        REQUIRE(t.device->Submit(lists, 1) != 0);
        t.device->EndFrame();

        Vector<u32> got_x;
        Vector<u32> got_y;
        got_x.Resize(kCount);
        got_y.Resize(kCount);
        REQUIRE(t.device->ReadbackBuffer(x, 0, got_x.Data(), kBytes));
        REQUIRE(t.device->ReadbackBuffer(y, 0, got_y.Data(), kBytes));
        u32 bad_x = 0;
        u32 bad_y = 0;
        for (u32 i = 0; i < kCount; ++i)
        {
            bad_x += got_x[i] != 3 * i;
            bad_y += got_y[i] != 7 * i;
        }
        CHECK(bad_x == 0);
        CHECK(bad_y == 0);

        t.device->WaitIdle();
        t.device->DestroyBindGroup(fill_group);
        t.device->DestroyBindGroup(saxpy_group);
        t.device->DestroyBuffer(x);
        t.device->DestroyBuffer(y);
        t.device->DestroyPipeline(fill);
        t.device->DestroyPipeline(saxpy);
        t.device->DestroyBindGroupLayout(one);
        t.device->DestroyBindGroupLayout(two);
        t.device->DestroyShader(fill_cs);
        t.device->DestroyShader(saxpy_cs);
    }
}

TEST_CASE("A compute shader writes a storage texture that is read back", "[rhi][gpu][compute]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        ComputeScene t(backend);
        constexpr u32 kTextureSize = 64;
        const ShaderHandle cs = CreateShaderFromSet(*t.device, shaders::kGradientCompShader, ShaderStage::Compute);
        const BindingDesc entry[] = {{0, BindingType::StorageTexture, ShaderStage::Compute, 1, 0}};
        const BindGroupLayoutHandle layout = t.Layout(entry, 1);
        const PipelineHandle pipeline = t.Pipeline(cs, layout, 0, "gradient");
        REQUIRE(pipeline.IsValid());
        TextureDesc td;
        td.width = kTextureSize;
        td.height = kTextureSize;
        td.format = Format::RGBA8_UNorm;
        td.usage = TextureUsage::Storage | TextureUsage::CopySrc;
        const TextureHandle image = t.device->CreateTexture(td);
        REQUIRE(image.IsValid());
        BindGroupEntry e;
        e.binding = 0;
        e.texture = image;
        BindGroupDesc gd;
        gd.layout = layout;
        gd.entries = rhi::Span<BindGroupEntry>(&e, 1);
        const BindGroupHandle group = t.device->CreateBindGroup(gd);
        REQUIRE(group.IsValid());

        t.device->BeginFrame();
        CommandList* list = t.device->CreateCommandList();
        list->Begin();
        list->Barrier(image, ResourceState::Undefined, ResourceState::ShaderWrite);
        list->SetPipeline(pipeline);
        list->SetBindGroup(0, group);
        list->Dispatch(kTextureSize / 8, kTextureSize / 8, 1);
        list->Barrier(image, ResourceState::ShaderWrite, ResourceState::CopySrc);  // compute write -> copy read
        list->End();
        CommandList* lists[] = {list};
        REQUIRE(t.device->Submit(lists, 1) != 0);
        t.device->EndFrame();

        Vector<u8> pixels;
        pixels.Resize(kTextureSize * kTextureSize * 4);
        REQUIRE(t.device->ReadbackTexture(image, 0, 0, pixels.Data(), pixels.Size(), ResourceState::CopySrc));
        u32 bad = 0;
        for (u32 py = 0; py < kTextureSize; ++py)
        {
            for (u32 px = 0; px < kTextureSize; ++px)
            {
                const u8* p = &pixels[(py * kTextureSize + px) * 4];
                // float -> unorm8 conversion may differ by one level between implementations.
                auto near = [](int got, int want) { return got >= want - 1 && got <= want + 1; };
                bad += !(near(p[0], static_cast<int>(px)) && near(p[1], static_cast<int>(py)) && p[2] == 0 &&
                         p[3] == 255);
            }
        }
        CHECK(bad == 0);

        t.device->WaitIdle();
        t.device->DestroyBindGroup(group);
        t.device->DestroyTexture(image);
        t.device->DestroyPipeline(pipeline);
        t.device->DestroyBindGroupLayout(layout);
        t.device->DestroyShader(cs);
    }
}
