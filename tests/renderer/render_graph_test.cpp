// Phase 5 Day 75: render graph declaration and compilation. Pure CPU: no device is created (resources are described,
// not allocated; imports use a made-up handle).
#include "sync/atomic.h"
#include "build_defs.h"
#include "lang/new.h"

#include <catch2/catch_test_macros.hpp>

#include "algorithm.h"
#include "catch_engine_types.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "os/os.h"
#include "renderer/render_graph.h"
#include "test_text.h"
#include "types.h"
#include "cstr/mem_ops.h"
#include "os/console.h"

using namespace engine_placeholder;  // the allocation hooks below are global and name the engine types

// Counts every heap allocation in this executable so a test can prove a region allocates none. The ThreadSanitizer
// runtime defines its own operator new (a second definition fails to link), so that flavor skips the counting test.
#if ENGINE_SANITIZER_THREAD
#define RENDER_GRAPH_TEST_NO_NEW_COUNTER 1
#endif
#if ENGINE_SANITIZED
#define RENDER_GRAPH_TEST_SANITIZED 1
#endif

static Atomic<unsigned long> g_heap_allocations{0};

// Read by text_pass_test.cpp, which proves the text path allocates nothing per glyph once it is warm. False where the
// sanitizer's runtime owns operator new.
#ifdef RENDER_GRAPH_TEST_NO_NEW_COUNTER
bool RenderTestCountsAllocations()
{
    return false;
}
#else
bool RenderTestCountsAllocations()
{
    return true;
}
#endif
unsigned long RenderTestHeapAllocations()
{
    return g_heap_allocations.Load();
}

#ifndef RENDER_GRAPH_TEST_NO_NEW_COUNTER
// Every form is replaced so allocation and release always pair up (sanitizers check that). `align` is 0 for the plain
// forms. The C library wrappers have no aligned allocation, so a block that needs more than the 16 bytes malloc gives
// is over-allocated: its start is rounded up to `align` and the pointer of the underlying allocation is kept just
// before it, which the aligned delete forms (they are told the alignment) read back. Plain and 16-byte blocks are
// exact-size malloc blocks, so a sanitizer sees any overrun.
static constexpr usize kMallocAlign = 16;
static void* CountedAlloc(usize size, usize align)
{
    g_heap_allocations.FetchAdd(1, MemoryOrder::Relaxed);
    if (size == 0)
    {
        size = 1;
    }
    if (align <= kMallocAlign)
    {
        return MemAllocate(size);
    }
    void* raw = MemAllocate(size + align + sizeof(void*));
    if (raw == nullptr)
    {
        return nullptr;
    }
    const usize start = (reinterpret_cast<usize>(raw) + sizeof(void*) + align - 1) / align * align;
    void* block = reinterpret_cast<void*>(start);
    reinterpret_cast<void**>(block)[-1] = raw;
    return block;
}
static void AlignedFree(void* p, usize align) noexcept
{
    if (align <= kMallocAlign)
    {
        MemFree(p);
    }
    else if (p != nullptr)
    {
        MemFree(reinterpret_cast<void**>(p)[-1]);
    }
}
void* operator new(usize n)
{
    if (void* p = CountedAlloc(n, 0))
    {
        return p;
    }
    throw BadAlloc();
}
void* operator new[](usize n)
{
    if (void* p = CountedAlloc(n, 0))
    {
        return p;
    }
    throw BadAlloc();
}
void* operator new(usize n, const NoThrowTag&) noexcept
{
    return CountedAlloc(n, 0);
}
void* operator new[](usize n, const NoThrowTag&) noexcept
{
    return CountedAlloc(n, 0);
}
void* operator new(usize n, AlignVal a)
{
    if (void* p = CountedAlloc(n, static_cast<usize>(a)))
    {
        return p;
    }
    throw BadAlloc();
}
void* operator new[](usize n, AlignVal a)
{
    if (void* p = CountedAlloc(n, static_cast<usize>(a)))
    {
        return p;
    }
    throw BadAlloc();
}
void* operator new(usize n, AlignVal a, const NoThrowTag&) noexcept
{
    return CountedAlloc(n, static_cast<usize>(a));
}
void* operator new[](usize n, AlignVal a, const NoThrowTag&) noexcept
{
    return CountedAlloc(n, static_cast<usize>(a));
}
void operator delete(void* p) noexcept
{
    MemFree(p);
}
void operator delete[](void* p) noexcept
{
    MemFree(p);
}
void operator delete(void* p, usize) noexcept
{
    MemFree(p);
}
void operator delete[](void* p, usize) noexcept
{
    MemFree(p);
}
void operator delete(void* p, const NoThrowTag&) noexcept
{
    MemFree(p);
}
void operator delete[](void* p, const NoThrowTag&) noexcept
{
    MemFree(p);
}
void operator delete(void* p, AlignVal a) noexcept
{
    AlignedFree(p, static_cast<usize>(a));
}
void operator delete[](void* p, AlignVal a) noexcept
{
    AlignedFree(p, static_cast<usize>(a));
}
void operator delete(void* p, AlignVal a, const NoThrowTag&) noexcept
{
    AlignedFree(p, static_cast<usize>(a));
}
void operator delete[](void* p, AlignVal a, const NoThrowTag&) noexcept
{
    AlignedFree(p, static_cast<usize>(a));
}
void operator delete(void* p, usize, AlignVal a) noexcept
{
    AlignedFree(p, static_cast<usize>(a));
}
void operator delete[](void* p, usize, AlignVal a) noexcept
{
    AlignedFree(p, static_cast<usize>(a));
}
#endif

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

namespace
{

constexpr usize kArenaBytes = 8u << 20;

// Transient resources must be declared with the usage flags their uses need (Day 76); these tests care about the
// schedule, so they allow every use.
rhi::TextureDesc AnyTexture()
{
    rhi::TextureDesc d;
    d.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::Storage | rhi::TextureUsage::ColorAttachment |
              rhi::TextureUsage::DepthAttachment | rhi::TextureUsage::CopySrc | rhi::TextureUsage::CopyDst;
    return d;
}

rhi::BufferDesc AnyBuffer()
{
    rhi::BufferDesc d;
    d.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::Index | rhi::BufferUsage::Uniform |
              rhi::BufferUsage::Storage | rhi::BufferUsage::CopySrc | rhi::BufferUsage::CopyDst;
    return d;
}

struct RenderGraphTestFixture
{
    ArenaAllocator arena{kArenaBytes};
    RenderGraph graph;
    explicit RenderGraphTestFixture(const RenderGraphDesc& desc = RenderGraphDesc{}) : graph(&arena, desc) {}

    GraphResource Tex(const char* name) { return graph.CreateTexture(name, AnyTexture()); }
    GraphResource Buf(const char* name) { return graph.CreateBuffer(name, AnyBuffer()); }
    GraphResource Backbuffer(bool read_only = false)
    {
        rhi::TextureHandle handle;
        handle.index = 1;
        handle.generation = 1;
        return graph.ImportTexture("backbuffer", handle, rhi::ResourceState::Undefined, rhi::ResourceState::Present,
                                   read_only);
    }
    PassBuilder Pass(const char* name, PassFlags flags = PassFlags::None)
    {
        return graph.AddPass(name, nullptr, nullptr, flags);
    }

    engine_test::Text Dump() const
    {
        Vector<char> buffer = engine_test::Chars(graph.Dump(nullptr, 0) + 1);
        graph.Dump(buffer.Data(), buffer.Size());
        return engine_test::MakeText(engine_test::View(buffer, buffer.Size() - 1));
    }
    engine_test::Text Errors() const
    {
        engine_test::Text all = engine_test::MakeText();
        for (u32 i = 0; i < graph.ErrorCount(); ++i)
        {
            all.Append(StringView(graph.Error(i)));
            all.Append("\n");
        }
        return all;
    }
    engine_test::TextList Schedule() const
    {
        engine_test::TextList names;
        for (u32 i = 0; i < graph.ScheduleCount(); ++i)
        {
            names.Push(StringView(graph.PassName(graph.Scheduled(i))));
        }
        return names;
    }
};

}  // namespace

TEST_CASE("A linear chain compiles to its declaration order", "[renderer][graph]")
{
    RenderGraphTestFixture f;
    const GraphResource a = f.Tex("a");
    const GraphResource b = f.Tex("b");
    const GraphResource out = f.Backbuffer();
    f.Pass("first").Write(a, GraphUsage::ColorAttachment);
    f.Pass("second").Read(a, GraphUsage::Sampled).Write(b, GraphUsage::ColorAttachment);
    f.Pass("third").Read(b, GraphUsage::Sampled).Write(out, GraphUsage::ColorAttachment);
    REQUIRE(f.graph.Compile());
    CHECK(f.Schedule().SameAs({"first", "second", "third"}));
    CHECK(f.Dump() == "graph passes=3 resources=3 scheduled=3 culled=0\n"
                      "schedule: first, second, third\n"
                      "culled:\n"
                      "pass 0 \"first\" at 0\n"
                      "  write \"a\" color\n"
                      "pass 1 \"second\" at 1\n"
                      "  read \"a\" sampled\n"
                      "  write \"b\" color\n"
                      "pass 2 \"third\" at 2\n"
                      "  read \"b\" sampled\n"
                      "  write \"backbuffer\" color\n"
                      "resource 0 \"a\" texture transient use=0..1\n"
                      "resource 1 \"b\" texture transient use=1..2\n"
                      "resource 2 \"backbuffer\" texture import use=2..2\n"
                      "edge \"first\" -> \"second\" raw \"a\"\n"
                      "edge \"second\" -> \"third\" raw \"b\"\n");
}

TEST_CASE("A diamond keeps declaration order among independent passes", "[renderer][graph]")
{
    RenderGraphTestFixture f;
    const GraphResource src = f.Tex("src");
    const GraphResource l = f.Tex("left");
    const GraphResource r = f.Tex("right");
    const GraphResource out = f.Backbuffer();
    f.Pass("source").Write(src, GraphUsage::ColorAttachment);
    f.Pass("left").Read(src, GraphUsage::Sampled).Write(l, GraphUsage::ColorAttachment);
    f.Pass("right").Read(src, GraphUsage::Sampled).Write(r, GraphUsage::ColorAttachment);
    f.Pass("join").Read(l, GraphUsage::Sampled).Read(r, GraphUsage::Sampled).Write(out, GraphUsage::ColorAttachment);
    REQUIRE(f.graph.Compile());
    CHECK(f.Schedule().SameAs({"source", "left", "right", "join"}));
    CHECK(f.graph.FirstUse(src) == 0);
    CHECK(f.graph.LastUse(src) == 2);
    CHECK(f.graph.FirstUse(l) == 1);
    CHECK(f.graph.LastUse(l) == 3);
}

TEST_CASE("Passes nothing consumes are culled, unless they have side effects", "[renderer][graph]")
{
    RenderGraphTestFixture f;
    const GraphResource used = f.Tex("used");
    const GraphResource dead = f.Tex("dead");
    const GraphResource dead2 = f.Tex("dead2");
    const GraphResource probe = f.Buf("probe");
    const GraphResource out = f.Backbuffer();
    f.Pass("make_used").Write(used, GraphUsage::ColorAttachment);
    f.Pass("make_dead").Write(dead, GraphUsage::ColorAttachment);
    f.Pass("use_dead").Read(dead, GraphUsage::Sampled).Write(dead2, GraphUsage::ColorAttachment);
    f.Pass("readback", PassFlags::SideEffects).Read(used, GraphUsage::Sampled).Write(probe, GraphUsage::StorageWrite);
    f.Pass("present").Read(used, GraphUsage::Sampled).Write(out, GraphUsage::ColorAttachment);
    REQUIRE(f.graph.Compile());
    CHECK(f.Schedule().SameAs({"make_used", "readback", "present"}));
    CHECK(f.graph.IsCulled(PassId{1}));
    CHECK(f.graph.IsCulled(PassId{2}));
    CHECK(f.graph.FirstUse(dead) == kNeverUsed);
    CHECK(f.graph.LastUse(dead2) == kNeverUsed);
    CHECK(f.graph.FirstUse(probe) == 1);
    const engine_test::Text dump = f.Dump();
    CHECK(dump.View().StartsWith(StringView("graph passes=5 resources=5 scheduled=3 culled=2\n")));
    CHECK(dump.View().Contains(StringView("culled: make_dead, use_dead\n")));
    CHECK(dump.View().Contains(StringView("resource 1 \"dead\" texture transient unused\n")));
}

TEST_CASE("An overwritten result is dead; edges name write-after-write and write-after-read", "[renderer][graph]")
{
    RenderGraphTestFixture f;
    const GraphResource t = f.Tex("t");
    const GraphResource out = f.Backbuffer();
    f.Pass("clear_old").Write(t, GraphUsage::ColorAttachment);  // 0: overwritten before anyone reads it
    f.Pass("draw").Write(t, GraphUsage::ColorAttachment);       // 1: WAW after 0
    f.Pass("blit").Read(t, GraphUsage::Sampled).Write(out, GraphUsage::ColorAttachment);  // 2: RAW after 1
    f.Pass("redraw").Write(t, GraphUsage::ColorAttachment);  // 3: WAR after 2 (and nobody reads it: culled)
    REQUIRE(f.graph.Compile());
    CHECK(f.Schedule().SameAs({"draw", "blit"}));
    const engine_test::Text dump = f.Dump();
    CHECK(dump.View().Contains(StringView("edge \"clear_old\" -> \"draw\" waw \"t\"\n")));
    CHECK(dump.View().Contains(StringView("edge \"draw\" -> \"blit\" raw \"t\"\n")));
    CHECK(dump.View().Contains(StringView("edge \"blit\" -> \"redraw\" war \"t\"\n")));
}

TEST_CASE("ReadWrite keeps the earlier writer alive", "[renderer][graph]")
{
    RenderGraphTestFixture f;
    const GraphResource t = f.Tex("t");
    const GraphResource out = f.Backbuffer();
    f.Pass("clear").Write(t, GraphUsage::ColorAttachment);
    f.Pass("blend").ReadWrite(t, GraphUsage::ColorAttachment);
    f.Pass("present").Read(t, GraphUsage::Sampled).Write(out, GraphUsage::ColorAttachment);
    REQUIRE(f.graph.Compile());
    CHECK(f.Schedule().SameAs({"clear", "blend", "present"}));
}

TEST_CASE("Explicit dependencies reorder only what they must", "[renderer][graph]")
{
    RenderGraphTestFixture f;
    const GraphResource out = f.Backbuffer();
    const PassId a = f.Pass("a", PassFlags::SideEffects).Id();
    f.Pass("b", PassFlags::SideEffects);
    const PassId c = f.Pass("c", PassFlags::SideEffects).Id();
    f.graph.AddDependency(c, a);  // c before a
    (void)out;
    REQUIRE(f.graph.Compile());
    // b and c are ready first; the smallest index goes first, then a becomes ready.
    CHECK(f.Schedule().SameAs({"b", "c", "a"}));
    CHECK(f.Dump().View().Contains(StringView("edge \"c\" -> \"a\" after\n")));
}

TEST_CASE("A cycle is rejected and named", "[renderer][graph]")
{
    RenderGraphTestFixture f;
    const PassId a = f.Pass("alpha", PassFlags::SideEffects).Id();
    const PassId b = f.Pass("beta", PassFlags::SideEffects).Id();
    const PassId c = f.Pass("gamma", PassFlags::SideEffects).Id();
    f.Pass("bystander", PassFlags::SideEffects);
    f.graph.AddDependency(a, b);
    f.graph.AddDependency(b, c);
    f.graph.AddDependency(c, a);
    CHECK_FALSE(f.graph.Compile());
    REQUIRE(f.graph.ErrorCount() == 1);
    const engine_test::Text e = f.Errors();
    CHECK(e.View().StartsWith(StringView("dependency cycle: ")));
    CHECK(e.View().Contains(StringView("alpha")));
    CHECK(e.View().Contains(StringView("beta")));
    CHECK(e.View().Contains(StringView("gamma")));
    CHECK_FALSE(e.View().Contains(StringView("bystander")));
}

TEST_CASE("Mistakes in a graph are reported by Compile with names", "[renderer][graph]")
{
    SECTION("reading a transient resource nobody wrote")
    {
        RenderGraphTestFixture f;
        const GraphResource t = f.Tex("hdr");
        f.Pass("tonemap", PassFlags::SideEffects).Read(t, GraphUsage::Sampled);
        CHECK_FALSE(f.graph.Compile());
        CHECK(f.Errors() == "pass 'tonemap' reads 'hdr' but no earlier pass writes it\n");
    }
    SECTION("reading it before the pass that writes it")
    {
        RenderGraphTestFixture f;
        const GraphResource t = f.Tex("hdr");
        f.Pass("tonemap", PassFlags::SideEffects).Read(t, GraphUsage::Sampled);
        f.Pass("lighting").Write(t, GraphUsage::ColorAttachment);
        CHECK_FALSE(f.graph.Compile());
        CHECK(f.Errors() == "pass 'tonemap' reads 'hdr' but no earlier pass writes it\n");
    }
    SECTION("a read of an import is fine")
    {
        RenderGraphTestFixture f;
        const GraphResource in = f.Backbuffer(true);
        f.Pass("sample", PassFlags::SideEffects).Read(in, GraphUsage::Sampled);
        CHECK(f.graph.Compile());
    }
    SECTION("a ReadWrite of an import needs no earlier writer either")
    {
        RenderGraphTestFixture f;
        const GraphResource out = f.Backbuffer();
        f.Pass("blend").ReadWrite(out, GraphUsage::ColorAttachment);
        CHECK(f.graph.Compile());
        CHECK(f.Schedule().SameAs({"blend"}));  // it writes an import, so it stays
    }
    SECTION("writing a read-only import")
    {
        RenderGraphTestFixture f;
        const GraphResource in = f.Backbuffer(true);
        f.Pass("draw").Write(in, GraphUsage::ColorAttachment);
        CHECK_FALSE(f.graph.Compile());
        CHECK(f.Errors() == "pass 'draw' writes 'backbuffer', which is a read-only import\n");
    }
    SECTION("a usage that does not suit the access")
    {
        RenderGraphTestFixture f;
        const GraphResource t = f.Tex("t");
        f.Pass("p").Read(t, GraphUsage::ColorAttachment);
        f.Pass("q").Write(t, GraphUsage::Sampled);
        CHECK_FALSE(f.graph.Compile());
        CHECK(f.Errors() == "pass 'p': usage 'color' of 't' is a write, so it cannot be declared with read\n"
                            "pass 'q': usage 'sampled' of 't' is a read, so it cannot be declared with write\n");
    }
    SECTION("a usage that does not suit the resource kind")
    {
        RenderGraphTestFixture f;
        const GraphResource t = f.Tex("t");
        const GraphResource b = f.Buf("b");
        f.Pass("p").Read(t, GraphUsage::VertexBuffer).Read(b, GraphUsage::Sampled);
        CHECK_FALSE(f.graph.Compile());
        CHECK(f.Errors() == "pass 'p': usage 'vertex' does not apply to the texture 't'\n"
                            "pass 'p': usage 'sampled' does not apply to the buffer 'b'\n");
    }
    SECTION("the same resource twice in a pass")
    {
        RenderGraphTestFixture f;
        const GraphResource t = f.Tex("t");
        f.Pass("p").Write(t, GraphUsage::ColorAttachment).Read(t, GraphUsage::Sampled);
        CHECK_FALSE(f.graph.Compile());
        CHECK(f.Errors() == "pass 'p' declares 't' twice; use ReadWrite for one access that reads and writes\n");
    }
    SECTION("an access declared after the next pass was added")
    {
        RenderGraphTestFixture f;
        const GraphResource t = f.Tex("t");
        PassBuilder first = f.Pass("first");
        f.Pass("second");
        first.Write(t, GraphUsage::ColorAttachment);
        CHECK_FALSE(f.graph.Compile());
        CHECK(f.Errors() == "pass 'first': its accesses must be declared before the next pass is added\n");
    }
    SECTION("an invalid resource and an invalid import")
    {
        RenderGraphTestFixture f;
        f.Pass("p").Read(GraphResource{}, GraphUsage::Sampled);
        f.graph.ImportTexture("bad", rhi::TextureHandle{}, rhi::ResourceState::Undefined, rhi::ResourceState::Undefined,
                              false);
        CHECK_FALSE(f.graph.Compile());
        CHECK(f.graph.ErrorCount() == 2);
    }
    SECTION("tables that are full")
    {
        RenderGraphDesc small;
        small.max_passes = 1;
        small.max_resources = 1;
        small.max_accesses = 1;
        small.max_dependencies = 1;
        RenderGraphTestFixture f(small);
        const GraphResource t = f.Tex("t");
        f.Tex("u");
        PassBuilder p = f.Pass("p");
        p.Write(t, GraphUsage::ColorAttachment);
        f.Pass("q");
        const GraphResource v = f.graph.CreateTexture("v", AnyTexture());
        (void)v;
        CHECK_FALSE(f.graph.Compile());
        CHECK(f.graph.ErrorCount() == 3);
        CHECK(f.Errors().View().Contains(StringView("resource 'u' not added")));
        CHECK(f.Errors().View().Contains(StringView("pass 'q' not added")));
    }
    SECTION("errors beyond the stored limit are counted")
    {
        RenderGraphTestFixture f;
        for (int i = 0; i < 20; ++i)
        {
            f.Pass("p").Read(GraphResource{}, GraphUsage::Sampled);
        }
        CHECK_FALSE(f.graph.Compile());
        CHECK(f.graph.ErrorCount() == kMaxGraphErrors);
        CHECK(f.graph.DroppedErrors() == 20 - kMaxGraphErrors);
    }
    SECTION("dependencies that name nothing")
    {
        RenderGraphTestFixture f;
        const PassId a = f.Pass("a").Id();
        f.graph.AddDependency(a, a);
        f.graph.AddDependency(a, PassId{});
        CHECK_FALSE(f.graph.Compile());
        CHECK(f.Errors() == "pass 'a' cannot depend on itself\na dependency names a pass that does not exist\n");
    }
}

TEST_CASE("A graph in an arena too small to hold it is invalid", "[renderer][graph]")
{
    ArenaAllocator tiny(512);
    RenderGraph graph(&tiny, RenderGraphDesc{});
    CHECK_FALSE(graph.Valid());
    graph.AddPass("p", nullptr, nullptr);  // ignored, not a crash
    CHECK_FALSE(graph.Compile());
    CHECK(graph.PassCount() == 0);
}

TEST_CASE("Pass callbacks and their context are kept", "[renderer][graph]")
{
    RenderGraphTestFixture f;
    int token = 0;
    PassExecuteFn fn = [](const PassExecuteContext&, void* user) { ++*static_cast<int*>(user); };
    const PassId id = f.graph.AddPass("p", fn, &token, PassFlags::SideEffects).Id();
    REQUIRE(f.graph.Compile());
    CHECK(f.graph.PassCallback(id) == fn);
    CHECK(f.graph.PassUser(id) == &token);
    f.graph.PassCallback(id)(PassExecuteContext{}, f.graph.PassUser(id));
    CHECK(token == 1);
}

TEST_CASE("Usages map to resource states", "[renderer][graph]")
{
    CHECK(StateForUsage(GraphUsage::ColorAttachment) == rhi::ResourceState::ColorAttachment);
    CHECK(StateForUsage(GraphUsage::Sampled) == rhi::ResourceState::ShaderRead);
    CHECK(StateForUsage(GraphUsage::StorageWrite) == rhi::ResourceState::ShaderWrite);
    CHECK(StateForUsage(GraphUsage::DepthRead) == rhi::ResourceState::DepthRead);
    CHECK(StateForUsage(GraphUsage::IndexBuffer) == rhi::ResourceState::IndexBuffer);
    CHECK(UsageWrites(GraphUsage::CopyDst));
    CHECK_FALSE(UsageWrites(GraphUsage::CopySrc));
}

TEST_CASE("The dump can be measured and truncates safely; the dot file marks culled passes", "[renderer][graph]")
{
    RenderGraphTestFixture f;
    const GraphResource a = f.Tex("a");
    const GraphResource out = f.Backbuffer();
    f.Pass("make").Write(a, GraphUsage::ColorAttachment);
    f.Pass("dead \"quoted\"").Read(a, GraphUsage::Sampled);
    f.Pass("show").Read(a, GraphUsage::Sampled).Write(out, GraphUsage::ColorAttachment);
    CHECK(f.Dump().View().Contains(StringView("(not compiled)")));  // before Compile only passes and resources
    REQUIRE(f.graph.Compile());
    const usize need = f.graph.Dump(nullptr, 0);
    char small[16];
    CHECK(f.graph.Dump(small, sizeof(small)) == need);
    CHECK(StringView(small) == f.Dump().View().Substring(0, 15));

    Vector<char> dot_chars = engine_test::Chars(f.graph.WriteDot(nullptr, 0) + 1);
    f.graph.WriteDot(dot_chars.Data(), dot_chars.Size());
    const engine_test::Text dot = engine_test::MakeText(engine_test::View(dot_chars, dot_chars.Size() - 1));
    CHECK(dot == "digraph render_graph {\n"
                 "  rankdir=LR;\n"
                 "  node [shape=box];\n"
                 "  p0 [label=\"0: make\"];\n"
                 "  p1 [label=\"dead \\\"quoted\\\"\", style=dashed];\n"
                 "  p2 [label=\"1: show\"];\n"
                 "  p0 -> p1 [label=\"raw: a\"];\n"
                 "  p0 -> p2 [label=\"raw: a\"];\n"
                 "}\n");
}

TEST_CASE("Building and compiling a graph allocates nothing from the heap", "[renderer][graph]")
{
#ifdef RENDER_GRAPH_TEST_NO_NEW_COUNTER
    SKIP("the ThreadSanitizer runtime owns operator new");
#endif
    ArenaAllocator arena(kArenaBytes);
    RenderGraphDesc desc;
    desc.max_passes = 600;
    desc.max_resources = 600;
    desc.max_accesses = 4000;
    const unsigned long before = g_heap_allocations.Load();
    RenderGraph graph(&arena, desc);
    GraphResource previous = graph.CreateTexture("t0", AnyTexture());
    graph.AddPass("p0", nullptr, nullptr, PassFlags::SideEffects).Write(previous, GraphUsage::ColorAttachment);
    for (int i = 1; i < 500; ++i)
    {
        const GraphResource next = graph.CreateTexture("t", AnyTexture());
        graph.AddPass("p", nullptr, nullptr, PassFlags::SideEffects)
            .Read(previous, GraphUsage::Sampled)
            .Write(next, GraphUsage::ColorAttachment);
        previous = next;
    }
    const bool ok = graph.Compile();
    char text[64];
    graph.Dump(text, sizeof(text));
    const unsigned long after = g_heap_allocations.Load();
    REQUIRE(ok);
    CHECK(after == before);
    // The counter itself works: a heap allocation is seen.
    void* volatile canary = ::operator new(sizeof(int));  // volatile: the compiler may not elide the pair
    ::operator delete(canary);
    CHECK(g_heap_allocations.Load() > after);
}

namespace
{

// Small deterministic generator so the random graphs are the same on every run.
struct Lcg
{
    u64 state;
    u32 Next()
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<u32>(state >> 33);
    }
};

}  // namespace

TEST_CASE("A 1000-pass random DAG matches an independent reference", "[renderer][graph][perf]")
{
    constexpr u32 kPasses = 1000;
    Lcg rng{12345};
    // Pass i writes resource i. It reads up to three earlier passes' resources. Roughly one pass in five is a root.
    Vector<Vector<u32>> reads;
    reads.Resize(kPasses);
    Vector<bool> root;
    root.Resize(kPasses);
    for (u32 i = 0; i < kPasses; ++i)
    {
        root[i] = rng.Next() % 5 == 0;
        if (i == 0)
        {
            continue;
        }
        const u32 n = rng.Next() % 4;
        for (u32 k = 0; k < n; ++k)
        {
            const u32 from = rng.Next() % i;
            if (IndexOf(reads[i], from) == reads[i].Size())
            {
                reads[i].Push(from);
            }
        }
    }

    ArenaAllocator arena(64u << 20);
    RenderGraphDesc desc;
    desc.max_passes = kPasses;
    desc.max_resources = kPasses;
    desc.max_accesses = kPasses * 5;
    desc.max_dependencies = 4;
    RenderGraph graph(&arena, desc);
    Vector<engine_test::Text> names;
    names.Reserve(kPasses);  // the graph keeps the name pointers, so the texts must not move
    Vector<GraphResource> res;
    res.Resize(kPasses);
    for (u32 i = 0; i < kPasses; ++i)
    {
        names.Push(engine_test::Format("r%u", i));
        res[i] = graph.CreateTexture(names[i].CStr(), AnyTexture());
    }
    Vector<engine_test::Text> pnames;
    pnames.Reserve(kPasses);
    for (u32 i = 0; i < kPasses; ++i)
    {
        pnames.Push(engine_test::Format("p%u", i));
        PassBuilder b = graph.AddPass(pnames[i].CStr(), nullptr, nullptr,
                                      root[i] ? PassFlags::SideEffects : PassFlags::None);
        for (u32 from : Span<u32>(reads[i]))
        {
            b.Read(res[from], GraphUsage::Sampled);
        }
        b.Write(res[i], GraphUsage::ColorAttachment);
    }
    const u64 t0 = os::MonotonicTimeNanos();
    REQUIRE(graph.Compile());
    const u64 t1 = os::MonotonicTimeNanos();
    const double ms = static_cast<double>(t1 - t0) / 1e6;
    os::Print("compile of a %u-pass graph: %.3f ms, %u scheduled\n", kPasses, ms, graph.ScheduleCount());
#ifndef RENDER_GRAPH_TEST_SANITIZED
    CHECK(ms <
          100.0);  // about 0.2 ms here; sanitizer builds are too slow for a wall-clock bound, the figure is printed
#endif

    // Reference: liveness by reverse sweep (every dependency points backwards), then an O(n^2) stable topological sort.
    Vector<bool> live;
    live.Resize(kPasses);
    for (u32 i = 0; i < kPasses; ++i)
    {
        live[i] = root[i];
    }
    for (u32 i = kPasses; i-- > 0;)
    {
        if (!live[i])
        {
            continue;
        }
        for (u32 from : Span<u32>(reads[i]))
        {
            live[from] = true;
        }
    }
    Vector<u32> expected;
    Vector<bool> done;
    done.Resize(kPasses);
    for (;;)
    {
        u32 pick = kPasses;
        for (u32 i = 0; i < kPasses && pick == kPasses; ++i)
        {
            if (done[i] || !live[i])
            {
                continue;
            }
            bool ready = true;
            for (u32 from : Span<u32>(reads[i]))
            {
                ready = ready && done[from];
            }
            if (ready)
            {
                pick = i;
            }
        }
        if (pick == kPasses)
        {
            break;
        }
        done[pick] = true;
        expected.Push(pick);
    }
    REQUIRE(graph.ScheduleCount() == expected.Size());
    u32 mismatches = 0;
    for (u32 pos = 0; pos < expected.Size(); ++pos)
    {
        if (graph.Scheduled(pos).index != expected[pos])
        {
            ++mismatches;
        }
    }
    CHECK(mismatches == 0);
    u32 culled = 0;
    for (u32 i = 0; i < kPasses; ++i)
    {
        if (graph.IsCulled(PassId{i}) != !live[i])
        {
            ++mismatches;
        }
        if (!live[i])
        {
            ++culled;
        }
    }
    CHECK(mismatches == 0);
    CHECK(culled > 0);
    CHECK(culled < kPasses);
}

TEST_CASE("Long names are measured and truncated correctly in the dump", "[renderer][graph]")
{
    RenderGraphTestFixture f;
    const engine_test::Text long_name = engine_test::Repeated(900, 'x');
    const GraphResource t = f.Tex(long_name.CStr());
    f.Pass(long_name.CStr(), PassFlags::SideEffects).Write(t, GraphUsage::ColorAttachment);
    REQUIRE(f.graph.Compile());
    const usize need = f.graph.Dump(nullptr, 0);
    CHECK(need > 2 * long_name.Length());  // the name appears more than once; no line is cut short
    Vector<char> text = engine_test::Chars(need + 1);
    CHECK(f.graph.Dump(text.Data(), need + 1) == need);
    CHECK(engine_test::View(text, need).Contains(long_name.View()));
    char small[100];
    CHECK(f.graph.Dump(small, sizeof(small)) == need);
    CHECK(StringView(small).Length() == sizeof(small) - 1);
}

TEST_CASE("A graph keeps its descriptors and imports; recompiling after a change refreshes the schedule",
          "[renderer][graph]")
{
    RenderGraphTestFixture f;
    rhi::TextureDesc td;
    td.width = 64;
    td.height = 32;
    td.format = rhi::Format::RGBA8_UNorm;
    td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled;
    const GraphResource t = f.graph.CreateTexture("t", td);
    const GraphResource out = f.Backbuffer();
    CHECK(f.graph.TextureDescOf(t).width == 64);
    CHECK_FALSE(f.graph.IsImport(t));
    CHECK(f.graph.IsImport(out));
    CHECK(f.graph.ImportedTexture(out).IsValid());
    CHECK(f.graph.InitialState(out) == rhi::ResourceState::Undefined);
    CHECK(f.graph.FinalState(out) == rhi::ResourceState::Present);
    CHECK(f.graph.KindOf(t) == GraphResourceKind::Texture);

    f.Pass("make").Write(t, GraphUsage::ColorAttachment);
    f.Pass("show").Read(t, GraphUsage::Sampled).Write(out, GraphUsage::ColorAttachment);
    REQUIRE(f.graph.Compile());
    REQUIRE(f.graph.Compile());  // nothing changed: returns at once
    CHECK(f.graph.ScheduleCount() == 2);
    f.Pass("extra", PassFlags::SideEffects);
    REQUIRE(f.graph.Compile());
    CHECK(f.graph.ScheduleCount() == 3);
    CHECK(f.Schedule().SameAs({"make", "show", "extra"}));
}

TEST_CASE("An explicit dependency does not keep its predecessor alive", "[renderer][graph]")
{
    RenderGraphTestFixture f;
    const PassId sync = f.Pass("sync").Id();
    const PassId work = f.Pass("work", PassFlags::SideEffects).Id();
    f.graph.AddDependency(sync, work);
    REQUIRE(f.graph.Compile());
    CHECK(f.Schedule().SameAs({"work"}));
    CHECK(f.graph.IsCulled(sync));
}
