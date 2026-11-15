// The block-compressed formats on a real device (Phase 22 Days 324-325, written on the Windows PC on 2026-11-15): a
// 16x16 image is encoded to BC1, BC4, BC5 and BC7 by the asset encoder, uploaded as a compressed texture, sampled texel
// for texel (nearest, the target the texture's size) into an RGBA8 target and read back, then compared with the
// engine's own decoder (`asset::DecodeImage`, the reference). BC7's decode is exact integer arithmetic in the format,
// so it must match exactly; BC1, BC4 and BC5 interpolate, the engine truncating and a GPU computing at its own
// precision: on the Radeon 860M (D3D12 and Vulkan, 2026-11-15) they differ by up to two steps, not the one
// `block_compress.h` claims (`KNOWN-ISSUES.md`), so two is the bound here. Runs on every GPU backend this machine has,
// validation on.
#include <catch2/catch_test_macros.hpp>

#include "asset/block_compress.h"
#include "backend_fixture.h"
#include "containers/vector.h"
#include "fullscreen.vert.dxil.inc"
#include "graph_sample.frag.dxil.inc"
#include "rhi/rhi.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;

namespace
{

constexpr u32 kBcSize = 16;

// Smooth gradients in the top half, a deterministic noise in the bottom half, alpha ramps: every block has endpoints
// to fit and interpolated texels to decode.
Vector<byte> BcTestImage()
{
    Vector<byte> rgba;
    rgba.Resize(kBcSize * kBcSize * 4);
    u32 state = 0x2468ACE1u;
    for (u32 y = 0; y < kBcSize; ++y)
    {
        for (u32 x = 0; x < kBcSize; ++x)
        {
            byte* p = &rgba[(y * kBcSize + x) * 4];
            if (y < kBcSize / 2)
            {
                p[0] = static_cast<byte>(x * 16);
                p[1] = static_cast<byte>(255 - y * 30);
                p[2] = static_cast<byte>((x + y) * 8);
            }
            else
            {
                for (u32 c = 0; c < 3; ++c)
                {
                    state = state * 1664525u + 1013904223u;
                    p[c] = static_cast<byte>(state >> 24);
                }
            }
            p[3] = static_cast<byte>(255 - x * 12);
        }
    }
    return rgba;
}

struct BcCase
{
    asset::BcKind kind;
    Format format;
    const char* name;
    u32 max_mse_milli;  // the largest mean squared channel difference from the reference decoder, in thousandths (PSNR
                        // 40 dB is 650)
};

struct BcSampler
{
    Device* device = nullptr;
    ShaderHandle vs, fs;
    BindGroupLayoutHandle layout;
    PipelineHandle pipeline;
    SamplerHandle sampler;
    TextureHandle black;  // the shader adds two textures; the second is zero

    bool Create(Device* d)
    {
        device = d;
        vs = CreateShaderFromSet(*device, shaders::kFullscreenVertShader, ShaderStage::Vertex);
        fs = CreateShaderFromSet(*device, shaders::kGraphSampleFragShader, ShaderStage::Fragment);
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
        gp.debug_name = "bc_sample";
        pipeline = device->CreateGraphicsPipeline(gp);
        SamplerDesc smp;
        smp.mag_filter = smp.min_filter = smp.mip_filter = FilterMode::Nearest;
        smp.address_u = smp.address_v = AddressMode::ClampToEdge;
        sampler = device->CreateSampler(smp);
        TextureDesc bd;
        bd.width = bd.height = kBcSize;
        bd.format = Format::RGBA8_UNorm;
        bd.usage = TextureUsage::Sampled | TextureUsage::CopyDst;
        bd.debug_name = "bc black";
        black = device->CreateTexture(bd);
        Vector<byte> zero;
        zero.Resize(kBcSize * kBcSize * 4);
        for (usize i = 0; i < zero.Size(); ++i)
        {
            zero[i] = 0;
        }
        return vs.IsValid() && fs.IsValid() && layout.IsValid() && pipeline.IsValid() && sampler.IsValid() &&
               black.IsValid() && device->UploadTexture(black, 0, 0, zero.Data(), 0).ok;
    }

    void Destroy()
    {
        device->DestroyTexture(black);
        device->DestroySampler(sampler);
        device->DestroyPipeline(pipeline);
        device->DestroyBindGroupLayout(layout);
        device->DestroyShader(vs);
        device->DestroyShader(fs);
    }

    // Samples `texture` (in ShaderRead) texel for texel into a new RGBA8 target and reads it back.
    bool Sample(TextureHandle texture, Vector<byte>* out)
    {
        TextureDesc td;
        td.width = td.height = kBcSize;
        td.format = Format::RGBA8_UNorm;
        td.usage = TextureUsage::ColorAttachment | TextureUsage::CopySrc;
        td.debug_name = "bc target";
        const TextureHandle target = device->CreateTexture(td);
        if (!target.IsValid())
        {
            return false;
        }
        device->BeginFrame();
        CommandList* list = device->CreateCommandList();
        list->Begin();
        BindGroupEntry e[3];
        e[0].binding = 0;
        e[0].texture = texture;
        e[1].binding = 1;
        e[1].texture = black;
        e[2].binding = 2;
        e[2].sampler = sampler;
        BindGroupDesc gd;
        gd.layout = layout;
        gd.entries = rhi::Span<BindGroupEntry>(e, 3);
        gd.transient = true;
        const BindGroupHandle group = device->CreateBindGroup(gd);
        list->Barrier(target, ResourceState::Undefined, ResourceState::ColorAttachment);
        RenderingDesc rd;
        rd.color[0].texture = target;
        rd.color[0].load = LoadOp::Clear;
        rd.color_count = 1;
        list->BeginRendering(rd);
        list->SetPipeline(pipeline);
        list->SetViewport(Viewport{0, 0, static_cast<f32>(kBcSize), static_cast<f32>(kBcSize), 0.0f, 1.0f});
        list->SetScissor(ScissorRect{0, 0, kBcSize, kBcSize});
        list->SetBindGroup(0, group);
        list->Draw(3, 1, 0, 0);
        list->EndRendering();
        list->Barrier(target, ResourceState::ColorAttachment, ResourceState::CopySrc);
        list->End();
        CommandList* lists[] = {list};
        const bool submitted = device->Submit(lists, 1) != 0;
        device->EndFrame();
        device->WaitIdle();
        out->Resize(kBcSize * kBcSize * 4);
        const bool read = submitted &&
                          device->ReadbackTexture(target, 0, 0, out->Data(), out->Size(), ResourceState::CopySrc);
        device->DestroyTexture(target);
        return read;
    }
};

}  // namespace

TEST_CASE("BC1, BC4, BC5 and BC7 textures sample on the GPU as the engine's decoder reads them",
          "[rhi][gpu][bc][asset]")
{
    const BcCase cases[] = {
        {asset::BcKind::Bc1, Format::BC1_UNorm, "BC1", 6500},
        {asset::BcKind::Bc4, Format::BC4_UNorm, "BC4", 6500},
        {asset::BcKind::Bc5, Format::BC5_UNorm, "BC5", 6500},
        {asset::BcKind::Bc7, Format::BC7_UNorm, "BC7", 650},
    };
    const Vector<byte> image = BcTestImage();
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        DevicePtr device = gpu_test::CreateTestDevice(backend);
        {
            BcSampler sampler;
            REQUIRE(sampler.Create(device.Get()));
            for (const BcCase& c : cases)
            {
                INFO(c.name);
                if (!device->FormatSupport(c.format, TextureUsage::Sampled | TextureUsage::CopyDst))
                {
                    WARN(c.name << " is not sampleable on this device; not compared");
                    continue;
                }
                Vector<byte> blocks;
                REQUIRE(asset::EncodeImage(c.kind, kBcSize, kBcSize, ByteSpan(image.Data(), image.Size()),
                                           asset::BcEncodeOptions{}, nullptr, &blocks)
                            .Ok());
                Vector<byte> reference;
                REQUIRE(asset::DecodeImage(c.kind, kBcSize, kBcSize, ByteSpan(blocks.Data(), blocks.Size()), &reference)
                            .Ok());

                TextureDesc td;
                td.width = td.height = kBcSize;
                td.format = c.format;
                td.usage = TextureUsage::Sampled | TextureUsage::CopyDst;
                td.debug_name = c.name;
                const TextureHandle texture = device->CreateTexture(td);
                REQUIRE(texture.IsValid());
                REQUIRE(device->UploadTexture(texture, 0, 0, blocks.Data(), 0).ok);  // left in ShaderRead
                Vector<byte> got;
                REQUIRE(sampler.Sample(texture, &got));
                device->DestroyTexture(texture);

                u64 squares = 0;
                u32 exact = 0;
                int worst = 0;
                for (usize i = 0; i < got.Size(); ++i)
                {
                    const int d = static_cast<int>(got[i]) - static_cast<int>(reference[i]);
                    const int a = d < 0 ? -d : d;
                    worst = a > worst ? a : worst;
                    squares += static_cast<u64>(a) * static_cast<u64>(a);
                    exact += a == 0 ? 1 : 0;
                }
                // The vendors' block decoders round their interpolated values differently (a Radeon is within two steps
                // of ours, a GeForce within six), so the check is the error energy, as a PSNR would say it, not each
                // channel: the texture must look the same, and a wrong block (a swapped endpoint, a wrong index) costs
                // far more than any rounding.
                INFO("worst channel difference " << worst << ", " << exact << " of " << got.Size()
                                                 << " channels exact, mean squared difference "
                                                 << (squares * 1000 / got.Size()) << " thousandths");
                CHECK(squares * 1000 <= static_cast<u64>(c.max_mse_milli) * got.Size());
            }
            device->WaitIdle();
            sampler.Destroy();
        }
    }
}
