/* host_os.h on POSIX (Linux, macOS): pthreads, dlopen, fork, and process_probe.h for the process facts (Phase 23 Day
   354; what the hosts called directly from Days 345 to 353). Included by host_os.h only. */
#ifndef HOST_OS_POSIX_H
#define HOST_OS_POSIX_H

#include <dirent.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../process_probe.h"

/* Call first in main: nothing on POSIX (Windows turns its failure dialogs off). */
static inline void host_quiet_failures(void) {}

/* ---- atomics ---- */

static inline int host_atomic_add(int* p, int v)
{
    return __atomic_fetch_add(p, v, __ATOMIC_RELAXED);
}
static inline int host_atomic_load(const int* p)
{
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}
static inline void host_atomic_store(int* p, int v)
{
    __atomic_store_n(p, v, __ATOMIC_RELEASE);
}

/* ---- threads ---- */

typedef struct host_thread
{
    pthread_t thread;
    void (*fn)(void*);
    void* arg;
} host_thread;
typedef pthread_t host_thread_id;

static inline void* host_thread_trampoline(void* self)
{
    host_thread* t = (host_thread*)self;
    t->fn(t->arg);
    return NULL;
}
/* Starts `fn(arg)` on a new thread; `t` must stay alive until host_thread_join. Returns 0 on success. */
static inline int host_thread_start(host_thread* t, void (*fn)(void*), void* arg)
{
    t->fn = fn;
    t->arg = arg;
    return pthread_create(&t->thread, NULL, host_thread_trampoline, t) == 0 ? 0 : -1;
}
static inline void host_thread_join(host_thread* t)
{
    pthread_join(t->thread, NULL);
}
static inline host_thread_id host_thread_self(void)
{
    return pthread_self();
}
static inline int host_thread_equal(host_thread_id a, host_thread_id b)
{
    return pthread_equal(a, b) != 0;
}
static inline void host_sleep_ms(unsigned ms)
{
    usleep((useconds_t)ms * 1000u);
}

/* ---- libraries ---- */

static inline void* host_lib_open(const char* path)
{
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}
static inline void* host_lib_symbol(void* lib, const char* name)
{
    return dlsym(lib, name);
}
static inline int host_lib_close(void* lib)
{
    return dlclose(lib) == 0 ? 0 : -1;
}
/* Whether the library at `path` is still in the address space. */
static inline int host_lib_is_loaded(const char* path)
{
    void* again = dlopen(path, RTLD_NOW | RTLD_NOLOAD);
    if (again != NULL)
    {
        dlclose(again);
        return 1;
    }
    return 0;
}
static inline const char* host_lib_error(void)
{
    const char* e = dlerror();
    return e != NULL ? e : "unknown reason";
}

/* ---- files ---- */

static inline int host_pid(void)
{
    return (int)getpid();
}
static inline void host_mkdir(const char* path)
{
    mkdir(path, 0700);
}
/* The files under `path` (recursively) whose names contain `part`. */
static inline int host_count_files(const char* path, const char* part)
{
    int count = 0;
    DIR* dir = opendir(path);
    struct dirent* entry;
    if (dir == NULL)
    {
        return 0;
    }
    while ((entry = readdir(dir)) != NULL)
    {
        char child[1024];
        struct stat info;
        snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        if (entry->d_name[0] == '.' || stat(child, &info) != 0)
        {
            continue;
        }
        if (S_ISDIR(info.st_mode))
        {
            count += host_count_files(child, part);
        }
        else if (strstr(entry->d_name, part) != NULL)
        {
            ++count;
        }
    }
    closedir(dir);
    return count;
}

/* ---- a crash in a child process ---- */

/* Call first in main: nothing on POSIX (the child is a fork; on Windows a second run of the program). */
static inline void host_child_entry(void (*child)(const char*))
{
    (void)child;
}
/* The wait status of the last child host_run_crash_child ran. */
static int g_host_child_status = 0;
/* Runs `child(arg)` in a child process started before this program has any thread of its own, and returns 1 when the
   child ended as the crash the test planted ends with the engine's handler (process_probe.h: SIGSEGV on Linux; on macOS
   exit code 1 from Breakpad, or SIGSEGV when a host handler was chained to), 0 otherwise. */
static inline int host_run_crash_child(void (*child)(const char*), const char* arg)
{
    int status = 0;
    pid_t pid = fork();
    if (pid == 0)
    {
        child(arg);
        _exit(4);
    }
    {
        /* Bounded as the Windows side is: a child that does not end is killed and the check fails at once. */
        int waited_ms = 0;
        pid_t done = 0;
        while ((done = waitpid(pid, &status, WNOHANG)) == 0 && waited_ms < 60000)
        {
            host_sleep_ms(10);
            waited_ms += 10;
        }
        if (done == 0)
        {
            fprintf(stderr, "host_run_crash_child: the child did not end within 60 s; it is killed\n");
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
        }
    }
    g_host_child_status = status;
    if (!ProbeEndedByFault(status))
    {
        printf("host_run_crash_child: the child ended with status 0x%x (signaled %d, signal %d, exited %d, code %d)\n",
               status, WIFSIGNALED(status) ? 1 : 0, WIFSIGNALED(status) ? WTERMSIG(status) : 0,
               WIFEXITED(status) ? 1 : 0, WIFEXITED(status) ? WEXITSTATUS(status) : 0);
        return 0;
    }
    return 1;
}
/* Ends the process at once, as a crashed child that was not caught would not get to (no atexit, no destructor). */
static inline void host_exit_now(int code)
{
    _exit(code);
}

/* ---- the host's own crash handler (what the engine's must give back) ---- */

/* Where the host's handler leaves a mark that it ran (empty: it leaves none); set before the crash. */
static char g_host_marker[1100];
static inline void host_set_crash_marker(const char* path)
{
    snprintf(g_host_marker, sizeof(g_host_marker), "%s", path);
}
static inline void host_own_handler(int signal_number)
{
    (void)signal_number;
    if (g_host_marker[0] != '\0')
    {
        const int fd = open(g_host_marker, O_WRONLY | O_CREAT | O_TRUNC, 0600); /* signal-safe calls only */
        if (fd >= 0)
        {
            if (write(fd, "ran\n", 4) < 0)
            {
            }
            close(fd);
        }
    }
}
/* Installs the host's own handler for the signal the engine's crash handler takes (process_probe.h: SIGSEGV on Linux,
   SIGABRT on macOS, where Breakpad takes faults as Mach exceptions). */
static inline void host_install_own_crash_handler(void)
{
    struct sigaction host;
    memset(&host, 0, sizeof(host));
    host.sa_handler = host_own_handler;
    sigaction(ENGINE_CRASH_HANDLER_SIGNAL, &host, NULL);
}
/* Installs the host's own handler for the signal a fault is delivered as, SIGSEGV (crash handler chaining: on macOS the
   engine's handler leaves it in place and calls it after the dump, as on Linux). */
static inline void host_install_own_fault_handler(void)
{
    struct sigaction host;
    memset(&host, 0, sizeof(host));
    host.sa_handler = host_own_handler;
    sigaction(SIGSEGV, &host, NULL);
}
/* Whether the last crash child died of SIGSEGV (a chained crash ends so, on Linux and macOS alike). */
static inline int host_crash_child_died_of_segv(void)
{
    return WIFSIGNALED(g_host_child_status) && WTERMSIG(g_host_child_status) == SIGSEGV;
}
/* Whether the handler that runs on a crash is the host's own. */
static inline int host_own_crash_handler_active(void)
{
    struct sigaction now;
    sigaction(ENGINE_CRASH_HANDLER_SIGNAL, NULL, &now);
    return now.sa_handler == host_own_handler;
}

/* ---- process facts (host_reload.c): process_probe.h, from /proc on Linux and from Mach and dyld on macOS ---- */

static inline host_process_facts host_read_process_facts(void)
{
    host_process_facts facts;
    facts.threads = ProbeThreadCount();
    facts.resident = ProbeResidentBytes();
    return facts;
}
/* Whether a mapped image's path contains `file_name`: 1, 0, or -1 when it cannot be told. */
static inline int host_library_mapped(const char* file_name)
{
    return ProbeMapped(file_name);
}

#endif
