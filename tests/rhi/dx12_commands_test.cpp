// The Direct3D 12 backend's command lists (Phase 13 Day 168): every RHI state transition, the triangle against its golden image, compute
// through every binding type, copies at odd sizes, GenerateMips against a CPU reference, recording on several threads, many bind groups per
// frame across the shader-visible ring's wrap, and a list that fails cleanly. Windows only.
#include <cmath>
#include <cstring>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "bindings_test.frag.dxil.inc"
#include "fill.comp.dxil.inc"
#include "fullscreen.vert.dxil.inc"
#include "gradient.comp.dxil.inc"
#include "image_compare.h"
#include "saxpy.comp.dxil.inc"
#include "triangle.frag.dxil.inc"
#include "triangle.vert.dxil.inc"
#include "containers/vector.h"
#include "rhi/dx12/dx12_debug.h"
#include "rhi/dx12/dx12_device.h"
#include "rhi/dx12/dx12_format.h"
#include "rhi/rhi.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::rhi::dx12;

namespace dx12_commands_test {

struct CommandsSilence {
    CommandsSilence() { ResetDebugMessageCounts(); }
    ~CommandsSilence() {
        const DebugMessageCounts c = GetDebugMessageCounts();
        CHECK(c.errors == 0);
        CHECK(c.warnings == 0);
    }
};

Dx12DevicePtr MakeCommandsDevice() {
    DeviceCreateInfo info;
    info.backend = Backend::Dx12;
    info.application_name = "dx12-commands-test";
    info.validation = ValidationSetting::On;
    return Dx12Device::Create(info);
}

ShaderHandle Dxil(Dx12Device& device, ShaderStage stage, const u8* bytes, usize size) {
    ShaderDesc sd;
    sd.stage = stage;
    sd.SetCode(DxilBlob(bytes, size));
    return device.CreateShader(sd);
}

BufferHandle CmdBuffer(Dx12Device& device, u64 size, BufferUsage usage, MemoryLocation location = MemoryLocation::GpuOnly) {
    BufferDesc d;
    d.size = size;
    d.usage = usage;
    d.location = location;
    return device.CreateBuffer(d);
}

TextureHandle CmdTexture(Dx12Device& device, u32 w, u32 h, Format format, TextureUsage usage, u32 mips = 1, u32 layers = 1) {
    TextureDesc d;
    d.width = w;
    d.height = h;
    d.format = format;
    d.usage = usage;
    d.mip_levels = mips;
    d.array_layers = layers;
    return device.CreateTexture(d);
}

BindGroupLayoutHandle CmdLayout(Dx12Device& device, const BindingDesc* b, usize n) {
    BindGroupLayoutDesc d;
    d.entries = rhi::Span<BindingDesc>(const_cast<BindingDesc*>(b), n);
    return device.CreateBindGroupLayout(d);
}

BindGroupHandle CmdGroup(Dx12Device& device, BindGroupLayoutHandle layout, const BindGroupEntry* e, usize n, bool transient = false) {
    BindGroupDesc d;
    d.layout = layout;
    d.transient = transient;
    d.entries = rhi::Span<BindGroupEntry>(const_cast<BindGroupEntry*>(e), n);
    return device.CreateBindGroup(d);
}

BindGroupEntry CmdBufferEntry(u32 binding, BufferHandle b, u64 offset = 0, u64 size = 0) {
    BindGroupEntry e;
    e.binding = binding;
    e.buffer = b;
    e.offset = offset;
    e.size = size;
    return e;
}

// Records `record` into one list of one frame, submits it and waits.
template <typename F>
bool RunList(Dx12Device& device, F record) {
    device.BeginFrame();
    CommandList* list = device.CreateCommandList();
    if (list == nullptr) {
        device.EndFrame();
        return false;
    }
    list->Begin();
    record(list);
    list->End();
    CommandList* lists[] = {list};
    const bool ok = device.Submit(lists, 1) != 0;
    device.EndFrame();
    device.WaitIdle();
    return ok;
}

constexpr u32 kSize = 64;
constexpr f32 kClip[3][2] = {{0.0f, -0.8f}, {0.8f, 0.8f}, {-0.8f, 0.8f}};
constexpr f32 kColor[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

// The two-dimensional mip reference: a bilinear sample (clamped) of the level above at each texel centre, as shaders/mipgen.comp does,
// in linear space (sRGB decoded first, encoded after).
f32 Decode(f32 c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
f32 Encode(f32 c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f; }

void Downsample(const Vector<f32>& src, u32 sw, u32 sh, u32 dw, u32 dh, Vector<f32>* dst) {
    dst->Resize(static_cast<usize>(dw) * dh * 4);
    for (u32 y = 0; y < dh; ++y) {
        for (u32 x = 0; x < dw; ++x) {
            const f32 u = (static_cast<f32>(x) + 0.5f) / static_cast<f32>(dw) * static_cast<f32>(sw) - 0.5f;
            const f32 v = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(dh) * static_cast<f32>(sh) - 0.5f;
            const i32 x0 = static_cast<i32>(std::floor(u)), y0 = static_cast<i32>(std::floor(v));
            const f32 fx = u - static_cast<f32>(x0), fy = v - static_cast<f32>(y0);
            auto at = [&](i32 px, i32 py, u32 c) {
                px = px < 0 ? 0 : (px >= static_cast<i32>(sw) ? static_cast<i32>(sw) - 1 : px);
                py = py < 0 ? 0 : (py >= static_cast<i32>(sh) ? static_cast<i32>(sh) - 1 : py);
                return src[(static_cast<usize>(py) * sw + static_cast<usize>(px)) * 4 + c];
            };
            for (u32 c = 0; c < 4; ++c) {
                const f32 top = at(x0, y0, c) * (1 - fx) + at(x0 + 1, y0, c) * fx;
                const f32 bottom = at(x0, y0 + 1, c) * (1 - fx) + at(x0 + 1, y0 + 1, c) * fx;
                (*dst)[(static_cast<usize>(y) * dw + x) * 4 + c] = top * (1 - fy) + bottom * fy;
            }
        }
    }
}

} // namespace dx12_commands_test

using namespace dx12_commands_test;

TEST_CASE("Every RHI state maps to its D3D12 state, and every transition records and runs with the debug layer silent", "[rhi][dx12][gpu]") {
    CHECK(ToD3D12State(ResourceState::Undefined) == D3D12_RESOURCE_STATE_COMMON);
    CHECK(ToD3D12State(ResourceState::VertexBuffer) == D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    CHECK(ToD3D12State(ResourceState::UniformBuffer) == D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    CHECK(ToD3D12State(ResourceState::IndexBuffer) == D3D12_RESOURCE_STATE_INDEX_BUFFER);
    CHECK(ToD3D12State(ResourceState::ShaderRead) == (D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    CHECK(ToD3D12State(ResourceState::ShaderWrite) == D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    CHECK(ToD3D12State(ResourceState::CopySrc) == D3D12_RESOURCE_STATE_COPY_SOURCE);
    CHECK(ToD3D12State(ResourceState::CopyDst) == D3D12_RESOURCE_STATE_COPY_DEST);
    CHECK(ToD3D12State(ResourceState::ColorAttachment) == D3D12_RESOURCE_STATE_RENDER_TARGET);
    CHECK(ToD3D12State(ResourceState::DepthAttachment) == D3D12_RESOURCE_STATE_DEPTH_WRITE);
    CHECK((ToD3D12State(ResourceState::DepthRead) & D3D12_RESOURCE_STATE_DEPTH_READ) != 0);
    CHECK(ToD3D12State(ResourceState::Present) == D3D12_RESOURCE_STATE_PRESENT);

    Dx12DevicePtr device = MakeCommandsDevice();
    REQUIRE(device);
    CommandsSilence silence;
    const TextureHandle color = CmdTexture(*device, 16, 16, Format::RGBA8_UNorm,
                                        TextureUsage::Sampled | TextureUsage::Storage | TextureUsage::ColorAttachment | TextureUsage::CopySrc | TextureUsage::CopyDst);
    const TextureHandle depth = CmdTexture(*device, 16, 16, Format::D32_Float, TextureUsage::Sampled | TextureUsage::DepthAttachment | TextureUsage::CopySrc);
    const TextureHandle depth_only = CmdTexture(*device, 16, 16, Format::D32_Float, TextureUsage::DepthAttachment);
    const BufferHandle buf = CmdBuffer(*device, 4096, BufferUsage::Vertex | BufferUsage::Index | BufferUsage::Uniform | BufferUsage::Storage |
                                                       BufferUsage::CopySrc | BufferUsage::CopyDst);
    REQUIRE((color.IsValid() && depth.IsValid() && depth_only.IsValid() && buf.IsValid()));
    const ResourceState color_states[] = {ResourceState::ShaderRead, ResourceState::ShaderWrite, ResourceState::CopySrc, ResourceState::CopyDst,
                                          ResourceState::ColorAttachment, ResourceState::Present};
    const ResourceState depth_states[] = {ResourceState::DepthAttachment, ResourceState::DepthRead, ResourceState::ShaderRead, ResourceState::CopySrc};
    const ResourceState buffer_states[] = {ResourceState::VertexBuffer, ResourceState::IndexBuffer, ResourceState::UniformBuffer, ResourceState::ShaderRead,
                                           ResourceState::ShaderWrite, ResourceState::CopySrc, ResourceState::CopyDst};
    // Every pair, each from the state the previous barrier left (the same state twice is a UAV barrier for ShaderWrite, else nothing).
    REQUIRE(RunList(*device, [&](CommandList* list) {
        ResourceState c = ResourceState::Undefined;
        for (ResourceState a : color_states) {
            for (ResourceState b : color_states) {
                list->Barrier(color, c, a);
                list->Barrier(color, a, b);
                c = b;
            }
        }
        ResourceState d = ResourceState::Undefined;
        for (ResourceState a : depth_states) {
            for (ResourceState b : depth_states) {
                list->Barrier(depth, d, a);
                list->Barrier(depth, a, b);
                d = b;
            }
        }
        list->Barrier(depth_only, ResourceState::Undefined, ResourceState::DepthAttachment);
        list->Barrier(depth_only, ResourceState::DepthAttachment, ResourceState::DepthRead); // no shader access: depth read only
        list->Barrier(depth_only, ResourceState::DepthRead, ResourceState::DepthAttachment);
        ResourceState s = ResourceState::Undefined;
        for (ResourceState a : buffer_states) {
            for (ResourceState b : buffer_states) {
                list->Barrier(buf, s, a);
                list->Barrier(buf, a, b);
                s = b;
            }
        }
        list->Barrier(color, c, ResourceState::ShaderRead);
    }));
    // A second list starts where the first left the texture: the device transitions it (its recorded state) before the list runs.
    REQUIRE(RunList(*device, [&](CommandList* list) { list->Barrier(color, ResourceState::Undefined, ResourceState::CopySrc); }));
    CHECK(device->TextureState(color) == D3D12_RESOURCE_STATE_COPY_SOURCE);
    device->DestroyTexture(color);
    device->DestroyTexture(depth);
    device->DestroyTexture(depth_only);
    device->DestroyBuffer(buf);
    device->Quiesce();
}

TEST_CASE("The triangle renders offscreen on DX12 and matches its golden image", "[rhi][dx12][gpu][golden]") {
    Dx12DevicePtr device = MakeCommandsDevice();
    REQUIRE(device);
    CommandsSilence silence;
    const ShaderHandle vs = Dxil(*device, ShaderStage::Vertex, shaders::kTriangleVertDxil, shaders::kTriangleVertDxilByteCount);
    const ShaderHandle fs = Dxil(*device, ShaderStage::Fragment, shaders::kTriangleFragDxil, shaders::kTriangleFragDxilByteCount);
    VertexBindingDesc binding = {20, false};
    VertexAttributeDesc attributes[2] = {{0, 0, VertexFormat::Float2, 0}, {1, 0, VertexFormat::Float3, 8}};
    GraphicsPipelineDesc gp;
    gp.vertex_shader = vs;
    gp.fragment_shader = fs;
    gp.vertex_bindings = rhi::Span<VertexBindingDesc>(&binding, 1);
    gp.vertex_attributes = rhi::Span<VertexAttributeDesc>(attributes, 2);
    gp.cull_mode = CullMode::None;
    gp.color_targets[0].format = Format::RGBA8_UNorm;
    gp.color_target_count = 1;
    const PipelineHandle pipeline = device->CreateGraphicsPipeline(gp);
    REQUIRE(pipeline.IsValid());
    f32 vertices[15];
    for (int i = 0; i < 3; ++i) {
        vertices[i * 5 + 0] = kClip[i][0];
        vertices[i * 5 + 1] = kClip[i][1];
        for (int c = 0; c < 3; ++c) vertices[i * 5 + 2 + c] = kColor[i][c];
    }
    const BufferHandle vb = CmdBuffer(*device, sizeof(vertices), BufferUsage::Vertex | BufferUsage::CopyDst);
    REQUIRE(device->UploadBuffer(vb, 0, vertices, sizeof(vertices)).ok);
    const TextureHandle target = CmdTexture(*device, kSize, kSize, Format::RGBA8_UNorm, TextureUsage::ColorAttachment | TextureUsage::CopySrc);
    REQUIRE(target.IsValid());
    REQUIRE(RunList(*device, [&](CommandList* list) {
        list->Barrier(vb, ResourceState::CopyDst, ResourceState::VertexBuffer);
        list->Barrier(target, ResourceState::Undefined, ResourceState::ColorAttachment);
        RenderingDesc pass;
        pass.color_count = 1;
        pass.color[0].texture = target;
        pass.color[0].clear_color[0] = pass.color[0].clear_color[1] = pass.color[0].clear_color[2] = 0.2f;
        list->BeginRendering(pass);
        list->PushDebugLabel("triangle");
        list->SetPipeline(pipeline);
        list->SetVertexBuffer(0, vb, 0);
        list->Draw(3, 1, 0, 0);
        list->PopDebugLabel();
        list->EndRendering();
        list->Barrier(target, ResourceState::ColorAttachment, ResourceState::CopySrc);
    }));
    gpu_test::Image image;
    image.width = image.height = kSize;
    image.rgba.resize(kSize * kSize * 4);
    REQUIRE(device->ReadbackTexture(target, 0, 0, image.rgba.data(), image.rgba.size(), ResourceState::CopySrc));
    CHECK(std::abs(image.Pixel(1, 1)[0] - 51) <= 1); // the clear color
    CHECK(std::abs(image.Pixel(6, 40)[0] - 51) <= 1); // just left of the triangle: background
    gpu_test::Image golden;
    REQUIRE(gpu_test::ReadPpm(gpu_test::GoldenPath("triangle.ppm").c_str(), &golden));
    const gpu_test::CompareResult result = gpu_test::Compare(golden, image, gpu_test::CompareOptions{}, "dx12_triangle");
    INFO(result.message);
    CHECK(result.same);
    device->DestroyTexture(target);
    device->DestroyBuffer(vb);
    device->DestroyPipeline(pipeline);
    device->DestroyShader(vs);
    device->DestroyShader(fs);
    device->Quiesce();
}

TEST_CASE("Every binding type reaches a DX12 shader: storage buffers and push constants, a storage texture, a uniform buffer, a texture and a sampler",
          "[rhi][dx12][gpu][compute]") {
    // Compute has the storage types (saxpy, gradient); bindings_test.frag the uniform buffer, sampled texture and sampler.
    Dx12DevicePtr device = MakeCommandsDevice();
    REQUIRE(device);
    CommandsSilence silence;
    constexpr u32 kCount = 1u << 16;
    constexpr u64 kBytes = u64{kCount} * 4;
    const ShaderHandle fill_cs = Dxil(*device, ShaderStage::Compute, shaders::kFillCompDxil, shaders::kFillCompDxilByteCount);
    const ShaderHandle saxpy_cs = Dxil(*device, ShaderStage::Compute, shaders::kSaxpyCompDxil, shaders::kSaxpyCompDxilByteCount);
    const ShaderHandle gradient_cs = Dxil(*device, ShaderStage::Compute, shaders::kGradientCompDxil, shaders::kGradientCompDxilByteCount);
    const BindingDesc one_b[] = {{0, BindingType::StorageBuffer, ShaderStage::Compute, 1, 4}};
    const BindingDesc two_b[] = {{0, BindingType::ReadOnlyStorageBuffer, ShaderStage::Compute, 1, 4}, {1, BindingType::StorageBuffer, ShaderStage::Compute, 1, 4}};
    const BindingDesc tex_b[] = {{0, BindingType::StorageTexture, ShaderStage::Compute, 1, 0}};
    const BindGroupLayoutHandle one = CmdLayout(*device, one_b, 1), two = CmdLayout(*device, two_b, 2), tex_layout = CmdLayout(*device, tex_b, 1);
    auto compute = [&](ShaderHandle cs, BindGroupLayoutHandle layout, u32 push) {
        ComputePipelineDesc cp;
        cp.shader = cs;
        cp.bind_group_layouts[0] = layout;
        cp.bind_group_layout_count = 1;
        cp.push_constant_bytes = push;
        return device->CreateComputePipeline(cp);
    };
    const PipelineHandle fill = compute(fill_cs, one, 4), saxpy = compute(saxpy_cs, two, 4), gradient = compute(gradient_cs, tex_layout, 0);
    REQUIRE((fill.IsValid() && saxpy.IsValid() && gradient.IsValid()));
    const BufferHandle x = CmdBuffer(*device, kBytes, BufferUsage::Storage | BufferUsage::CopySrc);
    const BufferHandle y = CmdBuffer(*device, kBytes, BufferUsage::Storage | BufferUsage::CopySrc | BufferUsage::CopyDst);
    Vector<u32> y_init;
    y_init.Resize(kCount);
    for (u32 i = 0; i < kCount; ++i) y_init[i] = i;
    REQUIRE(device->UploadBuffer(y, 0, y_init.Data(), kBytes).ok);
    const BindGroupEntry fill_e[] = {CmdBufferEntry(0, x)};
    const BindGroupEntry saxpy_e[] = {CmdBufferEntry(0, x), CmdBufferEntry(1, y)};
    const BindGroupHandle fill_group = CmdGroup(*device, one, fill_e, 1), saxpy_group = CmdGroup(*device, two, saxpy_e, 2);
    const TextureHandle image = CmdTexture(*device, kSize, kSize, Format::RGBA8_UNorm, TextureUsage::Storage | TextureUsage::CopySrc);
    BindGroupEntry te;
    te.binding = 0;
    te.texture = image;
    const BindGroupHandle tex_group = CmdGroup(*device, tex_layout, &te, 1);
    REQUIRE((fill_group.IsValid() && saxpy_group.IsValid() && tex_group.IsValid()));
    REQUIRE(RunList(*device, [&](CommandList* list) {
        list->Barrier(x, ResourceState::Undefined, ResourceState::ShaderWrite);
        list->Barrier(y, ResourceState::CopyDst, ResourceState::ShaderWrite);
        list->SetPipeline(fill);
        list->SetBindGroup(0, fill_group);
        const u32 k = 3;
        list->SetPushConstants(0, &k, 4);
        list->Dispatch(kCount / 64, 1, 1);
        list->Barrier(x, ResourceState::ShaderWrite, ResourceState::ShaderRead);
        list->SetPipeline(saxpy);
        list->SetBindGroup(0, saxpy_group);
        const u32 a = 2;
        list->SetPushConstants(0, &a, 4);
        list->Dispatch(kCount / 64, 1, 1);
        list->Barrier(y, ResourceState::ShaderWrite, ResourceState::CopySrc);
        list->Barrier(image, ResourceState::Undefined, ResourceState::ShaderWrite);
        list->SetPipeline(gradient);
        list->SetBindGroup(0, tex_group);
        list->Dispatch(kSize / 8, kSize / 8, 1);
        list->Barrier(image, ResourceState::ShaderWrite, ResourceState::CopySrc);
    }));
    Vector<u32> got;
    got.Resize(kCount);
    REQUIRE(device->ReadbackBuffer(y, 0, got.Data(), kBytes));
    u32 bad = 0;
    for (u32 i = 0; i < kCount; ++i) bad += got[i] != 7 * i;
    CHECK(bad == 0);
    Vector<u8> pixels;
    pixels.Resize(kSize * kSize * 4);
    REQUIRE(device->ReadbackTexture(image, 0, 0, pixels.Data(), pixels.Size(), ResourceState::CopySrc));
    bad = 0;
    for (u32 py = 0; py < kSize; ++py) {
        for (u32 px = 0; px < kSize; ++px) {
            const u8* p = &pixels[(py * kSize + px) * 4];
            auto near = [](int g, int w) { return g >= w - 1 && g <= w + 1; };
            bad += !(near(p[0], static_cast<int>(px)) && near(p[1], static_cast<int>(py)) && p[2] == 0 && p[3] == 255);
        }
    }
    CHECK(bad == 0);

    // bindings_test.frag over a fullscreen triangle: color = texture * scale + bias.
    const ShaderHandle vs = Dxil(*device, ShaderStage::Vertex, shaders::kFullscreenVertDxil, shaders::kFullscreenVertDxilByteCount);
    const ShaderHandle fs = Dxil(*device, ShaderStage::Fragment, shaders::kBindingsTestFragDxil, shaders::kBindingsTestFragDxilByteCount);
    const BindingDesc frag_b[] = {{0, BindingType::UniformBuffer, ShaderStage::Fragment, 1, 0},
                                  {1, BindingType::SampledTexture, ShaderStage::Fragment, 1, 0},
                                  {2, BindingType::Sampler, ShaderStage::Fragment, 1, 0}};
    const BindGroupLayoutHandle frag_layout = CmdLayout(*device, frag_b, 3);
    GraphicsPipelineDesc gp;
    gp.vertex_shader = vs;
    gp.fragment_shader = fs;
    gp.cull_mode = CullMode::None;
    gp.color_targets[0].format = Format::RGBA8_UNorm;
    gp.color_target_count = 1;
    gp.bind_group_layouts[0] = frag_layout;
    gp.bind_group_layout_count = 1;
    gp.push_constant_bytes = 16;
    gp.push_constant_stages = ShaderStage::Fragment;
    const PipelineHandle draw = device->CreateGraphicsPipeline(gp);
    REQUIRE(draw.IsValid());
    const f32 scale[4] = {0.5f, 0.5f, 0.5f, 1.0f};
    const BufferHandle ubo = CmdBuffer(*device, 16, BufferUsage::Uniform | BufferUsage::CopyDst);
    REQUIRE(device->UploadBuffer(ubo, 0, scale, sizeof(scale)).ok);
    const TextureHandle src = CmdTexture(*device, 4, 4, Format::RGBA8_UNorm, TextureUsage::Sampled | TextureUsage::CopyDst);
    u8 texels[4 * 4 * 4];
    for (u32 i = 0; i < 16; ++i) {
        texels[i * 4 + 0] = 200;
        texels[i * 4 + 1] = 100;
        texels[i * 4 + 2] = 0;
        texels[i * 4 + 3] = 255;
    }
    REQUIRE(device->UploadTexture(src, 0, 0, texels, 0).ok);
    const SamplerHandle smp = device->CreateSampler(SamplerDesc{});
    BindGroupEntry fe[3] = {CmdBufferEntry(0, ubo)};
    fe[1].binding = 1;
    fe[1].texture = src;
    fe[2].binding = 2;
    fe[2].sampler = smp;
    const BindGroupHandle frag_group = CmdGroup(*device, frag_layout, fe, 3);
    const TextureHandle out = CmdTexture(*device, 8, 8, Format::RGBA8_UNorm, TextureUsage::ColorAttachment | TextureUsage::CopySrc);
    REQUIRE((frag_group.IsValid() && out.IsValid()));
    REQUIRE(RunList(*device, [&](CommandList* list) {
        list->Barrier(ubo, ResourceState::CopyDst, ResourceState::UniformBuffer);
        list->Barrier(out, ResourceState::Undefined, ResourceState::ColorAttachment);
        RenderingDesc pass;
        pass.color_count = 1;
        pass.color[0].texture = out;
        list->BeginRendering(pass);
        list->SetBindGroup(0, frag_group); // before the pipeline: bound with the draw
        list->SetPipeline(draw);
        const f32 bias[4] = {0.0f, 0.0f, 0.25f, 0.0f};
        list->SetPushConstants(0, bias, sizeof(bias));
        list->Draw(3, 1, 0, 0);
        list->EndRendering();
        list->Barrier(out, ResourceState::ColorAttachment, ResourceState::CopySrc);
    }));
    u8 result[8 * 8 * 4];
    REQUIRE(device->ReadbackTexture(out, 0, 0, result, sizeof(result), ResourceState::CopySrc));
    CHECK(std::abs(result[(3 * 8 + 3) * 4 + 0] - 100) <= 2); // 200 * 0.5
    CHECK(std::abs(result[(3 * 8 + 3) * 4 + 1] - 50) <= 2);  // 100 * 0.5
    CHECK(std::abs(result[(3 * 8 + 3) * 4 + 2] - 64) <= 2);  // 0 + 0.25

    for (BindGroupHandle g : {fill_group, saxpy_group, tex_group, frag_group}) device->DestroyBindGroup(g);
    for (PipelineHandle p : {fill, saxpy, gradient, draw}) device->DestroyPipeline(p);
    for (BindGroupLayoutHandle l : {one, two, tex_layout, frag_layout}) device->DestroyBindGroupLayout(l);
    for (ShaderHandle s : {fill_cs, saxpy_cs, gradient_cs, vs, fs}) device->DestroyShader(s);
    for (BufferHandle b : {x, y, ubo}) device->DestroyBuffer(b);
    for (TextureHandle t : {image, src, out}) device->DestroyTexture(t);
    device->DestroySampler(smp);
    device->Quiesce();
    CHECK(device->Resources().UserObjects() == 0);
}

TEST_CASE("DX12 copies between buffers and odd-sized texture levels round-trip", "[rhi][dx12][gpu]") {
    Dx12DevicePtr device = MakeCommandsDevice();
    REQUIRE(device);
    CommandsSilence silence;
    constexpr u32 kTexW = 37, kTexH = 19;
    const TextureHandle tex = CmdTexture(*device, kTexW, kTexH, Format::RGBA8_UNorm, TextureUsage::Sampled | TextureUsage::CopySrc | TextureUsage::CopyDst, 2, 3);
    // Level 1 of layer 2 is 18 x 9 texels; rows at the 256-byte pitch D3D12 needs.
    constexpr u32 kLw = kTexW / 2, kLh = kTexH / 2, kPitch = 256;
    const BufferHandle src = CmdBuffer(*device, 512 + u64{kPitch} * kLh, BufferUsage::CopySrc | BufferUsage::CopyDst);
    const BufferHandle back = CmdBuffer(*device, u64{kPitch} * kLh, BufferUsage::CopyDst, MemoryLocation::GpuToCpu);
    const BufferHandle copy = CmdBuffer(*device, u64{kPitch} * kLh, BufferUsage::CopySrc | BufferUsage::CopyDst);
    REQUIRE((tex.IsValid() && src.IsValid() && back.IsValid() && copy.IsValid()));
    Vector<u8> sent;
    sent.Resize(512 + kPitch * kLh);
    for (usize i = 0; i < sent.Size(); ++i) sent[i] = static_cast<u8>(i * 7 + 3);
    REQUIRE(device->UploadBuffer(src, 0, sent.Data(), sent.Size()).ok);
    BufferTextureCopy region;
    region.buffer_offset = 512;
    region.buffer_row_pitch = kPitch;
    region.mip_level = 1;
    region.array_layer = 2;
    BufferTextureCopy out_region = region;
    out_region.buffer_offset = 0;
    REQUIRE(RunList(*device, [&](CommandList* list) {
        list->Barrier(src, ResourceState::CopyDst, ResourceState::CopySrc);
        list->Barrier(tex, ResourceState::Undefined, ResourceState::CopyDst);
        list->CopyBufferToTexture(src, tex, region);
        list->Barrier(tex, ResourceState::CopyDst, ResourceState::CopySrc);
        list->CopyTextureToBuffer(tex, copy, out_region); // a GPU-only buffer, then on to the readback heap
        list->Barrier(copy, ResourceState::CopyDst, ResourceState::CopySrc);
        list->CopyBuffer(copy, back, 0, 0, u64{kPitch} * kLh);
    }));
    const u8* mapped = static_cast<const u8*>(device->MapBuffer(back));
    REQUIRE(mapped != nullptr);
    u32 bad = 0;
    for (u32 row = 0; row < kLh; ++row) {
        bad += std::memcmp(mapped + row * kPitch, sent.Data() + 512 + row * kPitch, kLw * 4) != 0;
    }
    device->UnmapBuffer(back);
    CHECK(bad == 0);
    device->DestroyTexture(tex);
    for (BufferHandle b : {src, back, copy}) device->DestroyBuffer(b);
    device->Quiesce();
}

TEST_CASE("DX12 GenerateMips matches a CPU reference for linear and sRGB textures with layers", "[rhi][dx12][gpu]") {
    Dx12DevicePtr device = MakeCommandsDevice();
    REQUIRE(device);
    CommandsSilence silence;
    CHECK(device->CanGenerateMips(Format::RGBA8_UNorm));
    CHECK(device->CanGenerateMips(Format::RGBA8_SRGB));
    CHECK(device->CanGenerateMips(Format::RGBA16_Float));
    CHECK_FALSE(device->CanGenerateMips(Format::D32_Float));
    CHECK_FALSE(device->CanGenerateMips(Format::BC1_UNorm));
    CHECK_FALSE(device->CanGenerateMips(Format::R32_UInt));
    constexpr u32 kTexW = 37, kTexH = 23, kLevels = 6, kLayers = 2;
    for (Format format : {Format::RGBA8_UNorm, Format::RGBA8_SRGB}) {
        const bool srgb = format == Format::RGBA8_SRGB;
        INFO(FormatInfo(format).name);
        const TextureHandle tex = CmdTexture(*device, kTexW, kTexH, format, TextureUsage::Sampled | TextureUsage::CopySrc | TextureUsage::CopyDst, kLevels, kLayers);
        REQUIRE(tex.IsValid());
        Vector<u8> level0[kLayers];
        for (u32 l = 0; l < kLayers; ++l) {
            level0[l].Resize(kTexW * kTexH * 4);
            u32 s = 12345 + l * 777;
            for (usize i = 0; i < level0[l].Size(); ++i) {
                s = s * 1664525u + 1013904223u;
                level0[l][i] = static_cast<u8>(s >> 24);
            }
            REQUIRE(device->UploadTexture(tex, 0, l, level0[l].Data(), 0).ok);
        }
        REQUIRE(RunList(*device, [&](CommandList* list) { list->GenerateMips(tex); }));
        for (u32 l = 0; l < kLayers; ++l) {
            Vector<f32> ref;
            ref.Resize(kTexW * kTexH * 4);
            for (usize i = 0; i < ref.Size(); ++i) {
                const f32 v = static_cast<f32>(level0[l][i]) / 255.0f;
                ref[i] = srgb && i % 4 != 3 ? Decode(v) : v;
            }
            u32 w = kTexW, h = kTexH;
            for (u32 level = 1; level < kLevels; ++level) {
                const u32 nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
                Vector<f32> next;
                Downsample(ref, w, h, nw, nh, &next);
                Vector<u8> got;
                got.Resize(nw * nh * 4);
                REQUIRE(device->ReadbackTexture(tex, level, l, got.Data(), got.Size(), ResourceState::ShaderRead));
                int worst = 0;
                for (usize i = 0; i < got.Size(); ++i) {
                    const f32 r = srgb && i % 4 != 3 ? Encode(next[i]) : next[i];
                    const int want = static_cast<int>(r * 255.0f + 0.5f);
                    const int diff = std::abs(static_cast<int>(got[i]) - want);
                    worst = diff > worst ? diff : worst;
                }
                INFO("layer " << l << " level " << level << " (" << nw << "x" << nh << ")");
                CHECK(worst <= 2);
                ref = static_cast<Vector<f32>&&>(next);
                w = nw;
                h = nh;
            }
        }
        device->DestroyTexture(tex);
    }
    device->Quiesce();
}

TEST_CASE("DX12 command lists record on several threads at once and run in one submit", "[rhi][dx12][gpu]") {
    Dx12DevicePtr device = MakeCommandsDevice();
    REQUIRE(device);
    CommandsSilence silence;
    constexpr u32 kThreads = 4;
    TextureHandle targets[kThreads];
    for (TextureHandle& t : targets) {
        t = CmdTexture(*device, 8, 8, Format::RGBA8_UNorm, TextureUsage::ColorAttachment | TextureUsage::CopySrc);
        REQUIRE(t.IsValid());
    }
    device->BeginFrame();
    CommandList* lists[kThreads] = {};
    std::thread threads[kThreads];
    for (u32 i = 0; i < kThreads; ++i) {
        threads[i] = std::thread([&, i] {
            CommandList* list = device->CreateCommandList();
            if (list == nullptr) return;
            list->Begin();
            list->Barrier(targets[i], ResourceState::Undefined, ResourceState::ColorAttachment);
            RenderingDesc pass;
            pass.color_count = 1;
            pass.color[0].texture = targets[i];
            pass.color[0].clear_color[0] = static_cast<f32>(i + 1) / 8.0f;
            list->BeginRendering(pass);
            list->EndRendering();
            list->Barrier(targets[i], ResourceState::ColorAttachment, ResourceState::CopySrc);
            list->End();
            lists[i] = list;
        });
    }
    for (std::thread& t : threads) t.join();
    for (CommandList* l : lists) REQUIRE(l != nullptr);
    REQUIRE(device->Submit(lists, kThreads) != 0);
    device->EndFrame();
    device->WaitIdle();
    for (u32 i = 0; i < kThreads; ++i) {
        u8 px[8 * 8 * 4];
        REQUIRE(device->ReadbackTexture(targets[i], 0, 0, px, sizeof(px), ResourceState::CopySrc));
        CHECK(std::abs(px[0] - static_cast<int>((i + 1) * 255 / 8.0f + 0.5f)) <= 1);
        device->DestroyTexture(targets[i]);
    }
    device->Quiesce();
}

TEST_CASE("Many DX12 bind groups per frame over more frames than the ring has slots: every dispatch sees its own", "[rhi][dx12][gpu]") {
    Dx12DevicePtr device = MakeCommandsDevice();
    REQUIRE(device);
    CommandsSilence silence;
    constexpr u32 kGroups = 512, kFrames = 5, kSlice = 256; // 64 elements of 4 bytes per group
    const ShaderHandle cs = Dxil(*device, ShaderStage::Compute, shaders::kFillCompDxil, shaders::kFillCompDxilByteCount);
    const BindingDesc b[] = {{0, BindingType::StorageBuffer, ShaderStage::Compute, 1, 4}};
    const BindGroupLayoutHandle layout = CmdLayout(*device, b, 1);
    ComputePipelineDesc cp;
    cp.shader = cs;
    cp.bind_group_layouts[0] = layout;
    cp.bind_group_layout_count = 1;
    cp.push_constant_bytes = 4;
    const PipelineHandle fill = device->CreateComputePipeline(cp);
    const BufferHandle buf = CmdBuffer(*device, u64{kGroups} * kSlice, BufferUsage::Storage | BufferUsage::CopySrc);
    REQUIRE((fill.IsValid() && buf.IsValid()));
    for (u32 f = 0; f < kFrames; ++f) {
        device->BeginFrame();
        const u32 slot = static_cast<u32>(device->FrameNumber() % Dx12Device::kFrameSlots);
        CHECK(device->ViewRing().Used(slot) == 0); // emptied when the slot came round
        CommandList* list = device->CreateCommandList();
        REQUIRE(list != nullptr);
        list->Begin();
        list->Barrier(buf, ResourceState::Undefined, ResourceState::ShaderWrite);
        list->SetPipeline(fill);
        for (u32 g = 0; g < kGroups; ++g) {
            const BindGroupEntry e = CmdBufferEntry(0, buf, u64{g} * kSlice, kSlice);
            const BindGroupHandle group = CmdGroup(*device, layout, &e, 1, true);
            REQUIRE(group.IsValid());
            list->SetBindGroup(0, group);
            const u32 k = g + f * 1000;
            list->SetPushConstants(0, &k, 4);
            list->Dispatch(1, 1, 1);
        }
        list->Barrier(buf, ResourceState::ShaderWrite, ResourceState::CopySrc);
        list->End();
        CHECK(device->ViewRing().Used(slot) == kGroups);
        CommandList* lists[] = {list};
        REQUIRE(device->Submit(lists, 1) != 0);
        device->EndFrame();
        if (f == kFrames - 1) {
            device->WaitIdle();
            Vector<u32> got;
            got.Resize(kGroups * kSlice / 4);
            REQUIRE(device->ReadbackBuffer(buf, 0, got.Data(), got.Size() * 4));
            u32 bad = 0;
            for (u32 g = 0; g < kGroups; ++g) {
                for (u32 i = 0; i < kSlice / 4; ++i) bad += got[g * (kSlice / 4) + i] != i * (g + f * 1000);
            }
            CHECK(bad == 0);
        }
    }
    device->DestroyBuffer(buf);
    device->DestroyPipeline(fill);
    device->DestroyBindGroupLayout(layout);
    device->DestroyShader(cs);
    device->Quiesce();
}

TEST_CASE("A DX12 list begun and dropped without End is handed out again when its frame slot comes round", "[rhi][dx12][gpu]") {
    // Review finding (Day 168): the allocator's reset failed under a list that was still recording, so CreateCommandList returned null.
    Dx12DevicePtr device = MakeCommandsDevice();
    REQUIRE(device);
    device->BeginFrame();
    CommandList* dropped = device->CreateCommandList();
    REQUIRE(dropped != nullptr);
    dropped->Begin(); // never ended or submitted
    device->EndFrame();
    for (u32 f = 1; f <= Dx12Device::kFrameSlots; ++f) {
        device->BeginFrame();
        CommandList* list = device->CreateCommandList(); // the last frame reuses the dropped list's slot
        REQUIRE(list != nullptr);
        list->Begin();
        list->End();
        CommandList* lists[] = {list};
        CHECK(device->Submit(lists, 1) != 0);
        device->EndFrame();
    }
    device->WaitIdle();
}
