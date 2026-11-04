#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "fill.comp.spv.inc"
#include "gpu_fixture.h"
#include "gradient.comp.spv.inc"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"
#include "rhi/vulkan/vulkan_device.h"
#include "saxpy.comp.spv.inc"
#include "triangle.vert.spv.inc"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::rhi::vulkan;

namespace {

struct QuietLog {
    bool saved = LogRhi.Enabled();
    QuietLog() { LogRhi.SetEnabled(false); }
    ~QuietLog() { LogRhi.SetEnabled(saved); }
};

struct ComputeDevice {
    gpu_test::GpuFixture fx;
    VulkanDevice* device = nullptr;

    ComputeDevice() {
        fx.RequireValidation();
        DeviceCreateInfo info;
        info.validation = ValidationSetting::On;
        info.sync_validation = true;
        info.pipeline_cache = false;
        device = VulkanDevice::Create(info); // no window: compute and offscreen only
        if (device == nullptr) {
            SKIP("no suitable Vulkan device");
        }
    }
    ~ComputeDevice() {
        if (device != nullptr) {
            device->WaitIdle();
            DestroyDevice(device);
        }
    }

    ShaderHandle Shader(const u32* words, usize count) {
        ShaderDesc d;
        d.stage = ShaderStage::Compute;
        d.words = words;
        d.word_count = count;
        return device->CreateShader(d);
    }

    BindGroupLayoutHandle StorageLayout(u32 buffers) {
        BindingDesc entries[2] = {{0, BindingType::StorageBuffer, ShaderStage::Compute, 1}, {1, BindingType::StorageBuffer, ShaderStage::Compute, 1}};
        BindGroupLayoutDesc ld;
        ld.entries = Span<BindingDesc>(entries, buffers);
        return device->CreateBindGroupLayout(ld);
    }

    PipelineHandle Pipeline(ShaderHandle cs, BindGroupLayoutHandle layout, const char* name) {
        ComputePipelineDesc cp;
        cp.shader = cs;
        cp.bind_group_layouts[0] = layout;
        cp.bind_group_layout_count = 1;
        cp.push_constant_bytes = 4;
        cp.debug_name = name;
        return device->CreateComputePipeline(cp);
    }

    BufferHandle Storage(u64 bytes) {
        BufferDesc bd;
        bd.size = bytes;
        bd.usage = BufferUsage::Storage | BufferUsage::CopySrc | BufferUsage::CopyDst;
        bd.location = MemoryLocation::GpuOnly;
        return device->CreateBuffer(bd);
    }

    BindGroupHandle Group(BindGroupLayoutHandle layout, BufferHandle a, BufferHandle b = {}) {
        BindGroupEntry e[2];
        e[0].binding = 0;
        e[0].buffer = a;
        e[1].binding = 1;
        e[1].buffer = b;
        BindGroupDesc gd;
        gd.layout = layout;
        gd.entries = Span<BindGroupEntry>(e, b.IsValid() ? 2 : 1);
        return device->CreateBindGroup(gd);
    }
};

} // namespace

TEST_CASE("Compute shaders fill a buffer and a second dispatch reads it after a barrier", "[rhi][vulkan][gpu][compute]") {
    ComputeDevice t;
    t.fx.ExpectErrors(0);
    constexpr u32 kCount = 1u << 20; // 1M elements
    constexpr u64 kBytes = static_cast<u64>(kCount) * 4;

    const ShaderHandle fill_cs = t.Shader(shaders::kFillCompSpv, shaders::kFillCompSpvWordCount);
    const ShaderHandle saxpy_cs = t.Shader(shaders::kSaxpyCompSpv, shaders::kSaxpyCompSpvWordCount);
    REQUIRE(fill_cs.IsValid());
    REQUIRE(saxpy_cs.IsValid());
    const BindGroupLayoutHandle one = t.StorageLayout(1);
    const BindGroupLayoutHandle two = t.StorageLayout(2);
    const PipelineHandle fill = t.Pipeline(fill_cs, one, "fill");
    const PipelineHandle saxpy = t.Pipeline(saxpy_cs, two, "saxpy");
    REQUIRE(fill.IsValid());
    REQUIRE(saxpy.IsValid());

    const BufferHandle x = t.Storage(kBytes);
    const BufferHandle y = t.Storage(kBytes);
    REQUIRE(x.IsValid());
    REQUIRE(y.IsValid());
    std::vector<u32> y_init(kCount);
    for (u32 i = 0; i < kCount; ++i) y_init[i] = i;
    REQUIRE(t.device->UploadBuffer(y, 0, y_init.data(), kBytes).ok); // leaves y in CopyDst

    const BindGroupHandle fill_group = t.Group(one, x);
    const BindGroupHandle saxpy_group = t.Group(two, x, y);
    REQUIRE(fill_group.IsValid());
    REQUIRE(saxpy_group.IsValid());

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

    std::vector<u32> got_x(kCount);
    std::vector<u32> got_y(kCount);
    REQUIRE(t.device->ReadbackBuffer(x, 0, got_x.data(), kBytes));
    REQUIRE(t.device->ReadbackBuffer(y, 0, got_y.data(), kBytes));
    u32 bad_x = 0;
    u32 bad_y = 0;
    for (u32 i = 0; i < kCount; ++i) {
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

TEST_CASE("Compute pipeline creation refuses bad input", "[rhi][vulkan][gpu][compute]") {
    ComputeDevice t;
    t.fx.ExpectErrors(0);
    QuietLog quiet;
    const ShaderHandle cs = t.Shader(shaders::kFillCompSpv, shaders::kFillCompSpvWordCount);
    const BindGroupLayoutHandle one = t.StorageLayout(1);

    ComputePipelineDesc cp;
    CHECK_FALSE(t.device->CreateComputePipeline(cp).IsValid()); // no shader

    ShaderDesc vd;
    vd.stage = ShaderStage::Vertex; // a compute module declared as a vertex shader is refused by the module check
    vd.words = shaders::kFillCompSpv;
    vd.word_count = shaders::kFillCompSpvWordCount;
    CHECK_FALSE(t.device->CreateShader(vd).IsValid());

    // A shader of another stage is not a compute shader.
    ShaderDesc vertex;
    vertex.stage = ShaderStage::Vertex;
    vertex.words = shaders::kTriangleVertSpv;
    vertex.word_count = shaders::kTriangleVertSpvWordCount;
    const ShaderHandle vs = t.device->CreateShader(vertex);
    REQUIRE(vs.IsValid());
    cp.shader = vs;
    CHECK_FALSE(t.device->CreateComputePipeline(cp).IsValid());
    t.device->DestroyShader(vs);

    cp.shader = cs;
    cp.bind_group_layouts[0] = one;
    cp.bind_group_layout_count = 1;
    cp.push_constant_bytes = 6; // not a multiple of 4
    CHECK_FALSE(t.device->CreateComputePipeline(cp).IsValid());
    cp.push_constant_bytes = 4;

    t.device->DestroyBindGroupLayout(one);
    t.device->WaitIdle();
    t.device->BeginFrame();
    t.device->EndFrame();
    t.device->WaitIdle();
    CHECK_FALSE(t.device->CreateComputePipeline(cp).IsValid()); // the layout is gone
    t.device->DestroyShader(cs);
}

TEST_CASE("A compute shader writes a storage texture that is read back", "[rhi][vulkan][gpu][compute]") {
    ComputeDevice t;
    t.fx.ExpectErrors(0);
    constexpr u32 kSize = 64;
    const ShaderHandle cs = t.Shader(shaders::kGradientCompSpv, shaders::kGradientCompSpvWordCount);
    BindingDesc entry = {0, BindingType::StorageTexture, ShaderStage::Compute, 1};
    BindGroupLayoutDesc ld;
    ld.entries = Span<BindingDesc>(&entry, 1);
    const BindGroupLayoutHandle layout = t.device->CreateBindGroupLayout(ld);
    ComputePipelineDesc cp;
    cp.shader = cs;
    cp.bind_group_layouts[0] = layout;
    cp.bind_group_layout_count = 1;
    const PipelineHandle pipeline = t.device->CreateComputePipeline(cp);
    REQUIRE(pipeline.IsValid());

    TextureDesc td;
    td.width = kSize;
    td.height = kSize;
    td.format = Format::RGBA8_UNorm;
    td.usage = TextureUsage::Storage | TextureUsage::CopySrc;
    const TextureHandle image = t.device->CreateTexture(td);
    REQUIRE(image.IsValid());
    BindGroupEntry e;
    e.binding = 0;
    e.texture = image;
    BindGroupDesc gd;
    gd.layout = layout;
    gd.entries = Span<BindGroupEntry>(&e, 1);
    const BindGroupHandle group = t.device->CreateBindGroup(gd);
    REQUIRE(group.IsValid());

    t.device->BeginFrame();
    CommandList* list = t.device->CreateCommandList();
    list->Begin();
    list->Barrier(image, ResourceState::Undefined, ResourceState::ShaderWrite);
    list->SetPipeline(pipeline);
    list->SetBindGroup(0, group);
    list->Dispatch(kSize / 8, kSize / 8, 1);
    list->Barrier(image, ResourceState::ShaderWrite, ResourceState::CopySrc); // compute write -> copy read
    list->End();
    CommandList* lists[] = {list};
    REQUIRE(t.device->Submit(lists, 1) != 0);
    t.device->EndFrame();

    std::vector<unsigned char> pixels(kSize * kSize * 4);
    REQUIRE(t.device->ReadbackTexture(image, 0, 0, pixels.data(), pixels.size(), ResourceState::CopySrc));
    u32 bad = 0;
    for (u32 y = 0; y < kSize; ++y) {
        for (u32 x = 0; x < kSize; ++x) {
            const unsigned char* p = &pixels[(y * kSize + x) * 4];
            // float -> unorm8 conversion may differ by one level between implementations.
            auto near = [](int got, int want) { return got >= want - 1 && got <= want + 1; };
            bad += !(near(p[0], static_cast<int>(x)) && near(p[1], static_cast<int>(y)) && p[2] == 0 && p[3] == 255);
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
