// The render graph on Metal (Phase 21 Day 307): a pass that writes a target and the pass that samples it land in
// separate render encoders (the encoder boundary is what orders them on Metal, decision 9), the graph's barriers become
// nothing between encoders, and the sampled result is right. macOS only; the shared graph and scene suites run
// through Metal from backend_fixture.h.
#include "metal_test_support.h"  // first: it imports AppKit before any engine name is in scope

#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "fullscreen.vert.dxil.inc"
#include "graph_sample.frag.dxil.inc"
#include "mem/arena_allocator.h"
#include "renderer/render_graph.h"
#include "renderer/transient_pool.h"
#include "rhi/metal/metal_command_list.h"
#include "rhi/metal/metal_device.h"
#include "rhi/rhi.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::renderer;
using namespace engine_placeholder::rhi::metal;
using namespace engine_placeholder::rhi::metal::test;

namespace metal_graph_test
{

constexpr u32 kSize = 16;

struct Scene
{
    Device* device = nullptr;
    ShaderHandle vs;
    ShaderHandle fs;
    BindGroupLayoutHandle layout;
    PipelineHandle pipeline;
    SamplerHandle sampler;
    GraphResource a;
    GraphResource out;

    bool Create(Device* d)
    {
        device = d;
        vs = MetalShader(*device, shaders::kFullscreenVertShader, ShaderStage::Vertex);
        fs = MetalShader(*device, shaders::kGraphSampleFragShader, ShaderStage::Fragment);
        BindingDesc bindings[3] = {{0, BindingType::SampledTexture, ShaderStage::Fragment, 1},
                                   {1, BindingType::SampledTexture, ShaderStage::Fragment, 1},
                                   {2, BindingType::Sampler, ShaderStage::Fragment, 1}};
        BindGroupLayoutDesc ld;
        ld.entries = rhi::Span<BindingDesc>(bindings, 3);
        layout = device->CreateBindGroupLayout(ld);
        GraphicsPipelineDesc gp;
        gp.vertex_shader = vs;
        gp.fragment_shader = fs;
        gp.cull_mode = CullMode::None;
        gp.color_targets[0].format = Format::RGBA8_UNorm;
        gp.color_target_count = 1;
        gp.bind_group_layouts[0] = layout;
        gp.bind_group_layout_count = 1;
        pipeline = device->CreateGraphicsPipeline(gp);
        SamplerDesc smp;
        smp.mag_filter = smp.min_filter = smp.mip_filter = FilterMode::Nearest;
        smp.address_u = smp.address_v = AddressMode::ClampToEdge;
        sampler = device->CreateSampler(smp);
        return vs.IsValid() && fs.IsValid() && layout.IsValid() && pipeline.IsValid() && sampler.IsValid();
    }
    void Destroy()
    {
        device->DestroySampler(sampler);
        device->DestroyPipeline(pipeline);
        device->DestroyBindGroupLayout(layout);
        device->DestroyShader(vs);
        device->DestroyShader(fs);
    }
};

void ClearPass(const PassExecuteContext& ctx, void* user)
{
    const Scene* s = static_cast<const Scene*>(user);
    RenderingDesc rd;
    rd.color[0].texture = ctx.graph->ResolvedTexture(s->a);
    rd.color[0].clear_color[0] = 0.25f;  // a = (0.25, 0.5, 0.0, 0.5)
    rd.color[0].clear_color[1] = 0.5f;
    rd.color[0].clear_color[2] = 0.0f;
    rd.color[0].clear_color[3] = 0.5f;
    rd.color_count = 1;
    ctx.list->BeginRendering(rd);
    ctx.list->EndRendering();
}

void SamplePass(const PassExecuteContext& ctx, void* user)
{
    const Scene* s = static_cast<const Scene*>(user);
    BindGroupEntry e[3];
    e[0].binding = 0;
    e[0].texture = ctx.graph->ResolvedTexture(s->a);
    e[1].binding = 1;
    e[1].texture = ctx.graph->ResolvedTexture(s->a);
    e[2].binding = 2;
    e[2].sampler = s->sampler;
    BindGroupDesc gd;
    gd.layout = s->layout;
    gd.entries = rhi::Span<BindGroupEntry>(e, 3);
    gd.transient = true;
    const BindGroupHandle group = s->device->CreateBindGroup(gd);
    RenderingDesc rd;
    rd.color[0].texture = ctx.graph->ResolvedTexture(s->out);
    rd.color[0].clear_color[3] = 0.0f;
    rd.color_count = 1;
    ctx.list->BeginRendering(rd);
    ctx.list->SetPipeline(s->pipeline);
    ctx.list->SetBindGroup(0, group);
    ctx.list->Draw(3, 1, 0, 0);  // out = a + a = (0.5, 1.0, 0.0, 1.0)
    ctx.list->EndRendering();
}

}  // namespace metal_graph_test

using namespace metal_graph_test;

TEST_CASE("On Metal a render graph's writing pass and the pass that samples it are separate render encoders",
          "[rhi][metal][gpu][graph]")
{
    DevicePtr device = CreateDevice(MetalInfo("metal_graph_test"));
    REQUIRE(device);
    Scene scene;
    REQUIRE(scene.Create(device.Get()));
    TransientPool pool(device.Get(), TransientPoolDesc{});
    TextureDesc target_desc;
    target_desc.width = target_desc.height = kSize;
    target_desc.format = Format::RGBA8_UNorm;
    target_desc.usage = TextureUsage::ColorAttachment | TextureUsage::CopySrc;
    const TextureHandle target = device->CreateTexture(target_desc);
    REQUIRE(target.IsValid());
    ArenaAllocator arena(1u << 20);
    RenderGraph graph(&arena, RenderGraphDesc{});
    pool.BeginFrame(0);
    TextureDesc rt;
    rt.width = rt.height = kSize;
    rt.format = Format::RGBA16_Float;  // the renderer's HDR target format
    rt.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;
    scene.a = graph.CreateTexture("a", rt);
    scene.out = graph.ImportTexture("out", target, ResourceState::Undefined, ResourceState::CopySrc, false);
    graph.AddPass("clear", &ClearPass, &scene).Write(scene.a, GraphUsage::ColorAttachment);
    graph.AddPass("sample", &SamplePass, &scene)
        .Read(scene.a, GraphUsage::Sampled)
        .Write(scene.out, GraphUsage::ColorAttachment);
    device->BeginFrame();
    CommandList* list = device->CreateCommandList();
    REQUIRE(list != nullptr);
    list->Begin();
    REQUIRE(graph.Execute(&pool, list));
    list->End();
    const MetalCommandList::Counts counts = static_cast<MetalCommandList*>(list)->GetCounts();
    CHECK(counts.render_encoders == 2);  // one per pass: the encoder boundary orders the write before the read
    CHECK(counts.memory_barriers == 0);  // the graph's barriers are between encoders: nothing on Metal
    // a: Undefined -> ColorAttachment and ColorAttachment -> Sampled; out: Undefined -> ColorAttachment and
    // ColorAttachment -> CopySrc (its final state). None is inside a compute encoder (measured on Day 309).
    CHECK(counts.skipped_barriers == 4);
    REQUIRE(device->Submit(&list, 1) != 0);
    device->EndFrame();
    device->WaitIdle();
    pool.EndFrame();
    Vector<u8> pixels;
    pixels.Resize(kSize * kSize * 4);
    REQUIRE(device->ReadbackTexture(target, 0, 0, pixels.Data(), pixels.Size(), ResourceState::CopySrc));
    u32 bad = 0;
    for (u32 i = 0; i < kSize * kSize; ++i)
    {
        const u8* px = &pixels[i * 4];
        auto off = [](int got, int want) { return got < want - 2 || got > want + 2; };
        bad += off(px[0], 128) || off(px[1], 255) || off(px[2], 0) || off(px[3], 255) ? 1 : 0;
    }
    CHECK(bad == 0);
    device->DestroyTexture(target);
    scene.Destroy();
}
