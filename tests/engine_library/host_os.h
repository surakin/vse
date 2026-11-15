/* Phase 23 Day 354: what the C hosts of tests/engine_library need from the operating system, the same on every
   platform: an atomic counter, threads, sleeping, loading a library, a directory, a crash in a child process, and the
   process facts the unload tests read. A foreign host would use its own; these stand for it. The POSIX side is what the
   hosts used directly until Day 354 (pthreads, dlopen, fork, /proc), the Windows side is Win32 (CreateThread,
   LoadLibraryW, CreateProcessW, Toolhelp, psapi), each in its own directory (CLAUDE.md "Windows code lives in Windows
   directories"). Plain C, header only. */
#ifndef HOST_OS_H
#define HOST_OS_H

#include "build_defs.h"

/* The process facts host_reload.c reads. */
typedef struct host_process_facts
{
    int threads;        /* the process's threads, -1 if unknown */
    long long resident; /* resident bytes, -1 if unknown */
} host_process_facts;

#if ENGINE_PLATFORM_WINDOWS
#include "windows/host_os_windows.h"
#else
#include "posix/host_os_posix.h"
#endif

/* 1 where closing the engine library's last reference really unmaps it (Linux, Windows). On macOS dyld keeps an image that
   holds Objective-C classes (the Cocoa window, the Metal backend) loaded for the life of the process (Day 355): the hosts
   check everything else there (the close succeeds, the threads and the memory return, the engine holds no block) and
   tolerate the mapping. */
#define HOST_LIBRARY_UNLOADS (!ENGINE_PLATFORM_MACOS)

/* Whether the library at `path` left as it must on this platform: no longer loaded where libraries unload, anything on
   macOS. */
static inline int host_lib_gone(const char* path)
{
    return !HOST_LIBRARY_UNLOADS || !host_lib_is_loaded(path);
}

#endif
