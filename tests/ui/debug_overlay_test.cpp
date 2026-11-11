#include <catch2/catch_test_macros.hpp>

#include "sync/atomic.h"
#include "sync/thread.h"

#include "font_support.h"
#include "log/log.h"
#include "os/os.h"
#include "ui/debug_overlay.h"
#include "ui/surface.h"
#include "ui/testing/harness.h"

using namespace engine_placeholder;
using namespace engine_placeholder::ui;

namespace
{

LogChannel g_overlay_channel("overlay_test");

long OverlayDiscard(int, const void*, usize length)
{
    return static_cast<long>(length);
}

struct OverlayQuiet
{
    OverlayQuiet() { SetWriteSinkForTesting(&OverlayDiscard); }
    ~OverlayQuiet() { SetWriteSinkForTesting(&os::Write); }
};

void OverlayLog(const char* text)
{
    Log(g_overlay_channel, LogVerbosity::Warn, StringView(text));
}

struct OverlayRig
{
    OverlayRig() : h(640, 480, 1, StringView(UI_TEST_FONT_FILE))
    {
        UniquePtr<DebugOverlay> o = MakeUnique<DebugOverlay>();
        overlay = o.Get();
        KeyChord chord;
        chord.key = Key::Grave;
        attached = DebugOverlay::Attach(&h.GetSurface(), static_cast<UniquePtr<DebugOverlay>&&>(o), chord);
        h.Advance(0);
    }
    OverlayQuiet quiet;
    Harness h;
    DebugOverlay* overlay = nullptr;
    bool attached = false;
};

struct OverlayHits
{
    int calls = 0;
    String<HeapAllocator> argument{HeapAllocator::Default(), StringView()};
};

void OverlayEcho(void* user, StringView argument, LogCapture& out)
{
    OverlayHits* hits = static_cast<OverlayHits*>(user);
    ++hits->calls;
    hits->argument = String<HeapAllocator>(HeapAllocator::Default(), argument);
    out.Append(StringView("echoed"));
}

}  // namespace

TEST_CASE("The frame graph keeps the last samples and their statistics", "[ui][debug_overlay]")
{
    FrameGraph g;
    CHECK(g.Count() == 0);
    CHECK(g.Max() == 0);
    for (usize i = 0; i < FrameGraph::kSamples + 10; ++i)
    {
        g.Push(static_cast<f32>(i % 4));
    }
    CHECK(g.Count() == FrameGraph::kSamples);
    CHECK(g.Max() == 3.0f);
    g.Push(-5.0f);
    CHECK(g.Max() == 3.0f);
}

TEST_CASE("The log capture copies new lines and drops the oldest past its limit", "[ui][debug_overlay]")
{
    OverlayQuiet quiet;
    LogCapture c;
    CHECK(c.Poll() == 0);
    OverlayLog("first");
    OverlayLog("second line");
    CHECK(c.Poll() == 2);
    REQUIRE(c.Count() == 2);
    CHECK(c.VisibleLine(0).Find(StringView("first")) != c.VisibleLine(0).Length());
    c.SetFilter(StringView("second"));
    REQUIRE(c.VisibleCount() == 1);
    CHECK(c.VisibleLine(0).Find(StringView("second")) != c.VisibleLine(0).Length());
    c.SetFilter(StringView());
    for (usize i = 0; i < LogCapture::kMaxLines + 20; ++i)
    {
        c.Append(StringView("filler"));
    }
    CHECK(c.Count() == LogCapture::kMaxLines);
    CHECK(c.Dropped() > 0);
}

TEST_CASE("The log capture follows a thread that logs while the UI reads", "[ui][debug_overlay]")
{
    OverlayQuiet quiet;
    LogCapture c;
    Atomic<bool> done{false};
    Thread writer(
        [&done]
        {
            for (int i = 0; i < 200; ++i)
            {
                OverlayLog("from another thread");
            }
            done.Store(true);
        });
    usize seen = 0;
    while (!done.Load())
    {
        seen += c.Poll();
    }
    writer.Join();
    seen += c.Poll();
    CHECK(seen == 200);  // fewer than the ring holds, so none may be lost to a poll that meets a line half written
    CHECK(c.Count() <= LogCapture::kMaxLines);
}

TEST_CASE("The overlay is hidden until its key is pressed, and costs no updates while hidden", "[ui][debug_overlay]")
{
    OverlayRig rig;
    REQUIRE(rig.h.Valid());
    REQUIRE(rig.attached);
    CHECK_FALSE(rig.overlay->IsShown());
    CHECK_FALSE(rig.overlay->WantsUpdate());
    const u64 before = rig.overlay->Refreshes();
    OverlayLog("while hidden");
    for (int i = 0; i < 10; ++i)
    {
        rig.h.Advance(0.016f);
    }
    CHECK(rig.overlay->Refreshes() == before);
    rig.h.Key(Key::Grave);
    CHECK(rig.overlay->IsShown());
    rig.h.Advance(0.016f);
    CHECK(rig.overlay->Console()->Shown());
    rig.h.Key(Key::Grave);
    CHECK_FALSE(rig.overlay->IsShown());
}

TEST_CASE("The console shows what is logged while the overlay is shown", "[ui][debug_overlay]")
{
    OverlayRig rig;
    REQUIRE(rig.h.Valid());
    rig.overlay->Show(true);
    rig.h.Advance(0.016f);
    OverlayLog("visible line");
    rig.h.Advance(0.016f);
    CHECK(rig.overlay->Capture().Count() == 1);
    CHECK(rig.overlay->Console()->Shown());
}

TEST_CASE("Statistics feed the graph and the counters line", "[ui][debug_overlay]")
{
    OverlayRig rig;
    REQUIRE(rig.h.Valid());
    DebugStats stats;
    stats.frame_ms = 16.0f;
    stats.draw_calls = 42;
    rig.overlay->SetStats(stats);
    CHECK(rig.overlay->Graph()->Count() == 1);
    const StringView text = rig.overlay->Counters()->GetText();
    CHECK(text.Find(StringView("42")) != text.Length());
    CHECK(text.Find(StringView("62.5")) != text.Length());
}

TEST_CASE("Commands run from the command line and unknown ones are reported", "[ui][debug_overlay]")
{
    OverlayRig rig;
    REQUIRE(rig.h.Valid());
    OverlayHits hits;
    CHECK(rig.overlay->AddCommand(StringView("echo"), StringView("say it"), &OverlayEcho, &hits));
    CHECK_FALSE(rig.overlay->AddCommand(StringView("echo"), StringView("again"), &OverlayEcho, &hits));
    CHECK_FALSE(rig.overlay->AddCommand(StringView("help"), StringView("taken"), &OverlayEcho, &hits));
    CHECK_FALSE(rig.overlay->AddCommand(StringView("two words"), StringView("bad"), &OverlayEcho, &hits));
    rig.overlay->Show(true);
    rig.h.Advance(0.016f);
    rig.h.GetSurface().SetFocus(rig.overlay->CommandLine());
    rig.h.TypeText(StringView("echo  hello world"));
    rig.h.Key(Key::Enter);
    CHECK(hits.calls == 1);
    CHECK(hits.argument.View() == StringView("hello world"));
    CHECK(rig.overlay->CommandLine()->Text().Length() == 0);
    CHECK_FALSE(rig.overlay->Run(StringView("nonsense")));
    CHECK(rig.overlay->Run(StringView("help")));
    OverlayLog("before clear");
    rig.h.Advance(0.016f);
    const usize count = rig.overlay->Capture().Count();
    CHECK(rig.overlay->Run(StringView("clear")));
    CHECK(rig.overlay->Capture().Count() < count);
    rig.h.Advance(0.016f);
    CHECK(rig.overlay->Capture().Count() == 0);  // the lines logged before are not read again
}

TEST_CASE("The console keeps its place when the user has scrolled up", "[ui][debug_overlay]")
{
    OverlayRig rig;
    REQUIRE(rig.h.Valid());
    rig.overlay->Show(true);
    rig.h.Advance(0.016f);
    for (int i = 0; i < 100; ++i)
    {
        OverlayLog("filler");
    }
    rig.h.Advance(0.016f);
    const f32 bottom = rig.overlay->Console()->Offset();
    CHECK(bottom > 0);
    rig.overlay->Console()->SetOffset(0);
    OverlayLog("more");
    rig.h.Advance(0.016f);
    CHECK(rig.overlay->Console()->Offset() == 0);
}
