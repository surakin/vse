#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>

#include "containers/shared_ptr.h"
#include "containers/vector.h"

using namespace engine_placeholder;

namespace {

struct Counted {
    static std::atomic<int> alive;
    int value = 7;
    Counted() { alive.fetch_add(1); }
    ~Counted() { alive.fetch_sub(1); }
};
std::atomic<int> Counted::alive{0};

} // namespace

// Run under ThreadSanitizer this proves the reference count is atomic: the plain-count variant is reported as a race (checked once by hand on Phase 5 Day 80b).
TEST_CASE("SharedPtr: copies and releases from eight threads leave no object behind and no race", "[containers][shared_ptr][threads]") {
    constexpr int kThreads = 8;
    constexpr int kRounds = 20000;
    SharedPtr<Counted> shared = MakeShared<Counted>();
    REQUIRE(Counted::alive.load() == 1);
    std::atomic<int> wrong{0};
    Vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.Push(std::thread([&] {
            for (int i = 0; i < kRounds; ++i) {
                SharedPtr<Counted> mine = shared; // copy: increment
                SharedPtr<Counted> other = mine;
                if (other->value != 7) wrong.fetch_add(1);
            }                                      // release both: decrement
        }));
    }
    for (usize t = 0; t < threads.Size(); ++t) threads[t].join();
    CHECK(wrong.load() == 0);
    CHECK(Counted::alive.load() == 1);
    shared = SharedPtr<Counted>();
    CHECK(Counted::alive.load() == 0);
}
