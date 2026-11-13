// Phase 22 Day 317's packed vertex formats on the Metal backend (the twin of vulkan_vertex_format_test.cpp, written on
// the Mac's catch-up): each float-class format is drawn through triangle.vert as the color attribute and read back, so
// a wrong entry in the Metal table (metal_device_pipelines.mm) shows as a wrong pixel, under Metal's validation. The
// integer formats are read by Day 322's skinning shader. macOS only.
#include "metal_test_support.h"  // first: it imports AppKit before any engine name is in scope

#include "containers/array.h"
#include "containers/vector.h"
#include "cstr/mem_ops.h"

#include <catch2/catch_test_macros.hpp>

#include "rhi/rhi.h"
#include "triangle.frag.dxil.inc"
#include "triangle.vert.dxil.inc"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::rhi::metal::test;

namespace metal_vertex_format_test
{

constexpr u32 kTargetSize = 8;

struct Case
{
    VertexFormat format;
    u16 data[4];   // the attribute's 16-bit words, or bytes packed two to a word for the 8-bit format
    u8 expect[3];  // the color the attribute decodes to: (1.0, 0.5, 0) as bytes
};

}  // namespace metal_vertex_format_test

using namespace metal_vertex_format_test;

TEST_CASE("Metal: packed float-class vertex formats decode to the expected color", "[rhi][metal][gpu][vertexformat]")
{
    DevicePtr device = CreateDevice(MetalInfo("metal_vertex_format_test"));
    REQUIRE(device);
    // (1.0, 0.5, 0.0) in each format.
    const Case cases[] = {
        {VertexFormat::Float16x2, {0x3C00, 0x3800, 0, 0}, {255, 128, 0}},
        {VertexFormat::Float16x4, {0x3C00, 0x3800, 0, 0x3C00}, {255, 128, 0}},
        {VertexFormat::UNorm16x2, {0xFFFF, 0x8000, 0, 0}, {255, 128, 0}},
        {VertexFormat::UNorm16x4, {0xFFFF, 0x8000, 0, 0xFFFF}, {255, 128, 0}},
        {VertexFormat::SNorm16x2, {0x7FFF, 0x4000, 0, 0}, {255, 128, 0}},
        {VertexFormat::SNorm16x4, {0x7FFF, 0x4000, 0, 0x7FFF}, {255, 128, 0}},
        {VertexFormat::SNorm8x4, {0x407F, 0x7F00, 0, 0}, {255, 128, 0}},  // bytes 7F 40 00 7F, little-endian
    };
    const ShaderHandle vs = MetalShader(*device, shaders::kTriangleVertShader, ShaderStage::Vertex);
    const ShaderHandle fs = MetalShader(*device, shaders::kTriangleFragShader, ShaderStage::Fragment);
    REQUIRE((vs.IsValid() && fs.IsValid()));

    for (const Case& c : cases)
    {
        INFO("format " << static_cast<u32>(c.format));
        const u32 attribute_size = VertexFormatSize(c.format);
        const u32 stride = 8 + attribute_size;
        // Three vertices covering the target; the attribute bytes are the same in each.
        const f32 positions[3][2] = {{-1.0f, -1.0f}, {3.0f, -1.0f}, {-1.0f, 3.0f}};
        Vector<byte> bytes;
        bytes.Resize(static_cast<usize>(stride) * 3);
        MemSet(bytes.Data(), 0, bytes.Size());
        for (u32 v = 0; v < 3; ++v)
        {
            MemCopy(bytes.Data() + v * stride, positions[v], 8);
            MemCopy(bytes.Data() + v * stride + 8, c.data, attribute_size);
        }
        VertexBindingDesc binding = {stride, false};
        VertexAttributeDesc attributes[2] = {{0, 0, VertexFormat::Float2, 0}, {1, 0, c.format, 8}};
        GraphicsPipelineDesc gp;
        gp.vertex_shader = vs;
        gp.fragment_shader = fs;
        gp.vertex_bindings = rhi::Span<VertexBindingDesc>(&binding, 1);
        gp.vertex_attributes = rhi::Span<VertexAttributeDesc>(attributes, 2);
        gp.topology = PrimitiveTopology::TriangleList;
        gp.cull_mode = CullMode::None;
        gp.color_targets[0].format = Format::RGBA8_UNorm;
        gp.color_target_count = 1;
        gp.debug_name = "vertex_format";
        const PipelineHandle pipeline = device->CreateGraphicsPipeline(gp);
        REQUIRE(pipeline.IsValid());

        BufferDesc bd;
        bd.size = bytes.Size();
        bd.usage = BufferUsage::Vertex | BufferUsage::CopyDst;
        bd.location = MemoryLocation::GpuOnly;
        const BufferHandle vb = device->CreateBuffer(bd);
        REQUIRE(vb.IsValid());
        REQUIRE(device->UploadBuffer(vb, 0, bytes.Data(), bd.size).ok);

        TextureDesc td;
        td.width = kTargetSize;
        td.height = kTargetSize;
        td.format = Format::RGBA8_UNorm;
        td.usage = TextureUsage::ColorAttachment | TextureUsage::CopySrc;
        const TextureHandle target = device->CreateTexture(td);
        REQUIRE(target.IsValid());

        device->BeginFrame();
        CommandList* list = device->CreateCommandList();
        REQUIRE(list != nullptr);
        list->Begin();
        RenderingDesc pass;
        pass.color_count = 1;
        pass.color[0].texture = target;
        pass.color[0].load = LoadOp::Clear;
        list->BeginRendering(pass);
        list->SetPipeline(pipeline);
        list->SetVertexBuffer(0, vb, 0);
        list->Draw(3, 1, 0, 0);
        list->EndRendering();
        list->End();
        REQUIRE(device->Submit(&list, 1) != 0);
        device->EndFrame();
        Array<u8, kTargetSize * kTargetSize * 4> pixels = {};
        REQUIRE(device->ReadbackTexture(target, 0, 0, pixels.Data(), pixels.Size(), ResourceState::ColorAttachment));
        const u8* p = pixels.Data() + ((kTargetSize / 2) * kTargetSize + kTargetSize / 2) * 4;
        for (u32 k = 0; k < 3; ++k)
        {
            INFO("channel " << k << " is " << static_cast<u32>(p[k]));
            CHECK(p[k] + 2 >= c.expect[k]);
            CHECK(p[k] <= c.expect[k] + 2);
        }
        device->DestroyTexture(target);
        device->DestroyBuffer(vb);
        device->DestroyPipeline(pipeline);
    }
    device->WaitIdle();
    device->DestroyShader(vs);
    device->DestroyShader(fs);
}
