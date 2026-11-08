// Phase 5 Day 77: the frame loop and parallel recording. The null device is the oracle for ordering (it validates every
// barrier's `before` state against the lists submitted so far, in order) and counts live objects; a GPU test compares
// serial and parallel recording pixel for pixel and sweeps allocation failures.
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "fullscreen.vert.spv.inc"
#include "graph_sample.frag.spv.inc"
#include "jobs/scheduler.h"
#include "renderer/renderer.h"
#include "rhi/null/null_device.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

namespace {

rhi::TextureDesc ColorTex(u32 w, u32 h, rhi::Format format = rhi::Format::RGBA8_UNorm) {
    rhi::TextureDesc d;
    d.width = w;
    d.height = h;
    d.format = format;
    d.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled | rhi::TextureUsage::CopySrc;
    return d;
}

// Begins and ends a rendering pass on one resource, clearing it: enough for the null device to check the state.
void ClearPassFn(const PassExecuteContext& context, void* user) {
    const GraphResource* target = static_cast<const GraphResource*>(user);
    rhi::RenderingDesc rd;
    rd.color[0].texture = context.graph->ResolvedTexture(*target);
    rd.color_count = 1;
    context.list->BeginRendering(rd);
    context.list->EndRendering();
}

// A frame of three passes: an offscreen intermediate sized from the target, then the target.
struct ThreePass {
    GraphResource mid;
    GraphResource target;
    u32 builds = 0;
    u32 last_width = 0;
    u32 last_height = 0;
    bool bad_transient = false;
    bool refuse = false;
};

bool BuildThreePass(const FrameContext& context, void* user) {
    ThreePass* s = static_cast<ThreePass*>(user);
    ++s->builds;
    s->last_width = context.width;
    s->last_height = context.height;
    if (s->refuse) return false;
    s->target = context.target;
    s->mid = context.graph->CreateTexture("mid", ColorTex(s->bad_transient ? 0 : context.width / 2, s->bad_transient ? 0 : context.height / 2));
    context.graph->AddPass("mid", &ClearPassFn, &s->mid).Write(s->mid, GraphUsage::ColorAttachment);
    context.graph->AddPass("final", &ClearPassFn, &s->target).Read(s->mid, GraphUsage::Sampled).Write(s->target, GraphUsage::ColorAttachment);
    return true;
}

struct NullRenderer {
    rhi::NullDevicePtr device;
    rhi::Swapchain* swapchain = nullptr;
    ThreePass scene;
    UniquePtr<Renderer> renderer_owner;
    Renderer* renderer = nullptr; // view of renderer_owner

    explicit NullRenderer(bool with_swapchain, jobs::Scheduler* scheduler = nullptr, u32 lose_at = 0) {
        rhi::NullDeviceOptions options;
        options.lose_device_at_frame = lose_at;
        device = rhi::CreateNullDevice(options);
        if (with_swapchain) {
            rhi::SwapchainDesc sd;
            sd.width = 64;
            sd.height = 48;
            swapchain = device->CreateSwapchain(sd);
        }
        RendererDesc rd;
        rd.device = device.Get();
        rd.swapchain = swapchain;
        rd.scheduler = scheduler;
        rd.build = &BuildThreePass;
        rd.build_user = &scene;
        renderer_owner = MakeUnique<Renderer>(rd);
        renderer = renderer_owner.Get();
    }
    ~NullRenderer() {
        renderer_owner.Reset();
        if (swapchain != nullptr) device->DestroySwapchain(swapchain);
        device.Reset();
    }
};

} // namespace

TEST_CASE("A swapchain frame renders and presents on the null device", "[renderer][frame]") {
    NullRenderer r(true);
    for (int i = 0; i < 5; ++i) REQUIRE(r.renderer->RenderFrame(nullptr, nullptr, nullptr) == FrameStatus::Ok);
    CHECK(r.device->ErrorCount() == 0);
    CHECK(r.renderer->Stats().frames_rendered == 5);
    CHECK(r.scene.last_width == 64);
    CHECK(r.scene.last_height == 48);
    // The intermediate (32 x 24) is created once and reused every later frame.
    CHECK(r.renderer->Pool().Stats().textures_created == 1);
    CHECK(r.renderer->Pool().Stats().texture_reuses == 4);
}

TEST_CASE("Minimizing skips frames, restoring and resizing resume them with targets that follow", "[renderer][frame]") {
    NullRenderer r(true);
    REQUIRE(r.renderer->RenderFrame(nullptr, nullptr, nullptr) == FrameStatus::Ok);
    CHECK(r.swapchain->Resize(0, 0) == rhi::SwapchainStatus::Minimized);
    CHECK(r.renderer->RenderFrame(nullptr, nullptr, nullptr) == FrameStatus::Skipped);
    CHECK(r.renderer->RenderFrame(nullptr, nullptr, nullptr) == FrameStatus::Skipped);
    CHECK(r.renderer->Stats().frames_skipped == 2);
    CHECK(r.swapchain->Resize(100, 80) == rhi::SwapchainStatus::Ok);
    REQUIRE(r.renderer->RenderFrame(nullptr, nullptr, nullptr) == FrameStatus::Ok);
    CHECK(r.scene.last_width == 100);
    CHECK(r.scene.last_height == 80);
    CHECK(r.device->ErrorCount() == 0);
    // The 32 x 24 intermediate is idle now and the 50 x 40 one is new; the old one is destroyed once it ages out.
    CHECK(r.renderer->Pool().Stats().textures_created == 2);
    for (int i = 0; i < 6; ++i) REQUIRE(r.renderer->RenderFrame(nullptr, nullptr, nullptr) == FrameStatus::Ok);
    CHECK(r.renderer->Pool().Stats().textures_destroyed == 1);
    CHECK(r.renderer->Pool().Stats().textures_live == 1);
}

TEST_CASE("An offscreen frame ends in the requested state", "[renderer][frame]") {
    NullRenderer r(false);
    rhi::TextureDesc td = ColorTex(64, 48);
    td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc;
    const rhi::TextureHandle texture = r.device->CreateTexture(td);
    FrameTarget target;
    target.texture = texture;
    target.width = 64;
    target.height = 48;
    target.final_state = rhi::ResourceState::CopySrc;
    for (int i = 0; i < 3; ++i) REQUIRE(r.renderer->RenderFrame(nullptr, nullptr, &target) == FrameStatus::Ok);
    CHECK(r.device->ErrorCount() == 0);
    r.device->DestroyTexture(texture);
}

TEST_CASE("A frame callback that refuses, or a transient the device cannot make, fails the frame and later frames recover", "[renderer][frame][fault]") {
    NullRenderer r(true);
    r.scene.refuse = true;
    CHECK(r.renderer->RenderFrame(nullptr, nullptr, nullptr) == FrameStatus::Failed);
    CHECK(std::string(r.renderer->LastError()) == "the frame callback abandoned the frame");
    r.scene.refuse = false;
    r.scene.bad_transient = true; // a zero-size texture: the device refuses to create it
    CHECK(r.renderer->RenderFrame(nullptr, nullptr, nullptr) == FrameStatus::Failed);
    CHECK(std::string(r.renderer->LastError()) == "transient texture 'mid' could not be allocated");
    r.device->ResetErrors();
    r.scene.bad_transient = false;
    // The swapchain image of the failed frames was presented, so rendering simply continues.
    CHECK(r.renderer->RenderFrame(nullptr, nullptr, nullptr) == FrameStatus::Ok);
    CHECK(r.device->ErrorCount() == 0);
    CHECK(r.renderer->Stats().frames_failed == 2);
    CHECK(r.renderer->Stats().frames_rendered == 1);
}

TEST_CASE("Losing the device mid-run ends the frame cleanly and the renderer can be destroyed", "[renderer][frame][fault]") {
    NullRenderer r(true, nullptr, 4); // the device is lost at the start of the 4th frame
    for (int i = 0; i < 3; ++i) REQUIRE(r.renderer->RenderFrame(nullptr, nullptr, nullptr) == FrameStatus::Ok);
    CHECK(r.renderer->RenderFrame(nullptr, nullptr, nullptr) == FrameStatus::DeviceLost);
    CHECK(r.renderer->RenderFrame(nullptr, nullptr, nullptr) == FrameStatus::DeviceLost);
    // Destruction (here, at scope exit) must not crash or leak; the null device checks for leaks at destroy.
}

TEST_CASE("A thousand frames neither grow the pool nor leak device objects", "[renderer][frame]") {
    NullRenderer r(true);
    for (int i = 0; i < 10; ++i) REQUIRE(r.renderer->RenderFrame(nullptr, nullptr, nullptr) == FrameStatus::Ok);
    const rhi::NullDeviceStats warm = r.device->Statistics();
    for (int i = 0; i < 1000; ++i) REQUIRE(r.renderer->RenderFrame(nullptr, nullptr, nullptr) == FrameStatus::Ok);
    const rhi::NullDeviceStats after = r.device->Statistics();
    CHECK(after.textures == warm.textures);
    CHECK(after.buffers == warm.buffers);
    CHECK(after.bind_groups == warm.bind_groups);
    CHECK(r.renderer->Pool().Stats().textures_created == 1);
    CHECK(r.renderer->Pool().Stats().textures_destroyed == 0);
    CHECK(r.device->ErrorCount() == 0);
}

namespace {

// ---- parallel recording ----

constexpr u32 kIndependent = 48;

struct ThreadLog {
    std::atomic<u32> counts{0};
    std::thread::id ids[kIndependent];
};

struct IndependentPass {
    ThreadLog* log;
    u32 index;
};

void RecordThread(const PassExecuteContext& context, void* user) {
    IndependentPass* pass = static_cast<IndependentPass*>(user);
    pass->log->ids[pass->index] = std::this_thread::get_id();
    pass->log->counts.fetch_add(1);
    // A little work so the other participants get to steal passes.
    const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(300);
    while (std::chrono::steady_clock::now() < until) {
    }
    (void)context;
}

// Pass i toggles a buffer between CopyDst and CopySrc, so the passes form a chain whose barrier `before` states are only
// consistent if the lists are submitted in schedule order.
struct Chain {
    GraphResource buffer;
};

} // namespace

TEST_CASE("Independent passes record on several threads, their lists submitted in order", "[renderer][frame][parallel]") {
    jobs::Scheduler::Config config;
    config.participants = 4;
    config.pin = false;
    jobs::Scheduler scheduler(config);
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    {
        TransientPool pool(device.Get(), TransientPoolDesc{});
        ArenaAllocator arena(8u << 20);
        RenderGraph graph(&arena, RenderGraphDesc{});
        ThreadLog log;
        IndependentPass passes[kIndependent];
        for (u32 i = 0; i < kIndependent; ++i) {
            passes[i] = {&log, i};
            graph.AddPass("independent", &RecordThread, &passes[i], PassFlags::SideEffects);
        }
        // A chain of buffer transitions after them: wrong submission order would make the null device reject a barrier.
        rhi::BufferDesc bd;
        bd.size = 64;
        bd.usage = rhi::BufferUsage::CopySrc | rhi::BufferUsage::CopyDst;
        Chain chain;
        chain.buffer = graph.CreateBuffer("chain", bd);
        for (u32 i = 0; i < 40; ++i) {
            PassBuilder b = graph.AddPass("link", nullptr, nullptr, PassFlags::SideEffects);
            if (i % 2 == 0) b.Write(chain.buffer, GraphUsage::CopyDst);
            else b.Read(chain.buffer, GraphUsage::CopySrc);
        }
        device->BeginFrame();
        rhi::FenceValue fence = 0;
        REQUIRE(graph.ExecuteParallel(&pool, device.Get(), &scheduler, &fence));
        device->EndFrame();
        CHECK(fence != 0);
        CHECK(log.counts.load() == kIndependent);
        CHECK(device->ErrorCount() == 0);
        std::vector<std::thread::id> distinct;
        for (u32 i = 0; i < kIndependent; ++i) {
            bool seen = false;
            for (const std::thread::id& d : distinct) seen = seen || d == log.ids[i];
            if (!seen) distinct.push_back(log.ids[i]);
        }
        CHECK(distinct.size() >= 2); // 48 passes of 0.3 ms on 4 participants cannot all land on one thread
    }
    device.Reset();
}

TEST_CASE("ExecuteParallel without a scheduler records the same lists in order", "[renderer][frame][parallel]") {
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    {
        TransientPool pool(device.Get(), TransientPoolDesc{});
        ArenaAllocator arena(8u << 20);
        RenderGraph graph(&arena, RenderGraphDesc{});
        rhi::BufferDesc bd;
        bd.size = 64;
        bd.usage = rhi::BufferUsage::CopySrc | rhi::BufferUsage::CopyDst;
        const GraphResource buffer = graph.CreateBuffer("chain", bd);
        for (u32 i = 0; i < 20; ++i) {
            PassBuilder b = graph.AddPass("link", nullptr, nullptr, PassFlags::SideEffects);
            if (i % 2 == 0) b.Write(buffer, GraphUsage::CopyDst);
            else b.Read(buffer, GraphUsage::CopySrc);
        }
        device->BeginFrame();
        REQUIRE(graph.ExecuteParallel(&pool, device.Get(), nullptr, nullptr));
        device->EndFrame();
        CHECK(device->ErrorCount() == 0);
    }
    device.Reset();
}

TEST_CASE("A lost device during ExecuteParallel fails it and puts the pool back", "[renderer][frame][fault]") {
    rhi::NullDeviceOptions options;
    options.lose_device_at_frame = 1;
    rhi::NullDevicePtr device = rhi::CreateNullDevice(options);
    {
        TransientPool pool(device.Get(), TransientPoolDesc{});
        ArenaAllocator arena(8u << 20);
        RenderGraph graph(&arena, RenderGraphDesc{});
        const GraphResource t = graph.CreateTexture("t", ColorTex(8, 8));
        rhi::TextureHandle imported = device->CreateTexture(ColorTex(8, 8));
        const GraphResource out = graph.ImportTexture("out", imported, rhi::ResourceState::Undefined, rhi::ResourceState::CopySrc, false);
        graph.AddPass("a", nullptr, nullptr).Write(t, GraphUsage::ColorAttachment);
        graph.AddPass("b", nullptr, nullptr).Read(t, GraphUsage::Sampled).Write(out, GraphUsage::ColorAttachment);
        device->BeginFrame(); // the device is lost from here
        CHECK_FALSE(graph.ExecuteParallel(&pool, device.Get(), nullptr, nullptr));
        CHECK(std::string(graph.ExecuteError()) == "a command list could not be created (the device is lost or out of memory)");
        CHECK(pool.Stats().textures_idle == pool.Stats().textures_live); // nothing stays out of the pool
        device->EndFrame();
        device->DestroyTexture(imported);
    }
    device.Reset();
}

namespace {

// ---- real device: serial and parallel recording give the same pixels ----

constexpr u32 kSize = 16;

struct SampleScene {
    rhi::Device* device = nullptr;
    rhi::ShaderHandle vs, fs;
    rhi::BindGroupLayoutHandle layout;
    rhi::PipelineHandle pipeline;
    rhi::SamplerHandle sampler;
    GraphResource a, b, c, d, out;

    bool Create(rhi::Device* dev) {
        device = dev;
        rhi::ShaderDesc sd;
        sd.stage = rhi::ShaderStage::Vertex;
        sd.SetCode(rhi::SpirvBlob(shaders::kFullscreenVertSpv, shaders::kFullscreenVertSpvWordCount));
        vs = device->CreateShader(sd);
        sd.stage = rhi::ShaderStage::Fragment;
        sd.SetCode(rhi::SpirvBlob(shaders::kGraphSampleFragSpv, shaders::kGraphSampleFragSpvWordCount));
        fs = device->CreateShader(sd);
        rhi::BindingDesc bindings[3] = {{0, rhi::BindingType::SampledTexture, rhi::ShaderStage::Fragment, 1},
                                        {1, rhi::BindingType::SampledTexture, rhi::ShaderStage::Fragment, 1},
                                        {2, rhi::BindingType::Sampler, rhi::ShaderStage::Fragment, 1}};
        rhi::BindGroupLayoutDesc ld;
        ld.entries = rhi::Span<rhi::BindingDesc>(bindings, 3);
        layout = device->CreateBindGroupLayout(ld);
        rhi::GraphicsPipelineDesc gp;
        gp.vertex_shader = vs;
        gp.fragment_shader = fs;
        gp.cull_mode = rhi::CullMode::None;
        gp.color_targets[0].format = rhi::Format::RGBA8_UNorm;
        gp.color_target_count = 1;
        gp.bind_group_layouts[0] = layout;
        gp.bind_group_layout_count = 1;
        pipeline = device->CreateGraphicsPipeline(gp);
        rhi::SamplerDesc smp;
        smp.mag_filter = smp.min_filter = smp.mip_filter = rhi::FilterMode::Nearest;
        sampler = device->CreateSampler(smp);
        return vs.IsValid() && fs.IsValid() && layout.IsValid() && pipeline.IsValid() && sampler.IsValid();
    }
    void Destroy() {
        device->DestroySampler(sampler);
        device->DestroyPipeline(pipeline);
        device->DestroyBindGroupLayout(layout);
        device->DestroyShader(vs);
        device->DestroyShader(fs);
    }
    void Clear(const PassExecuteContext& ctx, GraphResource target, f32 r, f32 g, f32 bl, f32 al) const {
        rhi::RenderingDesc rd;
        rd.color[0].texture = ctx.graph->ResolvedTexture(target);
        rd.color[0].clear_color[0] = r;
        rd.color[0].clear_color[1] = g;
        rd.color[0].clear_color[2] = bl;
        rd.color[0].clear_color[3] = al;
        rd.color_count = 1;
        ctx.list->BeginRendering(rd);
        ctx.list->EndRendering();
    }
    void Add(const PassExecuteContext& ctx, GraphResource target, GraphResource first, GraphResource second) const {
        rhi::BindGroupEntry e[3];
        e[0].binding = 0;
        e[0].texture = ctx.graph->ResolvedTexture(first);
        e[1].binding = 1;
        e[1].texture = ctx.graph->ResolvedTexture(second);
        e[2].binding = 2;
        e[2].sampler = sampler;
        rhi::BindGroupDesc gd;
        gd.layout = layout;
        gd.entries = rhi::Span<rhi::BindGroupEntry>(e, 3);
        gd.transient = true;
        const rhi::BindGroupHandle group = device->CreateBindGroup(gd);
        rhi::RenderingDesc rd;
        rd.color[0].texture = ctx.graph->ResolvedTexture(target);
        rd.color_count = 1;
        ctx.list->BeginRendering(rd);
        ctx.list->SetPipeline(pipeline);
        ctx.list->SetViewport(rhi::Viewport{0, 0, static_cast<f32>(kSize), static_cast<f32>(kSize), 0.0f, 1.0f});
        ctx.list->SetScissor(rhi::ScissorRect{0, 0, kSize, kSize});
        ctx.list->SetBindGroup(0, group);
        ctx.list->Draw(3, 1, 0, 0);
        ctx.list->EndRendering();
    }
};

void ClearA(const PassExecuteContext& c, void* u) { static_cast<const SampleScene*>(u)->Clear(c, static_cast<const SampleScene*>(u)->a, 0.1f, 0.2f, 0.05f, 0.0f); }
void ClearC(const PassExecuteContext& c, void* u) { static_cast<const SampleScene*>(u)->Clear(c, static_cast<const SampleScene*>(u)->c, 0.3f, 0.1f, 0.2f, 0.0f); }
void AddAA(const PassExecuteContext& c, void* u) {
    const SampleScene* s = static_cast<const SampleScene*>(u);
    s->Add(c, s->b, s->a, s->a); // independent of ClearC: these two may record on different threads
}
void AddCC(const PassExecuteContext& c, void* u) {
    const SampleScene* s = static_cast<const SampleScene*>(u);
    s->Add(c, s->d, s->c, s->c);
}
void AddBD(const PassExecuteContext& c, void* u) {
    const SampleScene* s = static_cast<const SampleScene*>(u);
    s->Add(c, s->out, s->b, s->d);
}

bool BuildSample(const FrameContext& context, void* user) {
    SampleScene* s = static_cast<SampleScene*>(user);
    rhi::TextureDesc rt = ColorTex(kSize, kSize);
    s->a = context.graph->CreateTexture("a", rt);
    s->b = context.graph->CreateTexture("b", rt);
    s->c = context.graph->CreateTexture("c", rt);
    s->d = context.graph->CreateTexture("d", rt);
    s->out = context.target;
    context.graph->AddPass("clear a", &ClearA, s).Write(s->a, GraphUsage::ColorAttachment);
    context.graph->AddPass("clear c", &ClearC, s).Write(s->c, GraphUsage::ColorAttachment);
    context.graph->AddPass("a+a", &AddAA, s).Read(s->a, GraphUsage::Sampled).Write(s->b, GraphUsage::ColorAttachment);
    context.graph->AddPass("c+c", &AddCC, s).Read(s->c, GraphUsage::Sampled).Write(s->d, GraphUsage::ColorAttachment);
    context.graph->AddPass("b+d", &AddBD, s)
        .Read(s->b, GraphUsage::Sampled)
        .Read(s->d, GraphUsage::Sampled)
        .Write(s->out, GraphUsage::ColorAttachment);
    return true;
}

rhi::DevicePtr MakeGpuDevice(u32 fail_allocations_from = 0) {
    rhi::DeviceCreateInfo info;
    info.validation = rhi::ValidationSetting::On;
    info.sync_validation = true;
    info.pipeline_cache = false;
    info.faults.fail_allocations_from = fail_allocations_from;
    return rhi::CreateDevice(info);
}

bool GpuAvailable(rhi::Device* device) {
    if (device != nullptr) return true;
    const char* required = std::getenv("ENGINE_RHI_REQUIRE_GPU");
    if (required != nullptr && required[0] == '1') FAIL("ENGINE_RHI_REQUIRE_GPU=1 but no Vulkan device could be created");
    return false;
}

struct GpuRun {
    std::vector<u8> pixels;
    FrameStatus status = FrameStatus::Failed;
};

GpuRun RenderSample(rhi::Device* device, jobs::Scheduler* scheduler) {
    GpuRun run;
    SampleScene scene;
    if (!scene.Create(device)) return run;
    rhi::TextureDesc td = ColorTex(kSize, kSize);
    td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc;
    const rhi::TextureHandle target = device->CreateTexture(td);
    if (target.IsValid()) {
        RendererDesc rd;
        rd.device = device;
        rd.scheduler = scheduler;
        rd.build = &BuildSample;
        rd.build_user = &scene;
        {
            Renderer renderer(rd);
            FrameTarget ft;
            ft.texture = target;
            ft.width = ft.height = kSize;
            ft.final_state = rhi::ResourceState::CopySrc;
            for (int i = 0; i < 3; ++i) run.status = renderer.RenderFrame(nullptr, nullptr, &ft);
            device->WaitIdle();
            run.pixels.resize(kSize * kSize * 4);
            if (run.status == FrameStatus::Ok) device->ReadbackTexture(target, 0, 0, run.pixels.data(), run.pixels.size(), rhi::ResourceState::CopySrc);
        }
        device->DestroyTexture(target);
    }
    device->WaitIdle();
    scene.Destroy();
    return run;
}

} // namespace

TEST_CASE("Parallel recording gives the same pixels as serial recording", "[renderer][frame][parallel][gpu]") {
    rhi::DevicePtr device = MakeGpuDevice();
    if (!GpuAvailable(device.Get())) SKIP("no Vulkan device on this machine");
    jobs::Scheduler::Config config;
    config.participants = 4;
    config.pin = false;
    jobs::Scheduler scheduler(config);
    const GpuRun serial = RenderSample(device.Get(), nullptr);
    const GpuRun parallel = RenderSample(device.Get(), &scheduler);
    REQUIRE(serial.status == FrameStatus::Ok);
    REQUIRE(parallel.status == FrameStatus::Ok);
    CHECK(serial.pixels == parallel.pixels);
    // out = (a + a) + (c + c) = (0.2 + 0.6, 0.4 + 0.2, 0.1 + 0.4, 0 + 0) -> (204, 153, 128, 0) in 8 bits.
    const u8* px = &serial.pixels[(kSize / 2 * kSize + kSize / 2) * 4];
    CHECK(std::abs(static_cast<int>(px[0]) - 204) <= 2);
    CHECK(std::abs(static_cast<int>(px[1]) - 153) <= 2);
    CHECK(std::abs(static_cast<int>(px[2]) - 128) <= 2);
    device.Reset();
}

constexpr u32 kSweep = 24;

TEST_CASE("Device allocation failures at any point end frames cleanly", "[renderer][frame][fault][gpu]") {
    // fail_allocations_from = N makes the Nth device-memory allocation and every later one fail. Whatever the scene was
    // creating at that moment (a texture, the target, a pooled transient), nothing may crash and the teardown must be clean.
    rhi::DevicePtr probe = MakeGpuDevice();
    if (!GpuAvailable(probe.Get())) SKIP("no Vulkan device on this machine");
    probe.Reset();
    u32 failed_frames = 0;
    u32 ok_frames = 0;
    u32 refused = 0;
    for (u32 from = 1; from <= kSweep; ++from) {
        rhi::DevicePtr device = MakeGpuDevice(from);
        if (device == nullptr) { // the device's own setup (staging memory) hit the failure: creation is refused cleanly
            ++refused;
            continue;
        }
        const GpuRun run = RenderSample(device.Get(), nullptr);
        if (run.status == FrameStatus::Ok) ++ok_frames;
        else ++failed_frames;
        device.Reset();
    }
    CHECK(failed_frames > 0); // some allocation index must have hit a texture the frame needed
    CHECK(ok_frames + failed_frames + refused == kSweep);
}

TEST_CASE("Passes that are only ordering markers record no command list", "[renderer][frame][parallel]") {
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    {
        TransientPool pool(device.Get(), TransientPoolDesc{});
        ArenaAllocator arena(8u << 20);
        RenderGraph graph(&arena, RenderGraphDesc{});
        u32 ran = 0;
        auto count = [](const PassExecuteContext&, void* user) { ++*static_cast<u32*>(user); };
        graph.AddPass("marker 1", nullptr, nullptr, PassFlags::SideEffects);
        graph.AddPass("real", count, &ran, PassFlags::SideEffects);
        graph.AddPass("marker 2", nullptr, nullptr, PassFlags::SideEffects);
        device->BeginFrame();
        REQUIRE(graph.ExecuteParallel(&pool, device.Get(), nullptr, nullptr));
        CHECK(ran == 1);
        CHECK(device->Statistics().command_lists == 1); // only "real" got a list
        device->EndFrame();
    }
    device.Reset();
}

TEST_CASE("The frame counters add up to the frames begun", "[renderer][frame]") {
    NullRenderer r(true, nullptr, 7);
    r.renderer->RenderFrame(nullptr, nullptr, nullptr);                     // ok
    r.scene.refuse = true;
    r.renderer->RenderFrame(nullptr, nullptr, nullptr);                     // failed
    r.scene.refuse = false;
    r.swapchain->Resize(0, 0);
    r.renderer->RenderFrame(nullptr, nullptr, nullptr);                     // skipped
    r.swapchain->Resize(64, 48);
    for (int i = 0; i < 6; ++i) r.renderer->RenderFrame(nullptr, nullptr, nullptr); // frames 4-6 ok, 7+ lost
    const RendererStats s = r.renderer->Stats();
    CHECK(s.frames_rendered + s.frames_skipped + s.frames_failed + s.frames_lost == r.renderer->FrameIndex());
    CHECK(s.frames_lost > 0);
}
