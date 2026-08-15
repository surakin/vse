#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

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
    std::mutex mutex;

    std::thread worker([&]() {
        for (i32 i = 0; i < kIncrementsPerThread; ++i) {
            std::lock_guard<std::mutex> lock(mutex);
            ++counter;
        }
    });

    for (i32 i = 0; i < kIncrementsPerThread; ++i) {
        std::lock_guard<std::mutex> lock(mutex);
        ++counter;
    }

    worker.join();
    REQUIRE(counter == kIncrementsPerThread * 2);
}

TEST_CASE("std::atomic<int> survives concurrent increment from two threads with no mutex", "[concurrency]") {
    std::atomic<int> counter{0};

    std::thread worker([&]() {
        for (i32 i = 0; i < kIncrementsPerThread; ++i) {
            counter.fetch_add(1, std::memory_order_relaxed);
        }
    });

    for (i32 i = 0; i < kIncrementsPerThread; ++i) {
        counter.fetch_add(1, std::memory_order_relaxed);
    }

    worker.join();
    REQUIRE(counter.load() == kIncrementsPerThread * 2);
}

TEST_CASE("std::condition_variable hands off data from a producer to a consumer thread", "[concurrency]") {
    std::mutex mutex;
    std::condition_variable cv;
    bool data_ready = false;
    int payload = 0;
    int consumed = 0;

    std::thread consumer([&]() {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return data_ready; });
        consumed = payload;
    });

    {
        std::lock_guard<std::mutex> lock(mutex);
        payload = 42;
        data_ready = true;
    }
    cv.notify_one();

    consumer.join();
    REQUIRE(consumed == 42);
}
