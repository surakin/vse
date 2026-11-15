/* Phase 23 Day 345: loads the engine library and one plugin and checks that the plugin sees the engine's one allocator,
   await counters, log and frame pool. The paths are compiled in (ENGINE_LIB_PATH, PLUGIN_PATH). Exit 0: one of each;
   exit 1: a singleton seen twice. */
#include <dlfcn.h>
#include <stdio.h>

#include "host_api.h"

typedef void (*report_fn)(const struct engine_test_extension*, struct plugin_report*);

static int Same(const char* what, const void* a, const void* b)
{
    if (a == b)
    {
        return 0;
    }
    printf("singleton seen twice: %s (engine %p, plugin %p)\n", what, a, b);
    return 1;
}

int main(void)
{
    void* engine = dlopen(ENGINE_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
    if (engine == NULL)
    {
        printf("dlopen engine: %s\n", dlerror());
        return 2;
    }
    void* plugin = dlopen(PLUGIN_PATH, RTLD_NOW | RTLD_LOCAL);
    if (plugin == NULL)
    {
        printf("dlopen/dlsym: %s\n", dlerror());
        return 2;
    }
    report_fn report = (report_fn)dlsym(plugin, "plugin_report");
    const struct engine_test_extension* table = host_test_extension(engine);
    if (report == NULL || table == NULL || table->size < sizeof(struct engine_test_extension))
    {
        printf("no report function or a short table\n");
        return 2;
    }

    const unsigned long long engine_before = table->log_lines();
    struct plugin_report r = {0};
    report(table, &r);
    const unsigned long long engine_after = table->log_lines();

    int problems = 0;
    problems += Same("heap allocator", table->heap_default, r.heap_default);
    problems += Same("await-path counters", table->path_stats, r.path_stats);
    problems += Same("log channels", table->log_channels, r.log_channels);
    problems += Same("frame pool", table->frame_pool, r.frame_pool);
    /* One log: the line the plugin wrote shows in the engine's count, and the plugin's count is the engine's. */
    if (r.log_lines_after != r.log_lines_before + 1 || engine_after != engine_before + 1)
    {
        printf("singleton seen twice: log ring (engine %llu -> %llu, plugin %llu -> %llu)\n", engine_before,
               engine_after, r.log_lines_before, r.log_lines_after);
        ++problems;
    }
    if (problems == 0)
    {
        printf("dup_singletons: the plugin sees the engine's one allocator, counters, log and frame pool\n");
    }
    return problems == 0 ? 0 : 1;
}
