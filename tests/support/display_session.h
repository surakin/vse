#pragma once

#include <catch2/catch_test_macros.hpp>

#include <chrono>

#include "os/session.h"

// A test that opens a window starts with this: a locked, idle or switched-away session never maps one, so the test would fail for a reason that is not
// a defect. An unknown state does not skip (the test runs and can fail), so a platform without a session query loses nothing.
namespace engine_test {

// The session state, asked at most once every two seconds (a run has dozens of window tests; each query is a few bus round trips).
inline ::engine_placeholder::os::SessionState CachedSessionState() {
    static ::engine_placeholder::os::SessionState state = ::engine_placeholder::os::SessionState::Unknown;
    static std::chrono::steady_clock::time_point asked{};
    const auto now = std::chrono::steady_clock::now();
    if (asked == std::chrono::steady_clock::time_point{} || now - asked > std::chrono::seconds(2)) {
        state = ::engine_placeholder::os::QuerySessionState();
        asked = now;
    }
    return state;
}

} // namespace engine_test

#define SKIP_IF_NO_DISPLAY_SESSION()                                                                                                             \
    do {                                                                                                                                         \
        const ::engine_placeholder::os::SessionState session_state_ = ::engine_test::CachedSessionState();                            \
        if (!::engine_placeholder::os::SessionCanShowWindows(session_state_)) {                                                                  \
            SKIP("the session is " << ::engine_placeholder::os::SessionStateName(session_state_) << ": windows do not appear (unlock the screen)"); \
        }                                                                                                                                        \
    } while (false)
