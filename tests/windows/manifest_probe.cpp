// windows_manifest (Phase 13 Day 148): an executable built with the engine's options runs with what
// cmake/engine.manifest asks for. Prints the active code page (65001 is UTF-8) and the thread's DPI awareness; the
// ctest matches the line.
#include "os/windows/win32.h"
#include "log/output.h"

#include "os/console.h"

using namespace vse;

int main()
{
    const UINT code_page = GetACP();
    DPI_AWARENESS_CONTEXT context = GetThreadDpiAwarenessContext();
    const char* awareness = "other";
    if (AreDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
    {
        awareness = "per-monitor-v2";
    }
    else if (AreDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE))
    {
        awareness = "per-monitor";
    }
    else if (AreDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_UNAWARE))
    {
        awareness = "unaware";
    }
    out::Print("active code page %u; DPI awareness %s\n", code_page, awareness);
    return code_page == 65001 ? 0 : 1;
}
