// Phase 23 Day 345: the planted failure of the duplicated-singleton test. A plugin that links the static foundation and
// job system carries its own copy of every singleton, so under hidden visibility it sees another allocator, another set
// of await counters, another log (channel list, ring and line count) and another frame pool than the engine library's
// (spec, Spike 2). The same report as shared_plugin.c, from the plugin's own copy.
#include "engine_c/engine_test_extension.h"
#include "jobs/counter.h"
#include "jobs/frame_pool.h"
#include "log/log.h"
#include "mem/heap_allocator.h"

using namespace engine_placeholder;

extern "C" void plugin_report(const struct engine_test_extension*, struct plugin_report* out)
{
    out->heap_default = &HeapAllocator::Default();
    out->path_stats = &jobs::Counter::PathStats();
    out->log_channels = LogChannel::First();
    out->frame_pool = &jobs::FramePool::Global();
    out->log_lines_before = LogLinesWritten();
    Log(LogCore, LogVerbosity::Warn, StringView("dup_plugin: one line through the plugin's own log"));
    out->log_lines_after = LogLinesWritten();
}
