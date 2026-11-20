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
inline ::vse::os::SessionState CachedSessionState()
{
    static ::vse::os::SessionState state = ::vse::os::SessionState::Unknown;
    static ::vse::u64 asked = 0;  // 0: never asked
    const ::vse::u64 now = ::vse::os::MonotonicTimeNanos();
    if (asked == 0 || now - asked > 2'000'000'000ull)
    {
        state = ::vse::os::QuerySessionState();
        asked = now;
    }
    return state;
}

}  // namespace engine_test

#define SKIP_IF_NO_DISPLAY_SESSION()                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        const ::vse::os::SessionState session_state_ = ::engine_test::CachedSessionState();                            \
        if (!::vse::os::SessionCanShowWindows(session_state_))                                                         \
        {                                                                                                              \
            SKIP("the session is " << ::vse::os::SessionStateName(session_state_)                                      \
                                   << ": windows do not appear (unlock the screen)");                                  \
        }                                                                                                              \
    } while (false)
