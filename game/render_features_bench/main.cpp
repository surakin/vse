// render_features_bench: what the render features cost. Run by hand in a Release build; the numbers are recorded in
// docs/guide/instancing.md and docs/guide/terrain.md ("Cost"). `--scene check` is the regression check ctest runs in an
// optimized build (generous bounds: a 3x slowdown fails it, the results are checked equal at 1 and 16 threads).
//
//   render_features_bench [--scene cull|terrain|hierarchy|sky|memory|check|all] [--instances N] [--nodes N]
//
// `all` is every scene but check. `sky` needs a GPU and a Vulkan (or the platform's) device; it says so and skips
// without one.

#include "containers/array.h"
#include "containers/vector.h"
#include "cstr/format.h"
#include "cstr/number.h"
#include "debug/program_setup.h"
#include "features_gpu.h"
#include "features_world.h"
#include "jobs/scheduler.h"
#include "log/output.h"
#include "os/command_line.h"
#include "os/os.h"
#include "renderer/camera.h"
#include "renderer/instance_cull.h"
#include "renderer/instance_set.h"
#include "renderer/instance_tree.h"
#include "renderer/instance_tree_cull.h"
#include "renderer/render_settings.h"
#include "renderer/scene_frame.h"
#include "renderer/shared_settings.h"
#include "renderer/terrain_tiles.h"
#include "rhi/backend_registry.h"
#include "rhi/backends/backends.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"
#include "settings_main.h"
#include "string/string.h"

using namespace vse;
using namespace vse::renderer;
using namespace render_features;

namespace
{

f64 NowMs()
{
    return static_cast<f64>(os::MonotonicTimeNanos()) * 1e-6;
}

struct Rng
{
    u64 state = 12345;
    u64 Next()
    {
        u64 z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    f64 Unit() { return static_cast<f64>(Next() >> 11) * (1.0 / 9007199254740992.0); }
};

// The median of `runs` timings of `fn`, in milliseconds.
template <class Fn> f64 MedianMs(int runs, Fn fn)
{
    Vector<f64> times;
    for (int i = 0; i < runs; ++i)
    {
        const f64 t0 = NowMs();
        fn();
        times.Push(NowMs() - t0);
    }
    for (usize i = 1; i < times.Size(); ++i)
    {
        for (usize j = i; j > 0 && times[j] < times[j - 1]; --j)
        {
            const f64 t = times[j];
            times[j] = times[j - 1];
            times[j - 1] = t;
        }
    }
    return times[times.Size() / 2];
}

Camera TopCamera(const Vec3& eye, const Vec3& at, f32 far_plane)
{
    Camera camera = MakeLookAtCamera(eye, at, Vec3(0.0f, 1.0f, 0.0f));
    camera.aspect = 16.0f / 9.0f;
    camera.far_plane = far_plane;
    return camera;
}

// ---- cull: a million instances
// ------------------------------------------------------------------------------------------

InstanceSetPtr MakeSet(u32 count, f64 side)
{
    InstanceSetDesc desc;
    desc.has_data = false;
    desc.max_instances = count;
    InstanceSetPtr set = InstanceSet::Create(desc);
    Rng rng;
    for (u32 i = 0; i < count; ++i)
    {
        InstanceTransform t;
        t.position = Vec3d(rng.Unit() * side, rng.Unit() * 20.0, rng.Unit() * side);
        InstanceId id;
        set->Add(t, nullptr, &id);
    }
    return set;
}

struct CullResultSummary
{
    u32 emitted = 0;
    u32 runs = 0;
    u32 chunks_skipped = 0;
};

CullResultSummary CullOnce(const InstanceSet& set, jobs::Scheduler* scheduler, f64 side, InstanceCullResult* result)
{
    const f32 mid = static_cast<f32>(side * 0.5);
    const Camera camera = TopCamera(Vec3(mid, 60.0f, mid - 400.0f), Vec3(mid, 10.0f, mid + 200.0f), 3000.0f);
    InstanceCullParams p;
    p.frustum = Frustum::FromViewProjection(camera.ViewProjection());
    p.render_origin = Vec3d(0.0, 0.0, 0.0);
    p.camera_position = camera.position;
    p.mesh_radius = 1.0f;
    p.max_distance = 0.0f;
    CullInstances(set, p, scheduler, result);
    CullResultSummary s;
    s.emitted = result->stats.instances_emitted;
    s.runs = result->stats.runs;
    s.chunks_skipped = result->stats.chunks_skipped;
    return s;
}

// Returns the milliseconds at 1 thread, and prints the rows. `ok` is false when the result differs between thread
// counts.
f64 SceneCull(u32 instances, bool print, bool* ok)
{
    const f64 side = 4000.0;
    const f64 t0 = NowMs();
    const InstanceSetPtr set = MakeSet(instances, side);
    if (print)
    {
        out::Print("cull: %u instances made in %.0f ms; the set holds %.1f MiB (%.1f bytes an instance)\n", instances,
                   NowMs() - t0, static_cast<f64>(set->MemoryBytes()) / (1024.0 * 1024.0),
                   static_cast<f64>(set->MemoryBytes()) / static_cast<f64>(instances));
    }
    f64 single = 0.0;
    CullResultSummary first;
    const u32 thread_counts[5] = {1, 2, 4, 8, 16};
    for (u32 threads : thread_counts)
    {
        jobs::Scheduler::Config sc;
        sc.participants = threads;
        sc.pin = false;
        jobs::Scheduler scheduler(sc);
        InstanceCullResult result;
        CullResultSummary s;
        const f64 ms = MedianMs(9, [&]() { s = CullOnce(*set, &scheduler, side, &result); });
        if (threads == 1)
        {
            single = ms;
            first = s;
        }
        else if (s.emitted != first.emitted || s.runs != first.runs)
        {
            *ok = false;
        }
        if (print)
        {
            out::Print("cull: %2u threads %8.3f ms (%u visible in %u runs, %u chunks skipped)\n", threads, ms,
                       s.emitted, s.runs, s.chunks_skipped);
        }
    }
    return single;
}

// ---- terrain: tile selection on 8192 x 8192
// -----------------------------------------------------------------------------

f64 SceneTerrain(bool print, u32* tiles_out)
{
    const u32 side = 8192;
    TerrainTilesDesc desc;
    desc.samples_x = side;
    desc.samples_z = side;
    desc.cell_size = 1.0f;
    desc.range_min = 0.0f;
    desc.range_max = 400.0f;
    desc.blocks_x = (side - 1 + 7) / 8;
    desc.blocks_z = desc.blocks_x;
    desc.block_min.Resize(static_cast<usize>(desc.blocks_x) * desc.blocks_z);
    desc.block_max.Resize(desc.block_min.Size());
    for (u32 z = 0; z < desc.blocks_z; ++z)
    {
        for (u32 x = 0; x < desc.blocks_x; ++x)
        {
            const u32 h = static_cast<u32>(((x / 16) * 73856093u ^ (z / 16) * 19349663u) % 40000u) + 4000u;
            desc.block_min[static_cast<usize>(z) * desc.blocks_x + x] = static_cast<u16>(h);
            desc.block_max[static_cast<usize>(z) * desc.blocks_x + x] = static_cast<u16>(h + 900u);
        }
    }
    TerrainTiles tiles;
    const f64 t0 = NowMs();
    if (!tiles.Build(desc))
    {
        out::PrintError("render_features_bench: the 8192 x 8192 tile tree was refused\n");
        return -1.0;
    }
    if (print)
    {
        out::Print("terrain: %u levels, %u x %u tiles, quadtree built in %.0f ms\n", tiles.LevelCount(), tiles.TilesX(),
                   tiles.TilesZ(), NowMs() - t0);
    }
    const Camera camera = TopCamera(Vec3(4096.0f, 120.0f, 3500.0f), Vec3(4096.0f, 60.0f, 4600.0f), 6000.0f);
    const Frustum frustum = Frustum::FromViewProjection(camera.ViewProjection());
    TerrainSelectSettings settings;
    settings.max_distance = 0.0f;
    Vector<TerrainTile> selected;
    const f64 ms = MedianMs(200, [&]() { tiles.Select(camera.position, &frustum, settings, &selected); });
    *tiles_out = static_cast<u32>(selected.Size());
    if (print)
    {
        out::Print("terrain: selection %.4f ms, %u tiles to draw (one draw each)\n", ms, *tiles_out);
    }
    return ms;
}

// ---- hierarchy: a 100,000-node update with 1% dirty
// ---------------------------------------------------------------------

InstanceTreePtr MakeForestTree(u32 nodes, jobs::Scheduler* scheduler)
{
    TreeMeshInfo info;
    info.bounds = Aabb(Vec3(-1, -1, -1), Vec3(1, 1, 1));
    InstanceTreeBuilder builder(Span<const TreeMeshInfo>(&info, 1));
    Rng rng;
    u32 made = 0;
    while (made + 6 <= nodes)
    {
        TreeNodeDesc root;
        root.position = Vec3d(rng.Unit() * 2000.0, 0.0, rng.Unit() * 2000.0);
        root.mesh = 0;
        TreeNodeId id;
        builder.AddRoot(root, &id);
        ++made;
        TreeNodeId parent = id;
        for (u32 k = 0; k < 5; ++k)
        {
            TreeNodeDesc child;
            child.position = Vec3d(0.1 * static_cast<f64>(k), 1.5, 0.0);
            child.mesh = 0;
            TreeNodeId cid;
            builder.AddChild(k < 2 ? id : parent, child, &cid);
            parent = cid;
            ++made;
        }
    }
    return builder.Finish(scheduler);
}

f64 SceneHierarchy(u32 nodes, u32 threads, bool print, u32* updated_out, f64* memory_per_node)
{
    jobs::Scheduler::Config sc;
    sc.participants = threads;
    sc.pin = false;
    jobs::Scheduler scheduler(sc);
    const f64 t0 = NowMs();
    InstanceTreePtr tree = MakeForestTree(nodes, &scheduler);
    if (!tree)
    {
        return -1.0;
    }
    *memory_per_node = static_cast<f64>(tree->MemoryBytes()) / static_cast<f64>(tree->NodeCount());
    if (print && threads == 1)
    {
        out::Print("hierarchy: %u nodes (%u roots) built in %.0f ms; %.1f bytes a node\n", tree->NodeCount(),
                   tree->RootCount(), NowMs() - t0, *memory_per_node);
    }
    // 1% of the nodes move, picked by a stride so every run touches the same ones.
    const u32 step = 100;
    u32 updated = 0;
    const f64 ms = MedianMs(
        15,
        [&]()
        {
            for (u32 i = 0; i < tree->NodeCount(); i += step)
            {
                const InstanceTreeNode& n = tree->Node(i);
                tree->SetLocal(tree->IdAt(i), Vec3d(n.local.position.x + 0.01, n.local.position.y, n.local.position.z),
                               n.local.rotation, n.local.scale);
            }
            updated = tree->UpdateWorldTransforms(&scheduler);
        });
    *updated_out = updated;
    if (print)
    {
        out::Print("hierarchy: %2u threads: 1%% dirty (%u nodes set), %u nodes recomposed in %.3f ms\n", threads,
                   (tree->NodeCount() + step - 1) / step, updated, ms);
    }
    return ms;
}

// ---- sky: the cost of the sky pass
// --------------------------------------------------------------------------------------

demo::DemoSettings* g_demo_settings = nullptr;

int SceneSky(const RenderSettings& settings)
{
    jobs::Scheduler::Config sc;
    sc.pin = false;
    jobs::Scheduler scheduler(sc);
    FeaturesWorld world;
    WorldOptions wo;
    wo.trees = 20000;
    wo.rocks = 100;
    if (!world.Build(wo, &scheduler))
    {
        out::PrintError("render_features_bench: %s\n", world.Error());
        return 1;
    }
    Array<char, 64> adapter;
    rhi::DeviceCreateInfo base;
    g_demo_settings->FillDeviceCreateInfo(&base);
    rhi::DeviceCreateInfo info;
    MakeDeviceCreateInfo(settings, base, adapter.Data(), &info);
    rhi::DevicePtr device = rhi::CreateDevice(info);
    if (device == nullptr)
    {
        out::Print("sky: no device, skipped\n");
        return 0;
    }
    rhi::TextureDesc td;
    td.width = 1920;
    td.height = 1080;
    td.format = rhi::Format::RGBA8_SRGB;
    td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc;
    const rhi::TextureHandle target = device->CreateTexture(td);
    {
        SceneFrame frame;
        FeaturesGpu gpu;
        SceneFrameSetup fs;
        fs.initial_settings = &settings;
        fs.device = device.Get();
        fs.max_objects = 1024;
        fs.max_lights = 8;
        fs.scheduler = &scheduler;
        GpuOptions go;
        if (!target.IsValid() || !frame.Create(fs) ||
            !gpu.Create(device.Get(), frame.Materials(), frame.Templates(), &world, go))
        {
            out::PrintError("render_features_bench: the frame or the scene could not be made: %s\n", gpu.Error());
            return 1;
        }
        FrameTarget ft;
        ft.texture = target;
        ft.width = 1920;
        ft.height = 1080;
        ft.format = rhi::Format::RGBA8_SRGB;
        ft.final_state = rhi::ResourceState::CopySrc;
        const char* names[3] = {"no sky (a flat colour)", "procedural sky", "cube map sky"};
        f64 base_ms = 0.0;
        for (u32 mode = 0; mode < 3; ++mode)
        {
            SkySettings sky = gpu.Sky();
            if (mode == 0)
            {
                sky.mode = SkyMode::Color;
                sky.cube = GpuTexturePtr();
            }
            else
            {
                gpu.SetCubeSky(mode == 2);
                sky = gpu.Sky();
            }
            frame.SetSky(sky, 1.0f);
            Camera view = world.ChaseCamera(16.0f / 9.0f);
            ft.initial = rhi::ResourceState::Undefined;
            for (int warm = 0; warm < 20; ++warm)
            {
                frame.Render(&gpu.Scene(), &view, &ft);
                ft.initial = rhi::ResourceState::CopySrc;
            }
            device->WaitIdle();
            const f64 ms = MedianMs(41,
                                    [&]()
                                    {
                                        frame.Render(&gpu.Scene(), &view, &ft);
                                        device->WaitIdle();
                                    });
            if (mode == 0)
            {
                base_ms = ms;
            }
            out::Print("sky: %-24s %7.3f ms a frame at 1920 x 1080 (%+.3f ms against none)\n", names[mode], ms,
                       ms - base_ms);
        }
        device->WaitIdle();
        gpu.Destroy();
        frame.Destroy();
    }
    if (target.IsValid())
    {
        device->DestroyTexture(target);
    }
    return 0;
}

// ---- check: generous bounds
// ---------------------------------------------------------------------------------------------

bool Within(const char* what, f64 measured, f64 limit)
{
    const bool ok = measured <= limit;
    out::Print("check: %s %.3f (limit %.3f) %s\n", what, measured, limit, ok ? "ok" : "FAILED");
    return ok;
}

int SceneCheck()
{
    bool ok = true;
    const f64 cull_ms = SceneCull(1000000, false, &ok);
    u32 tiles = 0;
    const f64 terrain_ms = SceneTerrain(false, &tiles);
    u32 updated1 = 0;
    u32 updated16 = 0;
    f64 per_node = 0.0;
    const f64 hier1 = SceneHierarchy(100000, 1, false, &updated1, &per_node);
    const f64 hier16 = SceneHierarchy(100000, 16, false, &updated16, &per_node);
    if (updated1 != updated16 || tiles == 0)
    {
        out::PrintError("check: the results differ between thread counts (%u and %u nodes), or no tile was chosen\n",
                        updated1, updated16);
        ok = false;
    }
    ok = Within("cull of 1,000,000 instances at 1 thread, ms", cull_ms, 6.0) && ok;
    ok = Within("tile selection on 8192 x 8192, ms", terrain_ms, 0.05) && ok;
    ok = Within("hierarchy update of 100,000 nodes, 1% dirty, 1 thread, ms", hier1, 1.0) && ok;
    ok = Within("hierarchy update of 100,000 nodes, 1% dirty, 16 threads, ms", hier16, 2.0) && ok;
    out::Print("render_features_bench: check %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}

const char kUsage[] = "usage: %s [--scene cull|terrain|hierarchy|sky|memory|check|all] [--instances N] [--nodes N]\n";

}  // namespace

int main(int argc, char** argv)
{
    ::vse::rhi::RegisterCompiledBackends();
    rhi::LogRhi.SetVerbosity(LogVerbosity::Warn);
    demo::DemoSettings demo_settings;
    Array<char, 512> usage_text;
    const os::CommandLine<HeapAllocator> native(HeapAllocator::Default(), argc, argv);
    FormatTo(usage_text.Data(), usage_text.Size(), kUsage, native.ProgramName().Data());
    if (!demo_settings.Begin("render_features_bench", usage_text.Data(), argc, argv))
    {
        return 2;
    }
    g_demo_settings = &demo_settings;
    static const os::CommandLineOption kOptions[] = {{"scene", true}, {"instances", true}, {"nodes", true}};
    const os::CommandLine<HeapAllocator> line(HeapAllocator::Default(), demo_settings.RestCount(),
                                              demo_settings.RestArgs(), Span<const os::CommandLineOption>(kOptions, 3));
    if (!line.Ok() || line.ArgCount() != 0)
    {
        out::PrintError(kUsage, line.ProgramName().Data());
        return 2;
    }
    StringView scene("all");
    line.TryGetOption("scene", scene);
    long instances = 1000000;
    long nodes = 100000;
    StringView value;
    if ((line.TryGetOption("instances", value) && (instances = CStrToLong(value.Data())) < 1000) ||
        (line.TryGetOption("nodes", value) && (nodes = CStrToLong(value.Data())) < 100))
    {
        out::PrintError(kUsage, line.ProgramName().Data());
        return 2;
    }
    const int finish = demo_settings.Finish();
    if (finish >= 0)
    {
        return finish;
    }
    const bool all = scene == StringView("all");
    if (scene == StringView("check"))
    {
        return SceneCheck();
    }
    int code = 0;
    if (all || scene == StringView("cull") || scene == StringView("memory"))
    {
        bool ok = true;
        SceneCull(static_cast<u32>(instances), true, &ok);
        if (!ok)
        {
            out::PrintError("render_features_bench: the cull differs between thread counts\n");
            code = 1;
        }
    }
    if (all || scene == StringView("terrain"))
    {
        u32 tiles = 0;
        SceneTerrain(true, &tiles);
    }
    if (all || scene == StringView("hierarchy") || scene == StringView("memory"))
    {
        const u32 thread_counts[5] = {1, 2, 4, 8, 16};
        for (u32 threads : thread_counts)
        {
            u32 updated = 0;
            f64 per_node = 0.0;
            SceneHierarchy(static_cast<u32>(nodes), threads, true, &updated, &per_node);
        }
    }
    if (all || scene == StringView("sky"))
    {
        const RenderSettings settings = renderer::RenderSettingsFromSnapshot(demo_settings.GetRegistry(),
                                                                             *demo_settings.Get().Snapshot());
        code |= SceneSky(settings);
    }
    return code;
}
