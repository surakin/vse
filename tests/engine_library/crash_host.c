/* Phase 23 Day 345: the crash handler across the boundary. (1) The host has its own crash handler (SIGSEGV; on Windows
   the unhandled-exception filter, Day 354); the engine
   installs its handler (through the table) and uninstalls it, and the host's handler is back. (2) A child installs the
   handler and crashes inside a job of the shared engine: it dies of SIGSEGV and leaves an envelope holding a minidump
   in the dump directory. Plain flavor only. */
#include <stdio.h>
#include <string.h>

#include "host_api.h"

static const struct engine_test_extension* g_table;

static const struct engine_test_extension* Load(void)
{
    void* engine = host_lib_open(ENGINE_LIB_PATH);
    return host_test_extension(engine);
}

/* The crash child (a fork; on Windows a second run of this program): the handler, then a crash inside a job. */
static void CrashChild(const char* dump_dir)
{
    if (g_table->crash_install(dump_dir) == 1)
    {
        g_table->crash_in_job();
    }
    host_exit_now(5); /* not reached when the job crashes */
}

int main(void)
{
    /* A directory of this run: an envelope left by an earlier run must not make a failing run pass. */
    char dump_dir[1024];
    host_quiet_failures(); /* Windows: report failures on stderr, never in a dialog */
    snprintf(dump_dir, sizeof(dump_dir), "%s.%d", CRASH_DUMP_DIR, host_pid());
    host_mkdir(dump_dir);
    const struct engine_test_extension* table = Load();
    if (table == NULL)
    {
        printf("no state probe\n");
        return 2;
    }
    g_table = table;
    host_child_entry(&CrashChild); /* Windows: in the child run, this does not return */

    /* (1) The host's handler comes back. */
    host_install_own_crash_handler();
    if (table->crash_install(dump_dir) != 1)
    {
        printf("the engine did not install its crash handler\n");
        return 1;
    }
    const int during = host_own_crash_handler_active();
    table->crash_uninstall();
    const int after = host_own_crash_handler_active();
    if (during || !after)
    {
        printf("crash_host: the host's crash handler was %s while installed and is %s afterwards\n",
               during ? "kept (the engine installed nothing)" : "replaced", after ? "restored" : "NOT restored");
        return 1;
    }

    /* (2) A crash inside a job leaves a dump. */
    if (!host_run_crash_child(&CrashChild, dump_dir))
    {
        printf("crash_host: the child did not end as a crash\n");
        return 1;
    }
    if (host_count_files(dump_dir, ".envelope") < 1)
    {
        printf("crash_host: the crash inside a job left no envelope in %s\n", dump_dir);
        return 1;
    }
    printf("crash_host: handler restored, crash in a job left a dump\n");
    return 0;
}
