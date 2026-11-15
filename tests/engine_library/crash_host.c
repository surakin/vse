/* Phase 23 Day 345: the crash handler across the boundary. (1) The host has its own SIGSEGV handler; the engine installs its handler
   (through the table) and uninstalls it, and the host's handler is back. (2) A child installs the handler and crashes inside a job of
   the shared engine: it dies of SIGSEGV and leaves an envelope holding a minidump in the dump directory. Plain flavor only. */
#include <dirent.h>
#include <dlfcn.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "engine_c/engine_state_probe.h"

typedef const struct engine_state_probe* (*get_probe_fn)(void);

static void HostHandler(int signal_number)
{
    (void)signal_number;
}

static const struct engine_state_probe* Load(void)
{
    void* engine = dlopen(ENGINE_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
    get_probe_fn get_probe = engine != NULL ? (get_probe_fn)dlsym(engine, "engine_shared_state_probe") : NULL;
    return get_probe != NULL ? get_probe() : NULL;
}

static int CountEnvelopes(const char* path)
{
    int count = 0;
    DIR* dir = opendir(path);
    if (dir == NULL)
    {
        return 0;
    }
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL)
    {
        char child[1024];
        snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        struct stat info;
        if (entry->d_name[0] == '.' || stat(child, &info) != 0)
        {
            continue;
        }
        if (S_ISDIR(info.st_mode))
        {
            count += CountEnvelopes(child);
        }
        else if (strstr(entry->d_name, ".envelope") != NULL)
        {
            ++count;
        }
    }
    closedir(dir);
    return count;
}

int main(void)
{
    /* A directory of this run: an envelope left by an earlier run must not make a failing run pass. */
    char dump_dir[1024];
    snprintf(dump_dir, sizeof(dump_dir), "%s.%d", CRASH_DUMP_DIR, (int)getpid());
    mkdir(dump_dir, 0700);
    const struct engine_state_probe* table = Load();
    if (table == NULL)
    {
        printf("no state probe\n");
        return 2;
    }

    /* (1) The host's handler comes back. */
    struct sigaction host;
    memset(&host, 0, sizeof(host));
    host.sa_handler = HostHandler;
    sigaction(SIGSEGV, &host, NULL);
    if (table->crash_install(dump_dir) != 1)
    {
        printf("the engine did not install its crash handler\n");
        return 1;
    }
    struct sigaction during;
    sigaction(SIGSEGV, NULL, &during);
    table->crash_uninstall();
    struct sigaction after;
    sigaction(SIGSEGV, NULL, &after);
    if (during.sa_handler == HostHandler || after.sa_handler != HostHandler)
    {
        printf("crash_host: the host's SIGSEGV handler was %s while installed and is %s afterwards\n",
               during.sa_handler == HostHandler ? "kept (the engine installed nothing)" : "replaced",
               after.sa_handler == HostHandler ? "restored" : "NOT restored");
        return 1;
    }

    /* (2) A crash inside a job leaves a dump. */
    pid_t child = fork();
    if (child == 0)
    {
        if (table->crash_install(dump_dir) == 1)
        {
            table->crash_in_job();
        }
        _exit(3); /* not reached when the job crashes */
    }
    int status = 0;
    waitpid(child, &status, 0);
    if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGSEGV)
    {
        printf("crash_host: the child did not die of SIGSEGV (status %d)\n", status);
        return 1;
    }
    if (CountEnvelopes(dump_dir) < 1)
    {
        printf("crash_host: the crash inside a job left no envelope in %s\n", dump_dir);
        return 1;
    }
    printf("crash_host: handler restored, crash in a job left a dump\n");
    return 0;
}
