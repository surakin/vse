#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "jobs/async_io.h"
#include "jobs/frame_pool.h"
#include "jobs/scheduler.h"

using namespace engine_placeholder;
using namespace engine_placeholder::jobs;

namespace {

std::unique_ptr<Scheduler> AsyncIoTestMakeScheduler(usize n) {
    Scheduler::Config cfg;
    cfg.participants = n;
    return std::make_unique<Scheduler>(cfg);
}

// A scratch directory removed on destruction.
struct TempDir {
    std::string path;
    TempDir() {
        char tmpl[] = "/tmp/engine_async_io_XXXXXX";
        const char* p = mkdtemp(tmpl);
        path = p ? p : "";
    }
    ~TempDir() {
        // best-effort cleanup of the files the tests create
        std::string cmd = "rm -rf '" + path + "'";
        int rc = std::system(cmd.c_str());
        (void)rc;
    }
    std::string File(const char* name) const { return path + "/" + name; }
};

u8 PatternByte(usize i) { return static_cast<u8>((i * 131 + (i >> 8) * 7) & 0xFF); }

void WriteFile(const std::string& path, usize size) {
    FILE* f = std::fopen(path.c_str(), "wb");
    REQUIRE(f != nullptr);
    std::vector<u8> chunk(1 << 16);
    for (usize off = 0; off < size; off += chunk.size()) {
        usize n = size - off < chunk.size() ? size - off : chunk.size();
        for (usize i = 0; i < n; ++i) chunk[i] = PatternByte(off + i);
        REQUIRE(std::fwrite(chunk.data(), 1, n, f) == n);
    }
    std::fclose(f);
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

Job ReadJob(Scheduler* s, std::string* path, ReadOutcome* out) {
    FileReadResult r = co_await ReadFileAsync(*s, StringView(path->c_str(), path->size()));
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

Job ComputeJob(std::atomic<int>* counter) {
    counter->fetch_add(1, std::memory_order_release);
    co_return;
}

} // namespace

TEST_CASE("co_await ReadFileAsync returns the right bytes (small, multi-MB, empty)", "[jobs][io]") {
    TempDir dir;
    REQUIRE_FALSE(dir.path.empty());
    struct Case {
        const char* name;
        usize size;
    } cases[] = {{"small.bin", 1234}, {"big.bin", 9u << 20}, {"empty.bin", 0}, {"one.bin", 1}};
    for (auto& c : cases) WriteFile(dir.File(c.name), c.size);

    for (usize n : {usize{1}, usize{2}, usize{0}}) {
        auto s = AsyncIoTestMakeScheduler(n);
        for (auto& c : cases) {
            std::string path = dir.File(c.name);
            ReadOutcome out;
            s->Submit(ReadJob(s.get(), &path, &out));
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

    std::string missing = dir.File("does_not_exist");
    ReadOutcome a;
    s->Submit(ReadJob(s.get(), &missing, &a));
    s->HelpUntil(a.done, 1);
    REQUIRE_FALSE(a.ok);
    REQUIRE(a.size == 0);
    REQUIRE(a.on_participant);

    std::string too_long = "/tmp/" + std::string(kMaxAsyncPathBytes + 10, 'x');
    ReadOutcome b;
    s->Submit(ReadJob(s.get(), &too_long, &b));
    s->HelpUntil(b.done, 1);
    REQUIRE_FALSE(b.ok);

    // The I/O thread is still healthy afterwards.
    WriteFile(dir.File("after.bin"), 100);
    std::string after = dir.File("after.bin");
    ReadOutcome c;
    s->Submit(ReadJob(s.get(), &after, &c));
    s->HelpUntil(c.done, 1);
    REQUIRE(c.ok);
    REQUIRE(c.content_matches);
}

namespace {
Job ReadTinyArena(Scheduler* s, std::string* path, bool* ok, std::atomic<int>* done) {
    FileReadResult r = co_await ReadFileAsync(*s, StringView(path->c_str(), path->size()), 4096);
    *ok = r.ok;
    done->fetch_add(1, std::memory_order_release);
}
} // namespace

TEST_CASE("A file bigger than max_bytes fails instead of overrunning", "[jobs][io]") {
    TempDir dir;
    WriteFile(dir.File("f.bin"), 100'000);
    auto s = AsyncIoTestMakeScheduler(2);
    std::string path = dir.File("f.bin");
    bool ok = true;
    std::atomic<int> done{0};
    s->Submit(ReadTinyArena(s.get(), &path, &ok, &done));
    s->HelpUntil(done, 1);
    REQUIRE_FALSE(ok);
}

TEST_CASE("64 concurrent reads of different files all complete correctly", "[jobs][io]") {
    TempDir dir;
    constexpr int kFiles = 64;
    std::vector<std::string> paths(kFiles);
    std::vector<usize> sizes(kFiles);
    for (int i = 0; i < kFiles; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "f%02d.bin", i);
        sizes[i] = 1000 + static_cast<usize>(i) * 4099;
        paths[i] = dir.File(name);
        WriteFile(paths[i], sizes[i]);
    }
    for (usize n : {usize{1}, usize{3}}) {
        auto s = AsyncIoTestMakeScheduler(n);
        std::vector<ReadOutcome> outs(kFiles);
        for (int i = 0; i < kFiles; ++i) s->Submit(ReadJob(s.get(), &paths[i], &outs[i]));
        // Wait until every outcome is done (each flips its own counter).
        struct Ctx {
            std::vector<ReadOutcome>* outs;
        } ctx{&outs};
        s->HelpUntil(
            [](void* u) {
                for (auto& o : *static_cast<Ctx*>(u)->outs)
                    if (o.done.load(std::memory_order_acquire) == 0) return false;
                return true;
            },
            &ctx);
        for (int i = 0; i < kFiles; ++i) {
            REQUIRE(outs[i].ok);
            REQUIRE(outs[i].size == sizes[i]);
            REQUIRE(outs[i].content_matches);
            REQUIRE(outs[i].on_participant);
            REQUIRE_FALSE(outs[i].on_io_thread);
        }
    }
}

TEST_CASE("Compute jobs keep running while a slow read blocks the I/O thread", "[jobs][io]") {
    TempDir dir;
    // A FIFO with no writer: open() on it blocks the I/O thread until the
    // test opens the write end.
    std::string fifo = dir.File("blocked.fifo");
    REQUIRE(mkfifo(fifo.c_str(), 0600) == 0);

    for (usize n : {usize{1}, usize{3}}) {
        auto s = AsyncIoTestMakeScheduler(n);
        ReadOutcome blocked;
        // Unblocks the I/O thread at scope exit (before the scheduler is
        // destroyed) even if a REQUIRE below fails, so a failing test
        // fails instead of hanging in ~Scheduler's join.
        struct FifoRelease {
            const std::string* path;
            ~FifoRelease() {
                int fd = open(path->c_str(), O_WRONLY | O_NONBLOCK);
                if (fd >= 0) close(fd);
            }
        } release_on_exit{&fifo};
        s->Submit(ReadJob(s.get(), &fifo, &blocked));

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
            wfd = open(fifo.c_str(), O_WRONLY | O_NONBLOCK);
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
    WriteFile(dir.File("a.bin"), 5000);
    const u64 live_before = FramePool::Global().GetStats().Live();
    {
        auto s = AsyncIoTestMakeScheduler(2);
        std::string path = dir.File("a.bin");
        ReadOutcome out;
        s->Submit(ReadJob(s.get(), &path, &out));
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
    WriteFile(dir.File("a.bin"), 100);
    auto s = AsyncIoTestMakeScheduler(2);
    s->Io().Shutdown();
    std::string path = dir.File("a.bin");
    ReadOutcome out;
    s->Submit(ReadJob(s.get(), &path, &out));
    s->HelpUntil(out.done, 1);
    REQUIRE_FALSE(out.ok);
    REQUIRE(out.on_participant);
}

TEST_CASE("A read still pending when the I/O thread shuts down completes (drained)", "[jobs][io]") {
    TempDir dir;
    WriteFile(dir.File("a.bin"), 3000);
    auto s = AsyncIoTestMakeScheduler(2);
    std::string path = dir.File("a.bin");
    std::vector<ReadOutcome> outs(20);
    for (auto& o : outs) s->Submit(ReadJob(s.get(), &path, &o));
    // Keep the workers' continuations alive: join before destruction.
    struct Ctx { std::vector<ReadOutcome>* outs; } ctx{&outs};
    s->HelpUntil([](void* u) {
        for (auto& o : *static_cast<Ctx*>(u)->outs) if (o.done.load(std::memory_order_acquire) == 0) return false;
        return true;
    }, &ctx);
    for (auto& o : outs) REQUIRE(o.ok);
    s->Io().Shutdown();
}
