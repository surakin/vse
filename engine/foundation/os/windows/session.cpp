#include "os/session.h"

#include "os/windows/win32.h"

#include "unity/windows_enable.h"
#include <wtsapi32.h>
#include "unity/windows_disable.h"

// Session state on Windows (Phase 13 Day 149; the checklist is os/windows/SESSION.md).
//   Inactive: the calling process's session is not the connected, active one (fast user switching, a disconnected
//   remote session).
//   Locked:   the session's own lock flag (`WTSSessionInfoEx`, `WTS_SESSIONSTATE_LOCK`), or an input desktop that is
//             not "Default" (Winlogon while the lock takes over, which cannot be opened; "Screen-saver"). The flag is
//             needed: on Windows 11 the lock screen runs on the Default desktop, so a few seconds after Win+L the input
//             desktop is "Default" again while the session stays locked (logged on the Windows PC, 2026-11-15).
//   Idle:     Windows has no idle hint of its own (GetLastInputInfo would need a limit the engine does not have), so
//   never reported. Unknown:  the session query fails (a service session, a container without a desktop).

namespace vse::os
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
    WTSINFOEXW* info = nullptr;
    bool flag_locked = false;
    if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, WTSSessionInfoEx,
                                    reinterpret_cast<LPWSTR*>(&info), &bytes) &&
        info != nullptr && bytes >= sizeof(WTSINFOEXW) && info->Level == 1)
    {
        flag_locked = info->Data.WTSInfoExLevel1.SessionFlags == WTS_SESSIONSTATE_LOCK;
    }
    if (info != nullptr)
    {
        WTSFreeMemory(info);
    }
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
    return SessionStateFromFlags(true, active, locked || flag_locked, false);
}

}  // namespace vse::os
