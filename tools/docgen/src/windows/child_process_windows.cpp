// The sample runner's child processes on Windows (child_process.h; Phase 13 Day 161). Each child is started suspended,
// put in a job object of its own and then resumed, so a timeout (TerminateJobObject) or docgen ending (the job closes:
// JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) ends the compiler's or the program's whole tree, as a process group does on
// POSIX. The job also turns off the crash dialog for its processes (JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION), so a
// sample that faults ends instead of waiting for a click. The child inherits only its three standard handles
// (PROC_THREAD_ATTRIBUTE_HANDLE_LIST), not the files of the other children starting at the same time.
#include "child_process.h"

#include "os/windows/utf16.h"
#include "os/windows/win32.h"

namespace docgen
{

namespace
{

using vse::os::windows::PathToWin32;
using vse::os::windows::Utf8ToUtf16;

// Appends one argument by the rules CommandLineToArgvW and the C runtime read a command line back with: quoted when it
// is empty or holds a space, a tab or a quote; inside the quotes a quote is escaped with a backslash, and the
// backslashes before a quote or before the closing quote are doubled (backslashes elsewhere are literal).
void AppendArgument(vse::StringBuilder<ArenaAllocator>& line, StringView arg)
{
    bool quote = arg.Empty();
    for (usize i = 0; i < arg.Length(); ++i)
    {
        quote = quote || arg[i] == ' ' || arg[i] == '\t' || arg[i] == '"';
    }
    if (!quote)
    {
        line.Append(arg);
        return;
    }
    line.Append('"');
    usize backslashes = 0;
    for (usize i = 0; i < arg.Length(); ++i)
    {
        const char c = arg[i];
        if (c == '\\')
        {
            ++backslashes;
            continue;
        }
        const usize count = c == '"' ? backslashes * 2 + 1 : backslashes;
        for (usize k = 0; k < count; ++k)
        {
            line.Append('\\');
        }
        line.Append(c);
        backslashes = 0;
    }
    for (usize k = 0; k < backslashes * 2; ++k)
    {
        line.Append('\\');
    }
    line.Append('"');
}

// `text` as a NUL-terminated UTF-16 string in the arena; a path is converted as Win32 file functions take it.
wchar_t* Wide(StringView text, bool path, ArenaAllocator& arena)
{
    const usize n = path ? PathToWin32(text, nullptr, 0) : Utf8ToUtf16(text, nullptr, 0);
    wchar_t* w = static_cast<wchar_t*>(arena.Alloc((n + 1) * sizeof(wchar_t), alignof(wchar_t)));
    if (path)
    {
        PathToWin32(text, w, n + 1);
    }
    else
    {
        Utf8ToUtf16(text, w, n + 1);
    }
    return w;
}

// A handle the child inherits: the null device (input) or an output file, created or truncated.
HANDLE OpenForChild(const wchar_t* path, bool output)
{
    SECURITY_ATTRIBUTES inherit{};
    inherit.nLength = sizeof(inherit);
    inherit.bInheritHandle = TRUE;
    return CreateFileW(path, output ? GENERIC_WRITE : GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &inherit,
                       output ? CREATE_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void CloseIfOpen(HANDLE h)
{
    if (h != nullptr && h != INVALID_HANDLE_VALUE)
    {
        CloseHandle(h);
    }
}

void Release(ChildProcess& child)
{
    CloseIfOpen(static_cast<HANDLE>(child.process));
    CloseIfOpen(static_cast<HANDLE>(child.job));  // kills whatever the child left running (KILL_ON_JOB_CLOSE)
    child = ChildProcess{};
}

}  // namespace

bool StartChild(const Vector<StringView, ArenaAllocator>& args, StringView out_path, StringView err_path,
                StringView working_dir, ArenaAllocator& arena, ChildProcess* out)
{
    vse::StringBuilder<ArenaAllocator> line(arena);
    for (usize i = 0; i < args.Size(); ++i)
    {
        if (i > 0)
        {
            line.Append(' ');
        }
        AppendArgument(line, args[i]);
    }
    wchar_t* command_line = Wide(line.View(), false, arena);  // CreateProcessW may write to it
    const HANDLE in = OpenForChild(L"NUL", false);
    const HANDLE stdout_file = OpenForChild(Wide(out_path, true, arena), true);
    const HANDLE stderr_file = err_path.Empty() ? stdout_file : OpenForChild(Wide(err_path, true, arena), true);
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    bool ok = in != INVALID_HANDLE_VALUE && stdout_file != INVALID_HANDLE_VALUE &&
              stderr_file != INVALID_HANDLE_VALUE && job != nullptr;
    if (ok)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
                                                  JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
        ok = SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) != FALSE;
    }
    PROCESS_INFORMATION pi{};
    if (ok)
    {
        HANDLE inherited[3] = {in, stdout_file, stderr_file};
        const DWORD inherited_count = stderr_file == stdout_file ? 2 : 3;
        SIZE_T list_bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0,
                                          &list_bytes);  // reports the size it needs and fails, by design
        auto* attributes = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(arena.Alloc(list_bytes, 16));
        ok = InitializeProcThreadAttributeList(attributes, 1, 0, &list_bytes) != FALSE;
        if (ok)
        {
            ok = UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                           inherited_count * sizeof(HANDLE), nullptr, nullptr) != FALSE;
            STARTUPINFOEXW startup{};
            startup.StartupInfo.cb = sizeof(startup);
            startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
            startup.StartupInfo.hStdInput = in;
            startup.StartupInfo.hStdOutput = stdout_file;
            startup.StartupInfo.hStdError = stderr_file;
            startup.lpAttributeList = attributes;
            const wchar_t* directory = working_dir.Empty() ? nullptr : Wide(working_dir, true, arena);
            ok = ok && CreateProcessW(nullptr, command_line, nullptr, nullptr, TRUE,
                                      CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                                      directory, &startup.StartupInfo, &pi) != FALSE;
            DeleteProcThreadAttributeList(attributes);
        }
    }
    CloseIfOpen(in);  // the child has its own copies now
    CloseIfOpen(stdout_file);
    if (stderr_file != stdout_file)
    {
        CloseIfOpen(stderr_file);
    }
    if (ok && AssignProcessToJobObject(job, pi.hProcess) == FALSE)
    {
        TerminateProcess(pi.hProcess,
                         1);  // never left running outside a job, where a timeout could not reach its children
        WaitForSingleObject(pi.hProcess, INFINITE);
        ok = false;
    }
    if (!ok)
    {
        CloseIfOpen(pi.hThread);
        CloseIfOpen(pi.hProcess);
        CloseIfOpen(job);
        return false;
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    out->id = static_cast<vse::i64>(pi.dwProcessId);
    out->process = pi.hProcess;
    out->job = job;
    return true;
}

ChildPoll PollChild(ChildProcess& child, ChildEnd* end)
{
    const HANDLE process = static_cast<HANDLE>(child.process);
    const DWORD waited = WaitForSingleObject(process, 0);
    if (waited == WAIT_TIMEOUT)
    {
        return ChildPoll::Running;
    }
    DWORD code = 0;
    if (waited != WAIT_OBJECT_0 || GetExitCodeProcess(process, &code) == FALSE)
    {
        KillChild(child);
        return ChildPoll::Lost;
    }
    *end = ChildEnd{};
    if (code >= 0xC0000000u)
    {  // an NTSTATUS error: the process died of an exception (0xC0000005 an access violation, ...)
        end->killed = true;
        end->cause = static_cast<u32>(code);
    }
    else
    {
        end->exited = true;
        end->exit_code = static_cast<vse::i32>(code);
    }
    Release(child);
    return ChildPoll::Ended;
}

void KillChild(ChildProcess& child)
{
    if (child.process == nullptr)
    {
        return;
    }
    TerminateJobObject(static_cast<HANDLE>(child.job), 1);
    WaitForSingleObject(static_cast<HANDLE>(child.process), INFINITE);
    Release(child);
}

}  // namespace docgen
