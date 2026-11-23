/* Phase 23 Day 349: a C program drives the engine's runtime through the function table: frames, worlds, entities and
   their components, queries, counters and jobs, and the crash handler. It makes every bad call the API documents (a
   stale world, a stale entity, a wrong handle kind, a bad struct size) and expects the documented status each time. A
   second part runs two host threads at once (the thread rules of engine_c.h); under ThreadSanitizer that is the check
   of the locking. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "host_os.h"
#include "engine/engine_c.h"

static int g_failures = 0;
static const engine_api* g_api = NULL;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("host_runtime: line %d: CHECK failed: %s\n", __LINE__, #condition);                                 \
            host_atomic_add(&g_failures, 1);                                                                           \
        }                                                                                                              \
    } while (0)

#define CHECK_STATUS(call, expected)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status got_ = (call);                                                                             \
        if (got_ != (expected))                                                                                        \
        {                                                                                                              \
            printf("host_runtime: line %d: %s returned %d, expected %d\n", __LINE__, #call, (int)got_,                 \
                   (int)(expected));                                                                                   \
            host_atomic_add(&g_failures, 1);                                                                           \
        }                                                                                                              \
    } while (0)

static engine_string S(const char* text)
{
    engine_string s;
    s.data = text;
    s.length = strlen(text);
    return s;
}

static engine_handle NewEngine(const char* dump_dir)
{
    engine_setting_entry entry;
    engine_create_info info;
    engine_handle engine;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    if (dump_dir != NULL)
    {
        entry.key = S("crash.dump_directory");
        entry.value = S(dump_dir);
        info.settings = &entry;
        info.setting_count = 1;
    }
    memset(&engine, 0, sizeof(engine));
    CHECK_STATUS(g_api->create(&info, &engine), ENGINE_OK);
    return engine;
}

static engine_transform Transform(float x, float y, float z)
{
    engine_transform t;
    memset(&t, 0, sizeof(t));
    t.struct_size = sizeof(t);
    t.position[0] = x;
    t.position[1] = y;
    t.position[2] = z;
    t.rotation[3] = 1.0f;
    t.scale[0] = t.scale[1] = t.scale[2] = 1.0f;
    return t;
}

static engine_frame_info FrameInfo(double dt, uint64_t budget_us)
{
    engine_frame_info info;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.delta_seconds = dt;
    info.budget_us = budget_us;
    return info;
}

/* ---- jobs ---- */

typedef struct job_state
{
    int ran;        /* jobs that ran, counted atomically */
    int bad_thread; /* jobs that ran on the thread that submitted them */
    host_thread_id submitter;
    engine_handle world; /* a job that makes an entity in this world, when set */
} job_state;

static void CountJob(void* user)
{
    job_state* state = (job_state*)user;
    if (host_thread_equal(host_thread_self(), state->submitter))
    {
        host_atomic_add(&state->bad_thread, 1);
    }
    host_atomic_add(&state->ran, 1);
}

static void EntityJob(void* user)
{
    job_state* state = (job_state*)user;
    uint64_t key = 0;
    if (g_api->entity_create(state->world, &key) == ENGINE_OK && key != 0)
    {
        host_atomic_add(&state->ran, 1);
    }
}

static void WaitInsideJob(void* user)
{
    job_state* state = (job_state*)user;
    /* The counter handle is in `world` (reused as storage): a wait from inside a job is refused. */
    if (g_api->counter_wait(state->world, 0) == ENGINE_ERROR_INVALID_ARGUMENT)
    {
        host_atomic_add(&state->ran, 1);
    }
}

/* ---- the world, one thread ---- */

static void TestWorld(engine_handle engine)
{
    engine_handle world;
    enum
    {
        kEntities = 1000
    };
    static uint64_t keys[kEntities];
    static uint64_t found[kEntities + 8];
    size_t total = 0;
    int i;
    engine_frame_stats stats;
    engine_frame_info info = FrameInfo(0.016, 0);

    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    CHECK(world.value != 0);
    for (i = 0; i < kEntities; ++i)
    {
        engine_transform t = Transform((float)i, 2.0f, 3.0f);
        char name[16];
        CHECK_STATUS(g_api->entity_create(world, &keys[i]), ENGINE_OK);
        CHECK_STATUS(g_api->component_set(world, keys[i], ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
        snprintf(name, sizeof(name), "e%d", i);
        CHECK_STATUS(g_api->entity_name_set(world, keys[i], S(name)), ENGINE_OK);
        if (i % 4 == 0)
        {
            engine_mesh_reference mesh;
            memset(&mesh, 0, sizeof(mesh));
            mesh.struct_size = sizeof(mesh);
            mesh.mesh = 1000u + (uint64_t)i;
            mesh.material = 7;
            CHECK_STATUS(g_api->component_set(world, keys[i], ENGINE_COMPONENT_MESH_REFERENCE, &mesh), ENGINE_OK);
        }
    }

    /* queries: by component, the total beyond a small capacity, the empty mask */
    CHECK_STATUS(g_api->query(world, ENGINE_COMPONENT_BIT(ENGINE_COMPONENT_TRANSFORM), found, kEntities + 8, &total),
                 ENGINE_OK);
    CHECK(total == kEntities);
    CHECK_STATUS(g_api->query(world, ENGINE_COMPONENT_BIT(ENGINE_COMPONENT_MESH_REFERENCE), found, 10, &total),
                 ENGINE_OK);
    CHECK(total == kEntities / 4);
    CHECK_STATUS(g_api->query(world,
                              ENGINE_COMPONENT_BIT(ENGINE_COMPONENT_MESH_REFERENCE) |
                                  ENGINE_COMPONENT_BIT(ENGINE_COMPONENT_NAME),
                              NULL, 0, &total),
                 ENGINE_OK);
    CHECK(total == kEntities / 4);
    CHECK_STATUS(g_api->query(world, 0, NULL, 0, &total), ENGINE_OK);
    CHECK(total == kEntities);
    CHECK_STATUS(g_api->query(world, ENGINE_COMPONENT_MASK_ALL + 1u, found, 8, &total), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->query(world, 0, NULL, 4, &total), ENGINE_ERROR_INVALID_ARGUMENT);

    /* a frame runs the transform system: the world matrix is the local one for a root */
    memset(&stats, 0, sizeof(stats));
    stats.struct_size = sizeof(stats);
    CHECK_STATUS(g_api->frame(engine, &info, &stats), ENGINE_OK);
    CHECK(stats.frame_index == 1);
    CHECK(stats.worlds_run == 1 && stats.worlds_skipped == 0);
    CHECK(stats.entities == kEntities);
    for (i = 0; i < kEntities; i += 97)
    {
        engine_world_transform w;
        memset(&w, 0, sizeof(w));
        w.struct_size = sizeof(w);
        CHECK_STATUS(g_api->component_get(world, keys[i], ENGINE_COMPONENT_WORLD_TRANSFORM, &w), ENGINE_OK);
        CHECK(fabsf(w.matrix[12] - (float)i) < 1e-4f && fabsf(w.matrix[13] - 2.0f) < 1e-4f &&
              fabsf(w.matrix[14] - 3.0f) < 1e-4f);
    }
    /* a change shows after the next frame */
    {
        engine_transform moved = Transform(-5.0f, 0.0f, 0.0f);
        engine_world_transform w;
        memset(&w, 0, sizeof(w));
        w.struct_size = sizeof(w);
        CHECK_STATUS(g_api->component_set(world, keys[3], ENGINE_COMPONENT_TRANSFORM, &moved), ENGINE_OK);
        CHECK_STATUS(g_api->frame(engine, &info, NULL), ENGINE_OK);
        CHECK_STATUS(g_api->component_get(world, keys[3], ENGINE_COMPONENT_WORLD_TRANSFORM, &w), ENGINE_OK);
        CHECK(fabsf(w.matrix[12] + 5.0f) < 1e-4f);
    }

    /* reading back */
    {
        engine_transform t;
        engine_mesh_reference mesh;
        engine_buffer name;
        int has = -1;
        memset(&t, 0, sizeof(t));
        t.struct_size = sizeof(t);
        CHECK_STATUS(g_api->component_get(world, keys[10], ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
        CHECK(t.position[0] == 10.0f && t.scale[1] == 1.0f && t.rotation[3] == 1.0f);
        memset(&mesh, 0, sizeof(mesh));
        mesh.struct_size = sizeof(mesh);
        CHECK_STATUS(g_api->component_get(world, keys[8], ENGINE_COMPONENT_MESH_REFERENCE, &mesh), ENGINE_OK);
        CHECK(mesh.mesh == 1008u && mesh.material == 7);
        CHECK_STATUS(g_api->component_get(world, keys[9], ENGINE_COMPONENT_MESH_REFERENCE, &mesh),
                     ENGINE_ERROR_NOT_FOUND);
        memset(&name, 0, sizeof(name));
        CHECK_STATUS(g_api->entity_name_get(world, keys[10], &name), ENGINE_OK);
        CHECK(name.size == 3 && memcmp(name.data, "e10", 3) == 0);
        engine_buffer_release(&name);
        CHECK(name.data == NULL && name.release == NULL);
        CHECK_STATUS(g_api->component_has(world, keys[9], ENGINE_COMPONENT_MESH_REFERENCE, &has), ENGINE_OK);
        CHECK(has == 0);
        CHECK_STATUS(g_api->component_has(world, keys[8], ENGINE_COMPONENT_MESH_REFERENCE, &has), ENGINE_OK);
        CHECK(has == 1);
        CHECK_STATUS(g_api->component_remove(world, keys[8], ENGINE_COMPONENT_MESH_REFERENCE), ENGINE_OK);
        CHECK_STATUS(g_api->component_remove(world, keys[8], ENGINE_COMPONENT_MESH_REFERENCE), ENGINE_ERROR_NOT_FOUND);
    }

    /* every bad call */
    {
        engine_transform t = Transform(0, 0, 0);
        engine_transform small = t;
        engine_transform bad = t;
        engine_transform out;
        engine_buffer name;
        char long_name[ENGINE_NAME_MAX_BYTES + 2];
        uint64_t extra = 0;
        small.struct_size = 8;
        bad.position[1] = INFINITY;
        CHECK_STATUS(g_api->component_set(world, keys[0], ENGINE_COMPONENT_TRANSFORM, &small),
                     ENGINE_ERROR_STRUCT_SIZE);
        CHECK_STATUS(g_api->component_set(world, keys[0], ENGINE_COMPONENT_TRANSFORM, &bad),
                     ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->component_set(world, keys[0], ENGINE_COMPONENT_TRANSFORM, NULL),
                     ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->component_set(world, keys[0], ENGINE_COMPONENT_WORLD_TRANSFORM, &t),
                     ENGINE_ERROR_UNSUPPORTED);
        CHECK_STATUS(g_api->component_set(world, keys[0], ENGINE_COMPONENT_NAME, &t), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->component_set(world, keys[0], 99u, &t), ENGINE_ERROR_INVALID_ARGUMENT);
        memset(&out, 0, sizeof(out));
        out.struct_size = 4;
        CHECK_STATUS(g_api->component_get(world, keys[0], ENGINE_COMPONENT_TRANSFORM, &out), ENGINE_ERROR_STRUCT_SIZE);
        memset(long_name, 'x', sizeof(long_name));
        {
            engine_string too_long;
            too_long.data = long_name;
            too_long.length = ENGINE_NAME_MAX_BYTES + 1;
            CHECK_STATUS(g_api->entity_name_set(world, keys[0], too_long), ENGINE_ERROR_TOO_LARGE);
        }
        memset(&name, 0, sizeof(name));
        CHECK_STATUS(g_api->entity_name_get(world, 0, &name), ENGINE_ERROR_INVALID_ARGUMENT);
        /* a destroyed entity's key is stale; a destroyed world's handle is stale; a counter is not a world */
        CHECK_STATUS(g_api->entity_destroy(world, keys[5]), ENGINE_OK);
        CHECK_STATUS(g_api->entity_destroy(world, keys[5]), ENGINE_ERROR_STALE_HANDLE);
        CHECK_STATUS(g_api->component_set(world, keys[5], ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_ERROR_STALE_HANDLE);
        CHECK_STATUS(g_api->entity_create(world, &extra), ENGINE_OK); /* the slot is reused with another generation */
        CHECK(extra != keys[5]);
        out.struct_size = sizeof(out);
        CHECK_STATUS(g_api->component_get(world, keys[5], ENGINE_COMPONENT_TRANSFORM, &out), ENGINE_ERROR_STALE_HANDLE);
        CHECK_STATUS(g_api->entity_destroy(world, extra), ENGINE_OK);
    }

    /* destroy: the engine refuses while the world exists, the world's handle is stale afterwards */
    CHECK_STATUS(g_api->destroy(engine), ENGINE_ERROR_IN_USE);
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(g_api->entity_create(world, &keys[0]), ENGINE_ERROR_STALE_HANDLE);
    {
        engine_handle never;
        never.value = 0x0100000000FFFFFFull; /* a world kind, a generation that was never issued */
        CHECK_STATUS(g_api->world_destroy(never), ENGINE_ERROR_INVALID_ARGUMENT);
        never.value = 0;
        CHECK_STATUS(g_api->world_destroy(never), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->world_destroy(engine), ENGINE_ERROR_TYPE_MISMATCH); /* an engine handle is not a world */
    }

    /* frame arguments */
    {
        engine_frame_info bad = FrameInfo(-1.0, 0);
        engine_frame_info flags = FrameInfo(0.01, 0);
        engine_frame_info small = FrameInfo(0.01, 0);
        flags.flags = 1;
        small.struct_size = 8;
        CHECK_STATUS(g_api->frame(engine, &bad, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->frame(engine, &flags, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->frame(engine, &small, NULL), ENGINE_ERROR_STRUCT_SIZE);
        CHECK_STATUS(g_api->frame(engine, NULL, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->frame(engine, &info, NULL), ENGINE_OK); /* a frame without a world is a frame */
    }
}

static void TestBudget(engine_handle engine)
{
    engine_handle a, b;
    engine_frame_stats stats;
    engine_frame_info with_budget = FrameInfo(0.01, 1);
    engine_frame_info without = FrameInfo(0.01, 0);
    uint64_t key = 0;
    int i;
    CHECK_STATUS(g_api->world_create(engine, &a), ENGINE_OK);
    CHECK_STATUS(g_api->world_create(engine, &b), ENGINE_OK);
    for (i = 0; i < 2000; ++i) /* enough work that one microsecond is spent before the second world */
    {
        engine_transform t = Transform((float)i, 0, 0);
        CHECK_STATUS(g_api->entity_create(a, &key), ENGINE_OK);
        CHECK_STATUS(g_api->component_set(a, key, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
    }
    memset(&stats, 0, sizeof(stats));
    stats.struct_size = sizeof(stats);
    CHECK_STATUS(g_api->frame(engine, &with_budget, &stats), ENGINE_OK);
    CHECK(stats.worlds_run + stats.worlds_skipped == 2);
    CHECK(stats.worlds_run >= 1);     /* the first world always starts */
    CHECK(stats.worlds_skipped == 1); /* and the budget is spent when the second would start */
    CHECK_STATUS(g_api->frame(engine, &with_budget, &stats), ENGINE_OK); /* the skipped world goes first now */
    CHECK(stats.worlds_run >= 1);
    CHECK_STATUS(g_api->frame(engine, &without, &stats), ENGINE_OK);
    CHECK(stats.worlds_run == 2 && stats.worlds_skipped == 0);
    CHECK_STATUS(g_api->world_destroy(a), ENGINE_OK);
    CHECK_STATUS(g_api->world_destroy(b), ENGINE_OK);
}

/* ---- counters and jobs, one thread ---- */

static void TestJobs(engine_handle engine)
{
    enum
    {
        kJobs = 500
    };
    engine_handle counter;
    engine_handle world;
    job_state state;
    uint64_t pending = 99;
    int i;

    memset(&state, 0, sizeof(state));
    state.submitter = host_thread_self();
    CHECK_STATUS(g_api->counter_create(engine, &counter), ENGINE_OK);
    CHECK_STATUS(g_api->counter_value(counter, &pending), ENGINE_OK);
    CHECK(pending == 0);
    CHECK_STATUS(g_api->counter_wait(counter, 0), ENGINE_OK); /* nothing pending: done at once */
    for (i = 0; i < kJobs; ++i)
    {
        CHECK_STATUS(g_api->jobs_submit(engine, &CountJob, &state, counter), ENGINE_OK);
    }
    CHECK_STATUS(g_api->counter_wait(counter, UINT64_MAX), ENGINE_OK);
    CHECK(host_atomic_load(&state.ran) == kJobs);
    CHECK(state.bad_thread == 0); /* a job runs on an engine thread, never on the submitter's */
    CHECK_STATUS(g_api->counter_value(counter, &pending), ENGINE_OK);
    CHECK(pending == 0);

    /* a job may use the world functions through the table */
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    state.world = world;
    state.ran = 0;
    for (i = 0; i < 50; ++i)
    {
        CHECK_STATUS(g_api->jobs_submit(engine, &EntityJob, &state, counter), ENGINE_OK);
    }
    CHECK_STATUS(g_api->counter_wait(counter, UINT64_MAX), ENGINE_OK);
    CHECK(state.ran == 50);
    {
        size_t total = 0;
        CHECK_STATUS(g_api->query(world, 0, NULL, 0, &total), ENGINE_OK);
        CHECK(total == 50);
    }
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);

    /* a wait from inside a job is refused */
    state.world = counter;
    state.ran = 0;
    CHECK_STATUS(g_api->jobs_submit(engine, &WaitInsideJob, &state, counter), ENGINE_OK);
    CHECK_STATUS(g_api->counter_wait(counter, UINT64_MAX), ENGINE_OK);
    CHECK(state.ran == 1);

    /* a wait that times out; the counter cannot be destroyed while a job is pending */
    {
        static int release = 0;
        (void)release;
    }
    CHECK_STATUS(g_api->jobs_submit(engine, NULL, &state, counter), ENGINE_ERROR_INVALID_ARGUMENT);
    {
        engine_handle world_as_counter = world; /* a destroyed world's handle is not a counter either */
        CHECK_STATUS(g_api->counter_wait(world_as_counter, 0), ENGINE_ERROR_TYPE_MISMATCH);
        CHECK_STATUS(g_api->counter_value(engine, &pending), ENGINE_ERROR_TYPE_MISMATCH);
    }
    CHECK_STATUS(g_api->destroy(engine), ENGINE_ERROR_IN_USE); /* the counter is an object of the engine */
    CHECK_STATUS(g_api->counter_destroy(counter), ENGINE_OK);
    CHECK_STATUS(g_api->counter_destroy(counter), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(g_api->counter_wait(counter, 0), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(g_api->jobs_submit(engine, &CountJob, &state, counter), ENGINE_ERROR_STALE_HANDLE);
}

static int g_hold = 0; /* read and written through host_atomic_* */

static void HoldJob(void* user)
{
    (void)user;
    while (host_atomic_load(&g_hold) == 0)
    {
        host_sleep_ms(1);
    }
}

static void TestPendingCounter(engine_handle engine)
{
    engine_handle counter;
    uint64_t pending = 0;
    CHECK_STATUS(g_api->counter_create(engine, &counter), ENGINE_OK);
    host_atomic_store(&g_hold, 0);
    CHECK_STATUS(g_api->jobs_submit(engine, &HoldJob, NULL, counter), ENGINE_OK);
    CHECK_STATUS(g_api->counter_wait(counter, 20), ENGINE_ERROR_NOT_READY);
    CHECK_STATUS(g_api->counter_value(counter, &pending), ENGINE_OK);
    CHECK(pending == 1);
    CHECK_STATUS(g_api->counter_destroy(counter), ENGINE_ERROR_IN_USE);
    host_atomic_store(&g_hold, 1);
    CHECK_STATUS(g_api->counter_wait(counter, UINT64_MAX), ENGINE_OK);
    CHECK_STATUS(g_api->counter_destroy(counter), ENGINE_OK);
}

/* Many more jobs than the queues hold: the call that would pass the limit fails and nothing aborts. */
static void TestTooManyJobs(engine_handle engine)
{
    engine_handle counter;
    int accepted = 0, refused = 0, i;
    CHECK_STATUS(g_api->counter_create(engine, &counter), ENGINE_OK);
    host_atomic_store(&g_hold, 0);
    for (i = 0; i < 20000; ++i)
    {
        const engine_status status = g_api->jobs_submit(engine, &HoldJob, NULL, counter);
        if (status == ENGINE_OK)
        {
            ++accepted;
        }
        else
        {
            CHECK(status == ENGINE_ERROR_LIMIT_EXCEEDED);
            ++refused;
        }
    }
    CHECK(accepted > 0 && refused > 0);
    host_atomic_store(&g_hold, 1);
    CHECK_STATUS(g_api->counter_wait(counter, UINT64_MAX), ENGINE_OK);
    CHECK_STATUS(g_api->jobs_submit(engine, &HoldJob, NULL, counter), ENGINE_OK); /* the room is back */
    CHECK_STATUS(g_api->counter_wait(counter, UINT64_MAX), ENGINE_OK);
    CHECK_STATUS(g_api->counter_destroy(counter), ENGINE_OK);
}

/* ---- two host threads ---- */

typedef struct thread_args
{
    engine_handle engine;
    int id;
    int failures;
} thread_args;

static void ThreadMain(void* user)
{
    thread_args* args = (thread_args*)user;
    engine_handle world, counter;
    job_state state;
    uint64_t keys[200];
    size_t total = 0;
    int round, i;
    memset(&state, 0, sizeof(state));
    state.submitter = host_thread_self();
    CHECK_STATUS(g_api->world_create(args->engine, &world), ENGINE_OK);
    CHECK_STATUS(g_api->counter_create(args->engine, &counter), ENGINE_OK);
    for (round = 0; round < 20; ++round)
    {
        for (i = 0; i < 10; ++i)
        {
            engine_transform t = Transform((float)(args->id * 1000 + round * 10 + i), 0, 0);
            uint64_t key = 0;
            CHECK_STATUS(g_api->entity_create(world, &key), ENGINE_OK);
            CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
            CHECK_STATUS(g_api->jobs_submit(args->engine, &CountJob, &state, counter), ENGINE_OK);
        }
        CHECK_STATUS(g_api->query(world, ENGINE_COMPONENT_BIT(ENGINE_COMPONENT_TRANSFORM), keys, 200, &total),
                     ENGINE_OK);
        CHECK(total == (size_t)(round + 1) * 10);
    }
    CHECK_STATUS(g_api->counter_wait(counter, UINT64_MAX), ENGINE_OK);
    CHECK(host_atomic_load(&state.ran) == 200);
    CHECK_STATUS(g_api->counter_destroy(counter), ENGINE_OK);
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
    g_api->thread_detach();
}

static void TestTwoThreads(engine_handle engine)
{
    host_thread threads[2];
    thread_args args[2];
    engine_frame_info info = FrameInfo(0.01, 0);
    int i;
    for (i = 0; i < 2; ++i)
    {
        args[i].engine = engine;
        args[i].id = i;
        args[i].failures = 0;
        host_thread_start(&threads[i], ThreadMain, &args[i]);
    }
    for (i = 0; i < 20; ++i)
    {
        CHECK_STATUS(g_api->frame(engine, &info, NULL), ENGINE_OK); /* the host's own frame thread runs beside them */
    }
    for (i = 0; i < 2; ++i)
    {
        host_thread_join(&threads[i]);
    }
}

/* ---- the crash handler ---- */

#ifndef RUNTIME_NO_CRASH_CHILD
static void CrashJob(void* user)
{
    (void)user;
    *(volatile int*)0 = 1;
}

/* Runs in a child process before this program has any thread of its own: an engine of its own, the handler through the
 * table, a crash in a job. */
static void CrashChild(const char* dump_dir)
{
    engine_handle engine, counter;
    engine = NewEngine(dump_dir);
    if (g_api->crash_handler_install(engine) != ENGINE_OK)
    {
        host_exit_now(5); /* 3 is how a crash inside a job ends on Windows */
    }
    g_api->counter_create(engine, &counter);
    g_api->jobs_submit(engine, &CrashJob, NULL, counter);
    g_api->counter_wait(counter, 10000);
    host_exit_now(4); /* not reached: the job crashes */
}
#endif

static void TestCrashHandlerRestored(engine_handle engine)
{
    host_install_own_crash_handler();
    CHECK_STATUS(g_api->crash_handler_uninstall(engine), ENGINE_ERROR_NOT_FOUND);
    CHECK_STATUS(g_api->crash_handler_install(engine), ENGINE_OK);
    CHECK_STATUS(g_api->crash_handler_install(engine), ENGINE_ERROR_ALREADY_EXISTS);
    CHECK(!host_own_crash_handler_active());
    CHECK_STATUS(g_api->crash_handler_uninstall(engine), ENGINE_OK);
    CHECK(host_own_crash_handler_active());
    CHECK_STATUS(g_api->crash_handler_uninstall(engine), ENGINE_ERROR_NOT_FOUND);
    /* destroy takes the handler away too */
    CHECK_STATUS(g_api->crash_handler_install(engine), ENGINE_OK);
}

/* Maintenance (2026-11-20): spot lights and morph weights as components; a host of the first engine_light. */
static void TestLightsAndMorphs(engine_handle engine)
{
    engine_handle world;
    uint64_t key = 0;
    uint64_t found[4];
    size_t total = 0;
    engine_transform t = Transform(0.0f, 0.0f, 0.0f);
    engine_light light;
    engine_light back;
    engine_morph_weights weights;
    engine_morph_weights read;
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    CHECK_STATUS(g_api->entity_create(world, &key), ENGINE_OK);
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);

    /* a spot light round trips with its cones */
    memset(&light, 0, sizeof(light));
    light.struct_size = sizeof(light);
    light.kind = ENGINE_LIGHT_SPOT;
    light.color[0] = light.color[1] = light.color[2] = 1.0f;
    light.intensity = 5.0f;
    light.radius = 8.0f;
    light.inner_cone = 0.25f;
    light.outer_cone = 0.5f;
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_LIGHT, &light), ENGINE_OK);
    memset(&back, 0, sizeof(back));
    back.struct_size = sizeof(back);
    CHECK_STATUS(g_api->component_get(world, key, ENGINE_COMPONENT_LIGHT, &back), ENGINE_OK);
    CHECK(back.kind == ENGINE_LIGHT_SPOT && back.radius == 8.0f && back.inner_cone == 0.25f && back.outer_cone == 0.5f);
    /* bad cones and a spot without a radius are refused, and the light stays */
    light.inner_cone = 0.5f;
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_LIGHT, &light), ENGINE_ERROR_INVALID_ARGUMENT);
    light.inner_cone = 0.0f;
    light.outer_cone = 2.0f;
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_LIGHT, &light), ENGINE_ERROR_INVALID_ARGUMENT);
    light.outer_cone = NAN;
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_LIGHT, &light), ENGINE_ERROR_INVALID_ARGUMENT);
    light.outer_cone = 0.5f;
    light.radius = 0.0f;
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_LIGHT, &light), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->component_get(world, key, ENGINE_COMPONENT_LIGHT, &back), ENGINE_OK);
    CHECK(back.kind == ENGINE_LIGHT_SPOT && back.outer_cone == 0.5f);
    /* a host of the first version: its struct ends before the cones; it sets a point light, reads it without an
       overrun, and cannot make a spot light */
    {
        engine_light old;
        memset(&old, 0x5A, sizeof(old));
        old.struct_size = (uint32_t)offsetof(engine_light, inner_cone);
        old.kind = ENGINE_LIGHT_POINT;
        old.color[0] = old.color[1] = old.color[2] = 1.0f;
        old.intensity = 2.0f;
        old.radius = 3.0f;
        CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_LIGHT, &old), ENGINE_OK);
        old.kind = ENGINE_LIGHT_SPOT;
        CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_LIGHT, &old), ENGINE_ERROR_STRUCT_SIZE);
        memset(&old, 0x5A, sizeof(old));
        old.struct_size = (uint32_t)offsetof(engine_light, inner_cone);
        CHECK_STATUS(g_api->component_get(world, key, ENGINE_COMPONENT_LIGHT, &old), ENGINE_OK);
        CHECK(old.kind == ENGINE_LIGHT_POINT && old.radius == 3.0f &&
              old.struct_size == (uint32_t)offsetof(engine_light, inner_cone));
        {
            unsigned char untouched[4];
            memset(untouched, 0x5A, sizeof(untouched));
            CHECK(memcmp(&old.inner_cone, untouched, sizeof(untouched)) == 0); /* past the host's struct: not written */
        }
        old.struct_size = (uint32_t)offsetof(engine_light, radius);
        CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_LIGHT, &old), ENGINE_ERROR_STRUCT_SIZE);
    }

    /* morph weights round trip; the query finds them; bad ones are refused */
    memset(&weights, 0, sizeof(weights));
    weights.struct_size = sizeof(weights);
    weights.count = 3;
    weights.weights[0] = 0.25f;
    weights.weights[1] = 1.0f;
    weights.weights[2] = -0.5f;
    weights.weights[3] = 9.0f; /* beyond `count`: not stored */
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_MORPH_WEIGHTS, &weights), ENGINE_OK);
    memset(&read, 0, sizeof(read));
    read.struct_size = sizeof(read);
    CHECK_STATUS(g_api->component_get(world, key, ENGINE_COMPONENT_MORPH_WEIGHTS, &read), ENGINE_OK);
    CHECK(read.count == 3 && read.weights[0] == 0.25f && read.weights[1] == 1.0f && read.weights[2] == -0.5f &&
          read.weights[3] == 0.0f);
    CHECK_STATUS(g_api->query(world, ENGINE_COMPONENT_BIT(ENGINE_COMPONENT_MORPH_WEIGHTS), found, 4, &total),
                 ENGINE_OK);
    CHECK(total == 1 && found[0] == key);
    CHECK_STATUS(g_api->query(world, ENGINE_COMPONENT_MASK_ALL, NULL, 0, &total), ENGINE_OK);
    CHECK(total == 0); /* no entity has every component */
    weights.count = ENGINE_MORPH_WEIGHTS_MAX + 1;
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_MORPH_WEIGHTS, &weights),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    weights.count = 2;
    weights.weights[1] = INFINITY;
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_MORPH_WEIGHTS, &weights),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    weights.struct_size = 8;
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_MORPH_WEIGHTS, &weights), ENGINE_ERROR_STRUCT_SIZE);
    CHECK_STATUS(g_api->component_get(world, key, ENGINE_COMPONENT_MORPH_WEIGHTS, &read), ENGINE_OK);
    CHECK(read.count == 3);
    CHECK_STATUS(g_api->component_remove(world, key, ENGINE_COMPONENT_MORPH_WEIGHTS), ENGINE_OK);
    CHECK_STATUS(g_api->component_get(world, key, ENGINE_COMPONENT_MORPH_WEIGHTS, &read), ENGINE_ERROR_NOT_FOUND);
    /* a kind past the last is still unknown */
    CHECK_STATUS(g_api->component_get(world, key, (engine_component_kind)(ENGINE_COMPONENT_TERRAIN + 1), &read),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
}

int main(void)
{
    char dump_dir[1024];
    engine_handle engine;
    host_quiet_failures(); /* Windows: report failures on stderr, never in a dialog */

    snprintf(dump_dir, sizeof(dump_dir), "%s.%d", RUNTIME_DUMP_DIR, host_pid());
    host_mkdir(dump_dir);
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &g_api) != ENGINE_OK)
    {
        printf("host_runtime: no function table\n");
        return 2;
    }
    CHECK(g_api->struct_size >= sizeof(engine_api));
#ifndef RUNTIME_NO_CRASH_CHILD
    host_child_entry(&CrashChild); /* Windows: the crash child is a second run of this program, with the table */
#endif

    /* a crash in a job leaves a dump (the child starts before any thread of this program exists) */
#ifndef RUNTIME_NO_CRASH_CHILD /* ThreadSanitizer: the crash handler's dump helper traces the crashed process and      \
                                  never finishes (the Day 345 crash test is plain only too) */
    CHECK(host_run_crash_child(&CrashChild, dump_dir));
    CHECK(host_count_files(dump_dir, ".envelope") >= 1);
#endif

    engine = NewEngine(dump_dir);
    TestWorld(engine);
    TestLightsAndMorphs(engine);
    TestBudget(engine);
    TestJobs(engine);
    TestPendingCounter(engine);
    TestTooManyJobs(engine);
    TestTwoThreads(engine);
    TestCrashHandlerRestored(engine);
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    CHECK_STATUS(g_api->frame(engine, NULL, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    {
        engine_frame_info info = FrameInfo(0.01, 0);
        CHECK_STATUS(g_api->frame(engine, &info, NULL), ENGINE_ERROR_STALE_HANDLE);
    }
    g_api->thread_detach();

    if (g_failures != 0)
    {
        printf("host_runtime: %d check(s) failed\n", g_failures);
        return 1;
    }
    printf("host_runtime: ok\n");
    return 0;
}
