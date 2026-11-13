// Phase 22 Day 317: the packed vertex formats on a real Vulkan device. Every format is read from a vertex buffer
// (the device reports the vertex-buffer feature), and each float-class format is drawn through triangle.vert as the
// color attribute and read back, so a wrong mapping shows as a wrong pixel. The integer formats are read by Day 322's
// skinning shader, where their readback test lives.
#include "containers/vector.h"

#include <catch2/catch_test_macros.hpp>

#include "cstr/mem_ops.h"
#include "gpu_fixture.h"
#include "image_compare.h"
#include "render_target.h"
#include "rhi/rhi.h"
#include "rhi/vulkan/vulkan_device.h"
#include "triangle.frag.spv.inc"
#include "triangle.vert.spv.inc"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::rhi::vulkan;

namespace
{

constexpr u32 kSize = 16;

VulkanDevicePtr MakeDevice(gpu_test::GpuFixture& fx)
{
    fx.RequireValidation();
    DeviceCreateInfo info;
    info.validation = ValidationSetting::On;
    info.sync_validation = true;
    info.pipeline_cache = false;
    VulkanDevicePtr device = VulkanDevice::Create(info);
    if (device == nullptr)
    {
        SKIP("no suitable Vulkan device");
    }
    return device;
}

struct Case
{
    VertexFormat format;
    u16 data[4];   // the attribute's 16-bit words, or bytes packed two to a word for the 8-bit format
    u8 expect[3];  // the color the attribute decodes to: (1.0, 0.5, 0) as bytes
};

}  // namespace

TEST_CASE("The GPU reads every vertex format from a vertex buffer", "[rhi][vulkan][vertexformat]")
{
    gpu_test::GpuFixture fx;
    VulkanDevicePtr device = MakeDevice(fx);
    for (u32 i = 0; i < static_cast<u32>(VertexFormat::Count); ++i)
    {
        INFO("vertex format " << i);
        CHECK(device->SupportsVertexFormat(static_cast<VertexFormat>(i)));
    }
    CHECK_FALSE(device->SupportsVertexFormat(VertexFormat::Count));
}

TEST_CASE("Packed float-class vertex formats decode to the expected color", "[rhi][vulkan][vertexformat]")
{
    gpu_test::GpuFixture fx;
    VulkanDevicePtr device = MakeDevice(fx);
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

    ShaderDesc sd;
    sd.stage = ShaderStage::Vertex;
    sd.SetCode(SpirvBlob(shaders::kTriangleVertSpv, shaders::kTriangleVertSpvWordCount));
    ShaderHandle vs = device->CreateShader(sd);
    sd.stage = ShaderStage::Fragment;
    sd.SetCode(SpirvBlob(shaders::kTriangleFragSpv, shaders::kTriangleFragSpvWordCount));
    ShaderHandle fs = device->CreateShader(sd);

    for (const Case& c : cases)
    {
        INFO("format " << static_cast<u32>(c.format));
        const u32 attribute_size = VertexFormatSize(c.format);
        const u32 stride = 8 + attribute_size;
        // Three vertices covering the viewport; the attribute bytes are the same in each.
        const float positions[3][2] = {{-1.0f, -1.0f}, {3.0f, -1.0f}, {-1.0f, 3.0f}};
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
        PipelineHandle pipeline = device->CreateGraphicsPipeline(gp);
        REQUIRE(pipeline.IsValid());

        BufferDesc bd;
        bd.size = bytes.Size();
        bd.usage = BufferUsage::Vertex | BufferUsage::CopyDst;
        bd.location = MemoryLocation::GpuOnly;
        BufferHandle vb = device->CreateBuffer(bd);
        REQUIRE(vb.IsValid());
        REQUIRE(device->UploadBuffer(vb, 0, bytes.Data(), bd.size).ok);

        gpu_test::RenderTarget target(device.Get(), kSize, kSize);
        REQUIRE(target.Valid());
        device->BeginFrame();
        CommandList* list = device->CreateCommandList();
        list->Begin();
        list->Barrier(vb, ResourceState::CopyDst, ResourceState::VertexBuffer);
        list->Barrier(target.Texture(), ResourceState::Undefined, ResourceState::ColorAttachment);
        RenderingDesc pass;
        pass.color_count = 1;
        pass.color[0].texture = target.Texture();
        list->BeginRendering(pass);
        list->SetPipeline(pipeline);
        list->SetViewport(Viewport{0, 0, static_cast<f32>(kSize), static_cast<f32>(kSize), 0.0f, 1.0f});
        list->SetScissor(ScissorRect{0, 0, kSize, kSize});
        list->SetVertexBuffer(0, vb, 0);
        list->Draw(3, 1, 0, 0);
        list->EndRendering();
        list->End();
        CommandList* lists[] = {list};
        REQUIRE(device->Submit(lists, 1) != 0);
        device->EndFrame();
        device->WaitIdle();
        gpu_test::Image image;
        REQUIRE(target.Readback(&image));
        const byte* p = &image.rgba[(static_cast<usize>(kSize / 2) * kSize + kSize / 2) * 4];
        for (u32 k = 0; k < 3; ++k)
        {
            INFO("channel " << k << " is " << static_cast<u32>(p[k]));
            CHECK(p[k] + 2 >= c.expect[k]);
            CHECK(p[k] <= c.expect[k] + 2);
        }
        device->DestroyBuffer(vb);
        device->DestroyPipeline(pipeline);
    }
    device->WaitIdle();
    device->DestroyShader(vs);
    device->DestroyShader(fs);
}
