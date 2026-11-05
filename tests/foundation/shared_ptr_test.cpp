#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>
#include <utility>
#include <vector>

#include "containers/shared_ptr.h"
#include "types.h"

using engine_placeholder::LocalSharedPtr;
using engine_placeholder::MakeShared;
using engine_placeholder::PlainRefCount;
using engine_placeholder::SharedPtr;
using engine_placeholder::usize;

namespace {

struct Counted {
    static inline std::atomic<int> alive{0};
    int value;
    explicit Counted(int v = 0) : value(v) { ++alive; }
    Counted(const Counted&) = delete;
    ~Counted() { --alive; }
};

struct Base {
    static inline int destroyed = 0;
    virtual ~Base() { ++destroyed; }
    virtual int Id() const { return 1; }
};
struct Derived : Base {
    int Id() const override { return 2; }
};

} // namespace

TEST_CASE("SharedPtr: the object lives until the last owner goes", "[containers][shared_ptr]") {
    REQUIRE(Counted::alive == 0);
    SharedPtr<Counted> a = MakeShared<Counted>(4);
    REQUIRE(a.UseCount() == 1);
    {
        SharedPtr<Counted> b = a;
        REQUIRE(a.UseCount() == 2);
        REQUIRE(b->value == 4);
        a.Reset();
        REQUIRE_FALSE(a);
        REQUIRE(b.UseCount() == 1);
        REQUIRE(Counted::alive == 1);
    }
    REQUIRE(Counted::alive == 0);
}

TEST_CASE("SharedPtr: move transfers the share without touching the count; assignment releases the old object", "[containers][shared_ptr]") {
    SharedPtr<Counted> a = MakeShared<Counted>(1);
    SharedPtr<Counted> b = std::move(a);
    REQUIRE_FALSE(a);
    REQUIRE(b.UseCount() == 1);

    SharedPtr<Counted> c = MakeShared<Counted>(2);
    REQUIRE(Counted::alive == 2);
    c = b; // the object c held is released
    REQUIRE(Counted::alive == 1);
    REQUIRE(b.UseCount() == 2);
    c = c; // self-assignment is harmless
    REQUIRE(b.UseCount() == 2);
    b.Reset();
    c.Reset();
    REQUIRE(Counted::alive == 0);
}

TEST_CASE("SharedPtr: a derived pointer converts to a base pointer", "[containers][shared_ptr]") {
    Base::destroyed = 0;
    {
        SharedPtr<Derived> d = MakeShared<Derived>();
        SharedPtr<Base> b = d;
        REQUIRE(b->Id() == 2);
        REQUIRE(d.UseCount() == 2);
    }
    REQUIRE(Base::destroyed == 1);
}

TEST_CASE("LocalSharedPtr: the non-atomic policy counts the same way", "[containers][shared_ptr]") {
    LocalSharedPtr<Counted> a = MakeShared<Counted, PlainRefCount>(9);
    {
        LocalSharedPtr<Counted> b = a;
        REQUIRE(a.UseCount() == 2);
    }
    REQUIRE(a.UseCount() == 1);
    a.Reset();
    REQUIRE(Counted::alive == 0);
}

TEST_CASE("SharedPtr: owners on many threads copy and release concurrently, and the object dies exactly once", "[containers][shared_ptr][threads]") {
    SharedPtr<Counted> shared = MakeShared<Counted>(1);
    constexpr int kThreads = 8;
    constexpr int kRounds = 20000;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&shared] {
            for (int i = 0; i < kRounds; ++i) {
                SharedPtr<Counted> mine = shared; // distinct pointer objects, one shared block
                SharedPtr<Counted> other = mine;
                (void)other;
            }
        });
    }
    for (std::thread& th : threads) th.join();
    REQUIRE(shared.UseCount() == 1);
    REQUIRE(Counted::alive == 1);
    shared.Reset();
    REQUIRE(Counted::alive == 0);
}

TEST_CASE("SharedPtr: the last owner may be released on another thread", "[containers][shared_ptr][threads]") {
    SharedPtr<Counted> shared = MakeShared<Counted>(1);
    std::thread t([p = std::move(shared)]() mutable { p.Reset(); });
    t.join();
    REQUIRE(Counted::alive == 0);
}
