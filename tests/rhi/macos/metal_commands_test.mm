// The Metal backend's command lists (Phase 26 Day 320): encoders split by kind, state applied again after a split,
// barriers mapped per case, draws with a cleared pass and a strip with primitive restart, compute dispatches at the
// minted threadgroup size, lists recorded on two threads committed in submission order, mips by blit, debug labels
// across splits, and a list that failed while recording refused. macOS only.
#include "cstr/mem_ops.h"
#include "sync/thread.h"

#include <catch2/catch_test_macros.hpp>

#include "containers/array.h"
#include "fill.comp.dxil.inc"
#include "rhi/metal/metal_command_list.h"
#include "rhi/metal/metal_device.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"
#include "triangle.frag.dxil.inc"
#include "triangle.vert.dxil.inc"
#include "metal_test_support.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::rhi::metal;
using namespace engine_placeholder::rhi::metal::test;

namespace metal_commands_test
{

ShaderHandle Shader(Device& device, const ShaderBlobSet& set, ShaderStage stage)
{
    ShaderDesc d;
    d.stage = stage;
    d.SetCode(set.For(ShaderFormat::MetalLib));
    d.debug_name = set.name;
    return device.CreateShader(d);
}

BufferHandle Buffer(Device& device, u64 size, BufferUsage usage, MemoryLocation location = MemoryLocation::GpuOnly)
{
    BufferDesc d;
    d.size = size;
    d.usage = usage;
    d.location = location;
    return device.CreateBuffer(d);
}

// The fill.comp pipeline: out_buf[i] = i * k for i < count, with its layout and a group on `out`.
struct Fill
{
    ShaderHandle shader;
    BindGroupLayoutHandle layout;
    BindGroupHandle group;
    PipelineHandle pipeline;

    Fill(Device& device, BufferHandle out)
    {
        shader = Shader(device, shaders::kFillCompShader, ShaderStage::Compute);
        BindingDesc b;
        b.binding = 0;
        b.type = BindingType::StorageBuffer;
        b.stages = ShaderStage::Compute;
        b.stride = 4;
        BindGroupLayoutDesc ld;
        ld.entries = engine_placeholder::Span<BindingDesc>(&b, 1);
        layout = device.CreateBindGroupLayout(ld);
        BindGroupEntry e;
        e.binding = 0;
        e.buffer = out;
        BindGroupDesc gd;
        gd.layout = layout;
        gd.entries = engine_placeholder::Span<BindGroupEntry>(&e, 1);
        group = device.CreateBindGroup(gd);
        ComputePipelineDesc cp;
        cp.shader = shader;
        cp.bind_group_layouts[0] = layout;
        cp.bind_group_layout_count = 1;
        cp.push_constant_bytes = 8;
        cp.debug_name = "fill";
        pipeline = device.CreateComputePipeline(cp);
    }
    void Destroy(Device& device)
    {
        device.DestroyPipeline(pipeline);
        device.DestroyBindGroup(group);
        device.DestroyBindGroupLayout(layout);
        device.DestroyShader(shader);
    }
};

// The triangle pipeline (position and color per vertex, no bindings) into one RGBA8 target.
struct Triangle
{
    ShaderHandle vs;
    ShaderHandle fs;
    PipelineHandle pipeline;

    Triangle(Device& device, PrimitiveTopology topology)
    {
        vs = Shader(device, shaders::kTriangleVertShader, ShaderStage::Vertex);
        fs = Shader(device, shaders::kTriangleFragShader, ShaderStage::Fragment);
        VertexBindingDesc binding{20, false};
        VertexAttributeDesc attributes[2];
        attributes[0].location = 0;
        attributes[0].format = VertexFormat::Float2;
        attributes[1].location = 1;
        attributes[1].format = VertexFormat::Float3;
        attributes[1].offset = 8;
        GraphicsPipelineDesc gp;
        gp.vertex_shader = vs;
        gp.fragment_shader = fs;
        gp.vertex_bindings = rhi::Span<VertexBindingDesc>(&binding, 1);
        gp.vertex_attributes = rhi::Span<VertexAttributeDesc>(attributes, 2);
        gp.topology = topology;
        gp.primitive_restart = topology == PrimitiveTopology::TriangleStrip;
        gp.cull_mode = CullMode::None;
        gp.color_targets[0].format = Format::RGBA8_UNorm;
        gp.color_target_count = 1;
        gp.debug_name = "triangle";
        pipeline = device.CreateGraphicsPipeline(gp);
    }
    void Destroy(Device& device)
    {
        device.DestroyPipeline(pipeline);
        device.DestroyShader(vs);
        device.DestroyShader(fs);
    }
};

TextureHandle Target(Device& device)
{
    TextureDesc td;
    td.width = 8;
    td.height = 8;
    td.format = Format::RGBA8_UNorm;
    td.usage = TextureUsage::ColorAttachment | TextureUsage::CopySrc;
    return device.CreateTexture(td);
}

u32 Pixel(const Array<u8, 8 * 8 * 4>& p, u32 x, u32 y)
{
    const u8* q = p.Data() + (y * 8 + x) * 4;
    return static_cast<u32>(q[0]) | (static_cast<u32>(q[1]) << 8) | (static_cast<u32>(q[2]) << 16) |
           (static_cast<u32>(q[3]) << 24);
}

constexpr u32 kRed = 0xff0000ffu;   // RGBA8 bytes ff 00 00 ff
constexpr u32 kBlue = 0xffff0000u;  // 00 00 ff ff

}  // namespace metal_commands_test

using namespace metal_commands_test;

TEST_CASE("A Metal pass clears and draws, and a strip restarts at the all-ones index", "[rhi][metal][gpu]")
{
    DevicePtr device = CreateDevice(MetalInfo("metal_commands_test"));
    REQUIRE(device);
    // Two red triangles in a strip with a restart between: the top-left and bottom-right corners (y down: the
    // projection's flip, ENGINE_CLIP).
    const f32 vertices[6][5] = {{-1, -1, 1, 0, 0}, {0, -1, 1, 0, 0}, {-1, 0, 1, 0, 0},
                                {1, 1, 1, 0, 0},   {0, 1, 1, 0, 0},  {1, 0, 1, 0, 0}};
    const u16 indices[7] = {0, 1, 2, 0xffff, 3, 4, 5};
    const BufferHandle vb = Buffer(*device, sizeof(vertices), BufferUsage::Vertex | BufferUsage::CopyDst);
    const BufferHandle ib = Buffer(*device, sizeof(indices), BufferUsage::Index | BufferUsage::CopyDst);
    REQUIRE(device->UploadBuffer(vb, 0, vertices, sizeof(vertices)).ok);
    REQUIRE(device->UploadBuffer(ib, 0, indices, sizeof(indices)).ok);
    Triangle strip(*device, PrimitiveTopology::TriangleStrip);
    REQUIRE(strip.pipeline.IsValid());
    const TextureHandle target = Target(*device);
    device->BeginFrame();
    CommandList* list = device->CreateCommandList();
    REQUIRE(list != nullptr);
    list->Begin();
    RenderingDesc rd;
    rd.color[0].texture = target;
    rd.color[0].load = LoadOp::Clear;
    rd.color[0].clear_color[0] = 0.0f;
    rd.color[0].clear_color[2] = 1.0f;  // blue
    rd.color_count = 1;
    list->BeginRendering(rd);
    list->SetPipeline(strip.pipeline);
    list->SetVertexBuffer(0, vb, 0);
    list->SetIndexBuffer(ib, 0, IndexType::UInt16);
    list->DrawIndexed(7, 1, 0, 0, 0);
    list->EndRendering();
    list->End();
    const FenceValue fence = device->Submit(&list, 1);
    CHECK(fence != 0);
    CHECK(static_cast<MetalCommandList*>(list)->GetCounts().render_encoders == 1);
    device->EndFrame();
    Array<u8, 8 * 8 * 4> pixels = {};
    REQUIRE(device->ReadbackTexture(target, 0, 0, pixels.Data(), pixels.Size(), ResourceState::ColorAttachment));
    CHECK(Pixel(pixels, 1, 1) == kRed);   // the first triangle
    CHECK(Pixel(pixels, 6, 6) == kRed);   // the second, after the restart
    CHECK(Pixel(pixels, 6, 1) == kBlue);  // neither: the clear color
    CHECK(Pixel(pixels, 1, 6) == kBlue);
    strip.Destroy(*device);
    device->DestroyTexture(target);
    device->DestroyBuffer(vb);
    device->DestroyBuffer(ib);
}

TEST_CASE("Metal dispatches at the minted threadgroup, and state is applied again after an encoder split",
          "[rhi][metal][gpu]")
{
    DevicePtr device = CreateDevice(MetalInfo("metal_commands_test"));
    REQUIRE(device);
    const BufferHandle out = Buffer(*device, 256 * 4,
                                    BufferUsage::Storage | BufferUsage::CopySrc | BufferUsage::CopyDst);
    const BufferHandle copy = Buffer(*device, 256 * 4, BufferUsage::CopyDst | BufferUsage::CopySrc);
    Fill fill(*device, out);
    REQUIRE(fill.pipeline.IsValid());
    device->BeginFrame();
    CommandList* list = device->CreateCommandList();
    list->Begin();
    list->SetPipeline(fill.pipeline);
    list->SetBindGroup(0, fill.group);
    const u32 three[2] = {3, 256};
    list->SetPushConstants(0, three, 8);
    list->Dispatch(4, 1, 1);                                                 // 4 x 64 threads
    list->Barrier(out, ResourceState::ShaderWrite, ResourceState::CopySrc);  // in the compute encoder: a memory barrier
    list->CopyBuffer(out, copy, 0, 0, 256 * 4);                              // a blit encoder: the split
    list->Barrier(out, ResourceState::CopySrc, ResourceState::ShaderWrite);  // between encoders: nothing
    const u32 five[2] = {5, 256};
    list->SetPushConstants(0, five, 8);
    list->Dispatch(4, 1, 1);  // pipeline, group and push constants applied again on the new compute encoder
    list->End();
    const MetalCommandList::Counts counts = static_cast<MetalCommandList*>(list)->GetCounts();
    CHECK(counts.compute_encoders == 2);
    CHECK(counts.blit_encoders == 1);
    CHECK(counts.memory_barriers == 1);
    CHECK(counts.skipped_barriers == 1);
    CHECK(counts.binder_calls >= 2);  // the buffer on the second encoder too (the binder starts over)
    REQUIRE(device->Submit(&list, 1) != 0);
    device->EndFrame();
    Array<u32, 256> first = {};
    Array<u32, 256> second = {};
    REQUIRE(device->ReadbackBuffer(copy, 0, first.Data(), 256 * 4));
    REQUIRE(device->ReadbackBuffer(out, 0, second.Data(), 256 * 4));
    bool ok = true;
    for (u32 i = 0; i < 256; ++i)
    {
        ok = ok && first[i] == i * 3 && second[i] == i * 5;
    }
    CHECK(ok);
    fill.Destroy(*device);
    device->DestroyBuffer(out);
    device->DestroyBuffer(copy);
}

TEST_CASE("Metal lists recorded on two threads run in the order they are submitted", "[rhi][metal][gpu]")
{
    DevicePtr device = CreateDevice(MetalInfo("metal_commands_test"));
    REQUIRE(device);
    Array<u32, 64> source = {};
    for (u32 i = 0; i < 64; ++i)
    {
        source[i] = 0x1000 + i;
    }
    const BufferUsage usage = BufferUsage::CopySrc | BufferUsage::CopyDst;
    const BufferHandle a = Buffer(*device, 256, usage);
    const BufferHandle b = Buffer(*device, 256, usage);
    const BufferHandle c = Buffer(*device, 256, usage);
    REQUIRE(device->UploadBuffer(a, 0, source.Data(), 256).ok);
    device->BeginFrame();
    CommandList* first = device->CreateCommandList();
    CommandList* second = device->CreateCommandList();
    // Recorded in the opposite order on two threads; submitted a-to-b first, then b-to-c.
    Thread t2(
        [&]()
        {
            second->Begin();
            second->CopyBuffer(b, c, 0, 0, 256);
            second->End();
        });
    t2.Join();
    Thread t1(
        [&]()
        {
            first->Begin();
            first->CopyBuffer(a, b, 0, 0, 256);
            first->End();
        });
    t1.Join();
    CommandList* both[2] = {first, second};
    REQUIRE(device->Submit(both, 2) != 0);
    device->EndFrame();
    Array<u32, 64> got = {};
    REQUIRE(device->ReadbackBuffer(c, 0, got.Data(), 256));
    CHECK(MemCompare(got.Data(), source.Data(), 256) == 0);
    device->DestroyBuffer(a);
    device->DestroyBuffer(b);
    device->DestroyBuffer(c);
}

TEST_CASE("Metal generates mips by blit, and debug labels stay balanced across encoder splits", "[rhi][metal][gpu]")
{
    DevicePtr device = CreateDevice(MetalInfo("metal_commands_test"));
    REQUIRE(device);
    TextureDesc td;
    td.width = 8;
    td.height = 8;
    td.mip_levels = 4;
    td.format = Format::RGBA8_UNorm;
    td.usage = TextureUsage::Sampled | TextureUsage::CopySrc | TextureUsage::CopyDst | TextureUsage::ColorAttachment;
    const TextureHandle tex = device->CreateTexture(td);
    REQUIRE(tex.IsValid());
    Array<u8, 8 * 8 * 4> level0 = {};
    for (usize i = 0; i < level0.Size(); i += 4)
    {
        level0[i] = 200;
        level0[i + 1] = 100;
        level0[i + 2] = 50;
        level0[i + 3] = 255;
    }
    REQUIRE(device->UploadTexture(tex, 0, 0, level0.Data(), 0).ok);
    const BufferHandle scratch = Buffer(*device, 1024,
                                        BufferUsage::Storage | BufferUsage::CopySrc | BufferUsage::CopyDst);
    Fill fill(*device, scratch);
    device->BeginFrame();
    CommandList* list = device->CreateCommandList();
    list->Begin();
    list->PushDebugLabel("mips");
    list->GenerateMips(tex);  // a blit encoder
    list->PushDebugLabel("fill");
    list->SetPipeline(fill.pipeline);
    list->SetBindGroup(0, fill.group);
    const u32 push[2] = {1, 256};
    list->SetPushConstants(0, push, 8);
    list->Dispatch(4, 1, 1);  // a compute encoder: both labels opened again on it
    list->PopDebugLabel();
    list->PopDebugLabel();
    list->End();
    REQUIRE(device->Submit(&list, 1) != 0);
    device->EndFrame();
    Array<u8, 4> smallest = {};
    REQUIRE(device->ReadbackTexture(tex, 3, 0, smallest.Data(), 4, ResourceState::ShaderRead));
    CHECK(smallest[0] == 200);  // a solid picture halves to itself
    CHECK(smallest[1] == 100);
    CHECK(smallest[2] == 50);
    CHECK(smallest[3] == 255);
    fill.Destroy(*device);
    device->DestroyBuffer(scratch);
    device->DestroyTexture(tex);
}

TEST_CASE("A Metal list that failed while recording is refused, and nothing of its batch runs", "[rhi][metal][gpu]")
{
    DevicePtr device = CreateDevice(MetalInfo("metal_commands_test"));
    REQUIRE(device);
    const BufferHandle a = Buffer(*device, 256, BufferUsage::CopySrc | BufferUsage::CopyDst);
    const BufferHandle gone = Buffer(*device, 256, BufferUsage::CopySrc | BufferUsage::CopyDst);
    device->DestroyBuffer(gone);
    const bool was = LogRhi.Enabled();
    LogRhi.SetEnabled(false);
    device->BeginFrame();
    CommandList* list = device->CreateCommandList();
    list->Begin();
    list->CopyBuffer(a, gone, 0, 0, 256);
    list->End();
    CHECK(static_cast<MetalCommandList*>(list)->Failed());
    CHECK(device->Submit(&list, 1) == 0);
    device->EndFrame();
    LogRhi.SetEnabled(was);
    device->DestroyBuffer(a);
}

TEST_CASE("A compute pipeline bound before a Metal pass is still bound after it", "[rhi][metal][gpu]")
{
    DevicePtr device = CreateDevice(MetalInfo("metal_commands_test"));
    REQUIRE(device);
    const BufferHandle out = Buffer(*device, 256 * 4, BufferUsage::Storage | BufferUsage::CopySrc);
    Fill fill(*device, out);
    const TextureHandle target = Target(*device);
    device->BeginFrame();
    CommandList* list = device->CreateCommandList();
    list->Begin();
    list->SetPipeline(fill.pipeline);
    list->SetBindGroup(0, fill.group);
    RenderingDesc rd;
    rd.color[0].texture = target;
    rd.color_count = 1;
    list->BeginRendering(rd);
    list->EndRendering();
    const u32 seven[2] = {7, 256};
    list->SetPushConstants(0, seven, 8);  // needs a pipeline: the compute one is still bound
    list->Dispatch(4, 1, 1);
    list->End();
    REQUIRE(device->Submit(&list, 1) != 0);
    device->EndFrame();
    Array<u32, 256> got = {};
    REQUIRE(device->ReadbackBuffer(out, 0, got.Data(), 256 * 4));
    CHECK(got[10] == 70);
    fill.Destroy(*device);
    device->DestroyBuffer(out);
    device->DestroyTexture(target);
}

TEST_CASE("A failed Metal list still pairs its passes and labels, and seventeen nested labels balance",
          "[rhi][metal][gpu]")
{
    DevicePtr device = CreateDevice(MetalInfo("metal_commands_test"));
    REQUIRE(device);
    const BufferHandle a = Buffer(*device, 256, BufferUsage::CopySrc | BufferUsage::CopyDst);
    const BufferHandle gone = Buffer(*device, 256, BufferUsage::CopySrc | BufferUsage::CopyDst);
    device->DestroyBuffer(gone);
    const TextureHandle target = Target(*device);
    const bool was = LogRhi.Enabled();
    LogRhi.SetEnabled(false);
    device->BeginFrame();
    CommandList* list = device->CreateCommandList();
    list->Begin();
    list->PushDebugLabel("outer");
    list->CopyBuffer(a, gone, 0, 0, 256);  // fails: the rest is recorded as bookkeeping only
    RenderingDesc rd;
    rd.color[0].texture = target;
    rd.color_count = 1;
    list->BeginRendering(rd);
    list->EndRendering();  // no abort: the pass was opened in the bookkeeping
    list->PopDebugLabel();
    list->End();  // no abort: the label was closed
    CHECK(static_cast<MetalCommandList*>(list)->Failed());
    CHECK(device->Submit(&list, 1) == 0);
    LogRhi.SetEnabled(was);

    CommandList* deep = device->CreateCommandList();
    deep->Begin();
    for (int i = 0; i < 17; ++i)
    {
        deep->PushDebugLabel("level");
    }
    deep->CopyBuffer(a, a, 0, 128, 128);  // an encoder, with the 16 shown labels on it
    for (int i = 0; i < 17; ++i)
    {
        deep->PopDebugLabel();
    }
    deep->End();
    CHECK(device->Submit(&deep, 1) != 0);
    device->EndFrame();
    device->WaitIdle();
    device->DestroyBuffer(a);
    device->DestroyTexture(target);
}
