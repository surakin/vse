#include <catch2/catch_test_macros.hpp>

#include "debug/compiler.h"

#include "debug/death_test.h"
#include "log/assert.h"
#include "os/os.h"
#include "resource_guard.h"
#include "support/death_test_skip.h"

#include "sync/atomic.h"
#include <cstring>
#include <string>
#include "sync/thread.h"
#include "cstr/cstr.h"
#include "cstr/mem_ops.h"

using namespace engine_placeholder;
using namespace engine_placeholder::debug;

namespace {

// The failure the capture handler saw last, and how many it has seen. The counters are atomic because some tests fail
// checks from several threads; the strings are literals, so copying the pointers is safe.
struct FamilyCapture {
    Atomic<int> calls{0};
    AssertKind kind = AssertKind::Assert;
    const char* file = nullptr;
    int line = 0;
    const char* function = nullptr;
    const char* condition = nullptr;
    const char* message = nullptr;
};
FamilyCapture g_family;

AssertAction FamilyCaptureContinue(const AssertInfo& info) {
    g_family.kind = info.kind;
    g_family.file = info.file;
    g_family.line = info.line;
    g_family.function = info.function;
    g_family.condition = info.condition;
    g_family.message = info.message;
    g_family.calls.FetchAdd(1, MemoryOrder::Relaxed);
    return AssertAction::Continue;
}

AssertAction FamilyCountOnly(const AssertInfo&) {
    g_family.calls.FetchAdd(1, MemoryOrder::Relaxed);
    return AssertAction::Continue;
}

// Installs a handler for the life of the scope and restores the previous one.
class FamilyHandlerScope {
public:
    explicit FamilyHandlerScope(AssertHandler handler) : previous_(SetAssertHandler(handler)) {
        g_family.calls = 0;
        g_family.line = 0;
        ResetEnsureStateForTesting();
    }
    ~FamilyHandlerScope() { SetAssertHandler(previous_); }

private:
    AssertHandler previous_;
};

ENGINE_NOINLINE bool FamilyEnsureSiteA(bool value) {
    return ENGINE_ENSURE(value, "site A condition");
}
ENGINE_NOINLINE bool FamilyEnsureSiteB(bool value) {
    return ENGINE_ENSURE(value, "site B condition");
}
ENGINE_NOINLINE bool FamilyEnsureSiteC(bool value) {
    return ENGINE_ENSURE(value, "site C condition");
}

struct FamilySiteList {
    int count = 0;
    u32 counts[8] = {};
    int lines[8] = {};
};

void FamilyCollectSites(const EnsureSiteInfo& site, void* context) {
    FamilySiteList* list = static_cast<FamilySiteList*>(context);
    if (list->count < 8) {
        list->counts[list->count] = site.failure_count;
        list->lines[list->count] = site.line;
        ++list->count;
    }
}

// Write sink that keeps what the default handler prints.
char g_family_text[16384];
usize g_family_text_length = 0;
int g_family_sink_calls = 0;
long FamilyCapturingSink(int, const void* buf, usize len) {
    ++g_family_sink_calls;
    const usize room = sizeof(g_family_text) - g_family_text_length;
    const usize take = len < room ? len : room;
    MemCopy(g_family_text + g_family_text_length, buf, take);
    g_family_text_length += take;
    return static_cast<long>(len);
}

// Length of the first line of the captured text, newline included (0 if there is none): the default handler prints the
// message line first and the stack trace after it.
usize FamilyFirstLineLength() {
    for (usize i = 0; i < g_family_text_length; ++i) {
        if (g_family_text[i] == '\n') {
            return i + 1;
        }
    }
    return 0;
}

} // namespace

// ---- children for the death tests ---------------------------------------------------------------------------------

ENGINE_DEATH_TEST_FN(dt_family_assert_false) {
    ENGINE_ASSERT(1 + 1 == 3, "arithmetic broke");
}

ENGINE_DEATH_TEST_FN(dt_family_verify_false) {
    ENGINE_VERIFY(false, "verify failed on purpose");
}

ENGINE_DEATH_TEST_FN(dt_family_check_slow_false) {
    ENGINE_CHECK_SLOW(false, "slow check failed on purpose");
}

ENGINE_DEATH_TEST_FN(dt_family_unreachable) {
    ENGINE_UNREACHABLE("cannot get here");
}

// Even a handler that asks to continue cannot make an unreachable place continue.
ENGINE_DEATH_TEST_FN(dt_family_unreachable_with_continue_handler) {
    SetAssertHandler(&FamilyCountOnly);
    ENGINE_UNREACHABLE("cannot get here");
}

ENGINE_DEATH_TEST_FN(dt_family_not_implemented) {
    ENGINE_NOT_IMPLEMENTED();
}

ENGINE_DEATH_TEST_FN(dt_family_ensure_continues) {
    for (int i = 0; i < 3; ++i) {
        FamilyEnsureSiteA(false); // three failures, one report
    }
    os::Write(1, "AFTER-ENSURE\n", 13);
}

ENGINE_DEATH_TEST_FN(dt_family_legacy_assert_fail) {
    SetAssertHandler(&FamilyCountOnly); // a handler that continues still cannot keep AssertFail from aborting
    AssertFail("legacy.cpp", 42, "legacy message");
}

// ---- tests ----------------------------------------------------------------------------------------------------------

TEST_CASE("A failed ENGINE_ASSERT goes to the handler with the full record and the program carries on if it says so", "[assert][debug]") {
    FamilyHandlerScope scope(&FamilyCaptureContinue);
    const int expected_line = __LINE__ + 1;
    ENGINE_ASSERT(1 + 1 == 3, "arithmetic broke");
    CHECK(g_family.calls == 1);
    CHECK(g_family.kind == AssertKind::Assert);
    CHECK(CStrFind(g_family.file, "assert_family_test.cpp") != nullptr);
    CHECK(g_family.line == expected_line);
    CHECK(g_family.function != nullptr);
    CHECK(g_family.function[0] != '\0');
    CHECK(CStrCompare(g_family.condition, "1 + 1 == 3") == 0);
    CHECK(CStrCompare(g_family.message, "arithmetic broke") == 0);
}

TEST_CASE("A passing ENGINE_ASSERT does not call the handler and evaluates its condition once", "[assert][debug]") {
    FamilyHandlerScope scope(&FamilyCaptureContinue);
    int evaluations = 0;
    ENGINE_ASSERT(++evaluations == 1, "should pass");
    CHECK(evaluations == 1);
    CHECK(g_family.calls == 0);
}

TEST_CASE("SetAssertHandler returns the previous handler and nullptr restores the default", "[assert][debug]") {
    const AssertHandler original = SetAssertHandler(&FamilyCountOnly);
    CHECK(original == &DefaultAssertHandler);
    CHECK(SetAssertHandler(&FamilyCaptureContinue) == &FamilyCountOnly);
    CHECK(SetAssertHandler(nullptr) == &FamilyCaptureContinue);
    CHECK(SetAssertHandler(original) == &DefaultAssertHandler);
}

TEST_CASE("A handler installed on one thread is used by a failure on another", "[assert][debug]") {
    FamilyHandlerScope scope(&FamilyCountOnly);
    Thread other([] { ENGINE_ASSERT(false, "from another thread"); });
    other.Join();
    CHECK(g_family.calls == 1);
}

TEST_CASE("The default handler truncates a long message at 512 bytes, ends the line, and chooses the action by kind", "[assert][debug]") {
    char message[2000];
    MemSet(message, 'm', sizeof(message) - 1);
    message[sizeof(message) - 1] = '\0';
    g_family_text_length = 0;
    g_family_sink_calls = 0;
    SetWriteSinkForTesting(&FamilyCapturingSink);
    const AssertInfo assert_info{AssertKind::Assert, "a/file.cpp", 7, "Func", "x > 0", message};
    const AssertAction assert_action = DefaultAssertHandler(assert_info);
    const int assert_sink_calls = g_family_sink_calls; // message + trace in one write, then the context sections
    const usize first_length = FamilyFirstLineLength();
    const usize first_total = g_family_text_length;
    const char first_last_byte = first_length > 0 ? g_family_text[first_length - 1] : '\0';
    const bool first_starts_right = CStrCompareN(g_family_text, "ASSERT FAILED: a/file.cpp:7 Func: x > 0: mmm", 43) == 0;
    g_family_text_length = 0;
    g_family_sink_calls = 0;
    const AssertInfo ensure_info{AssertKind::Ensure, "a/file.cpp", 8, "Func", "y > 0", "short"};
    const AssertAction ensure_action = DefaultAssertHandler(ensure_info);
    const int ensure_sink_calls = g_family_sink_calls;
    SetWriteSinkForTesting(&os::Write);

    CHECK(first_length == 512);
    CHECK(assert_sink_calls >= 2);  // a fatal assert also prints the context sections
    CHECK(ensure_sink_calls == 1);  // an ensure is the message and its trace in one write, so concurrent reports do not interleave
    CHECK(first_total > first_length); // the trace follows the line
    CHECK(first_last_byte == '\n'); // even a truncated line ends in a newline
    CHECK(first_starts_right);
    CHECK(assert_action == AssertAction::Abort);   // no debugger attached in a test run
    CHECK(ensure_action == AssertAction::Continue); // ensure never aborts by default
    // The capture was reset before the second call, so its line starts at the beginning.
    const char* expected = "ENSURE FAILED: a/file.cpp:8 Func: y > 0: short\n";
    CHECK(FamilyFirstLineLength() == CStrLength(expected));
    CHECK(CStrCompareN(g_family_text, expected, CStrLength(expected)) == 0);
}

TEST_CASE("The default handler prints each kind with its own label and leaves out empty parts", "[assert][debug]") {
    struct Case {
        AssertKind kind;
        const char* condition;
        const char* expected;
    };
    const Case cases[] = {
        {AssertKind::Assert, "ok", "ASSERT FAILED: f.cpp:1 g: ok: m\n"},
        {AssertKind::Verify, "ok", "VERIFY FAILED: f.cpp:1 g: ok: m\n"},
        {AssertKind::Unreachable, "", "UNREACHABLE REACHED: f.cpp:1 g: m\n"},
        {AssertKind::NotImplemented, "", "NOT IMPLEMENTED: f.cpp:1 g: m\n"},
    };
    for (const Case& c : cases) {
        g_family_text_length = 0;
        SetWriteSinkForTesting(&FamilyCapturingSink);
        DefaultAssertHandler(AssertInfo{c.kind, "f.cpp", 1, "g", c.condition, "m"});
        SetWriteSinkForTesting(&os::Write);
        INFO(c.expected);
        CHECK(FamilyFirstLineLength() == CStrLength(c.expected));
        CHECK(CStrCompareN(g_family_text, c.expected, CStrLength(c.expected)) == 0);
    }
}

TEST_CASE("ENGINE_ENSURE returns its condition and does not report when it holds", "[ensure][debug]") {
    FamilyHandlerScope scope(&FamilyCountOnly);
    CHECK(FamilyEnsureSiteA(true));
    CHECK(g_family.calls == 0);
    CHECK(EnsureFailedSiteCount() == 0);
    int evaluations = 0;
    CHECK(ENGINE_ENSURE(++evaluations == 1, "evaluated once"));
    CHECK(evaluations == 1);
}

TEST_CASE("ENGINE_ENSURE reports the first failure at a site and only counts the rest", "[ensure][debug]") {
    FamilyHandlerScope scope(&FamilyCaptureContinue);
    for (int i = 0; i < 1000; ++i) {
        CHECK_FALSE(FamilyEnsureSiteA(false));
    }
    CHECK(g_family.calls == 1);
    CHECK(g_family.kind == AssertKind::Ensure);
    CHECK(CStrCompare(g_family.message, "site A condition") == 0);
    CHECK(CStrCompare(g_family.condition, "value") == 0);
    FamilySiteList sites;
    ForEachFailedEnsure(&FamilyCollectSites, &sites);
    REQUIRE(sites.count == 1);
    CHECK(sites.counts[0] == 1000);
}

TEST_CASE("Two ENGINE_ENSURE sites are reported once each and counted separately", "[ensure][debug]") {
    FamilyHandlerScope scope(&FamilyCountOnly);
    for (int i = 0; i < 5; ++i) {
        FamilyEnsureSiteA(false);
    }
    for (int i = 0; i < 7; ++i) {
        FamilyEnsureSiteB(false);
    }
    CHECK(g_family.calls == 2);
    CHECK(EnsureFailedSiteCount() == 2);
    FamilySiteList sites;
    ForEachFailedEnsure(&FamilyCollectSites, &sites);
    REQUIRE(sites.count == 2);
    CHECK(sites.counts[0] + sites.counts[1] == 12);
    // Most recently failed first: B, then A.
    CHECK(sites.counts[0] == 7);
    CHECK(sites.counts[1] == 5);
}

TEST_CASE("After the ensure state is reset a site is reported again", "[ensure][debug]") {
    FamilyHandlerScope scope(&FamilyCountOnly);
    FamilyEnsureSiteA(false);
    CHECK(g_family.calls == 1);
    ResetEnsureStateForTesting();
    CHECK(EnsureFailedSiteCount() == 0);
    FamilyEnsureSiteA(false);
    CHECK(g_family.calls == 2);
}

TEST_CASE("Eight threads failing the same ENGINE_ENSURE for the first time produce exactly one report", "[ensure][debug]") {
    for (int round = 0; round < 20; ++round) {
        FamilyHandlerScope scope(&FamilyCountOnly);
        Atomic<int> ready{0};
        Atomic<bool> go{false};
        Thread threads[8];
        for (Thread& t : threads) {
            t = Thread([&] {
                ready.FetchAdd(1);
                while (!go.Load()) {
                    ThisThread::Yield();
                }
                for (int i = 0; i < 100; ++i) {
                    FamilyEnsureSiteC(false);
                }
            });
        }
        while (ready.Load() < 8) {
            ThisThread::Yield();
        }
        go = true;
        for (Thread& t : threads) {
            t.Join();
        }
        REQUIRE(g_family.calls == 1);
        FamilySiteList sites;
        ForEachFailedEnsure(&FamilyCollectSites, &sites);
        REQUIRE(sites.count == 1);
        REQUIRE(sites.counts[0] == 800);
    }
}

TEST_CASE("ENGINE_VERIFY always evaluates its expression and reports a false one as a verify failure", "[verify][debug]") {
    FamilyHandlerScope scope(&FamilyCaptureContinue);
    int evaluations = 0;
    ENGINE_VERIFY(++evaluations == 1, "passes");
    CHECK(evaluations == 1);
    CHECK(g_family.calls == 0);
    ENGINE_VERIFY(++evaluations == 99, "fails");
    CHECK(evaluations == 2);
    CHECK(g_family.calls == 1);
    CHECK(g_family.kind == AssertKind::Verify);
}

TEST_CASE("ENGINE_CHECK_SLOW evaluates and checks when slow checks are on", "[check-slow][debug]") {
    FamilyHandlerScope scope(&FamilyCaptureContinue);
    int evaluations = 0;
    ENGINE_CHECK_SLOW(++evaluations == 1, "passes");
    CHECK(evaluations == 1);
    ENGINE_CHECK_SLOW(false, "fails");
    CHECK(g_family.calls == 1);
    CHECK(g_family.kind == AssertKind::Assert);
}

TEST_CASE("ENGINE_NOT_IMPLEMENTED reports every time in every configuration", "[assert][debug]") {
    FamilyHandlerScope scope(&FamilyCaptureContinue);
    ENGINE_NOT_IMPLEMENTED();
    CHECK(g_family.calls == 1);
    CHECK(g_family.kind == AssertKind::NotImplemented);
    CHECK(CStrCompare(g_family.message, "not implemented") == 0);
    CHECK(g_family.condition[0] == '\0');
}

// ---- death tests: the abort paths -----------------------------------------------------------------------------------

TEST_CASE("A failed ENGINE_ASSERT with the default handler prints one line and dies with an abort", "[assert][death][debug]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    ChildResourceGuard guard;
    const ChildResult r = RunInChild("dt_family_assert_false");
    CHECK(r.terminated);
    CHECK(r.reason == CrashReason::Abort);
    CHECK(r.StdoutContains("ASSERT FAILED: "));
    CHECK(r.StdoutContains("assert_family_test.cpp:"));
    CHECK(r.StdoutContains("1 + 1 == 3"));
    CHECK(r.StdoutContains("arithmetic broke"));
    // The stack trace follows the message, and its first frame is the function that failed the assert.
    const char* first_frame = CStrFind(r.stdout_text, "#0  0x");
    REQUIRE(first_frame != nullptr);
    const char* end_of_line = CStrFindChar(first_frame, '\n');
    REQUIRE(end_of_line != nullptr);
    CHECK(std::string(first_frame, end_of_line).find("dt_family_assert_false") != std::string::npos);
    CHECK(guard.Clean());
}

TEST_CASE("A failed ENGINE_VERIFY and ENGINE_CHECK_SLOW abort by default", "[assert][death][debug]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    const ChildResult verify = RunInChild("dt_family_verify_false");
    CHECK(verify.reason == CrashReason::Abort);
    CHECK(verify.StdoutContains("VERIFY FAILED: "));
    const ChildResult slow = RunInChild("dt_family_check_slow_false");
    CHECK(slow.reason == CrashReason::Abort);
    CHECK(slow.StdoutContains("slow check failed on purpose"));
}

TEST_CASE("ENGINE_UNREACHABLE and ENGINE_NOT_IMPLEMENTED abort", "[assert][death][debug]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    const ChildResult unreachable = RunInChild("dt_family_unreachable");
    CHECK(unreachable.reason == CrashReason::Abort);
    CHECK(unreachable.StdoutContains("UNREACHABLE REACHED: "));
    const ChildResult not_implemented = RunInChild("dt_family_not_implemented");
    CHECK(not_implemented.reason == CrashReason::Abort);
    CHECK(not_implemented.StdoutContains("NOT IMPLEMENTED: "));
}

TEST_CASE("A handler cannot make an unreachable place continue, nor AssertFail return", "[assert][death][debug]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    CHECK(RunInChild("dt_family_unreachable_with_continue_handler").reason == CrashReason::Abort);
    CHECK(RunInChild("dt_family_legacy_assert_fail").reason == CrashReason::Abort);
}

TEST_CASE("A failed ENGINE_ENSURE prints once and the program goes on", "[ensure][death][debug]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    const ChildResult r = RunInChild("dt_family_ensure_continues");
    CHECK(r.exited);
    CHECK(r.exit_code == 0);
    CHECK(r.StdoutContains("ENSURE FAILED: "));
    CHECK(r.StdoutContains("AFTER-ENSURE"));
    // Three failures, one line, one trace.
    const char* first = CStrFind(r.stdout_text, "ENSURE FAILED: ");
    REQUIRE(first != nullptr);
    CHECK(CStrFind(first + 1, "ENSURE FAILED: ") == nullptr);
    const char* frame = CStrFind(r.stdout_text, "#0  0x");
    REQUIRE(frame != nullptr);
    CHECK(CStrFind(frame + 1, "#0  0x") == nullptr);
    // The trace starts at the function that holds the ensure, not at the machinery behind the macro.
    const char* end_of_line = CStrFindChar(frame, '\n');
    REQUIRE(end_of_line != nullptr);
    CHECK(std::string(frame, end_of_line).find("FamilyEnsureSiteA") != std::string::npos);
}

TEST_CASE("An ensure site whose failure count wraps is not reported or linked a second time", "[ensure][debug]") {
    FamilyHandlerScope scope(&FamilyCountOnly);
    detail::EnsureSite site{"wrap.cpp", 1, "cond", "msg"};
    CHECK(detail::EnsureFailed(site, "Func") == AssertAction::Continue); // the first failure: reported
    CHECK(g_family.calls == 1);
    CHECK(EnsureFailedSiteCount() == 1);
    site.count = 0xffffffffu; // the next failure wraps the count to 0, which must not look like a first failure
    detail::EnsureFailed(site, "Func");
    detail::EnsureFailed(site, "Func");
    CHECK(g_family.calls == 1);
    CHECK(EnsureFailedSiteCount() == 1);
    FamilySiteList sites; // the list is still finite (a second link would make `site` its own successor)
    ForEachFailedEnsure(&FamilyCollectSites, &sites);
    CHECK(sites.count == 1);
    ResetEnsureStateForTesting(); // unlink the stack-allocated site before it goes out of scope
}

TEST_CASE("The default handler survives null strings in the failure record", "[assert][debug]") {
    g_family_text_length = 0;
    SetWriteSinkForTesting(&FamilyCapturingSink);
    const AssertAction action = DefaultAssertHandler(AssertInfo{AssertKind::Assert, nullptr, 3, nullptr, nullptr, nullptr});
    SetWriteSinkForTesting(&os::Write);
    CHECK(action == AssertAction::Abort);
    CHECK(g_family_text_length > 0);
    CHECK(CStrCompareN(g_family_text, "ASSERT FAILED: ", 15) == 0);
    CHECK(g_family_text[g_family_text_length - 1] == '\n');
}
