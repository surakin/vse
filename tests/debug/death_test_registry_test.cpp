#include <catch2/catch_test_macros.hpp>

#include "debug/crash_reason.h"
#include "debug/death_test.h"
#include "resource_guard.h"
#include "support/death_test_skip.h"

#include "cstr/cstr.h"

using namespace engine_placeholder;
using namespace engine_placeholder::debug;

ENGINE_DEATH_TEST_FN(dt_registry_marker) {}

TEST_CASE("A registered death test is found by name and an unknown one is not", "[death][debug]")
{
    const DeathTestEntry* entry = FindDeathTest("dt_registry_marker");
    REQUIRE(entry != nullptr);
    CHECK(CStrCompare(entry->name, "dt_registry_marker") == 0);
    CHECK(entry->fn != nullptr);
    CHECK(FindDeathTest("no_such_death_test") == nullptr);
    CHECK(FindDeathTest("dt_registry_mark") == nullptr);  // a prefix is not a match
}

TEST_CASE("Asking a child for an unregistered death test ends with the unknown-test exit code", "[death][debug]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    ChildResourceGuard guard;
    const ChildResult r = RunInChild("no_such_death_test");
    CHECK(r.supported);
    CHECK(r.started);
    CHECK(r.exited);
    CHECK(r.exit_code == kUnknownDeathTestExitCode);
    CHECK(r.StderrContains("no death test is registered"));
    CHECK(guard.Clean());
}

TEST_CASE("RunInChild reports an unsupported platform without starting anything", "[death][debug]")
{
    ChildResourceGuard guard;
    SetDeathTestSupportForTesting(false);
    CHECK_FALSE(DeathTestsSupported());
    const ChildResult r = RunInChild("dt_registry_marker");
    SetDeathTestSupportForTesting(true);
    CHECK_FALSE(r.supported);
    CHECK_FALSE(r.started);
    CHECK_FALSE(r.exited);
    CHECK_FALSE(r.terminated);
    CHECK(guard.Clean());
}

TEST_CASE("Crash reasons have stable names", "[debug]")
{
    CHECK(CStrCompare(CrashReasonName(CrashReason::None), "None") == 0);
    CHECK(CStrCompare(CrashReasonName(CrashReason::AccessViolation), "AccessViolation") == 0);
    CHECK(CStrCompare(CrashReasonName(CrashReason::BusError), "BusError") == 0);
    CHECK(CStrCompare(CrashReasonName(CrashReason::IllegalInstruction), "IllegalInstruction") == 0);
    CHECK(CStrCompare(CrashReasonName(CrashReason::ArithmeticFault), "ArithmeticFault") == 0);
    CHECK(CStrCompare(CrashReasonName(CrashReason::StackOverflow), "StackOverflow") == 0);
    CHECK(CStrCompare(CrashReasonName(CrashReason::Abort), "Abort") == 0);
    CHECK(CStrCompare(CrashReasonName(CrashReason::AssertFailed), "AssertFailed") == 0);
    CHECK(CStrCompare(CrashReasonName(CrashReason::EnsureFailed), "EnsureFailed") == 0);
    CHECK(CStrCompare(CrashReasonName(static_cast<CrashReason>(250)), "Unknown") == 0);
}
