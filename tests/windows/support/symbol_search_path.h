#pragma once

// DbgHelp's current symbol search path, for the test that it never names a symbol server (Phase 13 Day 154). Kept in a
// Windows directory because it needs <dbghelp.h> (platform_neutral_check rule 4); link dbghelp.
#include "os/windows/win32.h"

#include "unity/windows_enable.h"
#include <dbghelp.h>
#include "unity/windows_disable.h"

namespace test_support
{

/// Copies the search path of this process's DbgHelp session into `out`. Returns false when there is none.
inline bool SymbolSearchPath(wchar_t* out, unsigned long capacity)
{
    return SymGetSearchPathW(GetCurrentProcess(), out, capacity) != FALSE;
}

}  // namespace test_support
