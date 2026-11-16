/* Phase 23 Day 345 (Day 346: the extension `engine.test_state` of the function table): the struct the engine library
   hands to the boundary tests, shared by the library, the C hosts and the plugins of tests/engine_library. Plain C.
   Only a library built with ENGINE_C_TEST_EXTENSION has it. */
#ifndef ENGINE_TEST_EXTENSION_H
#define ENGINE_TEST_EXTENSION_H

struct engine_test_extension
{
    unsigned size;
    const void* heap_default; /* &HeapAllocator::Default() */
    const void* path_stats;   /* &jobs::Counter::PathStats() */
    const void* log_channels; /* the first LogChannel of the registry */
    const void* frame_pool;   /* &jobs::FramePool::Global() */
    unsigned long long (*log_lines)(void);
    void (*log_message)(const char* text);
    void* (*allocate)(unsigned long size);
    void (*release)(void* block, unsigned long size); /* frees what `allocate(size)` returned */
    int (*run_jobs)(int count);
    int (*crash_install)(const char* directory);
    void (*crash_uninstall)(void);
    void (*crash_in_job)(void);
    long (*live_blocks)(void);   /* blocks `allocate` returned and `release` has not taken back */
    int (*object_acquire)(void); /* counts an outstanding object on the engine (1), or 0 when no engine exists: destroy
                                    refuses while any is out */
    void (*object_release)(void);
    /* Phase 24 Day 369: loses the device of the surface whose engine_handle.value this is (1), or 0 when there is no
       engine or no such surface; the next frame finds it lost, surface_rebind makes a new device */
    int (*surface_lose_device)(unsigned long long surface);
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
