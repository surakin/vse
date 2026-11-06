// Built into its own executable with ENGINE_DEBUG_CHECKS=0 and ENGINE_SLOW_CHECKS=0 (see CMakeLists.txt), so it exercises
// what the assert macros do in a release configuration.
#include <catch2/catch_test_macros.hpp>

#include "log/assert.h"

using namespace engine_placeholder;

static_assert(ENGINE_DEBUG_CHECKS == 0, "this file is built with debug checks off");
static_assert(ENGINE_SLOW_CHECKS == 0, "this file is built with slow checks off");

namespace {

int g_off_calls = 0;
AssertKind g_off_kind = AssertKind::Assert;

AssertAction OffCountingHandler(const AssertInfo& info) {
    ++g_off_calls;
    g_off_kind = info.kind;
    return AssertAction::Continue;
}

class OffScope {
public:
    OffScope() : previous_(SetAssertHandler(&OffCountingHandler)) { g_off_calls = 0; }
    ~OffScope() { SetAssertHandler(previous_); }

private:
    AssertHandler previous_;
};

} // namespace

TEST_CASE("With debug checks off, ENGINE_VERIFY still evaluates its expression but does not check it", "[verify][checks-off]") {
    OffScope scope;
    int evaluations = 0;
    ENGINE_VERIFY(++evaluations == 99, "would fail if checked");
    CHECK(evaluations == 1);
    CHECK(g_off_calls == 0);
}

TEST_CASE("With slow checks off, ENGINE_CHECK_SLOW never evaluates its condition", "[check-slow][checks-off]") {
    OffScope scope;
    int evaluations = 0;
    ENGINE_CHECK_SLOW(++evaluations == 99, "would fail if checked");
    CHECK(evaluations == 0);
    CHECK(g_off_calls == 0);
}

TEST_CASE("With debug checks off, ENGINE_UNREACHABLE compiles to a hint and ENGINE_ASSERT still checks", "[assert][checks-off]") {
    OffScope scope;
    volatile bool reached = false;
    if (reached) {
        ENGINE_UNREACHABLE("never executed"); // must compile; running it would be undefined behavior
    }
    ENGINE_ASSERT(false, "assert is always on");
    CHECK(g_off_calls == 1);
    CHECK(g_off_kind == AssertKind::Assert);
    ENGINE_NOT_IMPLEMENTED();
    CHECK(g_off_calls == 2);
    CHECK(g_off_kind == AssertKind::NotImplemented);
}

TEST_CASE("With debug checks off, ENGINE_ENSURE still reports", "[ensure][checks-off]") {
    OffScope scope;
    ResetEnsureStateForTesting();
    CHECK_FALSE(ENGINE_ENSURE(false, "ensure is always on"));
    CHECK(g_off_calls == 1);
    CHECK(g_off_kind == AssertKind::Ensure);
}
