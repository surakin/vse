// The Direct3D 12 backend's resources (Phase 13 Day 165): the format table, buffers on every heap, texture uploads and readbacks through
// 256-byte row pitches at odd sizes, block-compressed levels, typeless depth with typed views, and the resource accounting. Windows only.
#include <cstdio>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "os/os.h"
#include "rhi/dx12/dx12_debug.h"
#include "rhi/dx12/dx12_device.h"
#include "rhi/dx12/dx12_format.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::rhi::dx12;

namespace {

// Fails the test on any debug-layer warning or error while it lives.
struct Silence {
    Silence() { ResetDebugMessageCounts(); }
    ~Silence() {
        const DebugMessageCounts c = GetDebugMessageCounts();
        CHECK(c.errors == 0);
        CHECK(c.warnings == 0);
    }
};

Dx12DevicePtr MakeDevice(ValidationSetting validation = ValidationSetting::On) {
    DeviceCreateInfo info;
    info.backend = Backend::Dx12;
    info.application_name = "dx12-resource-test";
    info.validation = validation;
    return Dx12Device::Create(info);
}

// Deterministic bytes for a test pattern.
void Fill(Vector<u8>& bytes, usize size, u32 seed) {
    bytes.Resize(size);
    u32 x = seed * 2654435761u + 1;
    for (usize i = 0; i < size; ++i) {
        x = x * 1664525u + 1013904223u;
        bytes[i] = static_cast<u8>(x >> 24);
    }
}

bool Same(const Vector<u8>& a, const Vector<u8>& b) { return a.Size() == b.Size() && std::memcmp(a.Data(), b.Data(), a.Size()) == 0; }

u32 Mip(u32 base, u32 mip) {
    const u32 e = base >> mip;
    return e == 0 ? 1 : e;
}

// Uploads a pattern to every mip and layer of a texture and reads each back.
void RoundTrip(Dx12Device& device, Format format, u32 width, u32 height, u32 mips, u32 layers, u32 depth = 1) {
    INFO(FormatInfo(format).name << " " << width << "x" << height << " mips " << mips << " layers " << layers << " depth " << depth);
    TextureDesc td;
    td.dimension = depth > 1 ? TextureDimension::Tex3D : TextureDimension::Tex2D;
    td.width = width;
    td.height = height;
    td.depth = depth;
    td.mip_levels = mips;
    td.array_layers = layers;
    td.format = format;
    td.usage = TextureUsage::Sampled | TextureUsage::CopyDst | TextureUsage::CopySrc;
    if (FormatInfo(format).is_depth) td.usage = td.usage | TextureUsage::DepthAttachment;
    td.debug_name = "round trip";
    const TextureHandle tex = device.CreateTexture(td);
    REQUIRE(tex.IsValid());
    for (u32 layer = 0; layer < layers; ++layer) {
        for (u32 mip = 0; mip < mips; ++mip) {
            const FormatProperties& fp = FormatInfo(format);
            const u32 w = Mip(width, mip), h = Mip(height, mip), d = depth > 1 ? Mip(depth, mip) : 1;
            const usize row = static_cast<usize>(FormatRowBytes(format, w));
            const usize rows = (h + fp.block_height - 1) / fp.block_height;
            Vector<u8> sent;
            Fill(sent, row * rows * d, mip * 31 + layer * 7 + 1);
            if (format == Format::D32_Float) {
                // Depth must be a float the copy keeps exactly: 0..1.
                for (usize i = 0; i + 4 <= sent.Size(); i += 4) {
                    const f32 v = static_cast<f32>(sent[i]) / 255.0f;
                    std::memcpy(&sent[i], &v, 4);
                }
            }
            const UploadTicket ticket = device.UploadTexture(tex, mip, layer, sent.Data(), 0);
            REQUIRE(ticket.ok);
            Vector<u8> got;
            got.Resize(sent.Size());
            REQUIRE(device.ReadbackTexture(tex, mip, layer, got.Data(), got.Size(), ResourceState::ShaderRead));
            CHECK(Same(sent, got));
        }
    }
    device.DestroyTexture(tex);
}

} // namespace

TEST_CASE("Every RHI format has a DXGI format, sRGB pairs map to sRGB, and depth has typeless and typed forms", "[rhi][dx12]") {
    for (usize i = 1; i < static_cast<usize>(Format::Count); ++i) {
        const Format f = static_cast<Format>(i);
        INFO(FormatInfo(f).name);
        CHECK(ToDxgi(f) != DXGI_FORMAT_UNKNOWN);
    }
    CHECK(ToDxgi(Format::RGBA8_SRGB) == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    CHECK(ToDxgi(Format::BGRA8_SRGB) == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
    CHECK(ToDxgi(Format::BC7_SRGB) == DXGI_FORMAT_BC7_UNORM_SRGB);
    CHECK(ToDxgiResource(Format::D32_Float, TextureUsage::DepthAttachment) == DXGI_FORMAT_D32_FLOAT);
    CHECK(ToDxgiResource(Format::D32_Float, TextureUsage::DepthAttachment | TextureUsage::Sampled) == DXGI_FORMAT_R32_TYPELESS);
    CHECK(ToDxgiResource(Format::D24_UNorm_S8_UInt, TextureUsage::Sampled) == DXGI_FORMAT_R24G8_TYPELESS);
    CHECK(ToDxgiShaderView(Format::D32_Float) == DXGI_FORMAT_R32_FLOAT);
    CHECK(ToDxgiShaderView(Format::D24_UNorm_S8_UInt) == DXGI_FORMAT_R24_UNORM_X8_TYPELESS);
    CHECK(ToDxgiDepthView(Format::D32_Float) == DXGI_FORMAT_D32_FLOAT);
    CHECK(ToDxgiDepthView(Format::RGBA8_UNorm) == DXGI_FORMAT_UNKNOWN);
    CHECK(ToDxgiVertex(VertexFormat::UInt1) == DXGI_FORMAT_R32_UINT);
    CHECK(ToDxgiVertex(VertexFormat::UNorm8x4) == DXGI_FORMAT_R8G8B8A8_UNORM);
    CHECK(ToD3D12State(ResourceState::Present) == D3D12_RESOURCE_STATE_PRESENT);
    CHECK(ToD3D12State(ResourceState::ShaderWrite) == D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

TEST_CASE("DX12 buffers on every heap round-trip their data, including uploads larger than a staging chunk", "[rhi][dx12][gpu]") {
    Dx12DevicePtr device = MakeDevice();
    REQUIRE(device);
    Silence silence;
    {
        // GPU-only: staged up, read back through a temporary readback buffer. 40 MiB crosses two 16 MiB chunks.
        BufferDesc bd;
        bd.size = 40ull << 20;
        bd.usage = BufferUsage::Storage | BufferUsage::CopyDst | BufferUsage::CopySrc;
        bd.debug_name = "big gpu-only";
        const BufferHandle big = device->CreateBuffer(bd);
        REQUIRE(big.IsValid());
        Vector<u8> sent;
        Fill(sent, static_cast<usize>(bd.size), 3);
        const UploadTicket t = device->UploadBuffer(big, 0, sent.Data(), bd.size);
        REQUIRE(t.ok);
        CHECK(t.fence != 0);
        CHECK(device->WaitFence(t.fence, ~0ull));
        Vector<u8> got;
        got.Resize(sent.Size());
        REQUIRE(device->ReadbackBuffer(big, 0, got.Data(), bd.size));
        CHECK(Same(sent, got));
        // A part at an odd offset.
        const u8 patch[5] = {1, 2, 3, 4, 5};
        REQUIRE(device->UploadBuffer(big, 12345, patch, 5).ok);
        u8 back[5] = {};
        REQUIRE(device->ReadbackBuffer(big, 12345, back, 5));
        CHECK(std::memcmp(patch, back, 5) == 0);
        device->DestroyBuffer(big);
    }
    {
        // CPU-to-GPU: written through the persistent mapping, at once.
        BufferDesc bd;
        bd.size = 1000;
        bd.usage = BufferUsage::Uniform | BufferUsage::CopyDst | BufferUsage::CopySrc;
        bd.location = MemoryLocation::CpuToGpu;
        const BufferHandle up = device->CreateBuffer(bd);
        REQUIRE(up.IsValid());
        u8* mapped = static_cast<u8*>(device->MapBuffer(up));
        REQUIRE(mapped != nullptr);
        for (u32 i = 0; i < 1000; ++i) mapped[i] = static_cast<u8>(i * 7);
        device->UnmapBuffer(up);
        const u8 more[3] = {9, 8, 7};
        CHECK(device->UploadBuffer(up, 997, more, 3).fence == 0); // nothing to wait for
        u8 got[1000] = {};
        REQUIRE(device->ReadbackBuffer(up, 0, got, 1000));
        CHECK(got[10] == 70);
        CHECK(got[999] == 7);
        // A uniform buffer is a multiple of 256 bytes, so every CBV over it fits.
        Dx12Device::BufferInfo info;
        REQUIRE(device->LookupBuffer(up, &info));
        CHECK(info.resource->GetDesc().Width == 1024);
        CHECK(info.desc.size == 1000);
        CHECK(info.gpu_address != 0);
        device->DestroyBuffer(up);
    }
    {
        // GPU-to-CPU: mappable, read directly.
        BufferDesc bd;
        bd.size = 64;
        bd.usage = BufferUsage::CopyDst;
        bd.location = MemoryLocation::GpuToCpu;
        const BufferHandle down = device->CreateBuffer(bd);
        REQUIRE(down.IsValid());
        CHECK(device->MapBuffer(down) != nullptr);
        device->UnmapBuffer(down);
        device->DestroyBuffer(down);
    }
    device->WaitIdle();
}

TEST_CASE("DX12 textures round-trip every format at odd sizes, with mips, layers and volumes", "[rhi][dx12][gpu]") {
    Dx12DevicePtr device = MakeDevice();
    REQUIRE(device);
    Silence silence;
    const Format formats[] = {Format::R8_UNorm,     Format::RG8_UNorm,   Format::RGBA8_UNorm, Format::RGBA8_SRGB,  Format::BGRA8_UNorm,
                              Format::BGRA8_SRGB,   Format::R16_Float,   Format::RG16_Float,  Format::RGBA16_Float, Format::R32_Float,
                              Format::RG32_Float,   Format::RGBA32_Float, Format::R32_UInt};
    for (Format f : formats) {
        if (!device->FormatSupport(f, TextureUsage::Sampled | TextureUsage::CopyDst | TextureUsage::CopySrc)) {
            WARN("not sampleable here: " << FormatInfo(f).name);
            continue;
        }
        RoundTrip(*device, f, 1, 1, 1, 1);
        RoundTrip(*device, f, 3, 7, 1, 1);
        RoundTrip(*device, f, 257, 5, 1, 1);
    }
    // An integer format counts as sampled (a shader loads it), as Vulkan's SAMPLED_IMAGE does; D3D12 reports it as loadable, not filterable.
    CHECK(device->FormatSupport(Format::R32_UInt, TextureUsage::Sampled));
    CHECK_FALSE(device->FormatSupport(Format::RGBA8_SRGB, TextureUsage::Storage)); // no unordered access to sRGB
    RoundTrip(*device, Format::RGBA8_UNorm, 64, 33, 7, 1);  // every mip down to 1x1
    RoundTrip(*device, Format::RGBA8_UNorm, 17, 9, 2, 3);   // an array
    RoundTrip(*device, Format::RGBA8_UNorm, 9, 5, 2, 1, 6); // a volume
    RoundTrip(*device, Format::D32_Float, 37, 11, 1, 1);    // depth, copied whole
    if (device->FormatSupport(Format::BC1_UNorm, TextureUsage::Sampled | TextureUsage::CopyDst | TextureUsage::CopySrc)) {
        RoundTrip(*device, Format::BC1_UNorm, 16, 8, 5, 1); // down to 1x1, which is one 4x4 block
        RoundTrip(*device, Format::BC7_UNorm, 12, 12, 4, 1);
    }
    device->WaitIdle();
}

TEST_CASE("A sampled DX12 depth texture is typeless with a typed depth view and a shader view", "[rhi][dx12][gpu]") {
    Dx12DevicePtr device = MakeDevice();
    REQUIRE(device);
    Silence silence;
    TextureDesc td;
    td.width = 64;
    td.height = 32;
    td.format = Format::D32_Float;
    td.usage = TextureUsage::DepthAttachment | TextureUsage::Sampled;
    const TextureHandle depth = device->CreateTexture(td);
    REQUIRE(depth.IsValid());
    Dx12Device::TextureInfo info;
    REQUIRE(device->LookupTexture(depth, &info));
    CHECK(info.resource_format == DXGI_FORMAT_R32_TYPELESS);
    CHECK(info.dsv.ptr != 0);
    CHECK(info.srv.ptr != 0);
    CHECK(info.rtv.ptr == 0);
    td.usage = TextureUsage::DepthAttachment; // not sampled: typed, no shader view
    const TextureHandle plain = device->CreateTexture(td);
    REQUIRE(plain.IsValid());
    REQUIRE(device->LookupTexture(plain, &info));
    CHECK(info.resource_format == DXGI_FORMAT_D32_FLOAT);
    CHECK(info.srv.ptr == 0);
    TextureDesc cd;
    cd.width = 8;
    cd.height = 8;
    cd.format = Format::RGBA8_UNorm;
    cd.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled | TextureUsage::Storage;
    const TextureHandle color = device->CreateTexture(cd);
    REQUIRE(color.IsValid());
    REQUIRE(device->LookupTexture(color, &info));
    CHECK(info.rtv.ptr != 0);
    CHECK(info.uav.ptr != 0);
    device->DestroyTexture(depth);
    device->DestroyTexture(plain);
    device->DestroyTexture(color);
    device->WaitIdle();
}

TEST_CASE("DX12 counts its buffers, textures and memory, and destroyed resources leave after their frame", "[rhi][dx12][gpu]") {
    Dx12DevicePtr device = MakeDevice();
    REQUIRE(device);
    Silence silence;
    device->Quiesce();
    const ResourceCounts before = device->Resources();
    CHECK(before.buffers == 0);
    CHECK(before.textures == 0);
    CHECK(before.internal_objects >= 1); // the staging ring
    BufferDesc bd;
    bd.size = 4096;
    bd.usage = BufferUsage::Vertex | BufferUsage::CopyDst;
    TextureDesc td;
    td.width = 128;
    td.height = 128;
    td.format = Format::RGBA8_UNorm;
    td.usage = TextureUsage::Sampled | TextureUsage::CopyDst;
    device->BeginFrame();
    const BufferHandle b = device->CreateBuffer(bd);
    const TextureHandle t = device->CreateTexture(td);
    REQUIRE(b.IsValid());
    REQUIRE(t.IsValid());
    const ResourceCounts during = device->Resources();
    CHECK(during.buffers == 1);
    CHECK(during.textures == 1);
    CHECK(during.internal_objects == before.internal_objects + 2);
    CHECK(during.device_memory_bytes >= before.device_memory_bytes + 4096 + 128 * 128 * 4);
    device->DestroyBuffer(b);
    device->DestroyTexture(t);
    CHECK(device->Resources().buffers == 0); // the handle is gone at once...
    device->EndFrame();
    device->Quiesce(); // ...and the memory once the frame has finished
    const ResourceCounts after = device->Resources();
    CHECK(after.internal_objects == before.internal_objects);
    CHECK(after.device_memory_bytes == before.device_memory_bytes);
}

TEST_CASE("Creating 1000 small buffers as committed resources: DX12 against Vulkan on this machine (measured, not asserted)", "[rhi][dx12][gpu][bench]") {
    auto measure = [](Device& device) {
        BufferDesc bd;
        bd.size = 256;
        bd.usage = BufferUsage::Uniform | BufferUsage::CopyDst;
        Vector<BufferHandle> handles;
        const u64 start = os::MonotonicTimeNanos();
        for (int i = 0; i < 1000; ++i) handles.Push(device.CreateBuffer(bd));
        const u64 ns = os::MonotonicTimeNanos() - start;
        for (usize i = 0; i < handles.Size(); ++i) {
            if (handles[i].IsValid()) device.DestroyBuffer(handles[i]);
        }
        device.WaitIdle();
        return static_cast<f64>(ns) / 1.0e6;
    };
    Dx12DevicePtr dx12 = MakeDevice(ValidationSetting::Off);
    REQUIRE(dx12);
    const f64 dx12_ms = measure(*dx12);
    dx12.Reset();
    DeviceCreateInfo vk;
    vk.backend = Backend::Vulkan;
    vk.validation = ValidationSetting::Off;
    vk.pipeline_cache = false;
    DevicePtr vulkan = CreateDevice(vk);
    char line[160];
    if (vulkan) {
        std::snprintf(line, sizeof(line), "1000 x 256-byte buffers: DX12 %.2f ms (committed), Vulkan %.2f ms", dx12_ms, measure(*vulkan));
    } else {
        std::snprintf(line, sizeof(line), "1000 x 256-byte buffers: DX12 %.2f ms (committed); no Vulkan device to compare", dx12_ms);
    }
    WARN(line);
}
