// Opens a region in which <windows.h> may be included inside a unity build. Pair with unity/windows_disable.h, which normally
// follows the Windows includes at once (see os/windows/win32.h, the one header Windows code includes).
//
// <windows.h> defines macros whose names are also engine identifiers: the A/W function switches (`LoadLibrary` becomes
// `LoadLibraryW`), the empty legacy `near` and `far`, `small` from <rpcndr.h>, `min`/`max` without NOMINMAX. In a unity build a
// translation unit holds several .cpp files, so a declaration seen before <windows.h> and a use after it would name two different
// functions (`os::LoadLibrary` against `os::LoadLibraryW`). This header remembers the state of each such name; windows_disable.h
// restores it, so the macros are gone again and Windows code calls the explicit `...W` functions. The list was measured on Phase 13
// Day 148 (every windows.h macro intersected with the engine's identifiers) plus function names the engine is likely to use. Not here:
// `interface`, which the COM headers (d3d12.h, dxgi.h) need, and the annotation and constant macros IN, OUT, OPTIONAL, ERROR and DELETE,
// which later SDK headers use (wtsapi32.h needs IN; found on Day 149). Intentionally has no include guard.
#pragma push_macro("LoadLibrary")
#pragma push_macro("CreateDirectory")
#pragma push_macro("SetCurrentDirectory")
#pragma push_macro("CreateSemaphore")
#pragma push_macro("near")
#pragma push_macro("far")
#pragma push_macro("small")
#pragma push_macro("CopyFile")
#pragma push_macro("CreateEvent")
#pragma push_macro("CreateFile")
#pragma push_macro("CreateMutex")
#pragma push_macro("CreateProcess")
#pragma push_macro("CreateWindow")
#pragma push_macro("DeleteFile")
#pragma push_macro("DrawText")
#pragma push_macro("GetCommandLine")
#pragma push_macro("GetCurrentDirectory")
#pragma push_macro("GetEnvironmentVariable")
#pragma push_macro("GetMessage")
#pragma push_macro("GetObject")
#pragma push_macro("GetUserName")
#pragma push_macro("MoveFile")
#pragma push_macro("PostMessage")
#pragma push_macro("RegisterClass")
#pragma push_macro("RemoveDirectory")
#pragma push_macro("SendMessage")
#pragma push_macro("SetEnvironmentVariable")
#pragma push_macro("Yield")
#pragma push_macro("min")
#pragma push_macro("max")
// Inside the region `far` and `near` are what <windows.h> makes them, empty: an SDK header included in a later pair of its own (winioctl.h,
// dbghelp.h, shlobj.h) spells `FAR` and `NEAR`, which expand to them, and windows_disable.h has taken the definitions away again
// (found on Phase 13 Day 151). <windows.h> redefines both the same way, so this changes nothing for it.
#undef far
#define far
#undef near
#define near
