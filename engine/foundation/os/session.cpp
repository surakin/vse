#include "os/session.h"

namespace engine_placeholder::os {

const char* SessionStateName(SessionState state) {
    switch (state) {
    case SessionState::Active: return "active";
    case SessionState::Locked: return "locked";
    case SessionState::Idle: return "idle";
    case SessionState::Inactive: return "inactive";
    case SessionState::Unknown: return "unknown";
    }
    return "unknown";
}

SessionState SessionStateFromFlags(bool known, bool active, bool locked, bool idle) {
    if (!known) return SessionState::Unknown;
    if (locked) return SessionState::Locked;
    if (idle) return SessionState::Idle;
    if (!active) return SessionState::Inactive;
    return SessionState::Active;
}

bool SessionCanShowWindows(SessionState state) { return state == SessionState::Active || state == SessionState::Unknown; }

} // namespace engine_placeholder::os
