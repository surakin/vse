#include <catch2/catch_test_macros.hpp>

#include "os/session.h"
#include "string_view.h"

using namespace engine_placeholder;
using namespace engine_placeholder::os;

TEST_CASE("Session state: flags map to a state, locked before idle before inactive", "[os][session]") {
    CHECK(SessionStateFromFlags(true, true, false, false) == SessionState::Active);
    CHECK(SessionStateFromFlags(true, true, true, false) == SessionState::Locked);
    CHECK(SessionStateFromFlags(true, true, true, true) == SessionState::Locked);
    CHECK(SessionStateFromFlags(true, true, false, true) == SessionState::Idle);
    CHECK(SessionStateFromFlags(true, false, false, false) == SessionState::Inactive);
    CHECK(SessionStateFromFlags(true, false, false, true) == SessionState::Idle);
    CHECK(SessionStateFromFlags(false, true, false, false) == SessionState::Unknown);
    CHECK(SessionStateFromFlags(false, false, true, true) == SessionState::Unknown);
}

TEST_CASE("Session state: only active and unknown expect windows, and every state has a name", "[os][session]") {
    CHECK(SessionCanShowWindows(SessionState::Active));
    CHECK(SessionCanShowWindows(SessionState::Unknown));
    CHECK_FALSE(SessionCanShowWindows(SessionState::Locked));
    CHECK_FALSE(SessionCanShowWindows(SessionState::Idle));
    CHECK_FALSE(SessionCanShowWindows(SessionState::Inactive));
    CHECK(StringView(SessionStateName(SessionState::Active)) == "active");
    CHECK(StringView(SessionStateName(SessionState::Locked)) == "locked");
    CHECK(StringView(SessionStateName(SessionState::Idle)) == "idle");
    CHECK(StringView(SessionStateName(SessionState::Inactive)) == "inactive");
    CHECK(StringView(SessionStateName(SessionState::Unknown)) == "unknown");
}

TEST_CASE("Session state: a live query answers with one of the states and can be repeated", "[os][session]") {
    const SessionState first = QuerySessionState();
    const SessionState second = QuerySessionState();
    CHECK(static_cast<int>(first) <= static_cast<int>(SessionState::Unknown));
    CHECK(static_cast<int>(second) <= static_cast<int>(SessionState::Unknown));
    WARN("session state: " << SessionStateName(first)); // shown in the log so a skipped window test can be explained
}
