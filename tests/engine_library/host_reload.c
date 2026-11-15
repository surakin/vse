/* Phase 23 Day 348: load, run, destroy, unload, 50 times in one process. After every cycle the library must be gone
   from /proc/self/maps and from the dynamic linker (RTLD_NOLOAD), the process must have the threads it started with;
   after the last, the resident size must be near what it was after the first few cycles (nothing accumulates per cycle)
   and the engine's own live-block count must be zero. Under asan the leak report at exit is the check for what is
   really leaked. */
#include <dirent.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "engine_c/engine_c.h"
#include "engine_c/engine_loader.h"
#include "engine_c/engine_test_extension.h"

#define CYCLES 50
#define WARMUP 5

static int g_failures = 0;
static int g_log_calls = 0;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("host_reload: line %d: CHECK failed: %s\n", __LINE__, #condition);                                  \
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

static int ThreadCount(void)
{
    int n = 0;
    DIR* dir = opendir("/proc/self/task");
    struct dirent* entry;
    if (dir == NULL)
    {
        return -1;
    }
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_name[0] != '.')
        {
            ++n;
        }
    }
    closedir(dir);
    return n;
}

static long ResidentBytes(void)
{
    long total = 0;
    long resident = 0;
    FILE* f = fopen("/proc/self/statm", "r");
    if (f == NULL)
    {
        return -1;
    }
    if (fscanf(f, "%ld %ld", &total, &resident) != 2)
    {
        resident = -1;
    }
    fclose(f);
    return resident < 0 ? -1 : resident * sysconf(_SC_PAGESIZE);
}

/* Whether any mapping of this process names the engine library file. */
static int Mapped(const char* file_name)
{
    char line[1024];
    int found = 0;
    FILE* f = fopen("/proc/self/maps", "r");
    if (f == NULL)
    {
        return -1;
    }
    while (fgets(line, sizeof(line), f) != NULL)
    {
        if (strstr(line, file_name) != NULL)
        {
            found = 1;
        }
    }
    fclose(f);
    return found;
}

static int g_longest_settle_ms = 0;

int main(void)
{
    const int threads_before = ThreadCount();
    int threads_after_first = 0; /* a sanitizer runtime may start one thread of its own at the first thread the process
                                    creates: it stays, and counts once */
    long resident_after_warmup = 0;
    int cycle;
    for (cycle = 0; cycle < CYCLES; ++cycle)
    {
        engine_loader loader;
        engine_create_info info;
        engine_handle engine;
        engine_string name = {"engine.test_state", sizeof("engine.test_state") - 1};
        const struct engine_test_extension* state;
        engine_setting_entry entry = {{"rhi.pipeline_cache", 18}, {"true", 4}};
        if (engine_load(ENGINE_LIB_PATH, ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, &loader) != ENGINE_OK)
        {
            printf("host_reload: cycle %d: %s\n", cycle, loader.message);
            return 2;
        }
        CHECK(Mapped(ENGINE_LIB_FILE) == 1);
        memset(&info, 0, sizeof(info));
        info.struct_size = sizeof(info);
        info.settings = &entry;
        info.setting_count = 1;
        info.log = &OnLog;
        info.log_user = &g_log_calls;
        info.flags = ENGINE_CREATE_LOG_SUPPRESS_CONSOLE;
        CHECK(loader.api->create(&info, &engine) == ENGINE_OK);
        state = (const struct engine_test_extension*)loader.api->get_extension(name);
        CHECK(state != NULL &&
              state->run_jobs(100) == 100); /* the headless frame, until the frame loop exists (Day 349) */
        if (state != NULL)
        {
            state->log_message("a line");
            CHECK(state->live_blocks() == 0);
        }
        CHECK(loader.api->destroy(engine) == ENGINE_OK);
        loader.api->thread_detach();
        CHECK(engine_unload(&loader) == ENGINE_OK);
        CHECK(dlopen(ENGINE_LIB_PATH, RTLD_NOW | RTLD_NOLOAD) == NULL);
        CHECK(Mapped(ENGINE_LIB_FILE) == 0);
        if (cycle == 0)
        {
            threads_after_first = ThreadCount();
            CHECK(threads_after_first >= threads_before && threads_after_first <= threads_before + 1);
        }
        else
        {
            /* No thread of the engine outlives its engine. A thread that has been joined can still be listed for a
               moment (the kernel removes its task entry just after), so the count settles within a short wait; a thread
               that is really left never goes away. */
            int waited_ms = 0;
            while (ThreadCount() != threads_after_first && waited_ms < 2000)
            {
                usleep(1000);
                ++waited_ms;
            }
            if (waited_ms > g_longest_settle_ms)
            {
                g_longest_settle_ms = waited_ms;
            }
            CHECK(ThreadCount() == threads_after_first);
        }
        if (cycle == WARMUP - 1)
        {
            resident_after_warmup = ResidentBytes();
        }
    }
    {
        const long resident_at_end = ResidentBytes();
        const long growth = resident_at_end - resident_after_warmup;
        printf(
            "host_reload: %d cycles, %d log lines, resident %ld KiB after cycle %d, %ld KiB at the end, longest wait for the thread count %d ms\n",
            CYCLES, g_log_calls, resident_after_warmup / 1024, WARMUP, resident_at_end / 1024, g_longest_settle_ms);
        CHECK(g_log_calls == CYCLES);
        CHECK(resident_after_warmup > 0 && growth < RESIDENT_GROWTH_LIMIT_KIB * 1024L);
    }
    if (g_failures != 0)
    {
        printf("host_reload: %d checks failed\n", g_failures);
        return 1;
    }
    printf("host_reload: ok\n");
    return 0;
}
