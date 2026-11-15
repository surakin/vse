/* host_os.h on Windows (Phase 23 Day 354): Win32 threads, LoadLibraryW, a second run of the program for the crash
   child, the unhandled-exception filter, Toolhelp and psapi for the process facts. Included by host_os.h only. Paths
   are UTF-8 in the hosts and converted at this boundary. */
#ifndef HOST_OS_WINDOWS_H
#define HOST_OS_WINDOWS_H

#include <crtdbg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os/windows/win32.h"
#include "unity/windows_enable.h"
#include <psapi.h>
#include <tlhelp32.h>
#include "unity/windows_disable.h"

/* ---- failures without dialogs ---- */

/* Call first in main: an assert of the debug C runtime, abort() and a crash report to standard error and end the
   program, never a dialog box that waits for a click on the desktop (a test runs unattended). */
static inline void host_quiet_failures(void)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
}

/* ---- atomics ---- */

static inline int host_atomic_add(int* p, int v)
{
    return (int)InterlockedExchangeAdd((volatile LONG*)p, (LONG)v);
}
static inline int host_atomic_load(const int* p)
{
    return (int)InterlockedCompareExchange((volatile LONG*)p, 0, 0);
}
static inline void host_atomic_store(int* p, int v)
{
    InterlockedExchange((volatile LONG*)p, (LONG)v);
}

/* ---- threads ---- */

typedef struct host_thread
{
    HANDLE thread;
    void (*fn)(void*);
    void* arg;
} host_thread;
typedef DWORD host_thread_id;

static DWORD WINAPI host_thread_trampoline(LPVOID self)
{
    host_thread* t = (host_thread*)self;
    t->fn(t->arg);
    return 0;
}
static inline int host_thread_start(host_thread* t, void (*fn)(void*), void* arg)
{
    t->fn = fn;
    t->arg = arg;
    t->thread = CreateThread(NULL, 0, host_thread_trampoline, t, 0, NULL);
    return t->thread != NULL ? 0 : -1;
}
static inline void host_thread_join(host_thread* t)
{
    WaitForSingleObject(t->thread, INFINITE);
    CloseHandle(t->thread);
    t->thread = NULL;
}
static inline host_thread_id host_thread_self(void)
{
    return GetCurrentThreadId();
}
static inline int host_thread_equal(host_thread_id a, host_thread_id b)
{
    return a == b;
}
static inline void host_sleep_ms(unsigned ms)
{
    Sleep(ms);
}

/* ---- paths ---- */

/* UTF-8 to UTF-16 in a fixed buffer; an empty string when it does not fit. */
static inline const wchar_t* host_wide(const char* utf8, wchar_t* out, int capacity)
{
    if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, capacity) == 0)
    {
        out[0] = L'\0';
    }
    return out;
}

/* ---- libraries ---- */

static DWORD g_host_lib_error = 0;

static inline void* host_lib_open(const char* path)
{
    wchar_t wide[MAX_PATH * 2];
    HMODULE module = LoadLibraryExW(host_wide(path, wide, MAX_PATH * 2), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    g_host_lib_error = module == NULL ? GetLastError() : 0;
    return (void*)module;
}
static inline void* host_lib_symbol(void* lib, const char* name)
{
    FARPROC p = GetProcAddress((HMODULE)lib, name);
    void* out;
    g_host_lib_error = p == NULL ? GetLastError() : 0;
    memcpy(&out, &p, sizeof(out)); /* function pointer to object pointer without a cast ISO C forbids */
    return out;
}
static inline int host_lib_close(void* lib)
{
    if (!FreeLibrary((HMODULE)lib))
    {
        g_host_lib_error = GetLastError();
        return -1;
    }
    return 0;
}
/* Whether the library at `path` is still in the address space (by its full path, as GetModuleHandleExW matches). */
static inline int host_lib_is_loaded(const char* path)
{
    wchar_t wide[MAX_PATH * 2];
    HMODULE module = NULL;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, host_wide(path, wide, MAX_PATH * 2),
                           &module) &&
        module != NULL)
    {
        return 1;
    }
    return 0;
}
static inline const char* host_lib_error(void)
{
    static char text[64];
    snprintf(text, sizeof(text), "Windows error %lu", (unsigned long)g_host_lib_error);
    return text;
}

/* ---- files ---- */

static inline int host_pid(void)
{
    return (int)GetCurrentProcessId();
}
static inline void host_mkdir(const char* path)
{
    wchar_t wide[MAX_PATH * 2];
    CreateDirectoryW(host_wide(path, wide, MAX_PATH * 2), NULL);
}
static inline int host_count_files(const char* path, const char* part)
{
    char pattern[1024];
    wchar_t wide[1024];
    WIN32_FIND_DATAW found;
    HANDLE find;
    int count = 0;
    snprintf(pattern, sizeof(pattern), "%s/*", path);
    find = FindFirstFileW(host_wide(pattern, wide, 1024), &found);
    if (find == INVALID_HANDLE_VALUE)
    {
        return 0;
    }
    do
    {
        char name[MAX_PATH * 3];
        if (found.cFileName[0] == L'.')
        {
            continue;
        }
        if (WideCharToMultiByte(CP_UTF8, 0, found.cFileName, -1, name, (int)sizeof(name), NULL, NULL) == 0)
        {
            continue;
        }
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            char child[1024];
            snprintf(child, sizeof(child), "%s/%s", path, name);
            count += host_count_files(child, part);
        }
        else if (strstr(name, part) != NULL)
        {
            ++count;
        }
    } while (FindNextFileW(find, &found));
    FindClose(find);
    return count;
}

/* ---- a crash in a child process ---- */

/* The child is a second run of this program with HOST_CRASH_CHILD set to its argument (Windows has no fork). Called
   first in main: in the child it runs `child` and never returns. */
static inline void host_child_entry(void (*child)(const char*))
{
    char arg[1024];
    DWORD n = GetEnvironmentVariableA("HOST_CRASH_CHILD", arg, (DWORD)sizeof(arg));
    if (n > 0 && n < sizeof(arg))
    {
        child(arg);
        TerminateProcess(GetCurrentProcess(), 4);
    }
}
/* Runs the program again as the crash child and returns 1 when it ended as a crash: the access violation the test
   planted, or exit code 3 (abort), which is how it ends when the fault happens inside a job: MSVC's coroutine machinery
   hands the fault to Job::promise_type::unhandled_exception, whose assert aborts after the report (KNOWN-ISSUES.md,
   Phase 23 Day 354). The caller checks the dump the engine's handler left either way. */
static inline int host_run_crash_child(void (*child)(const char*), const char* arg)
{
    wchar_t self[MAX_PATH * 2];
    wchar_t wide_arg[1024];
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    DWORD code = 0;
    (void)child;
    if (GetModuleFileNameW(NULL, self, MAX_PATH * 2) == 0)
    {
        return 0;
    }
    SetEnvironmentVariableW(L"HOST_CRASH_CHILD", host_wide(arg, wide_arg, 1024));
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    memset(&process, 0, sizeof(process));
    if (!CreateProcessW(self, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process))
    {
        SetEnvironmentVariableW(L"HOST_CRASH_CHILD", NULL);
        return 0;
    }
    SetEnvironmentVariableW(L"HOST_CRASH_CHILD", NULL);
    WaitForSingleObject(process.hProcess, 60000);
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (code != (DWORD)EXCEPTION_ACCESS_VIOLATION && code != 3)
    {
        printf("host_os: the crash child ended with 0x%08lx, not an access violation or an abort\n",
               (unsigned long)code);
    }
    return code == (DWORD)EXCEPTION_ACCESS_VIOLATION || code == 3;
}
static inline void host_exit_now(int code)
{
    TerminateProcess(GetCurrentProcess(), (UINT)code);
}

/* ---- the host's own crash handler ---- */

static LONG WINAPI host_own_filter(EXCEPTION_POINTERS* info)
{
    (void)info;
    return EXCEPTION_CONTINUE_SEARCH;
}
/* Installs the host's own unhandled-exception filter. */
static inline void host_install_own_crash_handler(void)
{
    SetUnhandledExceptionFilter(&host_own_filter);
}
/* Whether the top-level filter is the host's own (read by setting it and putting back what was there). */
static inline int host_own_crash_handler_active(void)
{
    LPTOP_LEVEL_EXCEPTION_FILTER now = SetUnhandledExceptionFilter(NULL);
    SetUnhandledExceptionFilter(now);
    return now == &host_own_filter;
}

/* ---- process facts (host_reload.c) ---- */

static inline host_process_facts host_read_process_facts(void)
{
    host_process_facts facts;
    const DWORD pid = GetCurrentProcessId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    PROCESS_MEMORY_COUNTERS memory;
    facts.threads = -1;
    facts.resident = -1;
    if (snapshot != INVALID_HANDLE_VALUE)
    {
        THREADENTRY32 entry;
        entry.dwSize = sizeof(entry);
        facts.threads = 0;
        if (Thread32First(snapshot, &entry))
        {
            do
            {
                if (entry.th32OwnerProcessID == pid)
                {
                    ++facts.threads;
                }
                entry.dwSize = sizeof(entry);
            } while (Thread32Next(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }
    memset(&memory, 0, sizeof(memory));
    if (GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)))
    {
        facts.resident = (long long)memory.WorkingSetSize;
    }
    return facts;
}
/* Whether a module named `file_name` (no folders) is loaded in this process. */
static inline int host_library_mapped(const char* file_name)
{
    wchar_t wide[MAX_PATH];
    return GetModuleHandleW(host_wide(file_name, wide, MAX_PATH)) != NULL ? 1 : 0;
}

#endif
