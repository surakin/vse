/* Phase 23 Day 348: a host thread is inside the engine (blocked in the log callback) while another thread destroys the
   engine. engine_destroy must not return, and the library must not be freed, until the callback has returned; meanwhile
   the handle is already stale and a new engine is refused. Unloading with a thread inside is the host's error
   (documented): this test unloads only after the threads are done. */
#include <stdio.h>
#include <string.h>

#include "host_os.h"
#include "engine/engine_c.h"
#include "engine/engine_loader.h"
#include "engine_c/engine_test_extension.h"

static int g_entered;
static int g_release;
static int g_destroy_returned;
static engine_loader g_loader;
static engine_handle g_engine;
static engine_status g_destroy_status;
static const struct engine_test_extension* g_state;
static int g_failures = 0;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("host_inside: line %d: CHECK failed: %s\n", __LINE__, #condition);                                  \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

static void OnLog(void* user, engine_log_level level, engine_string channel, engine_string message)
{
    (void)user;
    (void)level;
    (void)channel;
    (void)message;
    host_atomic_store(&g_entered, 1);
    while (host_atomic_load(&g_release) == 0)
    {
        host_sleep_ms(1);
    }
}

static void LogThread(void* unused)
{
    (void)unused;
    g_state->log_message("inside"); /* blocks in OnLog */
    g_loader.api->thread_detach();
}

static void DestroyThread(void* unused)
{
    (void)unused;
    g_destroy_status = g_loader.api->destroy(g_engine);
    host_atomic_store(&g_destroy_returned, 1);
    g_loader.api->thread_detach();
}

int main(void)
{
    engine_create_info info;
    engine_create_info another;
    engine_handle other;
    engine_info report;
    engine_string name = {"engine.test_state", sizeof("engine.test_state") - 1};
    host_thread logger;
    host_thread destroyer;
    int waited;
    host_quiet_failures(); /* Windows: report failures on stderr, never in a dialog */
    if (engine_load(ENGINE_LIB_PATH, ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, &g_loader) != ENGINE_OK)
    {
        printf("host_inside: %s\n", g_loader.message);
        return 2;
    }
    g_state = (const struct engine_test_extension*)g_loader.api->get_extension(name);
    if (g_state == NULL)
    {
        printf("host_inside: no test extension\n");
        return 2;
    }
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.log = &OnLog;
    CHECK(g_loader.api->create(&info, &g_engine) == ENGINE_OK);

    host_thread_start(&logger, LogThread, NULL);
    for (waited = 0; waited < 5000 && host_atomic_load(&g_entered) == 0; ++waited)
    {
        host_sleep_ms(1);
    }
    CHECK(host_atomic_load(&g_entered) == 1);

    host_thread_start(&destroyer, DestroyThread, NULL);
    host_sleep_ms(200);
    CHECK(host_atomic_load(&g_destroy_returned) == 0); /* a thread is inside the engine: destroy waits for it */
    memset(&report, 0, sizeof(report));
    report.struct_size = sizeof(report);
    CHECK(g_loader.api->get_info(g_engine, &report) == ENGINE_ERROR_STALE_HANDLE); /* but the handle is already gone */
    CHECK(g_loader.api->destroy(g_engine) == ENGINE_ERROR_STALE_HANDLE);
    another = info;
    another.log = NULL;
    CHECK(g_loader.api->create(&another, &other) ==
          ENGINE_ERROR_LIMIT_EXCEEDED); /* and no new engine while the old one is leaving */

    host_atomic_store(&g_release, 1);
    host_thread_join(&logger);
    host_thread_join(&destroyer);
    CHECK(host_atomic_load(&g_destroy_returned) == 1 && g_destroy_status == ENGINE_OK);
    CHECK(g_loader.api->create(&another, &other) == ENGINE_OK); /* the engine is gone: a new one may be made */
    CHECK(g_loader.api->destroy(other) == ENGINE_OK);
    g_loader.api->thread_detach();
    CHECK(engine_unload(&g_loader) == ENGINE_OK);
    CHECK(host_lib_gone(ENGINE_LIB_PATH));
    if (g_failures != 0)
    {
        printf("host_inside: %d checks failed\n", g_failures);
        return 1;
    }
    printf("host_inside: ok\n");
    return 0;
}
