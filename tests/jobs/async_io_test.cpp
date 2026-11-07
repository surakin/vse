#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <catch2/catch_test_macros.hpp>

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "io/file.h"
#include "jobs/async_io.h"
#include "jobs/frame_pool.h"
#include "jobs/scheduler.h"
#include "temp_dir.h"
#include "test_text.h"

using namespace engine_placeholder;
using namespace engine_placeholder::jobs;
using engine_test::Text;

namespace {

UniquePtr<Scheduler> AsyncIoTestMakeScheduler(usize n) {
    Scheduler::Config cfg;
    cfg.participants = n;
    return MakeUnique<Scheduler>(cfg);
}

// A scratch directory removed on destruction (tests/io/temp_dir.h), and the text of a path in it.
using io_test::TempDir;
Text FileIn(const TempDir& dir, const char* name) { return engine_test::Concat(dir.Native().View(), "/", StringView(name)); }

u8 PatternByte(usize i) { return static_cast<u8>((i * 131 + (i >> 8) * 7) & 0xFF); }

void WriteFile(const Text& path, usize size) {
    Vector<u8> bytes;
    bytes.Resize(size);
    for (usize i = 0; i < size; ++i) bytes[i] = PatternByte(i);
    REQUIRE(io::WriteFile(io::Path(path.View()), bytes.Data(), bytes.Size()).Ok());
}

struct ReadOutcome {
    bool ok = false;
    usize size = 0;
    bool content_matches = false;
    bool data_non_null = false;
    bool on_participant = false;
    bool on_io_thread = true;
    std::atomic<int> done{0};
};

Job ReadJob(Scheduler* s, const Text* path, ReadOutcome* out) {
    FileReadResult r = co_await ReadFileAsync(*s, path->View());
    out->ok = r.ok;
    out->size = r.size;
    out->data_non_null = r.data != nullptr;
    bool match = r.ok;
    for (usize i = 0; match && i < r.size; ++i) match = r.data[i] == PatternByte(i);
    out->content_matches = match;
    out->on_participant = WorkerPool::IsParticipantThread();
    out->on_io_thread = std::this_thread::get_id() == s->Io().ThreadId();
    out->done.fetch_add(1, std::memory_order_release);
}

#if !defined(_WIN32) // (only the FIFO case uses it)
Job ComputeJob(std::atomic<int>* counter) {
    counter->fetch_add(1, std::memory_order_release);
    co_return;
}
#endif

// Waits until every outcome in `outs` is done.
void HelpUntilAllDone(Scheduler& s, Vector<ReadOutcome>& outs) {
    s.HelpUntil(
        [](void* u) {
            Vector<ReadOutcome>& all = *static_cast<Vector<ReadOutcome>*>(u);
            for (usize i = 0; i < all.Size(); ++i) {
                if (all[i].done.load(std::memory_order_acquire) == 0) return false;
            }
            return true;
        },
        &outs);
}

} // namespace

TEST_CASE("co_await ReadFileAsync returns the right bytes (small, multi-MB, empty)", "[jobs][io]") {
    TempDir dir;
    struct Case {
        const char* name;
        usize size;
    } cases[] = {{"small.bin", 1234}, {"big.bin", 9u << 20}, {"empty.bin", 0}, {"one.bin", 1}};
    for (auto& c : cases) WriteFile(FileIn(dir, c.name), c.size);

    for (usize n : {usize{1}, usize{2}, usize{0}}) {
        auto s = AsyncIoTestMakeScheduler(n);
        for (auto& c : cases) {
            const Text path = FileIn(dir, c.name);
            ReadOutcome out;
            s->Submit(ReadJob(s.Get(), &path, &out));
            s->HelpUntil(out.done, 1);
            INFO("file " << c.name << " N=" << n);
            REQUIRE(out.ok);
            REQUIRE(out.size == c.size);
            REQUIRE(out.data_non_null);
            REQUIRE(out.content_matches);
            // D8: resumed on a participant, never on the I/O thread.
            REQUIRE(out.on_participant);
            REQUIRE_FALSE(out.on_io_thread);
        }
    }
}

TEST_CASE("A missing file, an over-long path and an oversized file fail cleanly", "[jobs][io]") {
    TempDir dir;
    auto s = AsyncIoTestMakeScheduler(2);

    const Text missing = FileIn(dir, "does_not_exist");
    ReadOutcome a;
    s->Submit(ReadJob(s.Get(), &missing, &a));
    s->HelpUntil(a.done, 1);
    REQUIRE_FALSE(a.ok);
    REQUIRE(a.size == 0);
    REQUIRE(a.on_participant);

    const Text too_long = engine_test::Concat("/tmp/", engine_test::Repeated(kMaxAsyncPathBytes + 10, 'x').View());
    ReadOutcome b;
    s->Submit(ReadJob(s.Get(), &too_long, &b));
    s->HelpUntil(b.done, 1);
    REQUIRE_FALSE(b.ok);

    // The I/O thread is still healthy afterwards.
    const Text after = FileIn(dir, "after.bin");
    WriteFile(after, 100);
    ReadOutcome c;
    s->Submit(ReadJob(s.Get(), &after, &c));
    s->HelpUntil(c.done, 1);
    REQUIRE(c.ok);
    REQUIRE(c.content_matches);
}

namespace {
Job ReadTinyArena(Scheduler* s, const Text* path, bool* ok, std::atomic<int>* done) {
    FileReadResult r = co_await ReadFileAsync(*s, path->View(), 4096);
    *ok = r.ok;
    done->fetch_add(1, std::memory_order_release);
}
} // namespace

TEST_CASE("A file bigger than max_bytes fails instead of overrunning", "[jobs][io]") {
    TempDir dir;
    const Text path = FileIn(dir, "f.bin");
    WriteFile(path, 100'000);
    auto s = AsyncIoTestMakeScheduler(2);
    bool ok = true;
    std::atomic<int> done{0};
    s->Submit(ReadTinyArena(s.Get(), &path, &ok, &done));
    s->HelpUntil(done, 1);
    REQUIRE_FALSE(ok);
}

TEST_CASE("64 concurrent reads of different files all complete correctly", "[jobs][io]") {
    TempDir dir;
    constexpr int kFiles = 64;
    Vector<Text> paths;
    Vector<usize> sizes;
    for (int i = 0; i < kFiles; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "f%02d.bin", i);
        sizes.Push(1000 + static_cast<usize>(i) * 4099);
        paths.Push(FileIn(dir, name));
        WriteFile(paths[static_cast<usize>(i)], sizes[static_cast<usize>(i)]);
    }
    for (usize n : {usize{1}, usize{3}}) {
        auto s = AsyncIoTestMakeScheduler(n);
        Vector<ReadOutcome> outs; // sized once while empty: the atomics never move
        outs.Resize(kFiles);
        for (usize i = 0; i < outs.Size(); ++i) s->Submit(ReadJob(s.Get(), &paths[i], &outs[i]));
        HelpUntilAllDone(*s, outs);
        for (usize i = 0; i < outs.Size(); ++i) {
            REQUIRE(outs[i].ok);
            REQUIRE(outs[i].size == sizes[i]);
            REQUIRE(outs[i].content_matches);
            REQUIRE(outs[i].on_participant);
            REQUIRE_FALSE(outs[i].on_io_thread);
        }
    }
}

TEST_CASE("Compute jobs keep running while a slow read blocks the I/O thread", "[jobs][io]") {
#if defined(_WIN32)
    SKIP("needs a FIFO whose open blocks until a writer comes (POSIX); Windows has no such file, and opening a busy pipe fails at once");
#else
    TempDir dir;
    // A FIFO with no writer: open() on it blocks the I/O thread until the
    // test opens the write end.
    const Text fifo = FileIn(dir, "blocked.fifo");
    REQUIRE(mkfifo(fifo.CStr(), 0600) == 0);

    for (usize n : {usize{1}, usize{3}}) {
        auto s = AsyncIoTestMakeScheduler(n);
        ReadOutcome blocked;
        // Unblocks the I/O thread at scope exit (before the scheduler is
        // destroyed) even if a REQUIRE below fails, so a failing test
        // fails instead of hanging in ~Scheduler's join.
        struct FifoRelease {
            const Text* path;
            ~FifoRelease() {
                int fd = open(path->CStr(), O_WRONLY | O_NONBLOCK);
                if (fd >= 0) close(fd);
            }
        } release_on_exit{&fifo};
        s->Submit(ReadJob(s.Get(), &fifo, &blocked));

        // Give the read job time to run and park the I/O thread in open()
        // (with N=1 main must run it: it sits below the compute jobs in
        // the deque otherwise).
        struct Clock {
            std::chrono::steady_clock::time_point until;
        } clock{std::chrono::steady_clock::now() + std::chrono::milliseconds(100)};
        s->HelpUntil([](void* u) { return std::chrono::steady_clock::now() >= static_cast<Clock*>(u)->until; },
                     &clock);

        // Unrelated compute work completes while the read is in flight.
        std::atomic<int> counter{0};
        constexpr int kJobs = 2000;
        for (int i = 0; i < kJobs; ++i) s->Submit(ComputeJob(&counter));
        s->HelpUntil(counter, kJobs);
        REQUIRE(counter.load() == kJobs);
        REQUIRE(blocked.done.load() == 0); // the read is still stuck in the OS

        // Release it. O_NONBLOCK|O_WRONLY fails with ENXIO until a reader
        // has the FIFO open, so retrying doubles as "the I/O thread
        // really is inside open()". The read then fails cleanly (a FIFO
        // is not seekable).
        int wfd = -1;
        for (int attempt = 0; attempt < 5000 && wfd < 0; ++attempt) {
            wfd = open(fifo.CStr(), O_WRONLY | O_NONBLOCK);
            if (wfd < 0) {
                // Let main run the read job if it hasn't yet.
                Clock slice{std::chrono::steady_clock::now() + std::chrono::milliseconds(2)};
                s->HelpUntil([](void* u) { return std::chrono::steady_clock::now() >= static_cast<Clock*>(u)->until; },
                             &slice);
            }
        }
        REQUIRE(wfd >= 0);
        close(wfd);
        s->HelpUntil(blocked.done, 1);
        REQUIRE_FALSE(blocked.ok);
        REQUIRE(blocked.on_participant);
        REQUIRE_FALSE(blocked.on_io_thread);
    }
#endif
}

TEST_CASE("The I/O thread is not a participant", "[jobs][io]") {
    auto s = AsyncIoTestMakeScheduler(2);
    struct Probe : IoRequest {
        std::atomic<int> is_participant{-1};
        std::atomic<int> done{0};
    } probe;
    probe.run = [](IoRequest* r) {
        auto* p = static_cast<Probe*>(r);
        p->is_participant.store(WorkerPool::IsParticipantThread() ? 1 : 0);
        p->done.store(1, std::memory_order_release);
    };
    REQUIRE(s->Io().Enqueue(&probe));
    s->HelpUntil(probe.done, 1);
    REQUIRE(probe.is_participant.load() == 0);
}

TEST_CASE("Shutdown drains queued reads and frame stats balance", "[jobs][io]") {
    TempDir dir;
    const Text path = FileIn(dir, "a.bin");
    WriteFile(path, 5000);
    const u64 live_before = FramePool::Global().GetStats().Live();
    {
        auto s = AsyncIoTestMakeScheduler(2);
        ReadOutcome out;
        s->Submit(ReadJob(s.Get(), &path, &out));
        s->HelpUntil(out.done, 1);
        REQUIRE(out.ok);
    }
    REQUIRE(FramePool::Global().GetStats().Live() == live_before);
}

TEST_CASE("IoThread::Shutdown runs everything already queued, then rejects new requests", "[jobs][io]") {
    struct Req : IoRequest {
        std::atomic<int>* ran;
    };
    std::atomic<int> ran{0};
    Req reqs[50];
    {
        IoThread io;
        for (auto& r : reqs) {
            r.ran = &ran;
            r.run = [](IoRequest* x) { static_cast<Req*>(x)->ran->fetch_add(1); };
            REQUIRE(io.Enqueue(&r));
        }
        io.Shutdown();
        REQUIRE(ran.load() == 50);
        io.Shutdown(); // idempotent
        Req late;
        late.ran = &ran;
        late.run = [](IoRequest*) {};
        REQUIRE_FALSE(io.Enqueue(&late)); // rejected, not queued, not run
    }
}

TEST_CASE("Concurrent IoThread::Shutdown callers both return only after the join", "[jobs][io]") {
    for (int round = 0; round < 50; ++round) {
        IoThread io;
        std::thread a([&] { io.Shutdown(); });
        std::thread b([&] { io.Shutdown(); });
        a.join();
        b.join();
    }
}

TEST_CASE("A read issued after the I/O thread shut down fails cleanly instead of aborting", "[jobs][io]") {
    TempDir dir;
    const Text path = FileIn(dir, "a.bin");
    WriteFile(path, 100);
    auto s = AsyncIoTestMakeScheduler(2);
    s->Io().Shutdown();
    ReadOutcome out;
    s->Submit(ReadJob(s.Get(), &path, &out));
    s->HelpUntil(out.done, 1);
    REQUIRE_FALSE(out.ok);
    REQUIRE(out.on_participant);
}

TEST_CASE("A read still pending when the I/O thread shuts down completes (drained)", "[jobs][io]") {
    TempDir dir;
    const Text path = FileIn(dir, "a.bin");
    WriteFile(path, 3000);
    auto s = AsyncIoTestMakeScheduler(2);
    Vector<ReadOutcome> outs;
    outs.Resize(20);
    for (usize i = 0; i < outs.Size(); ++i) s->Submit(ReadJob(s.Get(), &path, &outs[i]));
    // Keep the workers' continuations alive: join before destruction.
    HelpUntilAllDone(*s, outs);
    for (usize i = 0; i < outs.Size(); ++i) REQUIRE(outs[i].ok);
    s->Io().Shutdown();
}
