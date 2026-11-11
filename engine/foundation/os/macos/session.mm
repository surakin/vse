#include "os/session.h"

#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>

// The session on macOS (Phase 26 Day 301; os/macos/SESSION.md): the window server's dictionary of the calling process's
// session.
//   Unknown:  no dictionary (a login over ssh, or a daemon: the process is not in a window-server session).
//   Inactive: kCGSessionOnConsoleKey false (fast user switching put another user's session on the console); without the
//   key, Unknown. Locked:   CGSSessionScreenIsLocked true (the key is absent while unlocked). Idle:     the main
//   display is asleep. No input-time limit: the engine has none to give (as on Windows), and a display that is
//             awake shows frames.
namespace engine_placeholder::os
{

SessionState QuerySessionState()
{
    @autoreleasepool
    {
        NSDictionary* const session = CFBridgingRelease(::CGSessionCopyCurrentDictionary());
        if (session == nil)
        {
            return SessionState::Unknown;
        }
        NSNumber* const on_console = session[(__bridge NSString*)kCGSessionOnConsoleKey];
        NSNumber* const locked = session[@"CGSSessionScreenIsLocked"];
        // Without the console key the dictionary does not say whether the session is in front: Unknown, which never
        // hides a window test (session.h), rather than Inactive, which would skip them all (Day 301 review).
        if (![on_console isKindOfClass:[NSNumber class]])
        {
            return SessionState::Unknown;
        }
        const bool is_locked = [locked isKindOfClass:[NSNumber class]] && locked.boolValue;
        const bool idle = ::CGDisplayIsAsleep(::CGMainDisplayID()) != 0;
        return SessionStateFromFlags(true, on_console.boolValue, is_locked, idle);
    }
}

}  // namespace engine_placeholder::os
