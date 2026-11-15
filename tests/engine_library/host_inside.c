/* Phase 23 Day 348: a host thread is inside the engine (blocked in the log callback) while another thread destroys the
   engine. engine_destroy must not return, and the library must not be freed, until the callback has returned; meanwhile
   the handle is already stale and a new engine is refused. Unloading with a thread inside is the host's error
   (documented): this test unloads only after the threads are done. */
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "engine/engine_c.h"
#include "engine/engine_loader.h"
#include "engine_c/engine_test_extension.h"

static atomic_int g_entered;
static atomic_int g_release;
static atomic_int g_destroy_returned;
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
    atomic_store(&g_entered, 1);
    while (atomic_load(&g_release) == 0)
    {
        usleep(1000);
    }
}

static void* LogThread(void* unused)
{
    (void)unused;
    g_state->log_message("inside"); /* blocks in OnLog */
    g_loader.api->thread_detach();
    return NULL;
}

static void* DestroyThread(void* unused)
{
    (void)unused;
    g_destroy_status = g_loader.api->destroy(g_engine);
    atomic_store(&g_destroy_returned, 1);
    g_loader.api->thread_detach();
    return NULL;
}

int main(void)
{
    engine_create_info info;
    engine_create_info another;
    engine_handle other;
    engine_info report;
    engine_string name = {"engine.test_state", sizeof("engine.test_state") - 1};
    pthread_t logger;
    pthread_t destroyer;
    int waited;
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

    pthread_create(&logger, NULL, LogThread, NULL);
    for (waited = 0; waited < 5000 && atomic_load(&g_entered) == 0; ++waited)
    {
        usleep(1000);
    }
    CHECK(atomic_load(&g_entered) == 1);

    pthread_create(&destroyer, NULL, DestroyThread, NULL);
    usleep(200 * 1000);
    CHECK(atomic_load(&g_destroy_returned) == 0); /* a thread is inside the engine: destroy waits for it */
    memset(&report, 0, sizeof(report));
    report.struct_size = sizeof(report);
    CHECK(g_loader.api->get_info(g_engine, &report) == ENGINE_ERROR_STALE_HANDLE); /* but the handle is already gone */
    CHECK(g_loader.api->destroy(g_engine) == ENGINE_ERROR_STALE_HANDLE);
    another = info;
    another.log = NULL;
    CHECK(g_loader.api->create(&another, &other) ==
          ENGINE_ERROR_LIMIT_EXCEEDED); /* and no new engine while the old one is leaving */

    atomic_store(&g_release, 1);
    pthread_join(logger, NULL);
    pthread_join(destroyer, NULL);
    CHECK(atomic_load(&g_destroy_returned) == 1 && g_destroy_status == ENGINE_OK);
    CHECK(g_loader.api->create(&another, &other) == ENGINE_OK); /* the engine is gone: a new one may be made */
    CHECK(g_loader.api->destroy(other) == ENGINE_OK);
    g_loader.api->thread_detach();
    CHECK(engine_unload(&g_loader) == ENGINE_OK);
    CHECK(dlopen(ENGINE_LIB_PATH, RTLD_NOW | RTLD_NOLOAD) == NULL);
    if (g_failures != 0)
    {
        printf("host_inside: %d checks failed\n", g_failures);
        return 1;
    }
    printf("host_inside: ok\n");
    return 0;
}
