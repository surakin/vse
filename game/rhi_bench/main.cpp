// rhi_bench: records the Phase 3 performance baseline through the RHI (no window, no Vulkan names). Prints one line per
// measurement; nothing here is asserted in CI. See docs/guide/rhi.md ("Performance notes") for what the numbers mean.
//
//   rhi_bench [--draws N] [--frames N]     draw throughput for N cubes (default: 1000 and 10000)
//             [--skip-uploads] [--skip-pipelines]

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <vector>

#include <unistd.h>

#include "cube_scene.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"
#include "rhi/uniform_ring.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;

namespace {

constexpr u32 kSize = 512;

double Now() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}

struct Target {
    TextureHandle color;
    TextureHandle depth;
};

bool MakeTarget(Device* device, Target* t) {
    TextureDesc td;
    td.width = kSize;
    td.height = kSize;
    td.format = Format::RGBA8_UNorm;
    td.usage = TextureUsage::ColorAttachment | TextureUsage::CopySrc;
    t->color = device->CreateTexture(td);
    td.format = device->Caps().depth_format;
    td.usage = TextureUsage::DepthAttachment;
    t->depth = device->CreateTexture(td);
    return t->color.IsValid() && t->depth.IsValid();
}

// Records `draws` cubes into one list and returns the CPU time spent recording (seconds); the frame is submitted too.
double RunFrame(Device* device, demo::CubeRenderer* cube, UniformRing* ring, const Target& t, u32 draws, double* submit_seconds) {
    device->BeginFrame();
    ring->BeginFrame();
    CommandList* list = device->CreateCommandList();
    const double start = Now();
    list->Begin();
    cube->Prepare(list);
    list->Barrier(t.color, ResourceState::Undefined, ResourceState::ColorAttachment);
    list->Barrier(t.depth, ResourceState::Undefined, ResourceState::DepthAttachment);
    RenderingDesc pass;
    pass.color_count = 1;
    pass.color[0].texture = t.color;
    pass.depth.texture = t.depth;
    pass.depth.store = StoreOp::DontCare;
    list->BeginRendering(pass);
    const Mat4 vp = demo::PerspectiveVulkan(0.7853982f, 1.0f, 0.1f, 200.0f) * demo::LookAt(Vec3{0.0f, 0.0f, 60.0f}, Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f});
    for (u32 i = 0; i < draws; ++i) {
        const f32 x = static_cast<f32>(i % 100) * 0.4f - 20.0f;
        const f32 y = static_cast<f32>((i / 100) % 100) * 0.4f - 20.0f;
        cube->Draw(list, ring, vp * demo::ModelMatrix(Vec3{x, y, 0.0f}, 0.15f, 0.0f), kSize, kSize);
    }
    list->EndRendering();
    list->End();
    const double recorded = Now() - start;
    ring->Flush();
    const double submit_start = Now();
    CommandList* lists[] = {list};
    if (device->Submit(lists, 1) != 0) {
        cube->Submitted();
    }
    *submit_seconds = Now() - submit_start;
    device->EndFrame();
    return recorded;
}

void BenchDraws(Device* device, u32 draws, long frames) {
    Target t;
    demo::CubeRenderer cube;
    demo::CubeOptions options;
    options.depth_format = device->Caps().depth_format;
    UniformRing ring(device, static_cast<u64>(draws + 16) * 256);
    if (!MakeTarget(device, &t) || !cube.Create(device, options) || !ring.Valid()) {
        std::printf("draws %u: setup failed\n", draws);
        device->WaitIdle();
        cube.Destroy();
        if (t.color.IsValid()) {
            device->DestroyTexture(t.color);
        }
        if (t.depth.IsValid()) {
            device->DestroyTexture(t.depth);
        }
        return;
    }
    double submit = 0.0;
    RunFrame(device, &cube, &ring, t, draws, &submit); // warm up (geometry upload, pools)
    RunFrame(device, &cube, &ring, t, draws, &submit);
    device->WaitIdle();
    double record_total = 0.0;
    double submit_total = 0.0;
    const double start = Now();
    for (long f = 0; f < frames; ++f) {
        record_total += RunFrame(device, &cube, &ring, t, draws, &submit);
        submit_total += submit;
    }
    device->WaitIdle();
    const double wall = Now() - start;
    std::printf("draws        %6u cubes: record %7.3f ms/frame (%6.2f us/draw), submit %6.3f ms, wall %7.3f ms/frame (%.0f draws/s)\n", draws,
                record_total * 1e3 / static_cast<double>(frames), record_total * 1e6 / static_cast<double>(frames) / draws,
                submit_total * 1e3 / static_cast<double>(frames), wall * 1e3 / static_cast<double>(frames),
                static_cast<double>(draws) * static_cast<double>(frames) / wall);
    device->WaitIdle();
    cube.Destroy();
    device->DestroyTexture(t.color);
    device->DestroyTexture(t.depth);
}

void BenchBarriers(Device* device) {
    BufferDesc bd;
    bd.size = 1024;
    bd.usage = BufferUsage::Storage | BufferUsage::CopySrc | BufferUsage::CopyDst;
    bd.location = MemoryLocation::GpuOnly;
    const BufferHandle buffer = device->CreateBuffer(bd);
    TextureDesc td;
    td.width = 64;
    td.height = 64;
    td.format = Format::RGBA8_UNorm;
    td.usage = TextureUsage::Sampled | TextureUsage::CopyDst | TextureUsage::CopySrc;
    const TextureHandle texture = device->CreateTexture(td);
    if (!buffer.IsValid() || !texture.IsValid()) {
        std::printf("barriers: setup failed\n");
        if (buffer.IsValid()) {
            device->DestroyBuffer(buffer);
        }
        if (texture.IsValid()) {
            device->DestroyTexture(texture);
        }
        return;
    }
    constexpr u32 kBarriers = 20000;
    for (int pass = 0; pass < 2; ++pass) {
        const bool is_texture = pass == 1;
        device->BeginFrame();
        CommandList* list = device->CreateCommandList();
        list->Begin();
        if (is_texture) {
            list->Barrier(texture, ResourceState::Undefined, ResourceState::CopyDst);
        } else {
            list->Barrier(buffer, ResourceState::Undefined, ResourceState::CopyDst);
        }
        const double start = Now();
        for (u32 i = 0; i < kBarriers; ++i) {
            const bool even = (i & 1) == 0;
            if (is_texture) {
                list->Barrier(texture, even ? ResourceState::CopyDst : ResourceState::CopySrc, even ? ResourceState::CopySrc : ResourceState::CopyDst);
            } else {
                list->Barrier(buffer, even ? ResourceState::CopyDst : ResourceState::CopySrc, even ? ResourceState::CopySrc : ResourceState::CopyDst);
            }
        }
        const double elapsed = Now() - start;
        list->End();
        CommandList* lists[] = {list};
        device->Submit(lists, 1);
        device->EndFrame();
        std::printf("barrier      %s: %.3f us per call (%u recorded)\n", is_texture ? "texture" : "buffer ", elapsed * 1e6 / kBarriers, kBarriers);
        device->WaitIdle();
    }
    device->DestroyBuffer(buffer);
    device->DestroyTexture(texture);
}

void BenchUploads(Device* device) {
    struct Case {
        u64 bytes;
        int count;
    };
    const Case cases[] = {{1ull << 20, 256}, {64ull << 20, 8}};
    for (const Case& c : cases) {
        BufferDesc bd;
        bd.size = c.bytes;
        bd.usage = BufferUsage::Vertex | BufferUsage::CopyDst;
        bd.location = MemoryLocation::GpuOnly;
        const BufferHandle buffer = device->CreateBuffer(bd);
        std::vector<unsigned char> data(c.bytes, 0x5a);
        if (!buffer.IsValid()) {
            std::printf("upload %llu MiB: buffer creation failed\n", static_cast<unsigned long long>(c.bytes >> 20));
            continue;
        }
        device->UploadBuffer(buffer, 0, data.data(), c.bytes); // warm up the staging ring
        device->WaitIdle();
        const double start = Now();
        FenceValue last = 0;
        bool ok = true;
        for (int i = 0; i < c.count && ok; ++i) {
            const UploadTicket ticket = device->UploadBuffer(buffer, 0, data.data(), c.bytes);
            ok = ticket.ok;
            last = ticket.fence;
        }
        device->WaitFence(last, ~0ull);
        const double elapsed = Now() - start;
        if (!ok) {
            std::printf("upload       %3llu MiB: an upload failed; no figure\n", static_cast<unsigned long long>(c.bytes >> 20));
            device->WaitIdle();
            device->DestroyBuffer(buffer);
            continue;
        }
        const double mib = static_cast<double>(c.bytes >> 20) * c.count;
        std::printf("upload       %3llu MiB x %3d through the staging ring: %8.1f MiB/s (%.2f ms each)\n", static_cast<unsigned long long>(c.bytes >> 20),
                    c.count, mib / elapsed, elapsed * 1e3 / c.count);
        device->WaitIdle();
        device->DestroyBuffer(buffer);
    }
}

// Creates a device, builds a cube pipeline and returns the time CubeRenderer::Create took (shaders + pipeline + buffers).
double TimePipeline(bool cache, const char* dir) {
    DeviceCreateInfo info;
    info.validation = ValidationSetting::Off; // a baseline measures the engine, not the layer
    info.pipeline_cache = cache;
    info.pipeline_cache_dir = dir;
    Device* device = CreateDevice(info);
    if (device == nullptr) {
        return -1.0;
    }
    demo::CubeRenderer cube;
    demo::CubeOptions options;
    options.depth_format = device->Caps().depth_format;
    const double start = Now();
    const bool ok = cube.Create(device, options);
    const double elapsed = Now() - start;
    device->WaitIdle();
    cube.Destroy();
    DestroyDevice(device);
    return ok ? elapsed : -1.0;
}

void BenchPipelines() {
    char dir[] = "/tmp/rhi-bench-cache-XXXXXX";
    if (mkdtemp(dir) == nullptr) {
        std::printf("pipelines: no temp directory\n");
        return;
    }
    const double off = TimePipeline(false, dir);
    const double cold = TimePipeline(true, dir);
    const double warm = TimePipeline(true, dir);
    std::printf("pipeline     cube create: no cache %.3f ms, cold cache %.3f ms, warm cache %.3f ms\n", off * 1e3, cold * 1e3, warm * 1e3);
    char command[256];
    std::snprintf(command, sizeof(command), "rm -rf %s", dir);
    if (std::system(command) != 0) {
        std::printf("(could not remove %s)\n", dir);
    }
}

} // namespace

int main(int argc, char** argv) {
    long draws = 0;
    long frames = 30;
    bool uploads = true;
    bool pipelines = true;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--draws") == 0 && i + 1 < argc) {
            draws = std::atol(argv[++i]);
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = std::atol(argv[++i]);
        } else if (std::strcmp(argv[i], "--skip-uploads") == 0) {
            uploads = false;
        } else if (std::strcmp(argv[i], "--skip-pipelines") == 0) {
            pipelines = false;
        } else {
            std::fprintf(stderr, "usage: %s [--draws N] [--frames N] [--skip-uploads] [--skip-pipelines]\n", argv[0]);
            return 2;
        }
    }
    if (frames < 1) {
        frames = 1;
    }
    if (draws < 0 || draws > 1000000) {
        std::fprintf(stderr, "rhi_bench: --draws must be between 1 and 1000000\n");
        return 2;
    }
    DeviceCreateInfo info;
    info.validation = ValidationSetting::Off; // a baseline measures the engine, not the layer
    Device* device = CreateDevice(info);
    if (device == nullptr) {
        std::fprintf(stderr, "rhi_bench: no Vulkan device\n");
        return 1;
    }
    std::printf("backend %s, %ux%u target, %ld frames per draw measurement\n", device->BackendName(), kSize, kSize, frames);
    if (draws > 0) {
        BenchDraws(device, static_cast<u32>(draws), frames);
    } else {
        BenchDraws(device, 1000, frames);
        BenchDraws(device, 10000, frames);
    }
    BenchBarriers(device);
    if (uploads) {
        BenchUploads(device);
    }
    DestroyDevice(device);
    if (pipelines) {
        BenchPipelines();
    }
    return 0;
}
