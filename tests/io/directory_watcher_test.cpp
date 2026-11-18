// Phase 27 Day 464: the directory watcher, the native backend and the polling one under the same cases.
#include <catch2/catch_test_macros.hpp>

#include "build_defs.h"
#include "io/directory_watcher.h"
#include "io/file.h"
#include "io/file_system.h"
#include "os/os.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "sync/atomic.h"
#include "sync/thread.h"
#include "temp_dir.h"

using namespace engine_placeholder;

namespace
{

io::WatchOptions Options(bool polling)
{
    io::WatchOptions options;
    options.force_polling = polling;
    options.poll_interval_ms = 10;
    return options;
}

void Write(const io::Path& path, StringView text)
{
    REQUIRE(io::WriteFile(path, text.Data(), text.Length()).Ok());
}

// Collects events until `done` says the list is enough or two seconds pass.
template <typename Done> Vector<io::WatchEvent> Gather(io::DirectoryWatcher* watcher, Done done)
{
    Vector<io::WatchEvent> events;
    const u64 start = os::MonotonicTimeNanos();
    while (!done(events) && os::MonotonicTimeNanos() - start < 2000ull * 1000000ull)
    {
        watcher->Wait(50);
        REQUIRE(watcher->Poll(&events).Ok());
    }
    return events;
}

bool Has(const Vector<io::WatchEvent>& events, io::WatchEventKind kind, StringView name)
{
    for (usize i = 0; i < events.Size(); ++i)
    {
        if (events[i].kind == kind && events[i].name.View() == name)
        {
            return true;
        }
    }
    return false;
}

bool HasName(const Vector<io::WatchEvent>& events, StringView name)
{
    for (usize i = 0; i < events.Size(); ++i)
    {
        if (events[i].name.View() == name)
        {
            return true;
        }
    }
    return false;
}

void RunCases(bool polling)
{
    io_test::TempDir dir;
    UniquePtr<io::DirectoryWatcher> watcher;
    REQUIRE(io::DirectoryWatcher::Open(dir.Path(), Options(polling), &watcher).Ok());
    if (polling)
    {
        CHECK(!watcher->IsNative());
    }
#if ENGINE_PLATFORM_LINUX
    else
    {
        CHECK(watcher->IsNative());  // inotify
    }
#endif

    SECTION("a created file is reported, then a rewrite of it")
    {
        Write(dir.Path().Join("a.lua"), "one");
        Vector<io::WatchEvent> events = Gather(watcher.Get(),
                                               [](const Vector<io::WatchEvent>& e) { return HasName(e, "a.lua"); });
        CHECK(HasName(events, "a.lua"));
        Write(dir.Path().Join("a.lua"), "two and longer");
        events = Gather(watcher.Get(),
                        [](const Vector<io::WatchEvent>& e) { return Has(e, io::WatchEventKind::Modified, "a.lua"); });
        CHECK(Has(events, io::WatchEventKind::Modified, "a.lua"));
    }

    SECTION("the editor save: a temporary file renamed over the target")
    {
        Write(dir.Path().Join("t.lua"), "old");
        Gather(watcher.Get(), [](const Vector<io::WatchEvent>& e) { return HasName(e, "t.lua"); });
        Write(dir.Path().Join("t.lua.tmp"), "new text");
        REQUIRE(io::Rename(dir.Path().Join("t.lua.tmp"), dir.Path().Join("t.lua")).Ok());
        const Vector<io::WatchEvent> events = Gather(watcher.Get(), [](const Vector<io::WatchEvent>& e)
                                                     { return HasName(e, "t.lua"); });
        CHECK(HasName(events, "t.lua"));
    }

    SECTION("a removed file")
    {
        Write(dir.Path().Join("gone.lua"), "x");
        Gather(watcher.Get(), [](const Vector<io::WatchEvent>& e) { return HasName(e, "gone.lua"); });
        REQUIRE(io::Remove(dir.Path().Join("gone.lua")).Ok());
        const Vector<io::WatchEvent> events = Gather(watcher.Get(), [](const Vector<io::WatchEvent>& e)
                                                     { return Has(e, io::WatchEventKind::Removed, "gone.lua"); });
        CHECK(Has(events, io::WatchEventKind::Removed, "gone.lua"));
    }

    SECTION("a name with UTF-8")
    {
        Write(dir.Path().Join("sk\xC3\xA5l.lua"), "x");
        const Vector<io::WatchEvent> events = Gather(watcher.Get(), [](const Vector<io::WatchEvent>& e)
                                                     { return HasName(e, "sk\xC3\xA5l.lua"); });
        CHECK(HasName(events, "sk\xC3\xA5l.lua"));
    }

    SECTION("a burst of files is reported, or one overflow: never silence")
    {
        for (u32 i = 0; i < 1000; ++i)
        {
            char name[32];
            FormatTo(name, sizeof(name), "f%u.lua", i);
            Write(dir.Path().Join(StringView(name, CStrLength(name))), "x");
        }
        const Vector<io::WatchEvent> events = Gather(
            watcher.Get(), [](const Vector<io::WatchEvent>& e)
            { return HasName(e, "f999.lua") || (e.Size() != 0 && e[0].kind == io::WatchEventKind::Overflow); });
        CHECK(events.Size() != 0);
        bool overflow = false;
        for (usize i = 0; i < events.Size(); ++i)
        {
            overflow = overflow || events[i].kind == io::WatchEventKind::Overflow;
        }
        CHECK((overflow || HasName(events, "f999.lua")));
    }

    SECTION("a create followed by a remove inside one poll leaves nothing, or both events")
    {
        Write(dir.Path().Join("blink.lua"), "x");
        REQUIRE(io::Remove(dir.Path().Join("blink.lua")).Ok());
        Vector<io::WatchEvent> events;
        watcher->Wait(100);
        REQUIRE(watcher->Poll(&events).Ok());
        for (usize i = 0; i < events.Size(); ++i)
        {
            CHECK(events[i].name.View() != StringView(""));
        }
    }

    SECTION("Wait times out when nothing happens, and Wake returns a waiting thread")
    {
        CHECK_FALSE(watcher->Wait(30));
        Atomic<bool> returned(false);
        Thread waiter(
            [&]()
            {
                watcher->Wait(20000);
                returned.Store(true);
            });
        ThisThread::SleepForMilliseconds(50);
        watcher->Wake();
        waiter.Join();
        CHECK(returned.Load());
    }

    SECTION("no event names a path outside the directory")
    {
        Write(dir.Path().Join("in.lua"), "x");
        const Vector<io::WatchEvent> events = Gather(watcher.Get(), [](const Vector<io::WatchEvent>& e)
                                                     { return HasName(e, "in.lua"); });
        for (usize i = 0; i < events.Size(); ++i)
        {
            CHECK_FALSE(events[i].name.View().Contains(StringView("/")));
            CHECK_FALSE(events[i].old_name.View().Contains(StringView("/")));
        }
    }

    SECTION("a removed directory ends the watch")
    {
        io::Path sub = dir.Path().Join("sub");
        REQUIRE(io::CreateDirectory(sub).Ok());
        UniquePtr<io::DirectoryWatcher> inner;
        REQUIRE(io::DirectoryWatcher::Open(sub, Options(polling), &inner).Ok());
        REQUIRE(io::RemoveAll(sub).Ok());
        Vector<io::WatchEvent> events;
        bool failed = false;
        const u64 start = os::MonotonicTimeNanos();
        while (!failed && os::MonotonicTimeNanos() - start < 2000ull * 1000000ull)
        {
            inner->Wait(50);
            failed = inner->Poll(&events).Failed();
        }
        CHECK(failed);
    }
}

}  // namespace

TEST_CASE("directory watcher: the polling backend", "[io][watch]")
{
    RunCases(true);
}

TEST_CASE("directory watcher: the native backend where there is one", "[io][watch]")
{
    RunCases(false);
}

TEST_CASE("directory watcher: a missing directory and a bad interval fail by value", "[io][watch]")
{
    io_test::TempDir dir;
    UniquePtr<io::DirectoryWatcher> watcher;
    CHECK(io::DirectoryWatcher::Open(dir.Path().Join("nope"), Options(false), &watcher).Is(io::Error::NotFound));
    CHECK(io::DirectoryWatcher::Open(dir.Path().Join("nope"), Options(true), &watcher).Is(io::Error::NotFound));
    io::WatchOptions bad;
    bad.poll_interval_ms = 0;
    CHECK(io::DirectoryWatcher::Open(dir.Path(), bad, &watcher).Is(io::Error::Invalid));
    CHECK(watcher.Get() == nullptr);
}
