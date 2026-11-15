/* Phase 23 Day 345: the engine library leaves the address space after its jobs ran. Runs jobs on the calling thread and
   on a host thread that is joined, installs and uninstalls the crash handler (every engine worker registers with it),
   then dlclose; the library is unloaded when dlopen(RTLD_NOLOAD) no longer finds it (on Linux; macOS keeps it:
   process_probe.h). Built with the sanitizer of the engine library it loads. */
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>

#include "host_api.h"
#include "process_probe.h"

static const struct engine_test_extension* g_table;
static int g_thread_ran;

static void* ThreadMain(void* unused)
{
    (void)unused;
    g_thread_ran = g_table->run_jobs(500);
    return NULL;
}

int main(void)
{
    void* engine = dlopen(ENGINE_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
    if (engine == NULL)
    {
        printf("dlopen: %s\n", dlerror());
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
    pthread_t thread;
    if (pthread_create(&thread, NULL, ThreadMain, NULL) != 0)
    {
        printf("pthread_create failed\n");
        return 2;
    }
    pthread_join(thread, NULL);
    ok &= g_thread_ran == 500;
    g_table->crash_uninstall();
    if (!ok)
    {
        printf("unload_host: the engine did not run its jobs\n");
        return 1;
    }

    if (dlclose(engine) != 0)
    {
        printf("unload_host: dlclose failed: %s\n", dlerror());
        return 1;
    }
    if (!ProbeLibraryGone(ENGINE_LIB_PATH))
    {
        printf("unload_host: the engine library is still mapped after dlclose (a thread_local destructor pins it)\n");
        return 1;
    }
    if (!ENGINE_LIBRARY_UNLOADS)
    {
        printf("unload_host: the engine library was closed (dyld keeps an image with Objective-C classes mapped)\n");
        return 0;
    }
    printf("unload_host: the engine library left the address space\n");
    return 0;
}
