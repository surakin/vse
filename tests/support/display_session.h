#pragma once

#include <catch2/catch_test_macros.hpp>

#include "os/os.h"
#include "os/session.h"

// A test that opens a window starts with this: a locked, idle or switched-away session never maps one, so the test
// would fail for a reason that is not a defect. An unknown state does not skip (the test runs and can fail), so a
// platform without a session query loses nothing.
namespace engine_test
{

// The session state, asked at most once every two seconds (a run has dozens of window tests; each query is a few bus
// round trips).
inline ::engine_placeholder::os::SessionState CachedSessionState()
{
    static ::engine_placeholder::os::SessionState state = ::engine_placeholder::os::SessionState::Unknown;
    static ::engine_placeholder::u64 asked = 0;  // 0: never asked
    const ::engine_placeholder::u64 now = ::engine_placeholder::os::MonotonicTimeNanos();
    if (asked == 0 || now - asked > 2'000'000'000ull)
    {
        state = ::engine_placeholder::os::QuerySessionState();
        asked = now;
    }
    return state;
}

}  // namespace engine_test

#define SKIP_IF_NO_DISPLAY_SESSION()                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        const ::engine_placeholder::os::SessionState session_state_ = ::engine_test::CachedSessionState();             \
        if (!::engine_placeholder::os::SessionCanShowWindows(session_state_))                                          \
        {                                                                                                              \
            SKIP("the session is " << ::engine_placeholder::os::SessionStateName(session_state_)                       \
                                   << ": windows do not appear (unlock the screen)");                                  \
        }                                                                                                              \
    } while (false)
