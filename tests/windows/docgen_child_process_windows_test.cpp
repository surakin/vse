// docgen's child processes on Windows (tools/docgen/src/windows/child_process_windows.cpp, Phase 13 Day 161), through
// child_probe: the arguments arrive as given, the output files, the working directory, how a child ended, and that
// killing a child ends what it started.
#include "cstr/number.h"
#include "lang/initializer_list.h"

#include <catch2/catch_test_macros.hpp>

#include "child_process.h"
#include "io/file_system.h"
#include "mem/arena_allocator.h"
#include "os/os.h"
#include "temp_dir.h"
#include "test_text.h"

using namespace vse;
using docgen::ChildEnd;
using docgen::ChildPoll;
using docgen::ChildProcess;
using io::Path;
using io_test::TempDir;

namespace
{

StringView Probe()
{
    return StringView(CHILD_PROBE_PATH_UTF8);
}

// A NUL-terminated copy (StartChild needs every view to be followed by a NUL).
StringView Z(ArenaAllocator& arena, StringView text)
{
    return docgen::ArenaCat(arena, text, StringView());
}

Vector<StringView, ArenaAllocator> Args(ArenaAllocator& arena, InitializerList<StringView> items)
{
    Vector<StringView, ArenaAllocator> args(arena);
    args.Push(Z(arena, Probe()));
    for (const StringView item : items)
    {
        args.Push(Z(arena, item));
    }
    return args;
}

// Waits up to `seconds` for the child to end.
ChildPoll WaitFor(ChildProcess& child, ChildEnd* end, u64 seconds = 30)
{
    const u64 deadline = os::MonotonicTimeNanos() + seconds * 1'000'000'000ull;
    for (;;)
    {
        const ChildPoll polled = docgen::PollChild(child, end);
        if (polled != ChildPoll::Running || os::MonotonicTimeNanos() > deadline)
        {
            return polled;
        }
        os::SleepNanos(2'000'000);
    }
}

StringView PathOf(ArenaAllocator& arena, const Path& path)
{
    return Z(arena, path.View());
}

}  // namespace

TEST_CASE("docgen on Windows: a child gets its arguments as given, quotes and backslashes included",
          "[docgen][windows]")
{
    ArenaAllocator arena(1u << 20);
    TempDir dir;
    const Path out = dir / "out.txt";
    // Spaces, quotes, backslashes before a quote and at the end, and an empty argument: the rules a command line is
    // read back with.
    auto args = Args(arena,
                     {StringView("plain"), StringView("two words"), StringView("say \"hi\""), StringView("trailing\\"),
                      StringView("C:\\dir with space\\"), StringView("a\\\\\"b"), StringView("")});
    ChildProcess child;
    REQUIRE(docgen::StartChild(args, PathOf(arena, out), StringView(), StringView(), arena, &child));
    ChildEnd end;
    REQUIRE(WaitFor(child, &end) == ChildPoll::Ended);
    CHECK(end.exited);
    CHECK(end.exit_code == 0);
    const engine_test::Text got = io_test::ReadText(out);
    CHECK(got.View() ==
          StringView("[plain]\n[two words]\n[say \"hi\"]\n[trailing\\]\n[C:\\dir with space\\]\n[a\\\\\"b]\n[]\n"));
}

TEST_CASE("docgen on Windows: exit codes, and an exception status as how a child was ended", "[docgen][windows]")
{
    ArenaAllocator arena(1u << 20);
    TempDir dir;
    const Path out = dir / "out.txt";
    ChildProcess child;
    ChildEnd end;
    REQUIRE(docgen::StartChild(Args(arena, {StringView("--exit"), StringView("7")}), PathOf(arena, out), StringView(),
                               StringView(), arena, &child));
    REQUIRE(WaitFor(child, &end) == ChildPoll::Ended);
    CHECK(end.exited);
    CHECK(end.exit_code == 7);
    CHECK_FALSE(end.killed);
    // A fault ends the child with the exception's status; the job keeps the crash dialog away, so this does not wait
    // for a click.
    REQUIRE(docgen::StartChild(Args(arena, {StringView("--crash")}), PathOf(arena, out), StringView(), StringView(),
                               arena, &child));
    REQUIRE(WaitFor(child, &end) == ChildPoll::Ended);
    CHECK(end.killed);
    CHECK_FALSE(end.exited);
    CHECK(end.cause == 0xC0000005u);
}

TEST_CASE("docgen on Windows: standard error goes to its own file, or with the output", "[docgen][windows]")
{
    ArenaAllocator arena(1u << 20);
    TempDir dir;
    const Path out = dir / "out.txt";
    const Path err = dir / "err.txt";
    ChildProcess child;
    ChildEnd end;
    REQUIRE(docgen::StartChild(Args(arena, {StringView("--stderr"), StringView("to stderr")}), PathOf(arena, out),
                               PathOf(arena, err), StringView(), arena, &child));
    REQUIRE(WaitFor(child, &end) == ChildPoll::Ended);
    CHECK(io_test::ReadText(out).View() == StringView(""));
    CHECK(io_test::ReadText(err).View() == StringView("to stderr\n"));
    REQUIRE(docgen::StartChild(Args(arena, {StringView("--stderr"), StringView("merged")}), PathOf(arena, out),
                               StringView(), StringView(), arena, &child));
    REQUIRE(WaitFor(child, &end) == ChildPoll::Ended);
    CHECK(io_test::ReadText(out).View() == StringView("merged\n"));  // and the file was truncated, not appended to
}

TEST_CASE("docgen on Windows: a child runs in the working directory it was given", "[docgen][windows]")
{
    ArenaAllocator arena(1u << 20);
    TempDir dir;
    TempDir work;
    const Path out = dir / "out.txt";
    ChildProcess child;
    ChildEnd end;
    REQUIRE(docgen::StartChild(Args(arena, {StringView("--cwd")}), PathOf(arena, out), StringView(),
                               PathOf(arena, work.Path()), arena, &child));
    REQUIRE(WaitFor(child, &end) == ChildPoll::Ended);
    Path printed;
    const engine_test::Text text = io_test::ReadText(out);
    REQUIRE(Path::Parse(text.View().Substring(0, text.View().IndexOf('\n')), io::PathStyle::Windows, &printed));
    Path expected;
    REQUIRE(io::Canonical(work.Path(), &expected).Ok());
    Path printed_canonical;
    REQUIRE(io::Canonical(printed, &printed_canonical).Ok());
    CHECK(printed_canonical == expected);
}

TEST_CASE("docgen on Windows: killing a child also ends the process it started", "[docgen][windows]")
{
    ArenaAllocator arena(1u << 20);
    TempDir dir;
    const Path out = dir / "out.txt";
    ChildProcess child;
    REQUIRE(docgen::StartChild(Args(arena, {StringView("--parent"), StringView("60")}), PathOf(arena, out),
                               StringView(), StringView(), arena, &child));
    // The parent prints its child's process id once it has started it.
    unsigned long grandchild = 0;
    for (int i = 0; i < 1500 && grandchild == 0; ++i)
    {
        const engine_test::Text text = io_test::ReadText(out);
        if (text.View().IndexOf('\n') != text.View().Length())
        {
            grandchild = CStrToULong(text.CStr(), nullptr, 10);
        }
        else
        {
            os::SleepNanos(10'000'000);
        }
    }
    REQUIRE(grandchild != 0);
    const HANDLE grandchild_process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(grandchild));
    REQUIRE(grandchild_process != nullptr);
    ChildEnd end;
    CHECK(docgen::PollChild(child, &end) == ChildPoll::Running);
    const u64 started = os::MonotonicTimeNanos();
    docgen::KillChild(child);
    CHECK(os::MonotonicTimeNanos() - started < 10'000'000'000ull);  // not the 60 s the parent sleeps
    CHECK(WaitForSingleObject(grandchild_process, 10000) == WAIT_OBJECT_0);
    CloseHandle(grandchild_process);
    CHECK(child.process == nullptr);  // released
}

TEST_CASE("docgen on Windows: a program that does not exist does not start", "[docgen][windows]")
{
    ArenaAllocator arena(1u << 20);
    TempDir dir;
    Vector<StringView, ArenaAllocator> args(arena);
    args.Push(Z(arena, StringView("C:/no/such/program.exe")));
    ChildProcess child;
    CHECK_FALSE(docgen::StartChild(args, PathOf(arena, dir / "out.txt"), StringView(), StringView(), arena, &child));
    CHECK(child.process == nullptr);
}
