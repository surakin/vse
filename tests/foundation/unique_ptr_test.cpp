#include <catch2/catch_test_macros.hpp>

#include "lang/utility.h"

#include "containers/unique_ptr.h"
#include "types.h"

using engine_placeholder::DefaultDelete;
using engine_placeholder::MakeUnique;
using engine_placeholder::UniquePtr;
using engine_placeholder::usize;

namespace {

struct Counted {
    static inline int alive = 0;
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
    int payload[8] = {};
    int Id() const override { return 2; }
};

struct CountingDeleter {
    int* calls;
    void operator()(Counted* p) const {
        ++*calls;
        DefaultDelete<Counted>()(p);
    }
};
using TrackedPtr = UniquePtr<Counted, CountingDeleter>;

} // namespace

TEST_CASE("UniquePtr: owns one object and destroys it once", "[containers][unique_ptr]") {
    REQUIRE(Counted::alive == 0);
    {
        UniquePtr<Counted> p = MakeUnique<Counted>(5);
        REQUIRE(p);
        REQUIRE(Counted::alive == 1);
        REQUIRE(p->value == 5);
        REQUIRE((*p).value == 5);
        REQUIRE(p.Get() == &*p);
    }
    REQUIRE(Counted::alive == 0);
}

TEST_CASE("UniquePtr: move leaves the source empty; assignment destroys the old object", "[containers][unique_ptr]") {
    UniquePtr<Counted> a = MakeUnique<Counted>(1);
    UniquePtr<Counted> b = engine_placeholder::Move(a);
    REQUIRE_FALSE(a);
    REQUIRE(b->value == 1);

    UniquePtr<Counted> c = MakeUnique<Counted>(2);
    REQUIRE(Counted::alive == 2);
    c = engine_placeholder::Move(b); // the object c held is destroyed
    REQUIRE(Counted::alive == 1);
    REQUIRE(c->value == 1);
    c = nullptr;
    REQUIRE(Counted::alive == 0);
}

TEST_CASE("UniquePtr: Release gives up ownership, Reset replaces the object", "[containers][unique_ptr]") {
    UniquePtr<Counted> p = MakeUnique<Counted>(3);
    Counted* raw = p.Release();
    REQUIRE_FALSE(p);
    REQUIRE(Counted::alive == 1);
    p.Reset(raw); // takes it back
    REQUIRE(p.Get() == raw);
    p.Reset();
    REQUIRE(Counted::alive == 0);
}

TEST_CASE("UniquePtr: a derived pointer converts to a base pointer and deletes through the virtual destructor", "[containers][unique_ptr]") {
    Base::destroyed = 0;
    {
        UniquePtr<Base> base = MakeUnique<Derived>();
        REQUIRE(base->Id() == 2);
    }
    REQUIRE(Base::destroyed == 1);
}

TEST_CASE("UniquePtr: a custom deleter is called with the pointer and has its own alias", "[containers][unique_ptr]") {
    int calls = 0;
    {
        TrackedPtr p = MakeUnique<Counted, CountingDeleter>(7);
        // MakeUnique cannot know the deleter's state, so install the real one.
        p.GetDeleter().calls = &calls;
        REQUIRE(p->value == 7);
    }
    REQUIRE(calls == 1);
    REQUIRE(Counted::alive == 0);
}

TEST_CASE("UniquePtr: an empty deleter adds no size", "[containers][unique_ptr]") {
    static_assert(sizeof(UniquePtr<Counted>) == sizeof(Counted*));
    static_assert(sizeof(TrackedPtr) == 2 * sizeof(void*));
    REQUIRE(true);
}

TEST_CASE("MakeUnique: a large object is made and freed", "[containers][unique_ptr]") {
    struct Huge {
        unsigned char bytes[static_cast<usize>(1) << 20];
    };
    UniquePtr<Huge> p = MakeUnique<Huge>();
    REQUIRE(p);
}
