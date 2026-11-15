/* Phase 23 Day 345: a plugin that links only the C API: it holds no copy of the foundation and reports what the
 * engine's table shows. */
#include "engine_c/engine_test_extension.h"

void plugin_report(const struct engine_test_extension* engine, struct plugin_report* out)
{
    out->heap_default = engine->heap_default;
    out->path_stats = engine->path_stats;
    out->log_channels = engine->log_channels;
    out->frame_pool = engine->frame_pool;
    out->log_lines_before = engine->log_lines();
    engine->log_message("shared_plugin: one line through the engine's log");
    out->log_lines_after = engine->log_lines();
}
