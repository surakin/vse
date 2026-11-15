/* Phase 23 Day 345: the table `engine_shared_state_probe` returns, shared by the engine library, the C hosts and the plugins of the boundary
   tests (tests/engine_library). Plain C. Day 346 replaces it with the real C API. */
#ifndef ENGINE_STATE_PROBE_H
#define ENGINE_STATE_PROBE_H

struct engine_state_probe
{
    unsigned size;
    const void* heap_default;  /* &HeapAllocator::Default() */
    const void* path_stats;    /* &jobs::Counter::PathStats() */
    const void* log_channels;  /* the first LogChannel of the registry */
    const void* frame_pool;    /* &jobs::FramePool::Global() */
    unsigned long long (*log_lines)(void);
    void (*log_message)(const char* text);
    void* (*allocate)(unsigned long size);
    void (*release)(void* block, unsigned long size); /* frees what `allocate(size)` returned */
    int (*run_jobs)(int count);
    int (*crash_install)(const char* directory);
    void (*crash_uninstall)(void);
    void (*crash_in_job)(void);
    long (*live_blocks)(void); /* blocks `allocate` returned and `release` has not taken back */
};

/* What a plugin reports about the singletons it sees. */
struct plugin_report
{
    const void* heap_default;
    const void* path_stats;
    const void* log_channels;
    const void* frame_pool;
    unsigned long long log_lines_before;
    unsigned long long log_lines_after; /* after one message logged through the plugin's own view of the log */
};

#endif
