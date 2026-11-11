// The entry point of a windowed program on Windows (Phase 13 Day 158; platform/entry_point.h): UTF-8 arguments from the
// wide command line, and standard output and error for a GUI program.
#include "platform/entry_point.h"

#include "cstr/cstr.h"

#include "mem/heap_allocator.h"
#include "os/command_line.h"
#include "os/windows/utf16.h"
#include "os/windows/win32.h"

#include "unity/windows_enable.h"
#include <shellapi.h>
#include "unity/windows_disable.h"

namespace engine_placeholder::os::detail
{

namespace
{

bool UsableStdHandle(HANDLE handle)
{
    return handle != nullptr && handle != INVALID_HANDLE_VALUE && GetFileType(handle) != FILE_TYPE_UNKNOWN;
}

// A GUI program gets no console. Handles it inherited (a pipe, a redirected file) are kept as they are; with none, it
// attaches to its parent's console if there is one (started from a terminal) and writes there, through the Win32
// handles and the C runtime's streams.
void ConnectStandardStreams()
{
    const bool have_out = UsableStdHandle(GetStdHandle(STD_OUTPUT_HANDLE));
    const bool have_err = UsableStdHandle(GetStdHandle(STD_ERROR_HANDLE));
    if (have_out && have_err)
    {
        return;
    }
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
    {
        return;  // started from Explorer: no console, the writes fail, the log file has everything
    }
    const HANDLE console = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       nullptr, OPEN_EXISTING, 0, nullptr);
    if (console == INVALID_HANDLE_VALUE)
    {
        return;
    }
    FILE* reopened = nullptr;
    if (!have_out)
    {
        SetStdHandle(STD_OUTPUT_HANDLE, console);
        freopen_s(&reopened, "CONOUT$", "w", stdout);
    }
    if (!have_err)
    {
        SetStdHandle(STD_ERROR_HANDLE, console);
        freopen_s(&reopened, "CONOUT$", "w", stderr);
    }
}

}  // namespace

int RunWindowedProgram(int, char**, WindowedProgramEntry entry)
{
    ConnectStandardStreams();
    int count = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (wide == nullptr || count < 0)
    {
        return 1;
    }

    // One block, never freed (below): the `argv` pointers (with the closing null), then each argument's UTF-8 text and
    // its terminator.
    const usize pointers = (static_cast<usize>(count) + 1) * sizeof(char*);
    usize bytes = pointers;
    for (int i = 0; i < count; ++i)
    {
        bytes += windows::Utf16ToUtf8(wide[i], WStrLength(wide[i]), nullptr, 0) + 1;
    }
    void* block = HeapAllocator::Default().Alloc(bytes, alignof(char*));
    if (block == nullptr)
    {
        LocalFree(wide);
        return 1;
    }
    char** argv = static_cast<char**>(block);
    char* text = static_cast<char*>(block) + pointers;
    for (int i = 0; i < count; ++i)
    {
        const usize length = windows::Utf16ToUtf8(wide[i], WStrLength(wide[i]), text,
                                                  bytes - static_cast<usize>(text - static_cast<char*>(block)));
        argv[i] = text;
        text += length + 1;
    }
    argv[count] = nullptr;
    LocalFree(wide);

    // The block lives as long as the process: the stash points into it, and code that runs at exit may still read the
    // command line.
    StashNativeCommandLine(count, argv);
    return entry(count, argv);
}

}  // namespace engine_placeholder::os::detail
