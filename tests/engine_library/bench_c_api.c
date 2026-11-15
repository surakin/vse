/* Phase 23 Day 356: the per-call cost of the C API (hidden benchmark: built, never registered with ctest). A C program
   uses the engine through `engine_get_api` only and times the calls a host makes most: an empty frame, an entity create
   and destroy, a component set and get, and a lookup of a resource that is not there (the call path of a load without
   the work of one). Linked statically (`engine_c_api_bench_static`) or against libengine.so
   (`engine_c_api_bench_shared`): the difference is the cost of the shared boundary. Prints one line per call:
   nanoseconds per call (best of 5 rounds). */
#include <stdio.h>
#include <time.h>

#include "engine/engine_c.h"

static const engine_api* g_api = NULL;

static double NowNs(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e9 + (double)t.tv_nsec;
}

typedef struct state
{
    engine_handle engine;
    engine_handle world;
    uint64_t entity;
} state;

static void Frame(state* s)
{
    engine_frame_info info = {sizeof info, 0, 0.016, 0};
    g_api->frame(s->engine, &info, NULL);
}

static void CreateDestroy(state* s)
{
    uint64_t key = 0;
    g_api->entity_create(s->world, &key);
    g_api->entity_destroy(s->world, key);
}

static void SetTransform(state* s)
{
    engine_transform t = {sizeof t, {1, 2, 3}, {0, 0, 0, 1}, {1, 1, 1}};
    g_api->component_set(s->world, s->entity, ENGINE_COMPONENT_TRANSFORM, &t);
}

static void GetTransform(state* s)
{
    engine_transform t = {sizeof t, {0}, {0}, {0}};
    g_api->component_get(s->world, s->entity, ENGINE_COMPONENT_TRANSFORM, &t);
}

static void ResourceLookup(state* s)
{
    engine_resource_info info = {sizeof info, 0, 0, 0, {0}, 0, 0};
    g_api->resource_mounted(s->engine, 0x1234, &info);
}

static void Run(const char* name, void (*fn)(state*), state* s, int iterations)
{
    double best = 1e30;
    int round;
    for (round = 0; round < 5; ++round)
    {
        int i;
        const double start = NowNs();
        for (i = 0; i < iterations; ++i)
        {
            fn(s);
        }
        const double per = (NowNs() - start) / iterations;
        if (per < best)
        {
            best = per;
        }
    }
    printf("%-28s %10.1f ns/call\n", name, best);
}

int main(void)
{
    state s;
    engine_create_info info;
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &g_api) != ENGINE_OK)
    {
        printf("bench_c_api: no function table\n");
        return 2;
    }
    info = (engine_create_info){0};
    info.struct_size = sizeof info;
    if (g_api->create(&info, &s.engine) != ENGINE_OK || g_api->world_create(s.engine, &s.world) != ENGINE_OK ||
        g_api->entity_create(s.world, &s.entity) != ENGINE_OK)
    {
        printf("bench_c_api: setup failed\n");
        return 2;
    }
    Run("frame (one empty world)", Frame, &s, 20000);
    Run("entity create + destroy", CreateDestroy, &s, 200000);
    Run("component_set transform", SetTransform, &s, 500000);
    Run("component_get transform", GetTransform, &s, 500000);
    Run("resource_mounted (absent)", ResourceLookup, &s, 500000);
    g_api->world_destroy(s.world);
    g_api->destroy(s.engine);
    return 0;
}
