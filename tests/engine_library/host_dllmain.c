/* Phase 23 Day 354: loading the engine library runs nothing of the engine. It has no DllMain (Windows) of its own and
   no constructor that starts a thread or writes a line: after the load the process has the threads it had, and the
   first thing that happens is what the host asks for (engine_get_api, then create). After destroy, thread_detach and
   unload the library leaves the address space. The same program on Linux checks the same of libengine.so. */
#include <stdio.h>
#include <string.h>

#include "host_os.h"
#include "engine/engine_c.h"
#include "engine/engine_loader.h"

static int g_failures = 0;
static int g_log_lines = 0;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("host_dllmain: line %d: CHECK failed: %s\n", __LINE__, #condition);                                 \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

static void OnLog(void* user, engine_log_level level, engine_string channel, engine_string message)
{
    (void)level;
    (void)channel;
    (void)message;
    ++*(int*)user;
}

int main(void)
{
    engine_loader loader;
    engine_create_info info;
    engine_handle engine;
    int threads_before, threads_loaded, threads_running, waited;
    host_quiet_failures(); /* Windows: report failures on stderr, never in a dialog */

    threads_before = host_read_process_facts().threads;
    if (engine_load(ENGINE_LIB_PATH, ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, &loader) != ENGINE_OK)
    {
        printf("host_dllmain: %s\n", loader.message);
        return 2;
    }
    threads_loaded = host_read_process_facts().threads;
    /* A sanitizer runtime may start a thread of its own when the first module loads: one more is allowed, no engine
       worker (the job system starts a dozen). */
    CHECK(threads_loaded >= threads_before && threads_loaded <= threads_before + 1);

    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.flags = ENGINE_CREATE_LOG_SUPPRESS_CONSOLE;
    info.log = &OnLog;
    info.log_user = &g_log_lines;
    CHECK(loader.api->create(&info, &engine) == ENGINE_OK);
    threads_running = host_read_process_facts().threads; /* the job system starts its workers when it is first used */
    CHECK(loader.api->destroy(engine) == ENGINE_OK);
    loader.api->thread_detach();
    for (waited = 0; waited < 2000 && host_read_process_facts().threads > threads_loaded; ++waited)
    {
        host_sleep_ms(1); /* a joined thread can be listed for a moment */
    }
    CHECK(host_read_process_facts().threads <= threads_loaded);
    CHECK(engine_unload(&loader) == ENGINE_OK);
    CHECK(host_lib_gone(ENGINE_LIB_PATH)); /* unloaded where libraries unload; dyld keeps it on macOS (host_os.h) */
    printf("host_dllmain: %d threads before the load, %d after it, %d with an engine\n", threads_before, threads_loaded,
           threads_running);
    if (g_failures != 0)
    {
        printf("host_dllmain: %d checks failed\n", g_failures);
        return 1;
    }
    printf("host_dllmain: ok\n");
    return 0;
}
