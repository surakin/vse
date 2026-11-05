// Phase 4 Day 76: the render graph's barrier plan, `Execute`, and the transient pool. The plan tests are pure CPU (the
// expected barrier list is compared as text); the execution tests run on the null device, which validates every
// `before` state against the state it tracks and counts a mismatch as an error.
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "renderer/render_graph.h"
#include "renderer/transient_pool.h"
#include "rhi/null/null_device.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

namespace {

constexpr usize kArenaBytes = 8u << 20;

rhi::TextureDesc Tex(u32 w, u32 h, rhi::Format format, rhi::TextureUsage usage) {
    rhi::TextureDesc d;
    d.width = w;
    d.height = h;
    d.format = format;
    d.usage = usage;
    return d;
}

constexpr rhi::TextureUsage kColorSampled = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled;

struct Plan {
    ArenaAllocator arena{kArenaBytes};
    RenderGraph graph{&arena, RenderGraphDesc{}};

    GraphResource Transient(const char* name, rhi::TextureUsage usage = kColorSampled | rhi::TextureUsage::Storage | rhi::TextureUsage::CopySrc |
                                                                       rhi::TextureUsage::CopyDst) {
        return graph.CreateTexture(name, Tex(64, 64, rhi::Format::RGBA8_UNorm, usage));
    }
    GraphResource Import(const char* name, rhi::ResourceState initial, rhi::ResourceState final_state, bool read_only = false) {
        rhi::TextureHandle handle;
        handle.index = 1;
        handle.generation = 1;
        return graph.ImportTexture(name, handle, initial, final_state, read_only);
    }
    PassBuilder Pass(const char* name, PassFlags flags = PassFlags::None) { return graph.AddPass(name, nullptr, nullptr, flags); }

    std::string Barriers() {
        REQUIRE(graph.Compile());
        std::string text(graph.DumpBarriers(nullptr, 0), '\0');
        graph.DumpBarriers(text.data(), text.size() + 1);
        return text;
    }
    std::string Errors() const {
        std::string all;
        for (u32 i = 0; i < graph.ErrorCount(); ++i) {
            all += graph.Error(i);
            all += "\n";
        }
        return all;
    }
};

} // namespace

TEST_CASE("A transient written then sampled gets two barriers and the swapchain image ends in Present", "[renderer][graph][barrier]") {
    Plan p;
    const GraphResource t = p.Transient("t");
    const GraphResource bb = p.Import("bb", rhi::ResourceState::Undefined, rhi::ResourceState::Present);
    p.Pass("draw").Write(t, GraphUsage::ColorAttachment);
    p.Pass("post").Read(t, GraphUsage::Sampled).Write(bb, GraphUsage::ColorAttachment);
    CHECK(p.Barriers() ==
          "barrier at 0 \"t\" undefined -> color\n"
          "barrier at 1 \"t\" color -> shader-read\n"
          "barrier at 1 \"bb\" undefined -> color\n"
          "barrier at exit \"bb\" color -> present\n");
    CHECK(p.graph.StateAfterLastUse(t) == rhi::ResourceState::ShaderRead);
    CHECK(p.graph.StateAfterLastUse(bb) == rhi::ResourceState::ColorAttachment);
}

TEST_CASE("An import read and then written changes state once and returns to its final state", "[renderer][graph][barrier]") {
    Plan p;
    const GraphResource persist = p.Import("persist", rhi::ResourceState::ShaderRead, rhi::ResourceState::ShaderRead);
    const GraphResource bb = p.Import("bb", rhi::ResourceState::Undefined, rhi::ResourceState::Present);
    p.Pass("use").Read(persist, GraphUsage::Sampled).Write(bb, GraphUsage::ColorAttachment);
    p.Pass("update").Write(persist, GraphUsage::ColorAttachment);
    // The read finds the import already in ShaderRead; the later write is a write-after-read and changes the state, and
    // the exit barrier restores the declared final state.
    CHECK(p.Barriers() ==
          "barrier at 0 \"bb\" undefined -> color\n"
          "barrier at 1 \"persist\" shader-read -> color\n"
          "barrier at exit \"persist\" color -> shader-read\n"
          "barrier at exit \"bb\" color -> present\n");
}

TEST_CASE("A depth attachment turns into a depth read", "[renderer][graph][barrier]") {
    Plan p;
    const GraphResource depth = p.Transient("depth", rhi::TextureUsage::DepthAttachment | rhi::TextureUsage::Sampled);
    const GraphResource color = p.Transient("color");
    const GraphResource bb = p.Import("bb", rhi::ResourceState::Undefined, rhi::ResourceState::Present);
    p.Pass("depth-prepass").Write(depth, GraphUsage::DepthAttachment);
    p.Pass("forward").Read(depth, GraphUsage::DepthRead).Write(color, GraphUsage::ColorAttachment);
    p.Pass("present").Read(color, GraphUsage::Sampled).Write(bb, GraphUsage::ColorAttachment);
    CHECK(p.Barriers() ==
          "barrier at 0 \"depth\" undefined -> depth\n"
          "barrier at 1 \"depth\" depth -> depth-read\n"
          "barrier at 1 \"color\" undefined -> color\n"
          "barrier at 2 \"color\" color -> shader-read\n"
          "barrier at 2 \"bb\" undefined -> color\n"
          "barrier at exit \"bb\" color -> present\n");
}

TEST_CASE("Consecutive reads in one state share it and need no barrier", "[renderer][graph][barrier]") {
    Plan p;
    const GraphResource t = p.Transient("t");
    const GraphResource a = p.Transient("a");
    const GraphResource b = p.Transient("b");
    const GraphResource c = p.Transient("c");
    p.Pass("produce").Write(t, GraphUsage::ColorAttachment);
    p.Pass("read1").Read(t, GraphUsage::Sampled).Write(a, GraphUsage::ColorAttachment);
    p.Pass("read2").Read(t, GraphUsage::Sampled).Write(b, GraphUsage::ColorAttachment);
    p.Pass("read3").Read(t, GraphUsage::StorageRead).Read(a, GraphUsage::Sampled).Write(c, GraphUsage::ColorAttachment);
    p.Pass("copy").Read(t, GraphUsage::CopySrc).Read(b, GraphUsage::Sampled).Read(c, GraphUsage::Sampled).Write(p.Import("out", rhi::ResourceState::Undefined, rhi::ResourceState::CopySrc), GraphUsage::CopyDst);
    const std::string text = p.Barriers();
    // "t" is written once and read in shader-read three times (sampled, sampled, storage-read) with one barrier, then once
    // more as a copy source.
    CHECK(text.find("\"t\" undefined -> color") != std::string::npos);
    CHECK(text.find("\"t\" color -> shader-read") != std::string::npos);
    CHECK(text.find("\"t\" shader-read -> copy-src") != std::string::npos);
    u32 t_barriers = 0;
    for (u32 i = 0; i < p.graph.BarrierCount(); ++i) t_barriers += p.graph.BarrierAt(i).resource.index == t.index ? 1 : 0;
    CHECK(t_barriers == 3);
}

TEST_CASE("A write followed by a write in the same state still gets a barrier", "[renderer][graph][barrier]") {
    Plan p;
    const GraphResource t = p.Transient("t");
    const GraphResource out = p.Import("out", rhi::ResourceState::Undefined, rhi::ResourceState::CopySrc);
    p.Pass("fill").Write(t, GraphUsage::StorageWrite);
    p.Pass("blur").ReadWrite(t, GraphUsage::StorageWrite);
    p.Pass("copy").Read(t, GraphUsage::CopySrc).Write(out, GraphUsage::CopyDst);
    CHECK(p.Barriers() ==
          "barrier at 0 \"t\" undefined -> shader-write\n"
          "barrier at 1 \"t\" shader-write -> shader-write\n"
          "barrier at 2 \"t\" shader-write -> copy-src\n"
          "barrier at 2 \"out\" undefined -> copy-dst\n"
          "barrier at exit \"out\" copy-dst -> copy-src\n");
}

TEST_CASE("An import no pass uses is still brought to its final state", "[renderer][graph][barrier]") {
    Plan p;
    p.Import("bb", rhi::ResourceState::Undefined, rhi::ResourceState::Present);
    p.Import("same", rhi::ResourceState::ShaderRead, rhi::ResourceState::ShaderRead);
    CHECK(p.Barriers() == "barrier at exit \"bb\" undefined -> present\n");
}

TEST_CASE("A pass's attachments are all transitioned before it", "[renderer][graph][barrier]") {
    Plan p;
    const GraphResource c0 = p.Transient("c0");
    const GraphResource c1 = p.Transient("c1");
    const GraphResource depth = p.Transient("depth", rhi::TextureUsage::DepthAttachment);
    const GraphResource out = p.Import("out", rhi::ResourceState::Undefined, rhi::ResourceState::CopySrc);
    p.Pass("gbuffer").Write(c0, GraphUsage::ColorAttachment).Write(c1, GraphUsage::ColorAttachment).Write(depth, GraphUsage::DepthAttachment);
    p.Pass("use").Read(c0, GraphUsage::Sampled).Read(c1, GraphUsage::Sampled).Write(out, GraphUsage::CopyDst);
    REQUIRE(p.graph.Compile());
    u32 at_zero = 0;
    for (u32 i = 0; i < p.graph.BarrierCount(); ++i) {
        const GraphBarrier b = p.graph.BarrierAt(i);
        if (b.position == 0) {
            CHECK(b.before == rhi::ResourceState::Undefined);
            ++at_zero;
        }
        if (i > 0) CHECK(b.position >= p.graph.BarrierAt(i - 1).position); // ordered by position
    }
    CHECK(at_zero == 3);
}

TEST_CASE("A transient used in a way its description does not allow is a compile error", "[renderer][graph][barrier]") {
    Plan p;
    const GraphResource t = p.Transient("t", rhi::TextureUsage::ColorAttachment); // not sampled
    const GraphResource bb = p.Import("bb", rhi::ResourceState::Undefined, rhi::ResourceState::Present);
    p.Pass("draw").Write(t, GraphUsage::ColorAttachment);
    p.Pass("post").Read(t, GraphUsage::Sampled).Write(bb, GraphUsage::ColorAttachment);
    CHECK_FALSE(p.graph.Compile());
    REQUIRE(p.graph.ErrorCount() == 1);
    CHECK(p.Errors() == "pass 'post' uses transient texture 't' as sampled, but its description lacks the matching usage flag\n");
}

namespace {

// ---- execution on the null device ----

// A pass that begins and ends a rendering pass on one color attachment (and a depth attachment when given). The null
// device checks the attachments' states when the pass begins, so a missing barrier is an error.
struct DrawPass {
    GraphResource color[3];
    u32 color_count = 0;
    GraphResource depth;
    u32 ran = 0;
};

void RunDraw(const PassExecuteContext& context, void* user) {
    DrawPass* pass = static_cast<DrawPass*>(user);
    rhi::RenderingDesc rd;
    for (u32 i = 0; i < pass->color_count; ++i) rd.color[i].texture = context.graph->ResolvedTexture(pass->color[i]);
    rd.color_count = pass->color_count;
    if (pass->depth.IsValid()) rd.depth.texture = context.graph->ResolvedTexture(pass->depth);
    context.list->BeginRendering(rd);
    context.list->EndRendering();
    ++pass->ran;
}

} // namespace

TEST_CASE("A deferred-shaped graph executes on the null device with zero errors, frame after frame", "[renderer][graph][barrier]") {
    rhi::NullDevice* device = rhi::CreateNullDevice();
    {
        TransientPool pool(device, TransientPoolDesc{});
        const u32 w = 128;
        const u32 h = 96;
        rhi::TextureDesc out_desc = Tex(w, h, rhi::Format::RGBA8_UNorm, rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc);
        out_desc.debug_name = "backbuffer";
        const rhi::TextureHandle backbuffer = device->CreateTexture(out_desc);
        REQUIRE(backbuffer.IsValid());

        for (u32 frame = 0; frame < 4; ++frame) {
            ArenaAllocator arena(kArenaBytes);
            RenderGraph graph(&arena, RenderGraphDesc{});
            pool.BeginFrame(frame);
            const rhi::TextureUsage rt = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled;
            const GraphResource albedo = graph.CreateTexture("albedo", Tex(w, h, rhi::Format::RGBA8_UNorm, rt));
            const GraphResource normal = graph.CreateTexture("normal", Tex(w, h, rhi::Format::RGBA16_Float, rt));
            const GraphResource material = graph.CreateTexture("material", Tex(w, h, rhi::Format::RGBA8_UNorm, rt));
            const GraphResource depth = graph.CreateTexture("depth", Tex(w, h, rhi::Format::D32_Float, rhi::TextureUsage::DepthAttachment | rhi::TextureUsage::Sampled));
            const GraphResource hdr = graph.CreateTexture("hdr", Tex(w, h, rhi::Format::RGBA16_Float, rt));
            const GraphResource out = graph.ImportTexture("backbuffer", backbuffer, rhi::ResourceState::Undefined, rhi::ResourceState::CopySrc, false);

            DrawPass gbuffer;
            gbuffer.color[0] = albedo;
            gbuffer.color[1] = normal;
            gbuffer.color[2] = material;
            gbuffer.color_count = 3;
            gbuffer.depth = depth;
            DrawPass lighting;
            lighting.color[0] = hdr;
            lighting.color_count = 1;
            DrawPass tonemap;
            tonemap.color[0] = out;
            tonemap.color_count = 1;

            graph.AddPass("gbuffer", &RunDraw, &gbuffer)
                .Write(albedo, GraphUsage::ColorAttachment)
                .Write(normal, GraphUsage::ColorAttachment)
                .Write(material, GraphUsage::ColorAttachment)
                .Write(depth, GraphUsage::DepthAttachment);
            graph.AddPass("lighting", &RunDraw, &lighting)
                .Read(albedo, GraphUsage::Sampled)
                .Read(normal, GraphUsage::Sampled)
                .Read(material, GraphUsage::Sampled)
                .Read(depth, GraphUsage::DepthRead)
                .Write(hdr, GraphUsage::ColorAttachment);
            graph.AddPass("tonemap", &RunDraw, &tonemap).Read(hdr, GraphUsage::Sampled).Write(out, GraphUsage::ColorAttachment);

            device->BeginFrame();
            rhi::CommandList* list = device->CreateCommandList();
            list->Begin();
            REQUIRE(graph.Execute(&pool, list));
            list->End();
            rhi::CommandList* lists[] = {list};
            REQUIRE(device->Submit(lists, 1) != 0);
            device->EndFrame();
            pool.EndFrame();

            CHECK(gbuffer.ran == 1);
            CHECK(lighting.ran == 1);
            CHECK(tonemap.ran == 1);
            CHECK(device->ErrorCount() == 0);
            // Nothing stays bound after the frame.
            CHECK_FALSE(graph.ResolvedTexture(albedo).IsValid());
        }
        const TransientPoolStats stats = pool.Stats();
        // Frame 0 created the five targets; the later frames reused them. The backbuffer is not pooled.
        CHECK(stats.textures_created == 5);
        CHECK(stats.texture_reuses == 15);
        CHECK(stats.textures_idle == 5);
        device->DestroyTexture(backbuffer);
    }
    rhi::DestroyDevice(device);
}

TEST_CASE("Non-overlapping transient lifetimes in one frame share a texture", "[renderer][graph][pool]") {
    rhi::NullDevice* device = rhi::CreateNullDevice();
    {
        TransientPool pool(device, TransientPoolDesc{});
        ArenaAllocator arena(kArenaBytes);
        RenderGraph graph(&arena, RenderGraphDesc{});
        pool.BeginFrame(0);
        const rhi::TextureUsage rt = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled;
        const rhi::TextureDesc d = Tex(32, 32, rhi::Format::RGBA8_UNorm, rt);
        const GraphResource t1 = graph.CreateTexture("t1", d);
        const GraphResource t2 = graph.CreateTexture("t2", d);
        const GraphResource t3 = graph.CreateTexture("t3", d);
        const rhi::TextureHandle target = device->CreateTexture(Tex(32, 32, rhi::Format::RGBA8_UNorm, rhi::TextureUsage::ColorAttachment));
        const GraphResource out = graph.ImportTexture("out", target, rhi::ResourceState::Undefined, rhi::ResourceState::ColorAttachment, false);
        DrawPass p1, p2, p3, p4;
        p1.color[0] = t1;
        p2.color[0] = t2;
        p3.color[0] = t3;
        p4.color[0] = out;
        p1.color_count = p2.color_count = p3.color_count = p4.color_count = 1;
        graph.AddPass("p1", &RunDraw, &p1).Write(t1, GraphUsage::ColorAttachment);
        graph.AddPass("p2", &RunDraw, &p2).Read(t1, GraphUsage::Sampled).Write(t2, GraphUsage::ColorAttachment);
        graph.AddPass("p3", &RunDraw, &p3).Read(t2, GraphUsage::Sampled).Write(t3, GraphUsage::ColorAttachment);
        graph.AddPass("p4", &RunDraw, &p4).Read(t3, GraphUsage::Sampled).Write(out, GraphUsage::ColorAttachment);
        device->BeginFrame();
        rhi::CommandList* list = device->CreateCommandList();
        list->Begin();
        REQUIRE(graph.Execute(&pool, list));
        list->End();
        rhi::CommandList* lists[] = {list};
        REQUIRE(device->Submit(lists, 1) != 0);
        device->EndFrame();
        pool.EndFrame();
        // t1 is dead after p2, so t3 (first used by p3) takes its texture: two textures serve three resources, and the
        // reused one was handed back in the state t1 was left in, which the null device checked (no errors).
        const TransientPoolStats stats = pool.Stats();
        CHECK(stats.textures_created == 2);
        CHECK(stats.texture_reuses == 1);
        CHECK(device->ErrorCount() == 0);
        device->DestroyTexture(target);
    }
    rhi::DestroyDevice(device);
}

TEST_CASE("Execute reports a transient it cannot allocate and puts the pool back as it was", "[renderer][graph][pool]") {
    rhi::NullDevice* device = rhi::CreateNullDevice();
    {
        TransientPool pool(device, TransientPoolDesc{});
        ArenaAllocator arena(kArenaBytes);
        RenderGraph graph(&arena, RenderGraphDesc{});
        pool.BeginFrame(0);
        const rhi::TextureUsage rt = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled;
        const GraphResource a = graph.CreateTexture("a", Tex(8, 8, rhi::Format::RGBA8_UNorm, rt));
        const GraphResource b = graph.CreateTexture("b", Tex(0, 0, rhi::Format::RGBA8_UNorm, rt)); // the device refuses a zero size
        const rhi::TextureHandle target = device->CreateTexture(Tex(8, 8, rhi::Format::RGBA8_UNorm, rhi::TextureUsage::ColorAttachment));
        const GraphResource out = graph.ImportTexture("out", target, rhi::ResourceState::Undefined, rhi::ResourceState::ColorAttachment, false);
        graph.AddPass("p1", nullptr, nullptr).Write(a, GraphUsage::ColorAttachment);
        graph.AddPass("p2", nullptr, nullptr).Read(a, GraphUsage::Sampled).Write(out, GraphUsage::ColorAttachment);
        graph.AddPass("p3", nullptr, nullptr, PassFlags::SideEffects).Write(b, GraphUsage::ColorAttachment);
        device->BeginFrame();
        rhi::CommandList* list = device->CreateCommandList();
        list->Begin();
        // `a` is used by p1 and p2 and is back in the pool, in ShaderRead, when p3's `b` cannot be created.
        CHECK_FALSE(graph.Execute(&pool, list));
        CHECK(graph.ErrorCount() == 0); // a runtime failure is not a declaration error
        CHECK(std::string(graph.ExecuteError()) == "transient texture 'b' could not be allocated");
        CHECK(pool.Stats().textures_live == 1);
        CHECK(pool.Stats().textures_idle == 1); // a went back to the pool
        CHECK(device->ErrorCount() == 1);      // the device refused the zero-size texture
        device->ResetErrors();
        list->End(); // abandoned: never submitted
        device->EndFrame();

        // The abandoned list's barriers never ran, so `a` must be back in the state it was taken in (Undefined). A second
        // frame that uses it would otherwise start a barrier from ShaderRead, which the device never saw (an error).
        ArenaAllocator arena2(kArenaBytes);
        RenderGraph graph2(&arena2, RenderGraphDesc{});
        pool.BeginFrame(1);
        const GraphResource a2 = graph2.CreateTexture("a2", Tex(8, 8, rhi::Format::RGBA8_UNorm, rt));
        const GraphResource out2 = graph2.ImportTexture("out", target, rhi::ResourceState::Undefined, rhi::ResourceState::ColorAttachment, false);
        DrawPass d1, d2;
        d1.color[0] = a2;
        d2.color[0] = out2;
        d1.color_count = d2.color_count = 1;
        graph2.AddPass("q1", &RunDraw, &d1).Write(a2, GraphUsage::ColorAttachment);
        graph2.AddPass("q2", &RunDraw, &d2).Read(a2, GraphUsage::Sampled).Write(out2, GraphUsage::ColorAttachment);
        device->BeginFrame();
        rhi::CommandList* list2 = device->CreateCommandList();
        list2->Begin();
        REQUIRE(graph2.Execute(&pool, list2));
        list2->End();
        rhi::CommandList* lists[] = {list2};
        REQUIRE(device->Submit(lists, 1) != 0);
        device->EndFrame();
        CHECK(device->ErrorCount() == 0);
        CHECK(pool.Stats().texture_reuses == 1); // `a` was handed out again
        device->DestroyTexture(target);
    }
    rhi::DestroyDevice(device);
}

TEST_CASE("The pool reuses by description and destroys what stays idle too long", "[renderer][pool]") {
    rhi::NullDevice* device = rhi::CreateNullDevice();
    {
        TransientPoolDesc pd;
        pd.max_idle_frames = 2;
        TransientPool pool(device, pd);
        const rhi::TextureDesc d = Tex(16, 16, rhi::Format::RGBA8_UNorm, rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled);
        rhi::TextureDesc other = d;
        other.width = 17;
        rhi::BufferDesc bd;
        bd.size = 256;
        bd.usage = rhi::BufferUsage::Storage;

        pool.BeginFrame(0);
        PooledTexture t;
        PooledTexture u;
        PooledBuffer b;
        REQUIRE(pool.AcquireTexture(d, &t));
        CHECK(t.state == rhi::ResourceState::Undefined);
        REQUIRE(pool.AcquireTexture(d, &u)); // the first is in use: a second texture
        CHECK(u.handle != t.handle);
        REQUIRE(pool.AcquireBuffer(bd, &b));
        pool.ReleaseTexture(t.handle, rhi::ResourceState::ShaderRead);
        pool.ReleaseTexture(u.handle, rhi::ResourceState::ColorAttachment);
        pool.ReleaseBuffer(b.handle, rhi::ResourceState::ShaderWrite);
        pool.EndFrame();
        CHECK(pool.Stats().textures_created == 2);
        CHECK(pool.Stats().buffers_created == 1);
        CHECK(device->Statistics().textures == 2);

        // Frame 1: an equal description gets an idle texture back, with the state it was left in. A different
        // description gets a new one.
        pool.BeginFrame(1);
        PooledTexture again;
        REQUIRE(pool.AcquireTexture(d, &again));
        CHECK((again.handle == t.handle || again.handle == u.handle));
        CHECK(again.state != rhi::ResourceState::Undefined);
        PooledTexture different;
        REQUIRE(pool.AcquireTexture(other, &different));
        CHECK(pool.Stats().textures_created == 3);
        CHECK(pool.Stats().texture_reuses == 1);
        pool.ReleaseTexture(again.handle, rhi::ResourceState::ShaderRead);
        pool.ReleaseTexture(different.handle, rhi::ResourceState::ShaderRead);
        pool.EndFrame();
        CHECK(pool.Stats().textures_destroyed == 0);

        // Frames 2 and 3 use nothing. A resource last used in frame 1 is destroyed once it has been idle for more than
        // two frames: at the end of frame 4. The one used in frame 0 and never again (the second `d` texture, if frame 1
        // took the first) goes at the end of frame 3.
        for (u64 f = 2; f <= 3; ++f) {
            pool.BeginFrame(f);
            pool.EndFrame();
        }
        CHECK(pool.Stats().textures_destroyed == 1);
        CHECK(pool.Stats().buffers_destroyed == 1);
        CHECK(device->Statistics().textures == 2);
        pool.BeginFrame(4);
        pool.EndFrame();
        CHECK(pool.Stats().textures_destroyed == 3);
        CHECK(pool.Stats().textures_live == 0);
        CHECK(device->Statistics().textures == 0);
        CHECK(device->Statistics().buffers == 0);
    }
    rhi::DestroyDevice(device);
}

TEST_CASE("A destroyed pool leaves no device resources behind", "[renderer][pool]") {
    rhi::NullDevice* device = rhi::CreateNullDevice();
    {
        TransientPool pool(device, TransientPoolDesc{});
        pool.BeginFrame(0);
        PooledTexture t;
        REQUIRE(pool.AcquireTexture(Tex(4, 4, rhi::Format::RGBA8_UNorm, rhi::TextureUsage::Sampled), &t));
        pool.ReleaseTexture(t.handle, rhi::ResourceState::ShaderRead);
        CHECK(device->Statistics().textures == 1);
    }
    CHECK(device->Statistics().textures == 0);
    rhi::DestroyDevice(device);
}

TEST_CASE("A full pool destroys its longest-idle texture to make room", "[renderer][pool]") {
    rhi::NullDevice* device = rhi::CreateNullDevice();
    {
        TransientPoolDesc pd;
        pd.max_textures = 2;
        pd.max_buffers = 1;
        TransientPool pool(device, pd);
        const rhi::TextureUsage usage = rhi::TextureUsage::Sampled;
        PooledTexture t[3];
        pool.BeginFrame(0);
        REQUIRE(pool.AcquireTexture(Tex(4, 4, rhi::Format::RGBA8_UNorm, usage), &t[0]));
        pool.BeginFrame(1);
        REQUIRE(pool.AcquireTexture(Tex(8, 8, rhi::Format::RGBA8_UNorm, usage), &t[1]));
        pool.ReleaseTexture(t[0].handle, rhi::ResourceState::ShaderRead);
        pool.ReleaseTexture(t[1].handle, rhi::ResourceState::ShaderRead);
        // Both entries are idle and the pool is full; the 4x4 one was used earlier, so it is the one destroyed.
        pool.BeginFrame(2);
        REQUIRE(pool.AcquireTexture(Tex(16, 16, rhi::Format::RGBA8_UNorm, usage), &t[2]));
        CHECK(pool.Stats().textures_destroyed == 1);
        CHECK(pool.Stats().textures_live == 2);
        // Everything in use and full: the request fails instead.
        PooledTexture extra;
        REQUIRE(pool.AcquireTexture(Tex(8, 8, rhi::Format::RGBA8_UNorm, usage), &extra)); // reuses the idle 8x8
        CHECK_FALSE(pool.AcquireTexture(Tex(32, 32, rhi::Format::RGBA8_UNorm, usage), &t[0]));
        pool.ReleaseTexture(t[2].handle, rhi::ResourceState::ShaderRead);
        pool.ReleaseTexture(extra.handle, rhi::ResourceState::ShaderRead);
    }
    rhi::DestroyDevice(device);
}

TEST_CASE("A transient buffer goes through the same barriers and the pool across frames", "[renderer][graph][pool]") {
    rhi::NullDevice* device = rhi::CreateNullDevice();
    {
        TransientPool pool(device, TransientPoolDesc{});
        rhi::BufferDesc bd;
        bd.size = 1024;
        bd.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::CopySrc | rhi::BufferUsage::CopyDst;
        rhi::BufferDesc out_desc;
        out_desc.size = 1024;
        out_desc.usage = rhi::BufferUsage::CopyDst;
        const rhi::BufferHandle out_buffer = device->CreateBuffer(out_desc);
        REQUIRE(out_buffer.IsValid());
        for (u64 frame = 0; frame < 3; ++frame) {
            ArenaAllocator arena(kArenaBytes);
            RenderGraph graph(&arena, RenderGraphDesc{});
            pool.BeginFrame(frame);
            const GraphResource scratch = graph.CreateBuffer("scratch", bd);
            const GraphResource out = graph.ImportBuffer("out", out_buffer, rhi::ResourceState::Undefined, rhi::ResourceState::CopyDst, false);
            graph.AddPass("write", nullptr, nullptr).Write(scratch, GraphUsage::StorageWrite);
            graph.AddPass("copy", nullptr, nullptr).Read(scratch, GraphUsage::CopySrc).Write(out, GraphUsage::CopyDst);
            REQUIRE(graph.Compile());
            CHECK(graph.BarrierCount() == 3);
            device->BeginFrame();
            rhi::CommandList* list = device->CreateCommandList();
            list->Begin();
            REQUIRE(graph.Execute(&pool, list));
            list->End();
            rhi::CommandList* lists[] = {list};
            REQUIRE(device->Submit(lists, 1) != 0);
            device->EndFrame();
            pool.EndFrame();
            CHECK(device->ErrorCount() == 0);
        }
        CHECK(pool.Stats().buffers_created == 1);
        CHECK(pool.Stats().buffer_reuses == 2);
        device->DestroyBuffer(out_buffer);
    }
    rhi::DestroyDevice(device);
}
