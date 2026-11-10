#include "containers/vector.h"

#include <catch2/catch_test_macros.hpp>

#include "gpu_fixture.h"
#include "test_text.h"
#include "rhi/null/null_device.h"
#include "rhi/rhi.h"
#include "rhi/vulkan/vulkan_device.h"
#include "cstr/mem_ops.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::rhi::vulkan;

namespace {

VulkanDevicePtr VulkanTextureTestMakeDevice() {
    DeviceCreateInfo info;
    info.validation = ValidationSetting::On;
    info.sync_validation = true;
    VulkanDevicePtr device = VulkanDevice::Create(info);
    if (device == nullptr) {
        if (gpu_test::GpuRequired()) {
            FAIL("ENGINE_RHI_REQUIRE_GPU=1 but no Vulkan device could be created");
        }
        SKIP("no suitable Vulkan device");
    }
    return device;
}

constexpr TextureUsage kUploadable = TextureUsage::Sampled | TextureUsage::CopyDst | TextureUsage::CopySrc;

TextureHandle MakeTexture(Device* device, u32 w, u32 h, Format format, u32 mips = 1, u32 layers = 1, u32 depth = 1) {
    TextureDesc desc;
    desc.dimension = depth > 1 ? TextureDimension::Tex3D : TextureDimension::Tex2D;
    desc.width = w;
    desc.height = h;
    desc.depth = depth;
    desc.mip_levels = mips;
    desc.array_layers = layers;
    desc.format = format;
    desc.usage = kUploadable;
    return device->CreateTexture(desc);
}

Vector<byte> VulkanTextureTestPattern(usize size, unsigned seed) {
    Vector<byte> bytes = engine_test::ByteBuffer(size);
    unsigned state = seed * 2654435761u + 12345u;
    for (usize i = 0; i < size; ++i) {
        state = state * 1664525u + 1013904223u;
        bytes[i] = static_cast<byte>(state >> 24);
    }
    return bytes;
}

// Copies `rows` rows of `row_bytes` into a buffer with `pitch` bytes per row (padding filled with 0xEE).
Vector<byte> Padded(const Vector<byte>& tight, usize rows, usize row_bytes, usize pitch) {
    Vector<byte> out = engine_test::ByteBuffer(rows * pitch, 0xEE);
    for (usize r = 0; r < rows; ++r) {
        MemCopy(&out[r * pitch], &tight[r * row_bytes], row_bytes);
    }
    return out;
}

} // namespace

TEST_CASE("Vulkan FormatSupport and CanGenerateMips answer from the device's format table", "[rhi][vulkan][gpu][texture]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    VulkanDevicePtr device = VulkanTextureTestMakeDevice();
    CHECK(device->FormatSupport(Format::RGBA8_UNorm, TextureUsage::Sampled | TextureUsage::CopyDst | TextureUsage::CopySrc));
    CHECK(device->FormatSupport(Format::RGBA8_UNorm, TextureUsage::None));
    CHECK_FALSE(device->FormatSupport(Format::Undefined, TextureUsage::Sampled));
    CHECK((device->FormatSupport(Format::D32_Float, TextureUsage::DepthAttachment) || device->FormatSupport(Format::D24_UNorm_S8_UInt, TextureUsage::DepthAttachment)));
    CHECK_FALSE(device->FormatSupport(Format::RGBA8_UNorm, TextureUsage::DepthAttachment));
    CHECK(device->CanGenerateMips(Format::RGBA8_UNorm));
    CHECK_FALSE(device->CanGenerateMips(Format::D32_Float));
    CHECK_FALSE(device->CanGenerateMips(Format::BC1_UNorm));
    CHECK_FALSE(device->CanGenerateMips(Format::Undefined));
    device.Reset();
}

TEST_CASE("Vulkan texture upload and readback are bit-exact", "[rhi][vulkan][gpu][texture]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    VulkanDevicePtr device = VulkanTextureTestMakeDevice();

    SECTION("64x64 RGBA8, tightly packed") {
        TextureHandle tex = MakeTexture(device.Get(), 64, 64, Format::RGBA8_UNorm);
        REQUIRE(tex.IsValid());
        const Vector<byte> data = VulkanTextureTestPattern(64 * 64 * 4, 1);
        const UploadTicket ticket = device->UploadTexture(tex, 0, 0, data.Data(), 0);
        REQUIRE(ticket.ok);
        Vector<byte> back = engine_test::ByteBuffer(data.Size());
        REQUIRE(device->ReadbackTexture(tex, 0, 0, back.Data(), back.Size(), ResourceState::ShaderRead));
        CHECK(engine_test::SameBytes(back, data));
        device->DestroyTexture(tex);
    }
    SECTION("non-power-of-two 37x23 with a row pitch wider than the row") {
        TextureHandle tex = MakeTexture(device.Get(), 37, 23, Format::RGBA8_UNorm);
        REQUIRE(tex.IsValid());
        const Vector<byte> tight = VulkanTextureTestPattern(37 * 23 * 4, 2);
        const Vector<byte> padded = Padded(tight, 23, 37 * 4, 37 * 4 + 12);
        REQUIRE(device->UploadTexture(tex, 0, 0, padded.Data(), 37 * 4 + 12).ok);
        Vector<byte> back = engine_test::ByteBuffer(tight.Size());
        REQUIRE(device->ReadbackTexture(tex, 0, 0, back.Data(), back.Size(), ResourceState::ShaderRead));
        CHECK(engine_test::SameBytes(back, tight));
        device->DestroyTexture(tex);
    }
    SECTION("several mip levels and array layers uploaded one by one") {
        TextureHandle tex = MakeTexture(device.Get(), 16, 16, Format::RGBA8_UNorm, 3, 2);
        REQUIRE(tex.IsValid());
        Vector<byte> expected[3][2];
        for (u32 mip = 0; mip < 3; ++mip) {
            for (u32 layer = 0; layer < 2; ++layer) {
                const u32 side = 16 >> mip;
                expected[mip][layer] = VulkanTextureTestPattern(side * side * 4, 10 + mip * 2 + layer);
                REQUIRE(device->UploadTexture(tex, mip, layer, expected[mip][layer].Data(), 0).ok);
            }
        }
        for (u32 mip = 0; mip < 3; ++mip) {
            for (u32 layer = 0; layer < 2; ++layer) {
                Vector<byte> back = engine_test::ByteBuffer(expected[mip][layer].Size());
                REQUIRE(device->ReadbackTexture(tex, mip, layer, back.Data(), back.Size(), ResourceState::ShaderRead));
                CHECK(engine_test::SameBytes(back, expected[mip][layer]));
            }
        }
        device->DestroyTexture(tex);
    }
    SECTION("a level larger than one staging chunk (2048x1024 RGBA8, 8 MiB)") {
        TextureHandle tex = MakeTexture(device.Get(), 2048, 1024, Format::RGBA8_UNorm);
        REQUIRE(tex.IsValid());
        const Vector<byte> data = VulkanTextureTestPattern(2048u * 1024u * 4u, 3);
        REQUIRE(device->UploadTexture(tex, 0, 0, data.Data(), 0).ok);
        Vector<byte> back = engine_test::ByteBuffer(data.Size());
        REQUIRE(device->ReadbackTexture(tex, 0, 0, back.Data(), back.Size(), ResourceState::ShaderRead));
        CHECK(engine_test::SameBytes(back, data));
        device->DestroyTexture(tex);
    }
    SECTION("a 3D texture") {
        TextureHandle tex = MakeTexture(device.Get(), 8, 8, Format::R8_UNorm, 1, 1, 4);
        REQUIRE(tex.IsValid());
        const Vector<byte> data = VulkanTextureTestPattern(8 * 8 * 4, 4);
        REQUIRE(device->UploadTexture(tex, 0, 0, data.Data(), 0).ok);
        Vector<byte> back = engine_test::ByteBuffer(data.Size());
        REQUIRE(device->ReadbackTexture(tex, 0, 0, back.Data(), back.Size(), ResourceState::ShaderRead));
        CHECK(engine_test::SameBytes(back, data));
        device->DestroyTexture(tex);
    }
    SECTION("block-compressed BC1 when the device supports it") {
        if (!device->FormatSupport(Format::BC1_UNorm, kUploadable)) {
            SKIP("BC1 is not supported for copies on this device");
        }
        TextureHandle tex = MakeTexture(device.Get(), 16, 16, Format::BC1_UNorm);
        REQUIRE(tex.IsValid());
        const Vector<byte> data = VulkanTextureTestPattern(4 * 4 * 8, 5); // 4x4 blocks of 8 bytes
        REQUIRE(device->UploadTexture(tex, 0, 0, data.Data(), 0).ok);
        Vector<byte> back = engine_test::ByteBuffer(data.Size());
        REQUIRE(device->ReadbackTexture(tex, 0, 0, back.Data(), back.Size(), ResourceState::ShaderRead));
        CHECK(engine_test::SameBytes(back, data));
        device->DestroyTexture(tex);
    }
    device.Reset();
}

TEST_CASE("Vulkan GenerateMips averages a checkerboard", "[rhi][vulkan][gpu][texture]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    VulkanDevicePtr device = VulkanTextureTestMakeDevice();

    constexpr u32 kSide = 64;
    constexpr u32 kLevels = 7;
    TextureHandle tex = MakeTexture(device.Get(), kSide, kSide, Format::RGBA8_UNorm, kLevels);
    REQUIRE(tex.IsValid());
    Vector<byte> board = engine_test::ByteBuffer(kSide * kSide * 4);
    for (u32 y = 0; y < kSide; ++y) {
        for (u32 x = 0; x < kSide; ++x) {
            const byte v = ((x + y) & 1) != 0 ? 255 : 0;
            byte* px = &board[(y * kSide + x) * 4];
            px[0] = px[1] = px[2] = v;
            px[3] = 255;
        }
    }
    REQUIRE(device->UploadTexture(tex, 0, 0, board.Data(), 0).ok);

    device->BeginFrame();
    CommandList* list = device->CreateCommandList();
    REQUIRE(list != nullptr);
    list->Begin();
    list->GenerateMips(tex);
    list->End();
    CommandList* lists[] = {list};
    const FenceValue fence = device->Submit(lists, 1);
    REQUIRE(fence != 0);
    device->EndFrame();
    REQUIRE(device->WaitFence(fence, 10ull * 1000 * 1000 * 1000));

    for (u32 mip = 1; mip < kLevels; ++mip) {
        const u32 side = kSide >> mip;
        Vector<byte> level = engine_test::ByteBuffer(side * side * 4);
        REQUIRE(device->ReadbackTexture(tex, mip, 0, level.Data(), level.Size(), ResourceState::ShaderRead));
        for (u32 i = 0; i < side * side; ++i) {
            for (u32 c = 0; c < 3; ++c) {
                const int v = level[i * 4 + c];
                // Every 2x2 block of the checkerboard averages to 127.5; later levels average averages.
                CHECK(v >= 126);
                CHECK(v <= 129);
            }
            CHECK(level[i * 4 + 3] == 255);
        }
    }
    // Level 0 is untouched.
    Vector<byte> base = engine_test::ByteBuffer(board.Size());
    REQUIRE(device->ReadbackTexture(tex, 0, 0, base.Data(), base.Size(), ResourceState::ShaderRead));
    CHECK(engine_test::SameBytes(base, board));
    device->DestroyTexture(tex);
    device.Reset();
}

TEST_CASE("Vulkan copies between buffers and textures in a command list", "[rhi][vulkan][gpu][texture]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    VulkanDevicePtr device = VulkanTextureTestMakeDevice();

    // 64 texels of RGBA8 make a 256-byte row, so the tightly packed default already meets the portable pitch alignment.
    constexpr u32 kSide = 64;
    BufferDesc bd;
    bd.size = kSide * kSide * 4;
    bd.usage = BufferUsage::CopySrc | BufferUsage::CopyDst;
    BufferHandle in = device->CreateBuffer(bd);
    BufferHandle out = device->CreateBuffer(bd);
    const Vector<byte> data = VulkanTextureTestPattern(bd.size, 6);
    REQUIRE(device->UploadBuffer(in, 0, data.Data(), data.Size()).ok);
    TextureHandle tex = MakeTexture(device.Get(), kSide, kSide, Format::RGBA8_UNorm);

    device->BeginFrame();
    CommandList* list = device->CreateCommandList();
    list->Begin();
    list->Barrier(in, ResourceState::CopyDst, ResourceState::CopySrc);
    list->Barrier(tex, ResourceState::Undefined, ResourceState::CopyDst);
    list->CopyBufferToTexture(in, tex, BufferTextureCopy{});
    list->Barrier(tex, ResourceState::CopyDst, ResourceState::CopySrc);
    list->Barrier(out, ResourceState::Undefined, ResourceState::CopyDst);
    list->CopyTextureToBuffer(tex, out, BufferTextureCopy{});
    list->End();
    CommandList* lists[] = {list};
    REQUIRE(device->Submit(lists, 1) != 0);
    device->EndFrame();

    Vector<byte> back = engine_test::ByteBuffer(bd.size);
    REQUIRE(device->ReadbackBuffer(out, 0, back.Data(), back.Size()));
    CHECK(engine_test::SameBytes(back, data));
    device->DestroyTexture(tex);
    device->DestroyBuffer(in);
    device->DestroyBuffer(out);
    device.Reset();
}

TEST_CASE("Vulkan samplers are shared between equal descriptors and reference counted", "[rhi][vulkan][gpu][sampler]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    VulkanDevicePtr device = VulkanTextureTestMakeDevice();

    SamplerDesc a;
    SamplerDesc b = a;
    b.debug_name = "named"; // the name does not take part in the comparison
    SamplerDesc c = a;
    c.address_u = AddressMode::ClampToEdge;
    SamplerDesc aniso = a;
    aniso.max_anisotropy = 16.0f;

    SamplerHandle h1 = device->CreateSampler(a);
    SamplerHandle h2 = device->CreateSampler(b);
    SamplerHandle h3 = device->CreateSampler(c);
    SamplerHandle h4 = device->CreateSampler(aniso);
    REQUIRE(h1.IsValid());
    REQUIRE(h3.IsValid());
    REQUIRE(h4.IsValid());
    CHECK(h1 == h2);
    CHECK_FALSE(h1 == h3);
    CHECK(device->Counts().samplers == 3);

    device->DestroySampler(h2);
    CHECK(device->Counts().pending_image_destruction == 0); // h1 still holds a reference
    device->DestroySampler(h1);
    CHECK(device->Counts().pending_image_destruction == 1);
    device->DestroySampler(h3);
    device->DestroySampler(h4);
    device->WaitIdle();
    CHECK(device->Counts().samplers == 0);
    CHECK(device->Counts().pending_image_destruction == 0);

    SamplerDesc bad = a;
    bad.max_anisotropy = 0.5f;
    fx.ExpectErrors(0);
    CHECK_FALSE(device->CreateSampler(bad).IsValid());
    device.Reset();
}

TEST_CASE("Vulkan textures are released after their frame completes, and counted", "[rhi][vulkan][gpu][texture][frames]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    VulkanDevicePtr device = VulkanTextureTestMakeDevice();
    CHECK(device->Counts().raw_images == 0);

    device->BeginFrame();
    TextureHandle tex = MakeTexture(device.Get(), 16, 16, Format::RGBA8_UNorm);
    CHECK(device->Counts().textures == 1);
    CHECK(device->Counts().raw_images == 1);
    device->DestroyTexture(tex);
    CHECK(device->Counts().textures == 0);
    CHECK(device->Counts().pending_image_destruction == 1);
    device->EndFrame();
    device->WaitIdle();
    CHECK(device->Counts().pending_image_destruction == 0);
    CHECK(device->Counts().raw_images == 0);

    // A texture the application forgot is released with the device (with a warning), leaving nothing for validation to report.
    TextureHandle forgotten = MakeTexture(device.Get(), 8, 8, Format::RGBA8_UNorm);
    (void)forgotten;
    device.Reset();
}

TEST_CASE("Vulkan CreateTexture rejects what the device cannot do", "[rhi][vulkan][gpu][texture]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    VulkanDevicePtr device = VulkanTextureTestMakeDevice();

    TextureDesc cube;
    cube.dimension = TextureDimension::Cube;
    cube.width = cube.height = 4;
    cube.format = Format::RGBA8_UNorm;
    cube.usage = TextureUsage::Sampled;
    CHECK_FALSE(device->CreateTexture(cube).IsValid());

    TextureDesc depth_color;
    depth_color.width = depth_color.height = 4;
    depth_color.format = Format::RGBA8_UNorm;
    depth_color.usage = TextureUsage::DepthAttachment;
    CHECK_FALSE(device->CreateTexture(depth_color).IsValid());

    TextureDesc huge;
    huge.width = huge.height = device->Caps().max_texture_dimension_2d + 1;
    huge.format = Format::RGBA8_UNorm;
    huge.usage = TextureUsage::Sampled;
    CHECK_FALSE(device->CreateTexture(huge).IsValid());
    device.Reset();
}

TEST_CASE("Null device: texture upload follows the state rules", "[rhi][null][texture]") {
    NullDevicePtr device = CreateNullDevice();
    TextureHandle tex = MakeTexture(device.Get(), 8, 8, Format::RGBA8_UNorm, 3);
    const Vector<byte> data = engine_test::ByteBuffer(8 * 8 * 4, 7);

    CHECK(device->UploadTexture(tex, 0, 0, data.Data(), 0).ok);
    CHECK(device->StateOf(tex) == ResourceState::ShaderRead);
    CHECK(device->UploadTexture(tex, 1, 0, data.Data(), 0).ok); // another level: still ShaderRead
    CHECK(device->ErrorCount() == 0);

    Vector<byte> back = engine_test::ByteBuffer(8 * 8 * 4, 1);
    CHECK(device->ReadbackTexture(tex, 0, 0, back.Data(), back.Size(), ResourceState::ShaderRead));
    CHECK(device->ErrorCount() == 0);

    CHECK_FALSE(device->UploadTexture(tex, 5, 0, data.Data(), 0).ok); // mip out of range
    CHECK(device->ErrorCount() == 1);
    CHECK_FALSE(device->UploadTexture(tex, 0, 0, data.Data(), 8).ok); // pitch below a row
    CHECK(device->ErrorCount() == 2);
    CHECK_FALSE(device->ReadbackTexture(tex, 0, 0, back.Data(), 4, ResourceState::ShaderRead));  // too small
    CHECK(device->ErrorCount() == 3);

    device->BeginFrame();
    CommandList* list = device->CreateCommandList();
    list->Begin();
    list->Barrier(tex, ResourceState::ShaderRead, ResourceState::CopySrc);
    list->End();
    CommandList* lists[] = {list};
    device->Submit(lists, 1);
    device->EndFrame();
    CHECK(device->ErrorCount() == 3);
    CHECK_FALSE(device->UploadTexture(tex, 0, 0, data.Data(), 0).ok); // now in CopySrc
    CHECK(device->ErrorCount() == 4);
    CHECK(device->ReadbackTexture(tex, 0, 0, back.Data(), back.Size(), ResourceState::CopySrc)); // the caller names the state
    CHECK_FALSE(device->ReadbackTexture(tex, 0, 0, back.Data(), back.Size(), ResourceState::ShaderRead)); // wrong state
    CHECK(device->ErrorCount() == 5);

    CHECK(device->FormatSupport(Format::RGBA8_UNorm, TextureUsage::Sampled));
    CHECK_FALSE(device->FormatSupport(Format::D32_Float, TextureUsage::ColorAttachment));
    CHECK(device->CanGenerateMips(Format::RGBA8_UNorm));
    CHECK_FALSE(device->CanGenerateMips(Format::BC7_UNorm));
    device.Reset();
}

TEST_CASE("Null device: GenerateMips needs usage, levels and the ShaderRead state", "[rhi][null][texture]") {
    NullDevicePtr device = CreateNullDevice();
    TextureHandle tex = MakeTexture(device.Get(), 8, 8, Format::RGBA8_UNorm, 4);
    TextureHandle single = MakeTexture(device.Get(), 8, 8, Format::RGBA8_UNorm, 1);
    const Vector<byte> data = engine_test::ByteBuffer(8 * 8 * 4, 7);
    device->UploadTexture(tex, 0, 0, data.Data(), 0);
    device->UploadTexture(single, 0, 0, data.Data(), 0);

    device->BeginFrame();
    CommandList* list = device->CreateCommandList();
    list->Begin();
    list->GenerateMips(tex);
    CHECK(device->ErrorCount() == 0);
    list->GenerateMips(single);
    CHECK(device->ErrorCount() == 1);
    list->End();
    CommandList* lists[] = {list};
    device->Submit(lists, 1);
    CHECK(device->ErrorCount() == 1);
    CHECK(device->StateOf(tex) == ResourceState::ShaderRead);

    CommandList* second = device->CreateCommandList();
    second->Begin();
    second->Barrier(tex, ResourceState::ShaderRead, ResourceState::CopySrc);
    second->GenerateMips(tex); // no longer in ShaderRead when this runs
    second->End();
    CommandList* more[] = {second};
    device->Submit(more, 1);
    CHECK(device->ErrorCount() == 2);
    device->EndFrame();
    device.Reset();
}
