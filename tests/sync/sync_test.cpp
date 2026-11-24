// Oracle test of the sync module: each wrapper behaves as the standard facility it forwards to, and a side-by-side
// micro-benchmark shows the wrapper costs the same as the standard type (the zero-cost proof of Phase 16). This file
// includes the standard headers on purpose (the gate lists oracle files by name).
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

#include "build_defs.h"
#include "sync/atomic.h"
#include "sync/mutex.h"
#include "sync/thread.h"

using namespace vse;

namespace
{

template <class F> void RunThreads(int count, F body)
{
    std::vector<std::thread> threads;
    for (int i = 0; i < count; ++i)
    {
        threads.emplace_back([&body, i] { body(i); });
    }
    for (auto& t : threads)
    {
        t.join();
    }
}

enum class State : u8
{
    Idle,
    Busy
};

}  // namespace

TEST_CASE("Atomic: single-threaded operations match std::atomic", "[sync]")
{
    Atomic<int> a(5);
    std::atomic<int> s(5);
    CHECK(a.Load() == s.load());
    a.Store(9, MemoryOrder::Release);
    s.store(9, std::memory_order_release);
    CHECK(a.Load(MemoryOrder::Acquire) == s.load(std::memory_order_acquire));
    CHECK(a.Exchange(3) == s.exchange(3));
    CHECK(a.FetchAdd(4) == s.fetch_add(4));
    CHECK(a.FetchSub(2, MemoryOrder::Relaxed) == s.fetch_sub(2, std::memory_order_relaxed));
    CHECK(a.FetchOr(0x10) == s.fetch_or(0x10));
    CHECK(a.FetchAnd(0x1C) == s.fetch_and(0x1C));
    CHECK(a.FetchXor(0xFF) == s.fetch_xor(0xFF));
    CHECK(a.Load() == s.load());

    int expected = a.Load(), expected_s = s.load();
    CHECK(a.CompareExchangeStrong(expected, 100) == s.compare_exchange_strong(expected_s, 100));
    expected = 1;
    expected_s = 1;
    CHECK(a.CompareExchangeStrong(expected, 200, MemoryOrder::AcqRel, MemoryOrder::Acquire) ==
          s.compare_exchange_strong(expected_s, 200));
    CHECK(expected == expected_s);
    for (MemoryOrder order :
         {MemoryOrder::Relaxed, MemoryOrder::Acquire, MemoryOrder::Release, MemoryOrder::AcqRel, MemoryOrder::SeqCst})
    {
        int e = a.Load();
        while (!a.CompareExchangeWeak(e, e + 1, order))
        {
        }
    }
    CHECK(a.Load() == 105);

    CHECK((++a) == 106);
    CHECK((a++) == 106);
    CHECK((--a) == 106);
    CHECK((a--) == 106);
    a = 77;
    int plain = a;
    CHECK(plain == 77);
    STATIC_REQUIRE(Atomic<int>::kAlwaysLockFree == std::atomic<int>::is_always_lock_free);
    STATIC_REQUIRE(sizeof(Atomic<u64>) == sizeof(std::atomic<u64>));
}

TEST_CASE("Atomic works for bool, pointers, enums, 64-bit integers and floats", "[sync]")
{
    Atomic<bool> flag(false);
    CHECK(!flag.Exchange(true));
    CHECK(flag.Load());

    int items[4] = {};
    Atomic<int*> p(items);
    CHECK(p.FetchAdd(2) == items);
    CHECK(p.Load() == items + 2);

    Atomic<State> state(State::Idle);
    State expect = State::Idle;
    CHECK(state.CompareExchangeStrong(expect, State::Busy));
    CHECK(state.Load() == State::Busy);

    Atomic<u64> big(0xFFFFFFFFFFull);
    CHECK(big.FetchAdd(1) == 0xFFFFFFFFFFull);
    Atomic<f32> f(1.5f);
    CHECK(f.FetchAdd(0.5f) == 1.5f);
    CHECK(f.Load() == 2.0f);
}

TEST_CASE("Atomic fetch operations are atomic under contention", "[sync]")
{
    constexpr int kThreads = 8, kIterations = 100000;
    Atomic<u64> counter(0);
    RunThreads(kThreads,
               [&](int)
               {
                   for (int i = 0; i < kIterations; ++i)
                   {
                       counter.FetchAdd(1, MemoryOrder::Relaxed);
                   }
               });
    CHECK(counter.Load() == static_cast<u64>(kThreads) * kIterations);

    Atomic<u64> via_cas(0);
    RunThreads(kThreads,
               [&](int)
               {
                   for (int i = 0; i < kIterations; ++i)
                   {
                       u64 e = via_cas.Load(MemoryOrder::Relaxed);
                       while (!via_cas.CompareExchangeWeak(e, e + 1, MemoryOrder::AcqRel, MemoryOrder::Relaxed))
                       {
                       }
                   }
               });
    CHECK(via_cas.Load() == static_cast<u64>(kThreads) * kIterations);

    Atomic<u32> bits(0);
    RunThreads(kThreads,
               [&](int t)
               {
                   for (int i = 0; i < 1000; ++i)
                   {
                       bits.FetchOr(1u << t);
                   }
               });
    CHECK(bits.Load() == 0xFFu);
}

TEST_CASE("Fences compile and run", "[sync]")
{
#if !ENGINE_SANITIZER_THREAD  // GCC rejects atomic_thread_fence under -fsanitize=thread (-Werror=tsan)
    AtomicThreadFence(MemoryOrder::SeqCst);
    AtomicThreadFence(MemoryOrder::AcqRel);
#endif
    AtomicSignalFence(MemoryOrder::Release);
    SUCCEED();
}

TEST_CASE("Mutex gives mutual exclusion", "[sync]")
{
    Mutex mutex;
    int plain = 0;
    RunThreads(8,
               [&](int)
               {
                   for (int i = 0; i < 50000; ++i)
                   {
                       LockGuard lock(mutex);
                       ++plain;
                   }
               });
    CHECK(plain == 8 * 50000);

    CHECK(mutex.TryLock());
    std::atomic<bool> other_got_it{true};
    std::thread t([&] { other_got_it = mutex.TryLock(); });
    t.join();
    CHECK(!other_got_it);
    mutex.Unlock();
}

TEST_CASE("RecursiveMutex can be taken twice by one thread", "[sync]")
{
    RecursiveMutex mutex;
    LockGuard<RecursiveMutex> outer(mutex);
    LockGuard<RecursiveMutex> inner(mutex);
    CHECK(mutex.TryLock());
    mutex.Unlock();
}

TEST_CASE("UniqueLock locks, unlocks early and relocks", "[sync]")
{
    Mutex mutex;
    UniqueLock<Mutex> lock(mutex);
    CHECK(lock.OwnsLock());
    lock.Unlock();
    CHECK(!lock.OwnsLock());
    CHECK(mutex.TryLock());
    mutex.Unlock();
    lock.Lock();
    CHECK(lock.OwnsLock());
    UniqueLock<Mutex> deferred(mutex, DeferLock);
    CHECK(!deferred.OwnsLock());
    lock.Unlock();
    CHECK(deferred.TryLock());
    UniqueLock<Mutex> moved(Move(deferred));
    CHECK(moved.OwnsLock());
    CHECK(!deferred.OwnsLock());
}

TEST_CASE("ConditionVariable: ping-pong, predicate wait and timeout", "[sync]")
{
    Mutex mutex;
    ConditionVariable cv;
    int turn = 0;
    int rounds = 0;
    Thread other(
        [&]
        {
            for (int i = 0; i < 1000; ++i)
            {
                UniqueLock<Mutex> lock(mutex);
                cv.Wait(lock, [&] { return turn == 1; });
                turn = 0;
                ++rounds;
                cv.NotifyAll();
            }
        });
    for (int i = 0; i < 1000; ++i)
    {
        UniqueLock<Mutex> lock(mutex);
        turn = 1;
        cv.NotifyOne();
        cv.Wait(lock, [&] { return turn == 0; });
    }
    other.Join();
    CHECK(rounds == 1000);

    UniqueLock<Mutex> lock(mutex);
    CHECK(!cv.WaitForMilliseconds(lock, 5, [] { return false; }));
    CHECK(!cv.WaitForMilliseconds(lock, 5));
    CHECK(cv.WaitForMilliseconds(lock, 5, [] { return true; }));
}

TEST_CASE("Thread: join, detach, move, ids, arguments", "[sync]")
{
    Thread empty;
    CHECK(!empty.Joinable());
    CHECK(empty.Id() == ThreadId());

    Atomic<int> ran(0);
    Thread t([&ran](int add) { ran.FetchAdd(add); }, 7);
    CHECK(t.Joinable());
    CHECK(t.Id() != ThisThread::Id());
    Thread moved(Move(t));
    CHECK(!t.Joinable());
    moved.Join();
    CHECK(!moved.Joinable());
    CHECK(ran.Load() == 7);

    Atomic<bool> done(false);
    Thread d([&done] { done.Store(true); });
    d.Detach();
    CHECK(!d.Joinable());
    while (!done.Load())
    {
        ThisThread::Yield();
    }

    CHECK(ThisThread::Id() == ThisThread::Id());
    CHECK(ThisThread::Id().Hash() == ThisThread::Id().Hash());
    CHECK(Thread::HardwareConcurrency() == std::thread::hardware_concurrency());
    ThisThread::SleepForMilliseconds(1);
    ThisThread::SleepForMicroseconds(10);
}

TEST_CASE("CallOnce runs once", "[sync]")
{
    OnceFlag flag;
    Atomic<int> calls(0);
    RunThreads(8, [&](int) { CallOnce(flag, [&] { calls.FetchAdd(1); }); });
    CHECK(calls.Load() == 1);
}

// The wrapper and the standard type in one binary, one loop each: the ratio is the proof they cost the same. The check
// is loose (a factor of 5 and a few ns) because a Debug build with sanitizers and a loaded machine are noisy; the
// rounds of the two types alternate, so a burst of load hits both, and each takes the best of many rounds. The numbers
// are printed.
TEST_CASE("Benchmark: the wrappers cost what the standard types cost", "[sync][benchmark]")
{
    using Clock = std::chrono::steady_clock;
    constexpr int kN = 2000000;
    auto once = [](auto&& body)
    {
        auto t0 = Clock::now();
        body();
        return std::chrono::duration<double, std::nano>(Clock::now() - t0).count() / kN;
    };
    // Best of 15 alternating rounds of each: returns {standard, wrapper}.
    auto pair = [&once](auto&& standard, auto&& wrapper)
    {
        double best_std = 1e30;
        double best_wrap = 1e30;
        for (int round = 0; round < 15; ++round)
        {
            best_std = std::min(best_std, once(standard));
            best_wrap = std::min(best_wrap, once(wrapper));
        }
        return std::pair<double, double>(best_std, best_wrap);
    };
    std::atomic<u64> sa{0};
    Atomic<u64> wa{0};
    const auto add_times = pair(
        [&]
        {
            for (int i = 0; i < kN; ++i)
            {
                sa.fetch_add(1, std::memory_order_relaxed);
            }
        },
        [&]
        {
            for (int i = 0; i < kN; ++i)
            {
                wa.FetchAdd(1, MemoryOrder::Relaxed);
            }
        });
    std::mutex sm;
    Mutex wm;
    u64 sink = 0;
    const auto lock_times = pair(
        [&]
        {
            for (int i = 0; i < kN; ++i)
            {
                std::lock_guard<std::mutex> g(sm);
                ++sink;
            }
        },
        [&]
        {
            for (int i = 0; i < kN; ++i)
            {
                LockGuard g(wm);
                ++sink;
            }
        });
    const auto cas_times = pair(
        [&]
        {
            for (int i = 0; i < kN; ++i)
            {
                u64 e = sa.load(std::memory_order_relaxed);
                sa.compare_exchange_weak(e, e + 1, std::memory_order_acq_rel, std::memory_order_relaxed);
            }
        },
        [&]
        {
            for (int i = 0; i < kN; ++i)
            {
                u64 e = wa.Load(MemoryOrder::Relaxed);
                wa.CompareExchangeWeak(e, e + 1, MemoryOrder::AcqRel, MemoryOrder::Relaxed);
            }
        });
    const double std_add = add_times.first;
    const double my_add = add_times.second;
    const double std_lock = lock_times.first;
    const double my_lock = lock_times.second;
    const double std_cas = cas_times.first;
    const double my_cas = cas_times.second;
    std::printf(
        "sync benchmark (ns/op): fetch_add std %.2f wrapper %.2f | lock+unlock std %.2f wrapper %.2f | cas std %.2f wrapper %.2f (sink %llu)\n",
        std_add, my_add, std_lock, my_lock, std_cas, my_cas, static_cast<unsigned long long>(sink));
    CHECK(my_add < std_add * 5 + 3);
    CHECK(my_lock < std_lock * 5 + 3);
    CHECK(my_cas < std_cas * 5 + 3);
}

TEST_CASE("Timed waits clamp a huge duration instead of wrapping negative", "[sync]")
{
    Mutex mutex;
    ConditionVariable cv;
    UniqueLock<Mutex> lock(mutex);
    bool ready = false;
    Thread waker(
        [&]
        {
            ThisThread::SleepForMilliseconds(5);
            LockGuard guard(mutex);
            ready = true;
            cv.NotifyAll();
        });
    CHECK(cv.WaitForMilliseconds(lock, ~u64{0}, [&] { return ready; }));
    waker.Join();
    ThisThread::SleepForMicroseconds(1);
}
