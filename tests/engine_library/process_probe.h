/* Phase 23 Day 355: what the reload and unload hosts ask about their own process, per platform: the thread count, the
   resident size and whether a library is mapped (process_probe_linux.c reads /proc/self; macos/process_probe_macos.c
   asks Mach and dyld). Plain C. */
#ifndef ENGINE_TEST_PROCESS_PROBE_H
#define ENGINE_TEST_PROCESS_PROBE_H

#include <dlfcn.h>
#include <signal.h>
#include <sys/wait.h>

#include "foundation/build_defs.h"

/* 1 where dlclose really unmaps the engine library (Linux). On macOS dyld keeps an image that holds Objective-C classes
   (the Cocoa window, the Metal backend) loaded for the life of the process, so the library stays mapped after its last
   dlclose: the documented limit of Day 355. The hosts check everything else there (dlclose succeeds, the threads and
   the memory return, the engine holds no block) and tolerate the mapping. */
#define ENGINE_LIBRARY_UNLOADS (!ENGINE_PLATFORM_MACOS)

/* The threads of this process, or -1. */
int ProbeThreadCount(void);

/* The resident size of this process in bytes, or -1. */
long ProbeResidentBytes(void);

/* Whether a mapped image's path contains `file_name`: 1, 0, or -1 when it cannot be told. */
int ProbeMapped(const char* file_name);

/* Whether the dynamic linker still has `path` loaded (without loading it, and without keeping the reference RTLD_NOLOAD
 * hands back). */
static inline int ProbeLibraryLoaded(const char* path)
{
    void* handle = dlopen(path, RTLD_NOW | RTLD_NOLOAD);
    if (handle == NULL)
    {
        return 0;
    }
    dlclose(handle);
    return 1;
}

/* Whether the library left as it must on this platform: gone where libraries unload, anything on macOS. */
static inline int ProbeLibraryGone(const char* path)
{
    return !ENGINE_LIBRARY_UNLOADS || !ProbeLibraryLoaded(path);
}

/* The signal whose handler the engine's crash handler takes while it is installed (and gives back): SIGSEGV on Linux.
   On macOS Breakpad takes faults as Mach exceptions and installs a handler for SIGABRT only
   (engine/foundation/os/macos/debug/BACKEND.md). */
#if ENGINE_PLATFORM_MACOS
#define ENGINE_CRASH_HANDLER_SIGNAL SIGABRT
#else
#define ENGINE_CRASH_HANDLER_SIGNAL SIGSEGV
#endif

/* Whether a process that faulted with the engine's crash handler installed ended as it does after the dump: by SIGSEGV
   on Linux; on macOS Breakpad ends it with _exit(<exception type>), exit code 1 for EXC_BAD_ACCESS (BACKEND.md). */
static inline int ProbeEndedByFault(int status)
{
#if ENGINE_PLATFORM_MACOS
    return WIFEXITED(status) && WEXITSTATUS(status) == 1;
#else
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV;
#endif
}

#endif
