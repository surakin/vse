// Phase 5 Day 76: a render graph executed on a real device with validation and synchronization validation on. Four
// passes: a clear (write), a draw that samples it, a compute dispatch that writes a storage image, and a draw that
// samples both results into an imported target. The graph records every barrier; the test records none, and the
// validation layers (including the synchronization checks) must stay silent.
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "fullscreen.vert.spv.inc"
#include "gpu_fixture.h"
#include "gradient.comp.spv.inc"
#include "graph_sample.frag.spv.inc"
#include "renderer/render_graph.h"
#include "renderer/transient_pool.h"
#include "rhi/rhi.h"
#include "rhi/vulkan/vulkan_device.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::rhi::vulkan;
using namespace engine_placeholder::renderer;

namespace {

constexpr u32 kVulkanRenderGraphTestSize = 16;

struct GraphScene {
    Device* device = nullptr;
    ShaderHandle vs, fs, cs;
    BindGroupLayoutHandle sample_layout, compute_layout;
    PipelineHandle sample_pipeline, compute_pipeline;
    SamplerHandle sampler;

    // Per-frame resources, filled in when the graph is declared.
    GraphResource a, b, c, out;

    bool Create(Device* d) {
        device = d;
        ShaderDesc sd;
        sd.stage = ShaderStage::Vertex;
        sd.words = shaders::kFullscreenVertSpv;
        sd.word_count = shaders::kFullscreenVertSpvWordCount;
        vs = device->CreateShader(sd);
        sd.stage = ShaderStage::Fragment;
        sd.words = shaders::kGraphSampleFragSpv;
        sd.word_count = shaders::kGraphSampleFragSpvWordCount;
        fs = device->CreateShader(sd);
        sd.stage = ShaderStage::Compute;
        sd.words = shaders::kGradientCompSpv;
        sd.word_count = shaders::kGradientCompSpvWordCount;
        cs = device->CreateShader(sd);

        BindingDesc sample_bindings[3] = {{0, BindingType::SampledTexture, ShaderStage::Fragment, 1},
                                          {1, BindingType::SampledTexture, ShaderStage::Fragment, 1},
                                          {2, BindingType::Sampler, ShaderStage::Fragment, 1}};
        BindGroupLayoutDesc ld;
        ld.entries = Span<BindingDesc>(sample_bindings, 3);
        sample_layout = device->CreateBindGroupLayout(ld);
        BindingDesc compute_binding = {0, BindingType::StorageTexture, ShaderStage::Compute, 1};
        ld.entries = Span<BindingDesc>(&compute_binding, 1);
        compute_layout = device->CreateBindGroupLayout(ld);

        GraphicsPipelineDesc gp;
        gp.vertex_shader = vs;
        gp.fragment_shader = fs;
        gp.cull_mode = CullMode::None;
        gp.color_targets[0].format = Format::RGBA8_UNorm;
        gp.color_target_count = 1;
        gp.bind_group_layouts[0] = sample_layout;
        gp.bind_group_layout_count = 1;
        gp.debug_name = "graph_sample";
        sample_pipeline = device->CreateGraphicsPipeline(gp);
        ComputePipelineDesc cp;
        cp.shader = cs;
        cp.bind_group_layouts[0] = compute_layout;
        cp.bind_group_layout_count = 1;
        cp.debug_name = "graph_gradient";
        compute_pipeline = device->CreateComputePipeline(cp);

        SamplerDesc smp;
        smp.mag_filter = smp.min_filter = smp.mip_filter = FilterMode::Nearest;
        smp.address_u = smp.address_v = AddressMode::ClampToEdge;
        sampler = device->CreateSampler(smp);
        return vs.IsValid() && fs.IsValid() && cs.IsValid() && sample_layout.IsValid() && compute_layout.IsValid() && sample_pipeline.IsValid() &&
               compute_pipeline.IsValid() && sampler.IsValid();
    }

    void Destroy() {
        device->DestroySampler(sampler);
        device->DestroyPipeline(sample_pipeline);
        device->DestroyPipeline(compute_pipeline);
        device->DestroyBindGroupLayout(sample_layout);
        device->DestroyBindGroupLayout(compute_layout);
        device->DestroyShader(vs);
        device->DestroyShader(fs);
        device->DestroyShader(cs);
    }

    // Draws the fullscreen triangle into `target`, adding the textures `first` and `second`.
    void DrawSample(const PassExecuteContext& ctx, GraphResource target, GraphResource first, GraphResource second, const f32* clear) const {
        BindGroupEntry e[3];
        e[0].binding = 0;
        e[0].texture = ctx.graph->ResolvedTexture(first);
        e[1].binding = 1;
        e[1].texture = ctx.graph->ResolvedTexture(second);
        e[2].binding = 2;
        e[2].sampler = sampler;
        BindGroupDesc gd;
        gd.layout = sample_layout;
        gd.entries = Span<BindGroupEntry>(e, 3);
        gd.transient = true;
        const BindGroupHandle group = device->CreateBindGroup(gd);
        RenderingDesc rd;
        rd.color[0].texture = ctx.graph->ResolvedTexture(target);
        rd.color[0].load = LoadOp::Clear;
        rd.color[0].clear_color[0] = clear[0];
        rd.color[0].clear_color[1] = clear[1];
        rd.color[0].clear_color[2] = clear[2];
        rd.color[0].clear_color[3] = clear[3];
        rd.color_count = 1;
        ctx.list->BeginRendering(rd);
        ctx.list->SetPipeline(sample_pipeline);
        ctx.list->SetViewport(Viewport{0, 0, static_cast<f32>(kVulkanRenderGraphTestSize), static_cast<f32>(kVulkanRenderGraphTestSize), 0.0f, 1.0f});
        ctx.list->SetScissor(ScissorRect{0, 0, kVulkanRenderGraphTestSize, kVulkanRenderGraphTestSize});
        ctx.list->SetBindGroup(0, group);
        ctx.list->Draw(3, 1, 0, 0);
        ctx.list->EndRendering();
    }
};

void ClearPass(const PassExecuteContext& ctx, void* user) {
    const GraphScene* s = static_cast<const GraphScene*>(user);
    RenderingDesc rd;
    rd.color[0].texture = ctx.graph->ResolvedTexture(s->a);
    rd.color[0].load = LoadOp::Clear;
    rd.color[0].clear_color[0] = 0.1f; // a = (0.1, 0.2, 0.05, 0.0)
    rd.color[0].clear_color[1] = 0.2f;
    rd.color[0].clear_color[2] = 0.05f;
    rd.color[0].clear_color[3] = 0.0f;
    rd.color_count = 1;
    ctx.list->BeginRendering(rd);
    ctx.list->EndRendering();
}

void DoublePass(const PassExecuteContext& ctx, void* user) {
    const GraphScene* s = static_cast<const GraphScene*>(user);
    const f32 clear[4] = {0, 0, 0, 0};
    s->DrawSample(ctx, s->b, s->a, s->a, clear); // b = a + a = (0.2, 0.4, 0.1, 0.0)
}

void ComputePass(const PassExecuteContext& ctx, void* user) {
    const GraphScene* s = static_cast<const GraphScene*>(user);
    BindGroupEntry e;
    e.binding = 0;
    e.texture = ctx.graph->ResolvedTexture(s->c);
    BindGroupDesc gd;
    gd.layout = s->compute_layout;
    gd.entries = Span<BindGroupEntry>(&e, 1);
    gd.transient = true;
    const BindGroupHandle group = s->device->CreateBindGroup(gd);
    ctx.list->SetPipeline(s->compute_pipeline);
    ctx.list->SetBindGroup(0, group);
    ctx.list->Dispatch(kVulkanRenderGraphTestSize / 8, kVulkanRenderGraphTestSize / 8, 1); // c(x, y) = (x / 255, y / 255, 0, 1)
}

void CombinePass(const PassExecuteContext& ctx, void* user) {
    const GraphScene* s = static_cast<const GraphScene*>(user);
    const f32 clear[4] = {0, 0, 0, 0};
    s->DrawSample(ctx, s->out, s->b, s->c, clear); // out = b + c
}

} // namespace

TEST_CASE("A four-pass render graph runs on a real device with no validation or synchronization messages", "[rhi][vulkan][gpu][graph]") {
    gpu_test::GpuFixture fx;
    fx.RequireValidation();
    DeviceCreateInfo info;
    info.validation = ValidationSetting::On;
    info.sync_validation = true;
    info.pipeline_cache = false;
    VulkanDevicePtr device = VulkanDevice::Create(info);
    if (device == nullptr) {
        SKIP("no suitable Vulkan device");
    }
    fx.ExpectErrors(0);
    {
        GraphScene scene;
        REQUIRE(scene.Create(device.Get()));
        TransientPool pool(device.Get(), TransientPoolDesc{});

        TextureDesc target_desc;
        target_desc.width = target_desc.height = kVulkanRenderGraphTestSize;
        target_desc.format = Format::RGBA8_UNorm;
        target_desc.usage = TextureUsage::ColorAttachment | TextureUsage::CopySrc;
        target_desc.debug_name = "graph target";
        const TextureHandle target = device->CreateTexture(target_desc);
        REQUIRE(target.IsValid());

        // Three frames: the second and third reuse the pooled textures, so their first barriers start from the state the
        // previous frame left them in.
        for (u64 frame = 0; frame < 3; ++frame) {
            ArenaAllocator arena(4u << 20);
            RenderGraph graph(&arena, RenderGraphDesc{});
            pool.BeginFrame(frame);
            TextureDesc rt;
            rt.width = rt.height = kVulkanRenderGraphTestSize;
            rt.format = Format::RGBA8_UNorm;
            rt.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;
            TextureDesc st = rt;
            st.usage = TextureUsage::Storage | TextureUsage::Sampled;
            scene.a = graph.CreateTexture("a", rt);
            scene.b = graph.CreateTexture("b", rt);
            scene.c = graph.CreateTexture("c", st);
            scene.out = graph.ImportTexture("out", target, ResourceState::Undefined, ResourceState::CopySrc, false);

            graph.AddPass("clear", &ClearPass, &scene).Write(scene.a, GraphUsage::ColorAttachment);
            graph.AddPass("double", &DoublePass, &scene).Read(scene.a, GraphUsage::Sampled).Write(scene.b, GraphUsage::ColorAttachment);
            graph.AddPass("gradient", &ComputePass, &scene).Write(scene.c, GraphUsage::StorageWrite);
            graph.AddPass("combine", &CombinePass, &scene)
                .Read(scene.b, GraphUsage::Sampled)
                .Read(scene.c, GraphUsage::Sampled)
                .Write(scene.out, GraphUsage::ColorAttachment);

            device->BeginFrame();
            CommandList* list = device->CreateCommandList();
            list->Begin();
            REQUIRE(graph.Execute(&pool, list));
            list->End();
            CommandList* lists[] = {list};
            REQUIRE(device->Submit(lists, 1) != 0);
            device->EndFrame();
            device->WaitIdle();
            pool.EndFrame();

            std::vector<u8> pixels(kVulkanRenderGraphTestSize * kVulkanRenderGraphTestSize * 4);
            REQUIRE(device->ReadbackTexture(target, 0, 0, pixels.data(), pixels.size(), ResourceState::CopySrc));
            // out(x, y) = b + c = (0.2 + x / 255, 0.4 + y / 255, 0.1, 1.0), in 8 bits: (51 + x, 102 + y, 26, 255).
            u32 bad = 0;
            for (u32 y = 0; y < kVulkanRenderGraphTestSize; ++y) {
                for (u32 x = 0; x < kVulkanRenderGraphTestSize; ++x) {
                    const u8* px = &pixels[(y * kVulkanRenderGraphTestSize + x) * 4];
                    auto off = [](int got, int want) { return got < want - 2 || got > want + 2; };
                    if (off(px[0], 51 + static_cast<int>(x)) || off(px[1], 102 + static_cast<int>(y)) || off(px[2], 26) || px[3] != 255) ++bad;
                }
            }
            CHECK(bad == 0);
        }
        const TransientPoolStats stats = pool.Stats();
        CHECK(stats.textures_created == 3);
        CHECK(stats.texture_reuses == 6);
        device->WaitIdle();
        device->DestroyTexture(target);
        scene.Destroy();
    }
    device->WaitIdle();
    for (const gpu_test::CapturedMessage& m : fx.Messages()) {
        if (m.severity == LogVerbosity::Error || m.severity == LogVerbosity::Warn) FAIL_CHECK(m.id << ": " << m.text);
    }
    device.Reset();
}
