#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <new>
#include <thread>

#include "file_conformance.h"
#include "io/file.h"
#include "jobs/async_io.h"
#include "jobs/file_async.h"
#include "jobs/job.h"
#include "jobs/scheduler.h"
#include "jobs/worker_pool.h"
#include "slow_file.h"
#include "test_text.h"
#include "temp_dir.h"

#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define IO_TEST_SANITIZED 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define IO_TEST_SANITIZED 1
#endif

// --- an allocation counter (this test executable only), to prove the local completion path allocates nothing -----------------
namespace {
std::atomic<long> g_allocs{0};
std::atomic<bool> g_counting{false};
} // namespace
#ifndef IO_TEST_SANITIZED // the sanitizer runtimes define their own operator new
void* operator new(std::size_t n) {
    if (g_counting.load(std::memory_order_relaxed)) g_allocs.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n == 0 ? 1 : n)) return p;
    std::abort();
}
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#endif

using namespace engine_placeholder;
using io::Error;
using io::OpenMode;
using io_test::SlowFile;
using io_test::TempDir;

namespace {

io::FilePtr OpenWith(TempDir& dir, StringView content, OpenMode mode = OpenMode::Read) {
    static int counter = 0;
    const engine_test::Text name = engine_test::Format("async%d", counter++);
    const io::Path path = dir / name.View();
    REQUIRE(io::WriteFile(path, content.Data(), content.Length()).Ok());
    io::FilePtr f;
    REQUIRE(io::File::Open(path, mode, io::kOpenNone, &f).Ok());
    return f;
}

// Installs an executor as the default for one scope, so a failed REQUIRE cannot leave a dangling pointer behind.
struct ScopedDefaultExecutor {
    explicit ScopedDefaultExecutor(io::IoExecutor* e) { io::SetDefaultExecutor(e); }
    ~ScopedDefaultExecutor() { io::SetDefaultExecutor(nullptr); }
};

struct Gate { // waits for N completions and records their order
    std::mutex m;
    std::condition_variable cv;
    int count = 0;
    Vector<int> order;
    Vector<io::Status> statuses;
    void Add(int id, io::Status s) {
        std::lock_guard<std::mutex> lock(m);
        order.Push(id);
        statuses.Push(s);
        ++count;
        cv.notify_all();
    }
    void WaitFor(int n) {
        std::unique_lock<std::mutex> lock(m);
        cv.wait(lock, [&] { return count >= n; });
    }
};
struct Tagged {
    Gate* gate;
    int id;
};
void TaggedDone(void* user, const io::AsyncResult& r) {
    Tagged* t = static_cast<Tagged*>(user);
    t->gate->Add(t->id, r.status);
}

} // namespace

TEST_CASE("io::File: the network-like double passes the same conformance suite", "[io][async][slowfile]") {
    for (usize chunk : {usize{4096}, usize{1} << 20}) {
        INFO("chunk " << chunk);
        SlowFile::Options o;
        o.delay_us = 1;
        o.chunk = chunk;
        io_test::RunReadConformance([&](StringView c, bool) -> io::FilePtr { return MakeUnique<SlowFile>(c, o); });
        io_test::RunWriteConformance([&](StringView c, bool) -> io::FilePtr { return MakeUnique<SlowFile>(c, o); });
    }
}

TEST_CASE("io::File: the double expresses a size that is unknown until the first read", "[io][async][slowfile]") {
    SlowFile f("hello world", SlowFile::Options{50, 4, 0});
    u64 size = 0;
    CHECK((f.Capabilities() & io::kCapSizeKnown) == 0);
    CHECK(f.Size(&size).Is(Error::Unsupported));
    char b[16];
    usize n = 0;
    REQUIRE(f.Read(b, 16, 0, &n).Ok());
    CHECK(n == 4); // a short read, not an error
    CHECK((f.Capabilities() & io::kCapNativelyAsync) != 0);
    REQUIRE(f.Size(&size).Ok());
    CHECK(size == 11);
}

TEST_CASE("io::File: asynchronous reads on a local file", "[io][async]") {
    TempDir dir;
    const engine_test::Text content_text = io_test::PatternBytes(50000);
    const StringView content = content_text.View();
    io::FilePtr f = OpenWith(dir, content);
    // Completions arrive in submission order on the single built-in executor, with the right bytes.
    constexpr int kN = 8;
    Gate gate;
    Tagged tags[kN];
    io::AsyncRequest reqs[kN];
    Vector<char> bufs[kN];
    for (int i = 0; i < kN; ++i) {
        tags[i] = {&gate, i};
        bufs[i] = engine_test::Chars(1000);
        f->ReadAsync(&reqs[i], bufs[i].Data(), 1000, static_cast<u64>(i) * 1000, &TaggedDone, &tags[i]);
    }
    gate.WaitFor(kN);
    for (int i = 0; i < kN; ++i) {
        CHECK(gate.order[i] == i);
        CHECK(gate.statuses[i].Ok());
        CHECK(StringView(bufs[i].Data(), bufs[i].Size()) == content.Substring(static_cast<size_t>(i) * 1000, 1000));
    }
    // A write and a flush.
    io::FilePtr w = OpenWith(dir, "0123456789", OpenMode::ReadWrite);
    Gate g2;
    Tagged t0{&g2, 0};
    Tagged t1{&g2, 1};
    io::AsyncRequest wr;
    io::AsyncRequest fr;
    w->WriteAsync(&wr, "ABC", 3, 4, &TaggedDone, &t0);
    w->FlushAsync(&fr, &TaggedDone, &t1);
    g2.WaitFor(2);
    char out[10];
    REQUIRE(io::ReadExact(*w, out, 10, 0).Ok());
    CHECK(StringView(out, 10) == StringView("0123ABC789"));
}

TEST_CASE("io::File: a thousand concurrent reads of one file", "[io][async]") {
    TempDir dir;
    const engine_test::Text content_text = io_test::PatternBytes(100000);
    const StringView content = content_text.View();
    for (int kind = 0; kind < 2; ++kind) {
        io::FilePtr f = kind == 0 ? OpenWith(dir, content) : io::FilePtr(MakeUnique<SlowFile>(content, SlowFile::Options{5, 500, 0}));
        constexpr int kN = 1000;
        Gate gate;
        Vector<Tagged> tags;
        tags.Resize(kN);
        Vector<io::AsyncRequest> reqs;
        reqs.Resize(kN);
        Vector<char> bufs = engine_test::Chars(static_cast<usize>(kN) * 100); // request i reads into bufs[i * 100 ...]
        for (int i = 0; i < kN; ++i) {
            tags[i] = {&gate, i};
            f->ReadAsync(&reqs[i], bufs.Data() + i * 100, 100, static_cast<u64>((i * 97) % 99000), &TaggedDone, &tags[i]);
        }
        gate.WaitFor(kN);
        Vector<io::Status> by_id;
        by_id.Resize(kN);
        for (size_t k = 0; k < gate.order.Size(); ++k) by_id[static_cast<size_t>(gate.order[k])] = gate.statuses[k];
        int bad = 0;
        for (int i = 0; i < kN; ++i) {
            const size_t off = static_cast<size_t>((i * 97) % 99000);
            if (!by_id[i].Ok() || StringView(bufs.Data() + i * 100, 100) != content.Substring(off, 100)) ++bad;
        }
        CHECK(bad == 0);
        CHECK(gate.order.Size() == static_cast<size_t>(kN));
    }
}

TEST_CASE("io::File: cancellation", "[io][async]") {
    // One slow request keeps the double busy, so the next is still queued when it is cancelled.
    SlowFile f(engine_test::Repeated(100, 'x').View(), SlowFile::Options{30000, 100, 0});
    Gate gate;
    Tagged t0{&gate, 0};
    Tagged t1{&gate, 1};
    char a[10];
    char b[10];
    io::AsyncRequest r0;
    io::AsyncRequest r1;
    f.ReadAsync(&r0, a, 10, 0, &TaggedDone, &t0);
    f.ReadAsync(&r1, b, 10, 0, &TaggedDone, &t1);
    CHECK(f.Cancel(&r1));  // not started yet
    CHECK_FALSE(f.Cancel(&r1)); // already cancelled
    gate.WaitFor(2);       // both complete: the cancelled one exactly once, with Interrupted
    CHECK(gate.statuses[0].Ok());
    CHECK(gate.statuses[1].Is(Error::Interrupted));
    CHECK_FALSE(f.Cancel(&r0)); // after completion
    CHECK(gate.count == 2);
}

TEST_CASE("io::File: a completion may destroy the file", "[io][async]") {
    TempDir dir;
    struct Ctx {
        io::FilePtr file;
        io::AsyncRequest request;
        std::atomic<int> done{0};
        char buf[8];
    } ctx;
    ctx.file = OpenWith(dir, "contents!");
    ctx.file->ReadAsync(&ctx.request, ctx.buf, 8, 0, [](void* u, const io::AsyncResult& r) {
        Ctx* c = static_cast<Ctx*>(u);
        CHECK(r.status.Ok());
        c->file.Reset(); // destroys the file from inside its own completion
        c->done.store(1);
    }, &ctx);
    while (ctx.done.load() == 0) std::this_thread::yield();
    CHECK_FALSE(ctx.file);
}

TEST_CASE("io::File: destroying a file waits for its requests", "[io][async]") {
    TempDir dir;
    std::atomic<int> completed{0};
    constexpr int kN = 50;
    Vector<io::AsyncRequest> reqs;
    reqs.Resize(kN);
    Vector<char> bufs = engine_test::Chars(static_cast<usize>(kN) * 64);
    {
        io::FilePtr f = OpenWith(dir, io_test::PatternBytes(10000).View());
        for (int i = 0; i < kN; ++i) {
            f->ReadAsync(&reqs[i], bufs.Data() + i * 64, 64, 0, [](void* u, const io::AsyncResult&) { static_cast<std::atomic<int>*>(u)->fetch_add(1); }, &completed);
        }
    } // ~LocalFile waits until every request has begun completing
    while (completed.load() < kN) std::this_thread::yield();
    CHECK(completed.load() == kN);
    // The double completes queued requests with Interrupted when it is destroyed.
    std::atomic<int> interrupted{0};
    Vector<io::AsyncRequest> r2;
    r2.Resize(10);
    char tmp[10][8];
    {
        SlowFile slow(engine_test::Repeated(100, 'y').View(), SlowFile::Options{20000, 100, 0});
        for (int i = 0; i < 10; ++i) {
            slow.ReadAsync(&r2[i], tmp[i], 8, 0, [](void* u, const io::AsyncResult& r) {
                if (r.status.Is(Error::Interrupted)) static_cast<std::atomic<int>*>(u)->fetch_add(1);
            }, &interrupted);
        }
    }
    CHECK(interrupted.load() >= 8); // the first may already be running
}

TEST_CASE("io::File: an error in the middle of a chain", "[io][async]") {
    SlowFile f(io_test::PatternBytes(10000).View(), SlowFile::Options{10, 1000, 3});
    Vector<u8> all;
    const io::Status s = io::ReadAll(f, &all);
    CHECK(s.Is(Error::Io));
}

TEST_CASE("io::File: an executor that is shut down completes requests with Interrupted", "[io][async]") {
    TempDir dir;
    const engine_test::Text content_text = io_test::PatternBytes(4096);
    const StringView content = content_text.View();
    io::FilePtr f = OpenWith(dir, content);
    {
        auto io_thread = MakeUnique<jobs::IoThread>();
        ScopedDefaultExecutor scoped(io_thread.Get());
        constexpr int kN = 20;
        Gate gate;
        Vector<Tagged> tags;
        tags.Resize(kN);
        Vector<io::AsyncRequest> reqs;
        reqs.Resize(kN);
        char buf[kN][16];
        for (int i = 0; i < kN; ++i) {
            tags[i] = {&gate, i};
            f->ReadAsync(&reqs[i], buf[i], 16, 0, &TaggedDone, &tags[i]);
        }
        io_thread->Shutdown(); // drains: everything queued still runs
        gate.WaitFor(kN);
        for (int i = 0; i < kN; ++i) CHECK(gate.statuses[i].Ok());
        // A stopped executor removes itself as the default, so later requests run on the built-in one.
        Gate later;
        Tagged lt{&later, 0};
        io::AsyncRequest lr;
        char lb[16];
        f->ReadAsync(&lr, lb, 16, 0, &TaggedDone, &lt);
        later.WaitFor(1);
        CHECK(later.statuses[0].Ok());
    }
    // An executor that refuses work: the request completes at once, on the caller's thread, with Interrupted.
    struct Refusing final : io::IoExecutor {
        bool Submit(io::IoWork*) override { return false; }
    } refusing;
    ScopedDefaultExecutor scoped(&refusing);
    Gate refused;
    Tagged rt{&refused, 0};
    io::AsyncRequest rr;
    char rb[16];
    f->ReadAsync(&rr, rb, 16, 0, &TaggedDone, &rt);
    CHECK(refused.count == 1); // already complete when ReadAsync returned
    CHECK(refused.statuses[0].Is(Error::Interrupted));
}

TEST_CASE("io::File: the local completion path allocates nothing", "[io][async]") {
#ifdef IO_TEST_SANITIZED
    SKIP("allocation counting needs the plain flavor (the sanitizer runtimes own operator new)");
#endif
    TempDir dir;
    io::FilePtr f = OpenWith(dir, io_test::PatternBytes(10000).View());
    constexpr int kN = 1000;
    Gate gate;
    gate.order.Reserve(kN + 1);
    gate.statuses.Reserve(kN + 1);
    Vector<Tagged> tags;
    tags.Resize(kN);
    Vector<io::AsyncRequest> reqs;
    reqs.Resize(kN);
    char buf[64];
    // Warm up: starts the built-in executor thread.
    io::AsyncRequest warm;
    Tagged wt{&gate, -1};
    f->ReadAsync(&warm, buf, 64, 0, &TaggedDone, &wt);
    gate.WaitFor(1);
    g_allocs.store(0);
    g_counting.store(true);
    for (int i = 0; i < kN; ++i) {
        tags[i] = {&gate, i};
        f->ReadAsync(&reqs[i], buf, 64, 0, &TaggedDone, &tags[i]);
    }
    gate.WaitFor(kN + 1);
    g_counting.store(false);
    CHECK(g_allocs.load() == 0);
}

// --- coroutines on a scheduler -----------------------------------------------------------------------------------------------

namespace {

struct JobOut {
    std::atomic<int> done{0};
    bool ok = false;
    bool on_participant = false;
    engine_test::Text text = engine_test::MakeText();
    io::Status status;
};

jobs::Job ReadInJob(jobs::Scheduler* s, io::File* f, JobOut* out) {
    char buf[16];
    const io::AsyncResult r = co_await jobs::ReadAsync(*s, *f, buf, 16, 4);
    out->ok = r.status.Ok();
    out->text = engine_test::MakeText(StringView(buf, r.bytes));
    out->on_participant = jobs::WorkerPool::IsParticipantThread();
    out->done.fetch_add(1, std::memory_order_release);
}

jobs::Job ReadAllInJob(jobs::Scheduler* s, io::File* f, JobOut* out) {
    jobs::FileReadAllResult r = co_await jobs::ReadAllAsync(*s, *f);
    out->status = r.status;
    out->text = engine_test::MakeText(StringView(reinterpret_cast<const char*>(r.bytes.Data()), r.bytes.Size()));
    out->on_participant = jobs::WorkerPool::IsParticipantThread();
    out->done.fetch_add(1, std::memory_order_release);
}

jobs::Job WriteInJob(jobs::Scheduler* s, io::File* f, JobOut* out) {
    const io::AsyncResult w = co_await jobs::WriteAsync(*s, *f, "XY", 2, 0);
    const io::AsyncResult fl = co_await jobs::FlushAsync(*s, *f);
    out->ok = w.status.Ok() && w.bytes == 2 && fl.status.Ok();
    out->done.fetch_add(1, std::memory_order_release);
}

} // namespace

TEST_CASE("jobs: co_await file reads inside jobs on a scheduler", "[io][async][jobs]") {
    TempDir dir;
    jobs::Scheduler::Config cfg;
    cfg.participants = 4;
    jobs::Scheduler sched(cfg);
    ScopedDefaultExecutor scoped(&sched.Io());
    const engine_test::Text content_text = io_test::PatternBytes(70000);
    const StringView content = content_text.View();
    {
        io::FilePtr f = OpenWith(dir, content);
        JobOut a;
        sched.Submit(ReadInJob(&sched, f.Get(), &a));
        sched.HelpUntil(a.done, 1);
        CHECK(a.ok);
        CHECK(a.on_participant);
        CHECK(a.text.View() == content.Substring(4, 16));
        JobOut all;
        sched.Submit(ReadAllInJob(&sched, f.Get(), &all));
        sched.HelpUntil(all.done, 1);
        CHECK(all.status.Ok());
        CHECK(all.on_participant);
        CHECK(all.text.View() == content);
    }
    {
        io::FilePtr w = OpenWith(dir, "abcdef", OpenMode::ReadWrite);
        JobOut out;
        sched.Submit(WriteInJob(&sched, w.Get(), &out));
        sched.HelpUntil(out.done, 1);
        CHECK(out.ok);
        char b[6];
        REQUIRE(io::ReadExact(*w, b, 6, 0).Ok());
        CHECK(StringView(b, 6) == StringView("XYcdef"));
    }
    {
        // ReadAllAsync over the network-like double: short reads, an unknown size, and a failure in the middle.
        io::FilePtr slow = MakeUnique<SlowFile>(content, SlowFile::Options{5, 3000, 0});
        JobOut out;
        sched.Submit(ReadAllInJob(&sched, slow.Get(), &out));
        sched.HelpUntil(out.done, 1);
        CHECK(out.status.Ok());
        CHECK(out.text.View() == content);
        io::FilePtr failing = MakeUnique<SlowFile>(content, SlowFile::Options{5, 3000, 4});
        JobOut bad;
        sched.Submit(ReadAllInJob(&sched, failing.Get(), &bad));
        sched.HelpUntil(bad.done, 1);
        CHECK(bad.status.Is(Error::Io));
        CHECK(bad.text.Length() == 9000);
    }
}

namespace {
struct ProcOut {
    std::atomic<int> done{0};
    bool ok = false;
    usize size = 0;
};
jobs::Job ReadProcJob(jobs::Scheduler* s, ProcOut* out) {
    jobs::FileReadResult r = co_await jobs::ReadFileAsync(*s, "/proc/self/status");
    out->ok = r.ok;
    out->size = r.size;
    out->done.fetch_add(1, std::memory_order_release);
}
} // namespace

TEST_CASE("jobs::ReadFileAsync reads files whose reported size is wrong (/proc)", "[io][async][jobs]") {
    jobs::Scheduler::Config cfg;
    cfg.participants = 2;
    jobs::Scheduler sched(cfg);
    ProcOut out;
    sched.Submit(ReadProcJob(&sched, &out));
    sched.HelpUntil(out.done, 1);
    CHECK(out.ok);
    CHECK(out.size > 100);
}
