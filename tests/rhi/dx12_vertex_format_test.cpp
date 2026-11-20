// Phase 22 Day 317, the Windows half: the packed vertex formats on a real D3D12 device, the twin of
// vulkan_vertex_format_test.cpp. Each float-class format is drawn through triangle.vert as the color attribute and read
// back, so a wrong DXGI mapping shows as a wrong pixel; the debug layer must stay silent. An attribute aligned only to
// its component size (an offset of 10 for a 16-bit format, 9 for an 8-bit one) failed `CreateInputLayout` here, which
// is why `VertexFormatAlignment` is 4 for every format; the test draws at offsets 8 and 12. The integer formats are
// read by Day 322's skinning shader.
#include "containers/vector.h"

#include <catch2/catch_test_macros.hpp>

#include "cstr/mem_ops.h"
#include "rhi/dx12/dx12_debug.h"
#include "rhi/dx12/dx12_device.h"
#include "rhi/rhi.h"
#include "triangle.frag.dxil.inc"
#include "triangle.vert.dxil.inc"

using namespace vse;
using namespace vse::rhi;
using namespace vse::rhi::dx12;

namespace dx12_vertex_format_test
{

constexpr u32 kSize = 16;

struct VertexFormatSilence
{
    VertexFormatSilence() { ResetDebugMessageCounts(); }
    ~VertexFormatSilence()
    {
        const DebugMessageCounts c = GetDebugMessageCounts();
        CHECK(c.errors == 0);
        CHECK(c.warnings == 0);
    }
};

ShaderHandle VertexFormatDxil(Dx12Device& device, ShaderStage stage, const u8* bytes, usize size)
{
    ShaderDesc sd;
    sd.stage = stage;
    sd.SetCode(DxilBlob(bytes, size));
    return device.CreateShader(sd);
}

struct Case
{
    VertexFormat format;
    u16 data[4];   // the attribute's 16-bit words, or bytes packed two to a word for the 8-bit format
    u8 expect[3];  // the color the attribute decodes to: (1.0, 0.5, 0) as bytes
};

// Draws a triangle covering the target with the attribute at `offset` in a `stride`-byte vertex and returns the center
// pixel through `out`.
bool DrawCenter(Dx12Device& device, ShaderHandle vs, ShaderHandle fs, const Case& c, u32 offset, u32 stride, u8* out)
{
    const u32 attribute_size = VertexFormatSize(c.format);
    const float positions[3][2] = {{-1.0f, -1.0f}, {3.0f, -1.0f}, {-1.0f, 3.0f}};
    Vector<byte> bytes;
    bytes.Resize(static_cast<usize>(stride) * 3);
    MemSet(bytes.Data(), 0, bytes.Size());
    for (u32 v = 0; v < 3; ++v)
    {
        MemCopy(bytes.Data() + v * stride, positions[v], 8);
        MemCopy(bytes.Data() + v * stride + offset, c.data, attribute_size);
    }
    VertexBindingDesc binding = {stride, false};
    VertexAttributeDesc attributes[2] = {{0, 0, VertexFormat::Float2, 0}, {1, 0, c.format, offset}};
    GraphicsPipelineDesc gp;
    gp.vertex_shader = vs;
    gp.fragment_shader = fs;
    gp.vertex_bindings = rhi::Span<VertexBindingDesc>(&binding, 1);
    gp.vertex_attributes = rhi::Span<VertexAttributeDesc>(attributes, 2);
    gp.topology = PrimitiveTopology::TriangleList;
    gp.cull_mode = CullMode::None;
    gp.color_targets[0].format = Format::RGBA8_UNorm;
    gp.color_target_count = 1;
    const PipelineHandle pipeline = device.CreateGraphicsPipeline(gp);
    if (!pipeline.IsValid())
    {
        return false;
    }
    BufferDesc bd;
    bd.size = bytes.Size();
    bd.usage = BufferUsage::Vertex | BufferUsage::CopyDst;
    bd.location = MemoryLocation::GpuOnly;
    const BufferHandle vb = device.CreateBuffer(bd);
    TextureDesc td;
    td.width = td.height = kSize;
    td.format = Format::RGBA8_UNorm;
    td.usage = TextureUsage::ColorAttachment | TextureUsage::CopySrc;
    const TextureHandle target = device.CreateTexture(td);
    bool ok = vb.IsValid() && target.IsValid() && device.UploadBuffer(vb, 0, bytes.Data(), bd.size).ok;
    if (ok)
    {
        device.BeginFrame();
        CommandList* list = device.CreateCommandList();
        ok = list != nullptr;
        if (ok)
        {
            list->Begin();
            list->Barrier(vb, ResourceState::CopyDst, ResourceState::VertexBuffer);
            list->Barrier(target, ResourceState::Undefined, ResourceState::ColorAttachment);
            RenderingDesc pass;
            pass.color_count = 1;
            pass.color[0].texture = target;
            list->BeginRendering(pass);
            list->SetPipeline(pipeline);
            list->SetViewport(Viewport{0, 0, static_cast<f32>(kSize), static_cast<f32>(kSize), 0.0f, 1.0f});
            list->SetScissor(ScissorRect{0, 0, kSize, kSize});
            list->SetVertexBuffer(0, vb, 0);
            list->Draw(3, 1, 0, 0);
            list->EndRendering();
            list->Barrier(target, ResourceState::ColorAttachment, ResourceState::CopySrc);
            list->End();
            CommandList* lists[] = {list};
            ok = device.Submit(lists, 1) != 0;
        }
        device.EndFrame();
        device.WaitIdle();
    }
    Vector<byte> pixels;
    pixels.Resize(static_cast<usize>(kSize) * kSize * 4);
    ok = ok && device.ReadbackTexture(target, 0, 0, pixels.Data(), pixels.Size(), ResourceState::CopySrc);
    if (ok)
    {
        MemCopy(out, &pixels[(static_cast<usize>(kSize / 2) * kSize + kSize / 2) * 4], 3);
    }
    if (target.IsValid())
    {
        device.DestroyTexture(target);
    }
    if (vb.IsValid())
    {
        device.DestroyBuffer(vb);
    }
    device.DestroyPipeline(pipeline);
    return ok;
}

}  // namespace dx12_vertex_format_test

using namespace dx12_vertex_format_test;

TEST_CASE("Packed float-class vertex formats decode to the expected color on DX12, aligned to their component size",
          "[rhi][dx12][gpu][vertexformat]")
{
    DeviceCreateInfo info;
    info.backend = Backend::Dx12;
    info.application_name = "dx12-vertex-format-test";
    info.validation = ValidationSetting::On;
    Dx12DevicePtr device = Dx12Device::Create(info);
    REQUIRE(device);
    VertexFormatSilence silence;
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
    const ShaderHandle vs = VertexFormatDxil(*device, ShaderStage::Vertex, shaders::kTriangleVertDxil,
                                             shaders::kTriangleVertDxilByteCount);
    const ShaderHandle fs = VertexFormatDxil(*device, ShaderStage::Fragment, shaders::kTriangleFragDxil,
                                             shaders::kTriangleFragDxilByteCount);
    REQUIRE((vs.IsValid() && fs.IsValid()));
    for (const Case& c : cases)
    {
        const u32 size = VertexFormatSize(c.format);
        // Right after the position (offset 8), and one alignment step further (offset 12, with 4 bytes of padding
        // before it). The stride is rounded up to 4 bytes either way.
        const u32 offsets[2] = {8, 8 + VertexFormatAlignment(c.format)};
        for (u32 offset : offsets)
        {
            INFO("format " << static_cast<u32>(c.format) << " at offset " << offset);
            const u32 stride = (offset + size + 3) / 4 * 4;
            u8 p[3] = {0, 0, 0};
            REQUIRE(DrawCenter(*device, vs, fs, c, offset, stride, p));
            for (u32 k = 0; k < 3; ++k)
            {
                INFO("channel " << k << " is " << static_cast<u32>(p[k]));
                CHECK(p[k] + 2 >= c.expect[k]);
                CHECK(p[k] <= c.expect[k] + 2);
            }
        }
    }
    device->DestroyShader(vs);
    device->DestroyShader(fs);
    device->Quiesce();
}
