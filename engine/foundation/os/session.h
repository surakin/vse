#pragma once

#include "types.h"

// Session state: whether the interactive session can show windows. Window tests and tools ask before they open one, so
// a locked screen skips them instead of failing them. The answer comes from the platform (logind on Linux);
// `os/windows/SESSION.md` and `os/macos/SESSION.md` list what the other ports call.

namespace vse::os
{

/// The state of the session the program runs in.
enum class SessionState : u8
{
    Active,    ///< The session is in the foreground, unlocked and not idle.
    Locked,    ///< The screen is locked.
    Idle,      ///< The session reports idle (screen blanked or about to lock). Window tests skip on it too: a blanked
               ///< display gives no frames.
    Inactive,  ///< Another session is in the foreground (a switched virtual terminal or user).
    Unknown,   ///< The platform could not say (no session manager, a remote shell).
};

/// Returns the name of a state for logs and skip messages: `active`, `locked`, `idle`, `inactive` or `unknown`.
const char* SessionStateName(SessionState state);

/// Combines what a platform could read into a state. Locked wins over idle, idle over inactive.
///
/// - Param known: whether the platform answered at all; false gives `Unknown` whatever the other flags are.
/// - Param active: the session is in the foreground.
/// - Param locked: the screen is locked.
/// - Param idle: the session is idle.
SessionState SessionStateFromFlags(bool known, bool active, bool locked, bool idle);

/// Returns whether a window can be expected to appear: true for `Active` and `Unknown` (an unknown state never hides a
/// failure).
bool SessionCanShowWindows(SessionState state);

/// Asks the platform for the state of the current session: the one the calling process belongs to, which is not
/// necessarily the one that owns the display it will open a window on (a program started over ssh reports the ssh
/// session).
///
/// Linux asks systemd-logind; Windows reads the session's connect state (`WTSQuerySessionInformation`, not `WTSActive`
/// is `Inactive`) and the input desktop's name (not `Default` is `Locked`) and never reports `Idle`. macOS reads the
/// window server's session dictionary
/// (`CGSessionCopyCurrentDictionary`: off the console is `Inactive`, a locked screen `Locked`, a sleeping main display
/// `Idle`); a process outside a window-server session (started over ssh) has none and reports `Unknown`.
/// - Returns: the state, `Unknown` when the platform cannot say.
/// - Fails: reports `Unknown`; never aborts and waits at most a few seconds (a session manager that does not answer
/// gives `Unknown`).
/// - Thread-safety: thread-safe.
SessionState QuerySessionState();

}  // namespace vse::os
