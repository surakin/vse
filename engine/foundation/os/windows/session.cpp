#include "os/session.h"

#include "os/windows/win32.h"

#include "unity/windows_enable.h"
#include <wtsapi32.h>
#include "unity/windows_disable.h"

// Session state on Windows (Phase 13 Day 149; the checklist is os/windows/SESSION.md).
//   Inactive: the calling process's session is not the connected, active one (fast user switching, a disconnected
//   remote session). Locked:   the input desktop is not "Default": while the workstation is locked it is Winlogon (and
//   opening it fails), a screen saver
//             shows as "Screen-saver".
//   Idle:     Windows has no idle hint of its own (GetLastInputInfo would need a limit the engine does not have), so
//   never reported. Unknown:  the session query fails (a service session, a container without a desktop).

namespace engine_placeholder::os
{

SessionState QuerySessionState()
{
    WTS_CONNECTSTATE_CLASS* connect_state = nullptr;
    DWORD bytes = 0;
    if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, WTSConnectState,
                                     reinterpret_cast<LPWSTR*>(&connect_state), &bytes) ||
        connect_state == nullptr || bytes < sizeof(WTS_CONNECTSTATE_CLASS))
    {
        if (connect_state != nullptr)
        {
            WTSFreeMemory(connect_state);
        }
        return SessionState::Unknown;
    }
    const bool active = *connect_state == WTSActive;
    WTSFreeMemory(connect_state);

    bool locked = true;  // an input desktop that cannot be opened is Winlogon's
    if (HDESK desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS); desktop != nullptr)
    {
        wchar_t name[64] = {};
        DWORD needed = 0;
        if (GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name) - sizeof(wchar_t), &needed))
        {
            locked = CompareStringOrdinal(name, -1, L"Default", -1, TRUE) != CSTR_EQUAL;
        }
        CloseDesktop(desktop);
    }
    return SessionStateFromFlags(true, active, locked, false);
}

}  // namespace engine_placeholder::os
