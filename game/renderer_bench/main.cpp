// renderer_bench: records the Phase 5 performance baseline. Prints one line per measurement; nothing here is asserted
// in CI. See docs/guide/renderer.md ("Performance notes") for what the numbers mean.
//
//   renderer_bench [--section graph|frame|gpu|lights|pool|text|ui|all] [--rhi null|vulkan|dx12|metal|auto] [--frames N]
//   [--threads N]
//             [--objects 1000|10000|50000] [--single]
//
// `--rhi` is the graphics backend (default auto: Direct3D 12 on Windows, Vulkan elsewhere); `--device` is its older
// spelling.
//
// `frame` times a whole frame's CPU work (scene preparation, graph build and compile, recording, submission) at 1k, 10k
// and 50k objects; the device is idled after every frame, outside the timer, so the number is CPU work and not waiting
// for the GPU.

#include "sync/atomic.h"
#include "log/output.h"
#include "os/console.h"
#include "cstr/cstr.h"

#include "algorithm.h"
#include "debug/program_setup.h"
#include "demo_frame.h"
#include "demo_scene.h"
#include "io/path.h"
#include "jobs/scheduler.h"
#include "mem/arena_allocator.h"
#include "os/os.h"
#include "renderer/render_graph.h"
#include "renderer/text_renderer.h"
#include "rhi/backends/backends.h"
#include "rhi/backend_registry.h"
#include "rhi/null/null_device.h"
#include "rhi/rhi.h"
#include "rhi_option.h"
#include "text/font.h"
#include "text/glyph_atlas.h"
#include "text/layout.h"
#include "renderer/glyph_atlas_texture.h"
#include "renderer/transient_pool.h"
#include "renderer/ui_renderer.h"
#include "ui/draw_list.h"
#include "ui/layout.h"
#include "ui/testing/harness.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/list_box.h"
#include "types.h"
#include "cstr/mem_ops.h"
#include "cstr/number.h"

using namespace vse;
using namespace vse::renderer;

namespace
{

constexpr u32 kWidth = 1280;
constexpr u32 kHeight = 720;

double Now()
{
    return static_cast<double>(os::MonotonicTimeNanos()) * 1e-9;
}

struct Samples
{
    double v[512];
    u32 n = 0;
    void Add(double x)
    {
        if (n < 512)
        {
            v[n++] = x;
        }
    }
    double Median()
    {
        if (n == 0)
        {
            return 0;
        }
        Sort(Span<double>(v, n));
        return v[n / 2];
    }
    double Min()
    {
        double m = v[0];
        for (u32 i = 1; i < n; ++i)
        {
            m = vse::Min(m, v[i]);
        }
        return m;
    }
};

struct Options
{
    const char* section = "all";
    rhi::Backend backend = rhi::Backend::Auto;  // --rhi
    bool null_device = false;                   // --rhi null: the validating null device, without the registry
    u32 frames = 30;
    u32 threads = 0;  // 0: the scheduler's default
    u32 objects = 0;  // 0: the frame section runs 1k, 10k and 50k
    bool single_only = false;
};

// ---- graph: build + compile time against the pass count ----

void BenchGraph()
{
    out::Print("# graph: build + compile of a chain of N passes, each reading the previous pass's transient texture\n");
    const u32 counts[] = {10, 100, 1000};
    for (u32 passes : counts)
    {
        ArenaAllocator arena(64u << 20);
        Samples build, compile;
        for (int rep = 0; rep < 50; ++rep)
        {
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
            for (u32 i = 0; i < passes; ++i)
            {
                const GraphResource cur = graph.CreateTexture("t", td);
                PassBuilder pass = graph.AddPass("p", nullptr, nullptr, PassFlags::SideEffects);
                if (i > 0)
                {
                    pass.Read(prev, GraphUsage::Sampled);
                }
                pass.Write(cur, GraphUsage::ColorAttachment);
                prev = cur;
            }
            const double t1 = Now();
            if (!graph.Compile())
            {
                out::Print("graph: compile failed: %s\n", graph.Error(0));
                return;
            }
            const double t2 = Now();
            build.Add(t1 - t0);
            compile.Add(t2 - t1);
        }
        out::Print("graph  passes %5u  build %8.1f us  compile %8.1f us  (%.2f us/pass total)\n", passes,
                   build.Median() * 1e6, compile.Median() * 1e6, (build.Median() + compile.Median()) * 1e6 / passes);
    }
}

// ---- frame: one device, one scene, repeated frames ----

struct Run
{
    double cpu_median_ms = 0;
    double cpu_min_ms = 0;
    double total_median_ms = 0;  // submission to the end of WaitIdle: the frame's GPU work included
    double total_min_ms = 0;
    GBufferStats gbuffer;
    LightSummary lights;
    u32 volume_draws = 0;
    TransientPoolStats pool;
    u64 frames = 0;
    bool created = false;
    bool ok = false;
};

rhi::DevicePtr MakeDevice(const Options& o, rhi::NullDevicePtr* null_holder)
{
    if (o.null_device)
    {
        *null_holder = rhi::CreateNullDevice();
        return rhi::DevicePtr();
    }
    rhi::DeviceCreateInfo info;
    info.backend = o.backend;
    info.pipeline_cache = false;
    return rhi::CreateDevice(info);
}

Run RunScene(const Options& o, u32 objects, u32 lights, bool use_jobs)
{
    Run run;
    rhi::NullDevicePtr null_holder;
    rhi::DevicePtr gpu = MakeDevice(o, &null_holder);
    rhi::Device* device = o.null_device ? static_cast<rhi::Device*>(null_holder.Get()) : gpu.Get();
    if (device == nullptr)
    {
        out::Print("no device\n");
        return run;
    }
    jobs::Scheduler::Config config;
    config.participants = o.threads;
    UniquePtr<jobs::Scheduler> scheduler;
    if (use_jobs)
    {
        scheduler = MakeUnique<jobs::Scheduler>(config);
    }
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
        if (target.IsValid() && frame.Create(fs) && scene.Create(device, frame.Materials(), frame.Templates(), so))
        {
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
            Samples total;
            bool all_ok = true;
            for (u32 i = 0; i < o.frames + 5; ++i)
            {
                frame.SetTime(0.5f + 0.016f * static_cast<f32>(i), 0.016f);
                const double t0 = Now();
                const FrameStatus status = frame.Render(&scene.Scene(), &camera, &ft);
                const double t1 = Now();
                device->WaitIdle();
                const double t2 = Now();
                if (status != FrameStatus::Ok)
                {
                    all_ok = false;
                }
                if (i >= 5)
                {
                    cpu.Add(t1 - t0);
                    total.Add(t2 - t0);
                }
            }
            run.cpu_median_ms = cpu.Median() * 1e3;
            run.cpu_min_ms = cpu.Min() * 1e3;
            run.total_median_ms = total.Median() * 1e3;
            run.total_min_ms = total.Min() * 1e3;
            run.gbuffer = frame.GBufferStatistics();
            run.lights = frame.Lights();
            run.volume_draws = frame.PointDraws();
            run.pool = frame.PoolStats();
            run.frames = frame.Stats().frames_rendered;
            run.created = true;
            run.ok = all_ok;
        }
        else
        {
            out::Print("could not create the scene for %u objects\n", objects);
        }
        device->WaitIdle();
        scene.Destroy();
        frame.Destroy();
        if (target.IsValid())
        {
            device->DestroyTexture(target);
        }
    }
    scheduler.Reset();
    return run;
}

void BenchFrame(const Options& o)
{
    out::Print("# frame: CPU time of a whole frame at %ux%u, 300 point lights, device idled outside the timer\n",
               kWidth, kHeight);
    const u32 counts[] = {1000, 10000, 50000};
    for (u32 objects : counts)
    {
        if (o.objects != 0 && o.objects != objects)
        {
            continue;
        }
        for (int j = 0; j < (o.single_only ? 1 : 2); ++j)
        {
            const Run r = RunScene(o, objects, 300, j == 1);
            out::Print("frame  objects %6u  %-7s  median %8.3f ms  min %8.3f ms  per object %6.3f us  %s\n", objects,
                       j == 0 ? "single" : "jobs", r.cpu_median_ms, r.cpu_min_ms, r.cpu_median_ms * 1e3 / objects,
                       r.ok ? "ok" : "NOT OK");
            if (j == 0)
            {
                out::Print(
                    "       visible %u  drawn %u  draw calls %u  pipeline binds %u  material binds %u  objects per draw %.2f\n",
                    r.gbuffer.visible, r.gbuffer.drawn_objects, r.gbuffer.draws, r.gbuffer.pipeline_binds,
                    r.gbuffer.material_binds,
                    r.gbuffer.draws != 0 ? static_cast<double>(r.gbuffer.drawn_objects) / r.gbuffer.draws : 0.0);
            }
        }
    }
}

// The whole frame with its GPU work (glTF coverage Day 1): no point lights, so the G-buffer pass is most of the GPU
// time, and a change to the geometry shader shows as a change of these medians (the RHI has no timestamp queries, so
// the pass itself is not timed alone).
void BenchGpu(const Options& o)
{
    out::Print("# gpu: a whole frame at %ux%u from submission to the end of WaitIdle, no point lights\n", kWidth,
               kHeight);
    const u32 counts[] = {1000, 10000};
    for (u32 objects : counts)
    {
        if (o.objects != 0 && o.objects != objects)
        {
            continue;
        }
        const Run r = RunScene(o, objects, 0, false);
        out::Print("gpu    objects %6u  median %8.3f ms  min %8.3f ms  %s\n", objects, r.total_median_ms,
                   r.total_min_ms, r.ok ? "ok" : "NOT OK");
    }
}

void BenchLights(const Options& o)
{
    out::Print("# lights: CPU time of a frame of 1000 objects as the point light count grows\n");
    const u32 counts[] = {0, 100, 1000, 4000, 16000};
    double base = 0;
    for (u32 lights : counts)
    {
        const Run r = RunScene(o, 1000, lights, false);
        if (!r.created || !r.ok)
        {
            out::Print("lights %6u  NOT OK, stopping\n", lights);
            return;
        }
        if (lights == 0)
        {
            base = r.cpu_median_ms;
        }
        out::Print("lights %6u  median %8.3f ms  volume draws %5u  per light (over 0 lights) %7.3f us\n", lights,
                   r.cpu_median_ms, r.volume_draws, lights != 0 ? (r.cpu_median_ms - base) * 1e3 / lights : 0.0);
    }
}

void BenchPool(const Options& o)
{
    Options p = o;
    p.frames = 100;
    const Run r = RunScene(p, 1000, 300, false);
    if (!r.created || !r.ok)
    {
        out::Print("pool   NOT OK\n");
        return;
    }
    const u32 uses = r.pool.textures_created + r.pool.texture_reuses;
    out::Print("# pool: after %llu frames of the deferred pipeline\n", static_cast<unsigned long long>(r.frames));
    out::Print("pool   textures created %u  reused %u  hit rate %.1f%%  destroyed %u  live %u\n",
               r.pool.textures_created, r.pool.texture_reuses, uses != 0 ? 100.0 * r.pool.texture_reuses / uses : 0.0,
               r.pool.textures_destroyed, r.pool.textures_live);
}

// ---- text: layout, atlas and quad building (CPU only; the null device stands in for the renderer) ----

void BenchText()
{
    out::Print("# text: Liberation Sans, CPU work, no GPU (layout, atlas lookups, quad building)\n");
    text::FontLibrary library;
    text::Font font;
    const char* file = RENDERER_DEMO_FONT_FILE;
    if (!library.Initialize().Ok() ||
        !text::Font::OpenFile(library, io::Path(StringView(file, CStrLength(file))), text::FontOptions{}, &font).Ok())
    {
        out::Print("text   NOT OK: the font did not open\n");
        return;
    }
    // 1000 characters of mixed text (letters, spaces, digits, punctuation, a few accents), about 6 characters per word.
    static const char kWords[] =
        "The quick brown fox jumps over 13 lazy dogs, caf\xC3\xA9 na\xC3\xAFve r\xC3\xA9sum\xC3\xA9; sphinx of black quartz judge my vow. ";
    Vector<char> body;
    while (body.Size() < 1000)
    {
        for (usize i = 0; i + 1 < sizeof(kWords) && body.Size() < 1000; ++i)
        {
            body.Push(kWords[i]);
        }
    }
    const StringView text_view(body.Data(), body.Size());
    for (u32 size : {16u, 32u, 64u})
    {
        text::TextStyle style;
        style.font = &font;
        style.pixel_size = size;
        text::TextLayout layout;
        text::LayoutOptions wrapped;
        wrapped.max_width = 600;
        Samples plain, wrap;
        for (int rep = 0; rep < 200; ++rep)
        {
            const double t0 = Now();
            (void)text::Layout(text_view, style, text::LayoutOptions{}, &layout);
            const double t1 = Now();
            (void)text::Layout(text_view, style, wrapped, &layout);
            const double t2 = Now();
            plain.Add(t1 - t0);
            wrap.Add(t2 - t1);
        }
        out::Print("text   layout %2u px  1000 chars: one line %8.1f us, wrapped at 600 px %8.1f us  (%zu glyphs)\n",
                   size, plain.Median() * 1e6, wrap.Median() * 1e6, static_cast<size_t>(layout.glyphs.Size()));
    }

    // Atlas: a miss rasterizes and packs; a hit is a hash lookup.
    {
        text::GlyphAtlas atlas;
        (void)font.SetPixelSize(32);
        text::GlyphEntry entry;
        u32 added = 0;
        u64 dirty_bytes = 0;
        const double m0 = Now();
        for (u32 cp = 33; cp < 127; ++cp)
        {
            if (atlas.GetOrAdd(font, font.FindGlyph(cp), &entry).Ok())
            {
                ++added;
            }
        }
        const double m1 = Now();
        text::Rect dirty;
        if (atlas.TakeDirty(0, &dirty))
        {
            dirty_bytes = static_cast<u64>(dirty.w) * dirty.h;
        }
        const double h0 = Now();
        u32 hits = 0;
        for (int rep = 0; rep < 100; ++rep)
        {
            for (u32 cp = 33; cp < 127; ++cp)
            {
                if (atlas.GetOrAdd(font, font.FindGlyph(cp), &entry).Ok())
                {
                    ++hits;
                }
            }
        }
        const double h1 = Now();
        out::Print(
            "text   atlas 32 px  miss %6.2f us/glyph (%u glyphs)  hit %6.3f us/glyph  dirty rectangle of the %u glyphs %llu bytes (%llu per glyph); a page upload is %u bytes\n",
            (m1 - m0) * 1e6 / added, added, (h1 - h0) * 1e6 / hits, added, static_cast<unsigned long long>(dirty_bytes),
            static_cast<unsigned long long>(dirty_bytes / (added != 0 ? added : 1)), 1024u * 1024u);
    }

    // Quad building: layout once, `Add` 1000 glyphs to the renderer per frame, warm atlas.
    {
        rhi::NullDevicePtr device = rhi::CreateNullDevice();
        text::GlyphAtlas atlas;
        renderer::TextRenderer renderer_text;
        if (!renderer_text.Create(device.Get(), &atlas, renderer::TextRendererDesc{}, nullptr))
        {
            out::Print("text   NOT OK: the text renderer was not created\n");
            return;
        }
        text::TextStyle style;
        style.font = &font;
        style.pixel_size = 32;
        text::TextLayout layout;
        (void)text::Layout(text_view, style, text::LayoutOptions{}, &layout);
        Samples add, draw;
        for (int rep = 0; rep < 200; ++rep)
        {
            renderer_text.BeginFrame();
            const double t0 = Now();
            renderer_text.Add(layout, style, 0, 0);
            const double t1 = Now();
            add.Add(t1 - t0);
            renderer_text.BeginFrame();
            const double t2 = Now();
            renderer_text.Draw(text_view, style, text::LayoutOptions{}, 0, 0);
            draw.Add(Now() - t2);
        }
        out::Print(
            "text   quads 32 px  %zu glyphs: Add of a laid-out text %8.1f us (%.3f us/1000 glyphs), Draw (layout + Add) %8.1f us\n",
            static_cast<size_t>(layout.glyphs.Size()), add.Median() * 1e6,
            add.Median() * 1e6 * 1000.0 / static_cast<double>(layout.glyphs.Size()), draw.Median() * 1e6);
        renderer_text.Destroy();
    }
}

// Counts every operator new while `g_counting` is set (the ui section's steady-state allocation check). The bench is
// never built with a sanitizer.
Atomic<long> g_new_count{0};
Atomic<bool> g_counting{false};

class NumberItems : public ui::ListModel
{
public:
    usize ItemCount() const override { return 100000; }
    StringView ItemText(usize i) override
    {
        usize n = 0;
        char digits[24];
        do
        {
            digits[n++] = static_cast<char>('0' + i % 10);
            i /= 10;
        } while (i > 0);
        usize len = 0;
        for (const char* p = "Item "; *p != '\0'; ++p)
        {
            text_[len++] = *p;
        }
        while (n > 0)
        {
            text_[len++] = digits[--n];
        }
        return StringView(text_, len);
    }

private:
    char text_[48];
};

// The tree for `count` widgets: rows of ten buttons in a vertical box. The surface is as tall as the tree, so nothing
// is clipped away.
void BenchUiTree(u32 count)
{
    const u32 rows = (count + 9) / 10;
    ui::Harness h(1280, static_cast<f32>(rows) * 28.0f + 8.0f, 1.0f, StringView(RENDERER_DEMO_FONT_FILE));
    if (!h.Valid())
    {
        out::Print("ui     NOT OK: the font did not open\n");
        return;
    }
    const double b0 = Now();
    UniquePtr<ui::BoxLayout> column = MakeUnique<ui::BoxLayout>(ui::Orientation::Vertical, StringView("column"));
    column->SetSpacing(4);
    for (u32 r = 0; r < rows; ++r)
    {
        UniquePtr<ui::BoxLayout> row = MakeUnique<ui::BoxLayout>(ui::Orientation::Horizontal, StringView("row"));
        row->SetSpacing(4);
        for (u32 c = 0; c < 10 && r * 10 + c < count; ++c)
        {
            row->AddChild(
                UniquePtr<ui::Widget>(MakeUnique<ui::Button>(StringView("b"), StringView("Button")).Release()));
        }
        column->AddChild(UniquePtr<ui::Widget>(row.Release()));
    }
    ui::Widget* column_raw = column.Get();
    ui::Rect full;
    full.x = 0;
    full.y = 0;
    full.w = 1280;
    full.h = static_cast<f32>(rows) * 28.0f + 8.0f;
    column->SetBounds(full);
    h.GetSurface().Root()->AddChild(UniquePtr<ui::Widget>(column.Release()));
    const double b1 = Now();
    h.Advance(0);
    const double b2 = Now();
    Samples layout, paint;
    for (int rep = 0; rep < 20; ++rep)
    {
        column_raw->MarkDirty(ui::kLayoutDirty | ui::kDrawDirty);
        const double t0 = Now();
        h.Advance(0.016f);
        const double t1 = Now();
        layout.Add(t1 - t0);
        column_raw->MarkDirty(ui::kDrawDirty);
        const double t2 = Now();
        h.Advance(0.016f);
        paint.Add(Now() - t2);
    }
    const ui::DrawList& list = h.List();
    out::Print(
        "ui     %5u widgets: build tree %8.2f ms, first frame %8.2f ms, relayout + repaint %8.2f ms, repaint only %8.2f ms  (%zu vertices, %zu indices)\n",
        count, (b1 - b0) * 1e3, (b2 - b1) * 1e3, layout.Median() * 1e3, paint.Median() * 1e3,
        static_cast<size_t>(list.vertices.Size()), static_cast<size_t>(list.indices.Size()));

    // Steady state: an idle frame (nothing dirty) and a repaint frame, after the warm-up above.
    g_new_count.Store(0);
    g_counting.Store(true);
    for (int rep = 0; rep < 20; ++rep)
    {
        h.Advance(0.016f);
    }
    const long idle = g_new_count.Load();
    g_new_count.Store(0);
    for (int rep = 0; rep < 20; ++rep)
    {
        column_raw->MarkDirty(ui::kDrawDirty);
        h.Advance(0.016f);
    }
    const long repaint = g_new_count.Load();
    g_counting.Store(false);
    out::Print("ui     %5u widgets: allocations over 20 idle frames %ld, over 20 repaint frames %ld\n", count, idle,
               repaint);
}

void BenchUi()
{
    out::Print("# ui: Liberation Sans, CPU work on the offscreen harness and the null device (no GPU)\n");
    for (u32 count : {1000u, 5000u})
    {
        BenchUiTree(count);
    }

    // A list box with 100,000 items, scrolled a little every frame.
    {
        ui::Harness h(300, 300, 1.0f, StringView(RENDERER_DEMO_FONT_FILE));
        if (!h.Valid())
        {
            return;
        }
        NumberItems items;
        UniquePtr<ui::ListBox> box = MakeUnique<ui::ListBox>(StringView("list"));
        ui::ListBox* list = box.Get();
        ui::Rect bounds;
        bounds.x = 0;
        bounds.y = 0;
        bounds.w = 200;
        bounds.h = 150;
        list->SetBounds(bounds);
        h.GetSurface().Root()->AddChild(UniquePtr<ui::Widget>(box.Release()));
        list->SetModel(&items);
        h.Advance(0);
        const f32 row = list->RowHeight();
        list->SetOffset(row * 50000);
        for (int i = 0; i < 100; ++i)
        {
            list->SetOffset(row * 50000 + row * static_cast<f32>(i % 10) / 20.0f);
            h.Advance(0.016f);
        }
        Samples frame;
        g_new_count.Store(0);
        g_counting.Store(true);
        for (int i = 0; i < 200; ++i)
        {
            list->SetOffset(row * 50000 + row * static_cast<f32>(i % 10) / 20.0f);
            const double t0 = Now();
            h.Advance(0.016f);
            frame.Add(Now() - t0);
        }
        g_counting.Store(false);
        out::Print(
            "ui     list box, 100000 items, scrolling inside the laid-out rows: %8.1f us per frame, %ld allocations over 200 frames  (%zu vertices)\n",
            frame.Median() * 1e6, g_new_count.Load(), static_cast<size_t>(h.List().vertices.Size()));
        // Scrolling by whole rows brings new rows into view (their text is laid out the first time).
        Samples scroll;
        for (int i = 0; i < 200; ++i)
        {
            list->SetOffset(row * static_cast<f32>(60000 + i * 3));
            const double t0 = Now();
            h.Advance(0.016f);
            scroll.Add(Now() - t0);
        }
        out::Print("ui     list box, 100000 items, three new rows per frame: %8.1f us per frame\n",
                   scroll.Median() * 1e6);
    }

    // The UI renderer's pass on the null device: AddToGraph + graph execute (recording only).
    {
        rhi::NullDevicePtr device = rhi::CreateNullDevice();
        ui::Harness h(1280, 28.0f * 100.0f + 8.0f, 1.0f, StringView(RENDERER_DEMO_FONT_FILE));
        if (!h.Valid())
        {
            return;
        }
        UniquePtr<ui::BoxLayout> column = MakeUnique<ui::BoxLayout>(ui::Orientation::Vertical, StringView("column"));
        for (u32 r = 0; r < 100; ++r)
        {
            UniquePtr<ui::BoxLayout> row = MakeUnique<ui::BoxLayout>(ui::Orientation::Horizontal, StringView("row"));
            for (u32 c = 0; c < 10; ++c)
            {
                row->AddChild(
                    UniquePtr<ui::Widget>(MakeUnique<ui::Button>(StringView("b"), StringView("Button")).Release()));
            }
            column->AddChild(UniquePtr<ui::Widget>(row.Release()));
        }
        ui::Rect full;
        full.x = 0;
        full.y = 0;
        full.w = 1280;
        full.h = 28.0f * 100.0f + 8.0f;
        column->SetBounds(full);
        h.GetSurface().Root()->AddChild(UniquePtr<ui::Widget>(column.Release()));
        h.Advance(0);
        GlyphAtlasTexture pages;
        UiRenderer ui_renderer;
        if (!pages.Create(device.Get(), &h.Atlas(), nullptr) ||
            !ui_renderer.Create(device.Get(), &pages, UiRendererDesc{}, nullptr))
        {
            out::Print("ui     NOT OK: the UI renderer was not created\n");
            return;
        }
        TransientPool pool(device.Get(), TransientPoolDesc{});
        rhi::TextureDesc td;
        td.width = 1280;
        td.height = 720;
        td.format = rhi::Format::RGBA8_UNorm;
        td.usage = rhi::TextureUsage::ColorAttachment;
        const rhi::TextureHandle target = device->CreateTexture(td);
        Samples record;
        u64 frame_number = 0;
        for (int rep = 0; rep < 50; ++rep)
        {
            ArenaAllocator arena(4u << 20);
            RenderGraph graph(&arena, RenderGraphDesc{});
            pool.BeginFrame(frame_number++);
            const GraphResource image = graph.ImportTexture("target", target, rhi::ResourceState::Undefined,
                                                            rhi::ResourceState::ColorAttachment, false);
            ui_renderer.AddToGraph(&graph, &h.List(), image, rhi::Format::RGBA8_UNorm, 1280, 720);
            device->BeginFrame();
            rhi::CommandList* cl = device->CreateCommandList();
            cl->Begin();
            const double t0 = Now();
            (void)graph.Execute(&pool, cl);
            record.Add(Now() - t0);
            cl->End();
            device->EndFrame();
            device->WaitIdle();
            pool.EndFrame();
        }
        out::Print("ui     renderer: graph build + execute of one UI pass over %zu vertices (1000 buttons): %8.1f us\n",
                   static_cast<size_t>(h.List().vertices.Size()), record.Median() * 1e6);
        device->DestroyTexture(target);
        ui_renderer.Release();
    }
}

// Parses a whole non-negative number; false for anything else.
bool ParseCount(const char* text, u32* out, u32 max)
{
    char* end = nullptr;
    const unsigned long v = CStrToULong(text, &end, 10);
    if (end == text || *end != '\0' || text[0] == '-' || v > max)
    {
        return false;
    }
    *out = static_cast<u32>(v);
    return true;
}

bool Section(const Options& o, const char* name)
{
    return CStrCompare(o.section, "all") == 0 || CStrCompare(o.section, name) == 0;
}

int Usage(const char* program)
{
    out::PrintError(
        "usage: %s [--section graph|frame|gpu|lights|pool|text|ui|all] [--rhi null|vulkan|dx12|metal|auto] [--frames 1-500] [--threads 0-64] [--objects 1000|10000|50000] [--single]\n",
        program);
    return 2;
}

}  // namespace

int main(int argc, char** argv)
{
    ::vse::rhi::RegisterCompiledBackends();  // before any device: the backends this build has
    const vse::debug::ProgramDebugSetup debug_setup = vse::debug::StartProgramDebugging(argc, argv);
    vse::debug::RunCrashTest(
        debug_setup.crash_test);  // --crash-test=segv|abort|assert|ensure, before any window or device
    Options o;
    static const vse::os::CommandLineOption kCliOptions[] = {
        {"section", true},           {"rhi", true},     {"device", true},  {"frames", true},
        {"objects", true},           {"single", false}, {"threads", true}, vse::debug::kCrashHandlerOption,
        vse::debug::kCrashTestOption};
    const vse::os::CommandLine<vse::HeapAllocator> line(vse::HeapAllocator::Default(), argc, argv,
                                                        vse::Span<const vse::os::CommandLineOption>(kCliOptions, 9));
    const char* program = line.ProgramName().Data();
    if (!line.Ok() || line.ArgCount() != 0)
    {
        vse::out::PrintError("%s: %s\n", program, line.Ok() ? "unexpected argument" : line.Error().Data());
        return Usage(program);
    }
    StringView value;
    if (line.TryGetOption("section", value))
    {
        o.section = value.Data();
        if (!Section(o, "graph") && !Section(o, "frame") && !Section(o, "lights") && !Section(o, "pool") &&
            !Section(o, "gpu") && !Section(o, "text") && !Section(o, "ui"))
        {
            return Usage(program);
        }
    }
    // --device is the old name of --rhi: one of them, not both.
    if (line.HasFlag("rhi") && line.HasFlag("device"))
    {
        return Usage(program);
    }
    if (line.TryGetOption("rhi", value) || line.TryGetOption("device", value))
    {
        if (!demo::ParseRhiBackend(value.Data(), &o.backend))
        {
            return Usage(program);
        }
        o.null_device = o.backend == rhi::Backend::Null;
    }
    if (line.TryGetOption("frames", value) && (!ParseCount(value.Data(), &o.frames, 500) || o.frames == 0))
    {
        return Usage(program);
    }
    if (line.TryGetOption("objects", value) && (!ParseCount(value.Data(), &o.objects, 50000) ||
                                                (o.objects != 1000 && o.objects != 10000 && o.objects != 50000)))
    {
        return Usage(program);
    }
    o.single_only = line.HasFlag("single");
    if (line.TryGetOption("threads", value) && !ParseCount(value.Data(), &o.threads, 64))
    {
        return Usage(program);
    }
    if (!o.null_device && (Section(o, "frame") || Section(o, "lights") || Section(o, "pool") || Section(o, "gpu")))
    {
        char why[256] = {};
        const rhi::BackendDescriptor* backend = rhi::ResolveBackend(o.backend, why, sizeof(why));
        if (backend == nullptr)
        {
            out::PrintError("renderer_bench: %s\n", why);
            return 1;
        }
        out::Print("# backend %s\n", backend->name);
    }
    if (Section(o, "graph"))
    {
        BenchGraph();
    }
    if (Section(o, "frame"))
    {
        BenchFrame(o);
    }
    if (Section(o, "lights"))
    {
        BenchLights(o);
    }
    if (Section(o, "gpu"))
    {
        BenchGpu(o);
    }
    if (Section(o, "pool"))
    {
        BenchPool(o);
    }
    if (Section(o, "text"))
    {
        BenchText();
    }
    if (Section(o, "ui"))
    {
        BenchUi();
    }
    return 0;
}

void* operator new(usize n)
{
    if (g_counting.Load(MemoryOrder::Relaxed))
    {
        g_new_count.FetchAdd(1, MemoryOrder::Relaxed);
    }
    void* p = MemAllocate(n != 0 ? n : 1);
    if (p == nullptr)
    {
        os::Abort();
    }
    return p;
}
void* operator new[](usize n)
{
    return operator new(n);
}
void operator delete(void* p) noexcept
{
    MemFree(p);
}
void operator delete(void* p, usize) noexcept
{
    MemFree(p);
}
void operator delete[](void* p) noexcept
{
    MemFree(p);
}
void operator delete[](void* p, usize) noexcept
{
    MemFree(p);
}
