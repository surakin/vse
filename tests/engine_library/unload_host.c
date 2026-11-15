/* Phase 23 Day 345: the engine library leaves the address space after its jobs ran. Runs jobs on the calling thread and
   on a host thread that is joined, installs and uninstalls the crash handler (every engine worker registers with it),
   then unloads it (dlclose, FreeLibrary); the library is unloaded when the system's loader no longer finds it. Built
   with the sanitizer of the engine library it loads. */
#include <stdio.h>

#include "host_api.h"

static const struct engine_test_extension* g_table;
static int g_thread_ran;

static void ThreadMain(void* unused)
{
    (void)unused;
    g_thread_ran = g_table->run_jobs(500);
}

int main(void)
{
    host_quiet_failures(); /* Windows: report failures on stderr, never in a dialog */
    void* engine = host_lib_open(ENGINE_LIB_PATH);
    if (engine == NULL)
    {
        printf("dlopen: %s\n", host_lib_error());
        return 2;
    }
    g_table = host_test_extension(engine);
    if (g_table == NULL)
    {
        printf("no state probe\n");
        return 2;
    }

    int ok = 1;
    ok &= g_table->crash_install(UNLOAD_DUMP_DIR) == 1; /* workers started from now on register with the handler */
    ok &= g_table->run_jobs(500) == 500; /* on this thread: its frame-pool cache and workers' caches are in use */
    host_thread thread;
    if (host_thread_start(&thread, ThreadMain, NULL) != 0)
    {
        printf("could not start a thread\n");
        return 2;
    }
    host_thread_join(&thread);
    ok &= g_thread_ran == 500;
    g_table->crash_uninstall();
    if (!ok)
    {
        printf("unload_host: the engine did not run its jobs\n");
        return 1;
    }

    if (!host_lib_is_loaded(ENGINE_LIB_PATH))
    {
        printf("unload_host: the loader probe does not see the library while it is loaded\n");
        return 2;
    }
    if (host_lib_close(engine) != 0)
    {
        printf("unload_host: closing the library failed: %s\n", host_lib_error());
        return 1;
    }
    if (!host_lib_gone(ENGINE_LIB_PATH))
    {
        printf("unload_host: the engine library is still mapped after dlclose (a thread_local destructor pins it)\n");
        return 1;
    }
    if (!HOST_LIBRARY_UNLOADS)
    {
        printf("unload_host: the engine library was closed (dyld keeps an image with Objective-C classes mapped)\n");
        return 0;
    }
    printf("unload_host: the engine library left the address space\n");
    return 0;
}
