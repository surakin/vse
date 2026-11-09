#include "sync/atomic.h"
#include "sync/mutex.h"
#include "sync/thread.h"

#include <catch2/catch_test_macros.hpp>

#include "types.h"

using engine_placeholder::i32;

namespace {

// Repeated high enough to make a lost update (missing lock/fence) show
// up reliably rather than by luck; still fast (well under a second).
constexpr i32 kIncrementsPerThread = 500'000;

} // namespace

TEST_CASE("std::mutex guards a shared counter incremented from two threads", "[concurrency]") {
    int counter = 0;
    engine_placeholder::Mutex mutex;

    engine_placeholder::Thread worker([&]() {
        for (i32 i = 0; i < kIncrementsPerThread; ++i) {
            engine_placeholder::LockGuard<engine_placeholder::Mutex> lock(mutex);
            ++counter;
        }
    });

    for (i32 i = 0; i < kIncrementsPerThread; ++i) {
        engine_placeholder::LockGuard<engine_placeholder::Mutex> lock(mutex);
        ++counter;
    }

    worker.Join();
    REQUIRE(counter == kIncrementsPerThread * 2);
}

TEST_CASE("std::atomic<int> survives concurrent increment from two threads with no mutex", "[concurrency]") {
    engine_placeholder::Atomic<int> counter{0};

    engine_placeholder::Thread worker([&]() {
        for (i32 i = 0; i < kIncrementsPerThread; ++i) {
            counter.FetchAdd(1, engine_placeholder::MemoryOrder::Relaxed);
        }
    });

    for (i32 i = 0; i < kIncrementsPerThread; ++i) {
        counter.FetchAdd(1, engine_placeholder::MemoryOrder::Relaxed);
    }

    worker.Join();
    REQUIRE(counter.Load() == kIncrementsPerThread * 2);
}

TEST_CASE("std::condition_variable hands off data from a producer to a consumer thread", "[concurrency]") {
    engine_placeholder::Mutex mutex;
    engine_placeholder::ConditionVariable cv;
    bool data_ready = false;
    int payload = 0;
    int consumed = 0;

    engine_placeholder::Thread consumer([&]() {
        engine_placeholder::UniqueLock<engine_placeholder::Mutex> lock(mutex);
        cv.Wait(lock, [&] { return data_ready; });
        consumed = payload;
    });

    {
        engine_placeholder::LockGuard<engine_placeholder::Mutex> lock(mutex);
        payload = 42;
        data_ready = true;
    }
    cv.NotifyOne();

    consumer.Join();
    REQUIRE(consumed == 42);
}
