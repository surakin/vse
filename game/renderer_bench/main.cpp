// renderer_bench: records the Phase 5 performance baseline. Prints one line per measurement; nothing here is asserted in CI. See
// docs/guide/renderer.md ("Performance notes") for what the numbers mean.
//
//   renderer_bench [--section graph|frame|lights|pool|all] [--device vulkan|null] [--frames N] [--threads N]
//             [--objects 1000|10000|50000] [--single]
//
// `frame` times a whole frame's CPU work (scene preparation, graph build and compile, recording, submission) at 1k, 10k and 50k
// objects; the device is idled after every frame, outside the timer, so the number is CPU work and not waiting for the GPU.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "algorithm.h"
#include "debug/program_setup.h"
#include "demo_frame.h"
#include "demo_scene.h"
#include "jobs/scheduler.h"
#include "mem/arena_allocator.h"
#include "os/os.h"
#include "renderer/render_graph.h"
#include "rhi/backends/backends.h"
#include "rhi/null/null_device.h"
#include "rhi/rhi.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

namespace {

constexpr u32 kWidth = 1280;
constexpr u32 kHeight = 720;

double Now() { return static_cast<double>(os::MonotonicTimeNanos()) * 1e-9; }

struct Samples {
    double v[512];
    u32 n = 0;
    void Add(double x) {
        if (n < 512) v[n++] = x;
    }
    double Median() {
        if (n == 0) return 0;
        Sort(Span<double>(v, n));
        return v[n / 2];
    }
    double Min() {
        double m = v[0];
        for (u32 i = 1; i < n; ++i) m = engine_placeholder::Min(m, v[i]);
        return m;
    }
};

struct Options {
    const char* section = "all";
    bool null_device = false;
    u32 frames = 30;
    u32 threads = 0; // 0: the scheduler's default
    u32 objects = 0; // 0: the frame section runs 1k, 10k and 50k
    bool single_only = false;
};

// ---- graph: build + compile time against the pass count ----

void BenchGraph() {
    std::printf("# graph: build + compile of a chain of N passes, each reading the previous pass's transient texture\n");
    const u32 counts[] = {10, 100, 1000};
    for (u32 passes : counts) {
        ArenaAllocator arena(64u << 20);
        Samples build, compile;
        for (int rep = 0; rep < 50; ++rep) {
            arena.Reset();
            RenderGraphDesc desc;
            desc.max_passes = passes + 4;
            desc.max_resources = passes + 4;
            desc.max_accesses = passes * 3 + 16;
            RenderGraph graph(&arena, desc);
            rhi::TextureDesc td;
            td.width = kWidth;
            td.height = kHeight;
            td.format = rhi::Format::RGBA8_UNorm;
            td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled;
            const double t0 = Now();
            GraphResource prev;
            for (u32 i = 0; i < passes; ++i) {
                const GraphResource cur = graph.CreateTexture("t", td);
                PassBuilder pass = graph.AddPass("p", nullptr, nullptr, PassFlags::SideEffects);
                if (i > 0) pass.Read(prev, GraphUsage::Sampled);
                pass.Write(cur, GraphUsage::ColorAttachment);
                prev = cur;
            }
            const double t1 = Now();
            if (!graph.Compile()) {
                std::printf("graph: compile failed: %s\n", graph.Error(0));
                return;
            }
            const double t2 = Now();
            build.Add(t1 - t0);
            compile.Add(t2 - t1);
        }
        std::printf("graph  passes %5u  build %8.1f us  compile %8.1f us  (%.2f us/pass total)\n", passes, build.Median() * 1e6, compile.Median() * 1e6,
                    (build.Median() + compile.Median()) * 1e6 / passes);
    }
}

// ---- frame: one device, one scene, repeated frames ----

struct Run {
    double cpu_median_ms = 0;
    double cpu_min_ms = 0;
    GBufferStats gbuffer;
    LightSummary lights;
    u32 volume_draws = 0;
    TransientPoolStats pool;
    u64 frames = 0;
    bool created = false;
    bool ok = false;
};

rhi::DevicePtr MakeDevice(const Options& o, rhi::NullDevicePtr* null_holder) {
    if (o.null_device) {
        *null_holder = rhi::CreateNullDevice();
        return rhi::DevicePtr();
    }
    rhi::DeviceCreateInfo info;
    info.backend = rhi::Backend::Vulkan; // the benchmarks draw with Vulkan until Phase 13 Day 173 lets them choose
    info.pipeline_cache = false;
    return rhi::CreateDevice(info);
}

Run RunScene(const Options& o, u32 objects, u32 lights, bool use_jobs) {
    Run run;
    rhi::NullDevicePtr null_holder;
    rhi::DevicePtr vulkan = MakeDevice(o, &null_holder);
    rhi::Device* device = o.null_device ? static_cast<rhi::Device*>(null_holder.Get()) : vulkan.Get();
    if (device == nullptr) {
        std::printf("no device\n");
        return run;
    }
    jobs::Scheduler::Config config;
    config.participants = o.threads;
    UniquePtr<jobs::Scheduler> scheduler;
    if (use_jobs) scheduler = MakeUnique<jobs::Scheduler>(config);
    {
        rhi::TextureDesc td;
        td.width = kWidth;
        td.height = kHeight;
        td.format = rhi::Format::RGBA8_SRGB;
        td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc;
        const rhi::TextureHandle target = device->CreateTexture(td);
        renderer_demo::DemoFrame frame;
        renderer_demo::DemoScene scene;
        renderer_demo::FrameSetup fs;
        fs.device = device;
        fs.max_objects = objects + 16;
        fs.max_lights = lights + 2;
        fs.scheduler = scheduler.Get();
        renderer_demo::SceneOptions so;
        so.objects = objects;
        so.lights = lights;
        so.asset_dir = RENDERER_DEMO_ASSET_DIR;
        if (target.IsValid() && frame.Create(fs) && scene.Create(device, frame.Materials(), frame.Templates(), so)) {
            scene.Animate(0.5f);
            Camera camera = scene.OverviewCamera(static_cast<f32>(kWidth) / static_cast<f32>(kHeight));
            FitCameraDepthRange(scene.Scene(), &camera, 0.1f);
            FrameTarget ft;
            ft.texture = target;
            ft.width = kWidth;
            ft.height = kHeight;
            ft.format = rhi::Format::RGBA8_SRGB;
            ft.initial = rhi::ResourceState::Undefined;
            ft.final_state = rhi::ResourceState::CopySrc;
            Samples cpu;
            bool all_ok = true;
            for (u32 i = 0; i < o.frames + 5; ++i) {
                frame.SetTime(0.5f + 0.016f * static_cast<f32>(i), 0.016f);
                const double t0 = Now();
                const FrameStatus status = frame.Render(&scene.Scene(), &camera, &ft);
                const double t1 = Now();
                device->WaitIdle();
                if (status != FrameStatus::Ok) all_ok = false;
                if (i >= 5) cpu.Add(t1 - t0);
            }
            run.cpu_median_ms = cpu.Median() * 1e3;
            run.cpu_min_ms = cpu.Min() * 1e3;
            run.gbuffer = frame.GBufferStatistics();
            run.lights = frame.Lights();
            run.volume_draws = frame.PointDraws();
            run.pool = frame.PoolStats();
            run.frames = frame.Stats().frames_rendered;
            run.created = true;
            run.ok = all_ok;
        } else {
            std::printf("could not create the scene for %u objects\n", objects);
        }
        device->WaitIdle();
        scene.Destroy();
        frame.Destroy();
        if (target.IsValid()) device->DestroyTexture(target);
    }
    scheduler.Reset();
    return run;
}

void BenchFrame(const Options& o) {
    std::printf("# frame: CPU time of a whole frame at %ux%u, 300 point lights, device idled outside the timer\n", kWidth, kHeight);
    const u32 counts[] = {1000, 10000, 50000};
    for (u32 objects : counts) {
        if (o.objects != 0 && o.objects != objects) continue;
        for (int j = 0; j < (o.single_only ? 1 : 2); ++j) {
            const Run r = RunScene(o, objects, 300, j == 1);
            std::printf("frame  objects %6u  %-7s  median %8.3f ms  min %8.3f ms  per object %6.3f us  %s\n", objects, j == 0 ? "single" : "jobs",
                        r.cpu_median_ms, r.cpu_min_ms, r.cpu_median_ms * 1e3 / objects, r.ok ? "ok" : "NOT OK");
            if (j == 0) {
                std::printf("       visible %u  drawn %u  draw calls %u  pipeline binds %u  material binds %u  objects per draw %.2f\n", r.gbuffer.visible,
                            r.gbuffer.drawn_objects, r.gbuffer.draws, r.gbuffer.pipeline_binds, r.gbuffer.material_binds,
                            r.gbuffer.draws != 0 ? static_cast<double>(r.gbuffer.drawn_objects) / r.gbuffer.draws : 0.0);
            }
        }
    }
}

void BenchLights(const Options& o) {
    std::printf("# lights: CPU time of a frame of 1000 objects as the point light count grows\n");
    const u32 counts[] = {0, 100, 1000, 4000, 16000};
    double base = 0;
    for (u32 lights : counts) {
        const Run r = RunScene(o, 1000, lights, false);
        if (!r.created || !r.ok) {
            std::printf("lights %6u  NOT OK, stopping\n", lights);
            return;
        }
        if (lights == 0) base = r.cpu_median_ms;
        std::printf("lights %6u  median %8.3f ms  volume draws %5u  per light (over 0 lights) %7.3f us\n", lights, r.cpu_median_ms, r.volume_draws,
                    lights != 0 ? (r.cpu_median_ms - base) * 1e3 / lights : 0.0);
    }
}

void BenchPool(const Options& o) {
    Options p = o;
    p.frames = 100;
    const Run r = RunScene(p, 1000, 300, false);
    if (!r.created || !r.ok) {
        std::printf("pool   NOT OK\n");
        return;
    }
    const u32 uses = r.pool.textures_created + r.pool.texture_reuses;
    std::printf("# pool: after %llu frames of the deferred pipeline\n", static_cast<unsigned long long>(r.frames));
    std::printf("pool   textures created %u  reused %u  hit rate %.1f%%  destroyed %u  live %u\n", r.pool.textures_created, r.pool.texture_reuses,
                uses != 0 ? 100.0 * r.pool.texture_reuses / uses : 0.0, r.pool.textures_destroyed, r.pool.textures_live);
}

// Parses a whole non-negative number; false for anything else.
bool ParseCount(const char* text, u32* out, u32 max) {
    char* end = nullptr;
    const unsigned long v = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0' || text[0] == '-' || v > max) return false;
    *out = static_cast<u32>(v);
    return true;
}

bool Section(const Options& o, const char* name) { return std::strcmp(o.section, "all") == 0 || std::strcmp(o.section, name) == 0; }

int Usage(const char* program) {
    std::fprintf(stderr, "usage: %s [--section graph|frame|lights|pool|all] [--device vulkan|null] [--frames 1-500] [--threads 0-64] [--objects 1000|10000|50000] [--single]\n",
                 program);
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    ::engine_placeholder::rhi::RegisterCompiledBackends(); // before any device: the backends this build has
    const engine_placeholder::debug::ProgramDebugSetup debug_setup = engine_placeholder::debug::StartProgramDebugging(argc, argv);
    engine_placeholder::debug::RunCrashTest(debug_setup.crash_test); // --crash-test=segv|abort|assert|ensure, before any window or device
    Options o;
    for (int i = 1; i < argc; ++i) {
        if (engine_placeholder::debug::IsProgramDebugArgument(argv[i])) {
            continue; // --crash-handler=... and --crash-test=... are handled by StartProgramDebugging
        }
        const bool has_value = i + 1 < argc;
        if (std::strcmp(argv[i], "--section") == 0 && has_value) {
            o.section = argv[++i];
            if (!Section(o, "graph") && !Section(o, "frame") && !Section(o, "lights") && !Section(o, "pool")) return Usage(argv[0]);
        } else if (std::strcmp(argv[i], "--device") == 0 && has_value) {
            o.null_device = std::strcmp(argv[++i], "null") == 0;
        } else if (std::strcmp(argv[i], "--frames") == 0 && has_value) {
            if (!ParseCount(argv[++i], &o.frames, 500) || o.frames == 0) return Usage(argv[0]);
        } else if (std::strcmp(argv[i], "--objects") == 0 && has_value) {
            if (!ParseCount(argv[++i], &o.objects, 50000) || (o.objects != 1000 && o.objects != 10000 && o.objects != 50000)) return Usage(argv[0]);
        } else if (std::strcmp(argv[i], "--single") == 0) {
            o.single_only = true;
        } else if (std::strcmp(argv[i], "--threads") == 0 && has_value) {
            if (!ParseCount(argv[++i], &o.threads, 64)) return Usage(argv[0]);
        } else {
            return Usage(argv[0]);
        }
    }
    if (Section(o, "graph")) BenchGraph();
    if (Section(o, "frame")) BenchFrame(o);
    if (Section(o, "lights")) BenchLights(o);
    if (Section(o, "pool")) BenchPool(o);
    return 0;
}
