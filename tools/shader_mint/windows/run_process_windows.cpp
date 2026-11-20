// RunProcess on Windows (Phase 13 Day 163): CreateProcessW with a command line quoted by AppendWindowsArgument, the
// standard handles inherited, then a wait for the exit code.
#include "os/console.h"

#include "os/windows/utf16.h"
#include "os/windows/win32.h"
#include "run_process.h"

namespace vse::shader_mint
{

bool RunProcess(const Vector<Text>& args)
{
    if (args.Empty())
    {
        os::PrintError("shader_mint: no program to run\n");
        return false;
    }
    Text line = MakeText();
    for (usize i = 0; i < args.Size(); ++i)
    {
        AppendWindowsArgument(&line, args[i].View());
    }
    const usize wide_length = os::windows::Utf8ToUtf16(line.View(), nullptr, 0);
    Vector<wchar_t> wide;
    wide.Resize(wide_length + 1);
    os::windows::Utf8ToUtf16(line.View(), wide.Data(), wide.Size());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    // The child writes to the tool's own standard output and error (a console, or the pipes of the build tool that runs
    // us), so slangc's messages reach the build log. A short-lived build helper: inheriting the tool's inheritable
    // handles is harmless.
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(nullptr, wide.Data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &info))
    {
        os::PrintError("shader_mint: cannot run %s (Windows error %lu)\n", args[0].CStr(), GetLastError());
        return false;
    }
    CloseHandle(info.hThread);
    const DWORD waited = WaitForSingleObject(info.hProcess, INFINITE);
    DWORD code = 1;
    const bool have_code = waited == WAIT_OBJECT_0 && GetExitCodeProcess(info.hProcess, &code);
    CloseHandle(info.hProcess);
    if (!have_code)
    {
        os::PrintError("shader_mint: waiting for %s failed\n", args[0].CStr());
        return false;
    }
    if (code >= 0xC0000000u)
    {
        os::PrintError("shader_mint: %s was ended by exception 0x%08lX\n", args[0].CStr(), code);
        return false;
    }
    if (code != 0)
    {
        os::PrintError("shader_mint: %s exited with status %lu\n", args[0].CStr(), code);
        return false;
    }
    return true;
}

}  // namespace vse::shader_mint
