#pragma once

// The one way Windows code includes <windows.h> (Phase 13 Day 148). Only files under the Windows directories include
// this (os/windows/, io/windows/, os/windows/debug/, settings/windows/, platform/windows/, rhi/dx12/), never a neutral
// header.
//
// <windows.h> is included between unity/windows_enable.h and unity/windows_disable.h, which remove at once the macros
// whose names are also engine identifiers (`LoadLibrary`, `CreateDirectory`, `near`, `far`, `small`, ...; the list and
// the reason are in windows_enable.h). Consequences for Windows code:
//   - Call the explicit wide functions: `LoadLibraryExW`, `CreateFileW`, `CreateDirectoryW`, never the macro names.
//   Strings at the
//     Win32 boundary are UTF-16 (`wchar_t`); the engine is UTF-8 everywhere else (os/windows/utf16.h converts, Day
//     149).
//   - Ordinary macros (`INVALID_HANDLE_VALUE`, `MAX_PATH`, `ERROR_FILE_NOT_FOUND`, `WAIT_TIMEOUT`, ...) stay available.
//   - A file that needs more Windows headers (d3d12.h, dbghelp.h, shlobj.h, aclapi.h) includes them inside its own
//   enable/disable pair,
//     after this header.
// The build defines WIN32_LEAN_AND_MEAN, NOMINMAX, UNICODE and _UNICODE for every Windows target
// (cmake/EngineCompilerOptions.cmake).

#include "unity/windows_enable.h"
#include <windows.h>
#include "unity/windows_disable.h"
