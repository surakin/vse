#include <catch2/catch_test_macros.hpp>

#include "sync/atomic.h"
#include "sync/thread.h"
#include "lang/utility.h"
#include <vector>

#include "containers/shared_ptr.h"
#include "types.h"

using engine_placeholder::LocalSharedPtr;
using engine_placeholder::MakeShared;
using engine_placeholder::PlainRefCount;
using engine_placeholder::SharedPtr;
using engine_placeholder::usize;

namespace {

struct SharedPtrTestCounted {
    static inline engine_placeholder::Atomic<int> alive{0};
    int value;
    explicit SharedPtrTestCounted(int v = 0) : value(v) { ++alive; }
    SharedPtrTestCounted(const SharedPtrTestCounted&) = delete;
    ~SharedPtrTestCounted() { --alive; }
};

struct SharedPtrTestBase {
    static inline int destroyed = 0;
    virtual ~SharedPtrTestBase() { ++destroyed; }
    virtual int Id() const { return 1; }
};
struct SharedPtrTestDerived : SharedPtrTestBase {
    int Id() const override { return 2; }
};

} // namespace

TEST_CASE("SharedPtr: the object lives until the last owner goes", "[containers][shared_ptr]") {
    REQUIRE(SharedPtrTestCounted::alive == 0);
    SharedPtr<SharedPtrTestCounted> a = MakeShared<SharedPtrTestCounted>(4);
    REQUIRE(a.UseCount() == 1);
    {
        SharedPtr<SharedPtrTestCounted> b = a;
        REQUIRE(a.UseCount() == 2);
        REQUIRE(b->value == 4);
        a.Reset();
        REQUIRE_FALSE(a);
        REQUIRE(b.UseCount() == 1);
        REQUIRE(SharedPtrTestCounted::alive == 1);
    }
    REQUIRE(SharedPtrTestCounted::alive == 0);
}

TEST_CASE("SharedPtr: move transfers the share without touching the count; assignment releases the old object", "[containers][shared_ptr]") {
    SharedPtr<SharedPtrTestCounted> a = MakeShared<SharedPtrTestCounted>(1);
    SharedPtr<SharedPtrTestCounted> b = engine_placeholder::Move(a);
    REQUIRE_FALSE(a);
    REQUIRE(b.UseCount() == 1);

    SharedPtr<SharedPtrTestCounted> c = MakeShared<SharedPtrTestCounted>(2);
    REQUIRE(SharedPtrTestCounted::alive == 2);
    c = b; // the object c held is released
    REQUIRE(SharedPtrTestCounted::alive == 1);
    REQUIRE(b.UseCount() == 2);
    SharedPtr<SharedPtrTestCounted>& alias = c;
    c = alias; // self-assignment is harmless
    REQUIRE(b.UseCount() == 2);
    b.Reset();
    c.Reset();
    REQUIRE(SharedPtrTestCounted::alive == 0);
}

TEST_CASE("SharedPtr: a derived pointer converts to a base pointer", "[containers][shared_ptr]") {
    SharedPtrTestBase::destroyed = 0;
    {
        SharedPtr<SharedPtrTestDerived> d = MakeShared<SharedPtrTestDerived>();
        SharedPtr<SharedPtrTestBase> b = d;
        REQUIRE(b->Id() == 2);
        REQUIRE(d.UseCount() == 2);
    }
    REQUIRE(SharedPtrTestBase::destroyed == 1);
}

TEST_CASE("LocalSharedPtr: the non-atomic policy counts the same way", "[containers][shared_ptr]") {
    LocalSharedPtr<SharedPtrTestCounted> a = MakeShared<SharedPtrTestCounted, PlainRefCount>(9);
    {
        LocalSharedPtr<SharedPtrTestCounted> b = a;
        REQUIRE(a.UseCount() == 2);
    }
    REQUIRE(a.UseCount() == 1);
    a.Reset();
    REQUIRE(SharedPtrTestCounted::alive == 0);
}

TEST_CASE("SharedPtr: owners on many threads copy and release concurrently, and the object dies exactly once", "[containers][shared_ptr][threads]") {
    SharedPtr<SharedPtrTestCounted> shared = MakeShared<SharedPtrTestCounted>(1);
    constexpr int kThreads = 8;
    constexpr int kRounds = 20000;
    std::vector<engine_placeholder::Thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&shared] {
            for (int i = 0; i < kRounds; ++i) {
                SharedPtr<SharedPtrTestCounted> mine = shared; // distinct pointer objects, one shared block
                SharedPtr<SharedPtrTestCounted> other = mine;
                (void)other;
            }
        });
    }
    for (engine_placeholder::Thread& th : threads) th.Join();
    REQUIRE(shared.UseCount() == 1);
    REQUIRE(SharedPtrTestCounted::alive == 1);
    shared.Reset();
    REQUIRE(SharedPtrTestCounted::alive == 0);
}

TEST_CASE("SharedPtr: the last owner may be released on another thread", "[containers][shared_ptr][threads]") {
    SharedPtr<SharedPtrTestCounted> shared = MakeShared<SharedPtrTestCounted>(1);
    engine_placeholder::Thread t([p = engine_placeholder::Move(shared)]() mutable { p.Reset(); });
    t.Join();
    REQUIRE(SharedPtrTestCounted::alive == 0);
}
