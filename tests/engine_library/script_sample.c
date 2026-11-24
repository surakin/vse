/* Phase 27 Day 472: the scripting sample. A C host creates a world, registers host functions, loads a script whose
   behavior moves 1,000 entities for 600 frames and reacts to an event the host emits, receives the events the scripts
   emit, runs pure tasks, and prints the state hash of the world. The hash is the same whatever number of script
   partitions the world runs (1, 2, 4, 8): the program runs the whole scenario for each and compares. A second part
   has three host threads call into their own worlds (script_call, script_task_run) while another thread runs frames. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_os.h"
#include "engine/engine_c.h"

#define ENTITY_COUNT 1000
/* The frames a scenario runs: the ordinary run stops soon after the script's step 300 (its 'halfway' event) and the
   soak form (ENGINE_TEST_SOAK=1, a ctest labelled soak) runs the full 600; the sanitizer flavors are slow, so the short
   form is what they run day to day (tests/support/soak.h's rule). */
#define FRAME_COUNT_SHORT 320
#define FRAME_COUNT_SOAK 600
static uint64_t g_frame_count = FRAME_COUNT_SHORT;
#define FRAME_COUNT g_frame_count

static int g_failures = 0;
static const engine_api* g_api = NULL;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("script_sample: line %d: CHECK failed: %s\n", __LINE__, #condition);                                \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

#define CHECK_STATUS(call, expected)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status got_ = (call);                                                                             \
        if (got_ != (expected))                                                                                        \
        {                                                                                                              \
            printf("script_sample: line %d: %s returned %d, expected %d\n", __LINE__, #call, (int)got_,                \
                   (int)(expected));                                                                                   \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

static engine_string Str(const char* text)
{
    engine_string s;
    s.data = text;
    s.length = strlen(text);
    return s;
}

typedef struct pool_storage
{
    engine_value items[16];
    uint8_t bytes[256];
    engine_value_pool pool;
} pool_storage;

static void PoolInit(pool_storage* p)
{
    memset(p, 0, sizeof(*p));
    p->pool.struct_size = sizeof(p->pool);
    p->pool.items = p->items;
    p->pool.item_capacity = sizeof(p->items) / sizeof(p->items[0]);
    p->pool.bytes = p->bytes;
    p->pool.byte_capacity = sizeof(p->bytes);
}

static void PoolOneInt(pool_storage* p, int64_t v)
{
    PoolInit(p);
    p->items[0].tag = ENGINE_VALUE_INT;
    p->items[0].i = v;
    p->pool.root_count = 1;
    p->pool.item_count = 1;
    p->pool.item_capacity = 1;
    p->pool.byte_capacity = 0;
}

/* The map {index = v}. */
static void PoolIndexMap(pool_storage* p, int64_t v)
{
    PoolInit(p);
    p->items[0].tag = ENGINE_VALUE_MAP;
    p->items[0].count = 1;
    p->items[0].i = 1;
    p->items[1].tag = ENGINE_VALUE_STRING;
    p->items[1].i = 0;
    p->items[1].count = 5;
    memcpy(p->bytes, "index", 5);
    p->items[2].tag = ENGINE_VALUE_INT;
    p->items[2].i = v;
    p->pool.root_count = 1;
    p->pool.item_count = 3;
    p->pool.byte_count = 5;
    p->pool.item_capacity = 3;
    p->pool.byte_capacity = 5;
}

/* ---- host functions and the event listener (the world is held while a frame runs: they only count) */

static int g_tags = 0;
static int g_tag_sum = 0;
static int g_halfway = 0;
static int64_t g_halfway_sum = 0;
static int g_pure_calls = 0;

static engine_status HostTag(void* user, const engine_value_pool* args, engine_value_pool* results)
{
    (void)user;
    host_atomic_add(&g_tags, 1);
    host_atomic_add(&g_tag_sum, (int)args->items[0].i);
    results->root_count = 0;
    results->item_count = 0;
    results->byte_count = 0;
    return ENGINE_OK;
}

static engine_status HostTwice(void* user, const engine_value_pool* args, engine_value_pool* results)
{
    (void)user;
    host_atomic_add(&g_pure_calls, 1);
    memset(&results->items[0], 0, sizeof(engine_value));
    results->items[0].tag = ENGINE_VALUE_INT;
    results->items[0].i = args->items[0].i * 2;
    results->root_count = 1;
    results->item_count = 1;
    results->byte_count = 0;
    return ENGINE_OK;
}

static void OnHalfway(void* user, engine_string name, uint64_t sender, const engine_value_pool* payload)
{
    (void)user;
    (void)sender;
    CHECK(name.length == 7 && memcmp(name.data, "halfway", 7) == 0);
    CHECK(payload->root_count == 1 && payload->items[0].tag == ENGINE_VALUE_INT);
    ++g_halfway;
    g_halfway_sum += payload->items[0].i;
}

static int g_everything = 0;
static void OnAnything(void* user, engine_string name, uint64_t sender, const engine_value_pool* payload)
{
    (void)user;
    (void)name;
    (void)sender;
    (void)payload;
    ++g_everything;
}

static const char
    kScript[] = "behavior('mover', {\n"
                "  on_create = function(self)\n"
                "    self.step = 0\n"
                "    host.tag(self.params.index)\n"
                "    event.on('boost', function(me, payload) me.boost = payload end)\n"
                "  end,\n"
                "  on_update = function(self, dt)\n"
                "    self.step = self.step + 1\n"
                "    local i = self.params.index\n"
                "    transform.translate(self.entity, vec3.new(((i % 7) + 1) * 0.01 + (self.boost or 0), 0, 0))\n"
                "    if self.step == 300 then event.emit('halfway', i) end\n"
                "  end,\n"
                "})\n"
                "function add(a, b) return a + b end\n";

/* ---- the state hash: FNV-1a over the bits of every entity's position, in creation order */

static uint64_t Fnv(uint64_t h, const void* data, size_t size)
{
    const uint8_t* p = (const uint8_t*)data;
    size_t i;
    for (i = 0; i < size; ++i)
    {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

static uint64_t HashWorld(engine_handle world, const uint64_t* entities, size_t count)
{
    uint64_t h = 14695981039346656037ull;
    size_t i;
    for (i = 0; i < count; ++i)
    {
        engine_transform t;
        memset(&t, 0, sizeof(t));
        t.struct_size = sizeof(t);
        CHECK_STATUS(g_api->component_get(world, entities[i], ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
        h = Fnv(h, t.position, sizeof(t.position));
    }
    return h;
}

/* ---- the tasks: the same answers whatever the partitions */

static void CheckTasks(engine_handle world)
{
    engine_script_task task;
    pool_storage in, out;
    int64_t total = 0;
    uint32_t i;

    memset(&task, 0, sizeof(task));
    task.struct_size = sizeof(task);
    task.mode = ENGINE_SCRIPT_TASK_RUN;
    task.source = Str("return host.twice(args) + 1");
    PoolOneInt(&in, 20);
    task.data = &in.pool;
    PoolInit(&out);
    CHECK_STATUS(g_api->script_task_run(world, &task, &out.pool), ENGINE_OK);
    CHECK(out.pool.root_count == 1 && out.items[0].tag == ENGINE_VALUE_INT && out.items[0].i == 41);

    /* a parallel_for: the sum of 1..100 in chunks of ten */
    task.mode = ENGINE_SCRIPT_TASK_FOR;
    task.source = Str("local s = 0 for i = first, last do s = s + i end return s");
    task.data = NULL;
    task.count = 100;
    task.chunk = 10;
    PoolInit(&out);
    CHECK_STATUS(g_api->script_task_run(world, &task, &out.pool), ENGINE_OK);
    CHECK(out.pool.root_count == 1 && out.items[0].tag == ENGINE_VALUE_ARRAY && out.items[0].count == 10);
    for (i = 0; i < out.items[0].count; ++i)
    {
        total += out.items[out.items[0].i + i].i;
    }
    CHECK(total == 5050);

    /* a count near 2^64 is refused (the chunk count must not wrap), and a struct that is too short is refused first */
    task.count = 0xFFFFFFFFFFFFFFFFull;
    task.chunk = 1;
    PoolInit(&out);
    CHECK(g_api->script_task_run(world, &task, &out.pool) != ENGINE_OK);
    task.count = 100;
    task.chunk = 10;
    {
        engine_script_task shorter = task;
        shorter.struct_size = 4;
        PoolInit(&out);
        CHECK_STATUS(g_api->script_task_run(world, &shorter, &out.pool), ENGINE_ERROR_STRUCT_SIZE);
    }

    /* a task that raises an error fails the call with the script's words in the last error */
    task.mode = ENGINE_SCRIPT_TASK_RUN;
    task.source = Str("error('nope')");
    PoolInit(&out);
    CHECK_STATUS(g_api->script_task_run(world, &task, &out.pool), ENGINE_ERROR_INTERNAL);

    /* a task cannot touch the world: `transform` is not there */
    task.source = Str("return transform.get(args)");
    PoolInit(&out);
    CHECK_STATUS(g_api->script_task_run(world, &task, &out.pool), ENGINE_ERROR_INTERNAL);

    /* a budget set after the task pool exists applies to the next task (the pool is remade) */
    {
        engine_script_budget budget;
        memset(&budget, 0, sizeof(budget));
        budget.struct_size = sizeof(budget);
        budget.instructions = 1000;
        CHECK_STATUS(g_api->script_set_budget(world, &budget), ENGINE_OK);
        task.source = Str("local s = 0 for i = 1, 10000000 do s = s + i end return s");
        PoolInit(&out);
        CHECK(g_api->script_task_run(world, &task, &out.pool) != ENGINE_OK);
        budget.instructions = 0;
        CHECK_STATUS(g_api->script_set_budget(world, &budget), ENGINE_OK);
        task.source = Str("return 7");
        PoolInit(&out);
        CHECK_STATUS(g_api->script_task_run(world, &task, &out.pool), ENGINE_OK);
        CHECK(out.pool.root_count == 1 && out.items[0].i == 7);
    }

    /* bad arguments */
    task.mode = 9;
    CHECK_STATUS(g_api->script_task_run(world, &task, &out.pool), ENGINE_ERROR_INVALID_ARGUMENT);
    task.mode = ENGINE_SCRIPT_TASK_RUN;
    task.reserved = 1;
    CHECK_STATUS(g_api->script_task_run(world, &task, &out.pool), ENGINE_ERROR_INVALID_ARGUMENT);
    task.reserved = 0;
    task.struct_size = 4;
    CHECK_STATUS(g_api->script_task_run(world, &task, &out.pool), ENGINE_ERROR_STRUCT_SIZE);
    CHECK_STATUS(g_api->script_task_run(world, NULL, &out.pool), ENGINE_ERROR_INVALID_ARGUMENT);
}

/* ---- host threads with worlds of their own, beside a frame thread */

typedef struct worker
{
    engine_handle world;
    int failures;
    int64_t sum;
    host_thread thread;
} worker;

static void WorkerMain(void* arg)
{
    worker* w = (worker*)arg;
    int i;
    for (i = 0; i < 100; ++i)
    {
        pool_storage in, out;
        engine_script_task task;
        int tries = 0;
        engine_status status;
        PoolInit(&in);
        in.items[0].tag = ENGINE_VALUE_INT;
        in.items[0].i = i;
        in.items[1].tag = ENGINE_VALUE_INT;
        in.items[1].i = 2;
        in.pool.root_count = 2;
        in.pool.item_count = 2;
        in.pool.item_capacity = 2;
        in.pool.byte_capacity = 0;
        do
        {
            PoolInit(&out);
            status = g_api->script_call(w->world, Str("add"), &in.pool, &out.pool);
            if (status == ENGINE_ERROR_NOT_READY)
            {
                host_sleep_ms(1); /* a frame holds the world: the call does not wait */
            }
        } while (status == ENGINE_ERROR_NOT_READY && ++tries < 100000);
        if (status != ENGINE_OK || out.pool.root_count != 1 || out.items[0].i != i + 2)
        {
            ++w->failures;
        }
        else
        {
            w->sum += out.items[0].i;
        }
        memset(&task, 0, sizeof(task));
        task.struct_size = sizeof(task);
        task.mode = ENGINE_SCRIPT_TASK_RUN;
        task.source = Str("return args + 1");
        PoolOneInt(&in, i);
        task.data = &in.pool;
        tries = 0;
        do
        {
            PoolInit(&out);
            status = g_api->script_task_run(w->world, &task, &out.pool);
            if (status == ENGINE_ERROR_NOT_READY)
            {
                host_sleep_ms(1);
            }
        } while (status == ENGINE_ERROR_NOT_READY && ++tries < 100000);
        if (status != ENGINE_OK || out.pool.root_count != 1 || out.items[0].i != i + 1)
        {
            ++w->failures;
        }
        else
        {
            w->sum += out.items[0].i;
        }
    }
    g_api->thread_detach();
}

static void CheckThreads(engine_handle engine, engine_handle main_world)
{
    worker workers[3];
    engine_script_load load;
    engine_frame_info frame;
    uint64_t script = 0;
    int i;

    (void)main_world;
    memset(&load, 0, sizeof(load));
    load.struct_size = sizeof(load);
    load.name = Str("worker.lua");
    load.source = Str("function add(a, b) return a + b end");
    memset(&frame, 0, sizeof(frame));
    frame.struct_size = sizeof(frame);
    frame.delta_seconds = 1.0 / 60.0;
    for (i = 0; i < 3; ++i)
    {
        memset(&workers[i], 0, sizeof(workers[i]));
        CHECK_STATUS(g_api->world_create(engine, &workers[i].world), ENGINE_OK);
        CHECK_STATUS(g_api->script_load(workers[i].world, &load, &script), ENGINE_OK);
    }
    for (i = 0; i < 3; ++i)
    {
        CHECK(host_thread_start(&workers[i].thread, &WorkerMain, &workers[i]) == 0);
    }
    for (i = 0; i < 200; ++i) /* this thread runs frames: the other worlds' calls wait for a free moment */
    {
        CHECK_STATUS(g_api->frame(engine, &frame, NULL), ENGINE_OK);
    }
    for (i = 0; i < 3; ++i)
    {
        host_thread_join(&workers[i].thread);
        CHECK(workers[i].failures == 0);
        CHECK(workers[i].sum == 2 * 4950 + 200 + 100); /* sum of (i + 2) and of (i + 1) for i in 0..99 */
        CHECK_STATUS(g_api->world_destroy(workers[i].world), ENGINE_OK);
    }
}

/* ---- one run of the scenario */

static uint64_t RunScenario(const char* partitions, int with_threads)
{
    engine_create_info create;
    engine_setting_entry entry;
    engine_handle engine, world;
    engine_script_load load;
    engine_script_component component;
    engine_script_stats stats;
    engine_frame_info frame;
    engine_transform transform;
    pool_storage params, boost;
    uint64_t script = 0, hash;
    static uint64_t entities[ENTITY_COUNT];
    uint32_t halfway_id = 0, anything_id = 0;
    int i;

    g_tags = g_tag_sum = g_halfway = g_everything = g_pure_calls = 0;
    g_halfway_sum = 0;
    entry.key = Str("script.partitions");
    entry.value = Str(partitions);
    memset(&create, 0, sizeof(create));
    create.struct_size = sizeof(create);
    create.settings = &entry;
    create.setting_count = 1;
    memset(&engine, 0, sizeof(engine));
    CHECK_STATUS(g_api->create(&create, &engine), ENGINE_OK);
    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);

    CHECK_STATUS(g_api->script_register_function(world, Str("host.tag"), 1, 0, &HostTag, NULL), ENGINE_OK);
    CHECK_STATUS(
        g_api->script_register_function(world, Str("host.twice"), 1, ENGINE_SCRIPT_FUNCTION_PURE, &HostTwice, NULL),
        ENGINE_OK);
    /* Subscribing before the first script is allowed: the runtime takes the subscription when it is made. */
    CHECK_STATUS(g_api->script_event_subscribe(world, Str("halfway"), &OnHalfway, NULL, &halfway_id), ENGINE_OK);
    CHECK_STATUS(g_api->script_event_subscribe(world, Str(""), &OnAnything, NULL, &anything_id), ENGINE_OK);
    CHECK(halfway_id != 0 && anything_id != 0 && halfway_id != anything_id);
    CHECK_STATUS(g_api->script_event_subscribe(world, Str("x"), NULL, NULL, &halfway_id),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    /* The host cannot emit before a script exists. */
    CHECK_STATUS(g_api->script_event_emit(world, Str("boost"), NULL, 0), ENGINE_ERROR_NOT_READY);

    memset(&load, 0, sizeof(load));
    load.struct_size = sizeof(load);
    load.name = Str("sample.lua");
    load.source = Str(kScript);
    CHECK_STATUS(g_api->script_load(world, &load, &script), ENGINE_OK);

    for (i = 0; i < ENTITY_COUNT; ++i)
    {
        CHECK_STATUS(g_api->entity_create(world, &entities[i]), ENGINE_OK);
        memset(&transform, 0, sizeof(transform));
        transform.struct_size = sizeof(transform);
        transform.rotation[3] = 1.0f;
        transform.scale[0] = transform.scale[1] = transform.scale[2] = 1.0f;
        CHECK_STATUS(g_api->component_set(world, entities[i], ENGINE_COMPONENT_TRANSFORM, &transform), ENGINE_OK);
        PoolIndexMap(&params, i);
        memset(&component, 0, sizeof(component));
        component.struct_size = sizeof(component);
        component.enabled = 1;
        memcpy(component.behavior, "mover", 5);
        component.params = &params.pool;
        CHECK_STATUS(g_api->component_set(world, entities[i], ENGINE_COMPONENT_SCRIPT, &component), ENGINE_OK);
    }

    memset(&frame, 0, sizeof(frame));
    frame.struct_size = sizeof(frame);
    frame.delta_seconds = 1.0 / 60.0;
    for (i = 0; (uint64_t)i < FRAME_COUNT; ++i)
    {
        if (i == 100)
        {
            /* The host emits an event to every script that listens; it is delivered at the start of the next tick. */
            PoolInit(&boost);
            boost.items[0].tag = ENGINE_VALUE_NUMBER;
            boost.items[0].f = 0.25;
            boost.pool.root_count = 1;
            boost.pool.item_count = 1;
            boost.pool.item_capacity = 1;
            boost.pool.byte_capacity = 0;
            CHECK_STATUS(g_api->script_event_emit(world, Str("boost"), &boost.pool, 0), ENGINE_OK);
            CHECK_STATUS(g_api->script_event_emit(world, Str(""), NULL, 0), ENGINE_ERROR_INVALID_ARGUMENT);
        }
        CHECK_STATUS(g_api->frame(engine, &frame, NULL), ENGINE_OK);
    }

    CHECK(g_tags == ENTITY_COUNT && g_tag_sum == 499500);
    CHECK(g_halfway == ENTITY_COUNT && g_halfway_sum == 499500);
    CHECK(g_everything >= ENTITY_COUNT + 1); /* the 'halfway' events and the host's 'boost' */
    memset(&stats, 0, sizeof(stats));
    stats.struct_size = sizeof(stats);
    CHECK_STATUS(g_api->script_stats(world, &stats), ENGINE_OK);
    CHECK(stats.instances == ENTITY_COUNT && stats.errors == 0 && stats.ticks == FRAME_COUNT);
    hash = HashWorld(world, entities, ENTITY_COUNT);

    CheckTasks(world);
    CHECK(g_pure_calls >= 1);

    /* An unsubscribed listener hears nothing more. */
    CHECK_STATUS(g_api->script_event_unsubscribe(world, halfway_id), ENGINE_OK);
    CHECK_STATUS(g_api->script_event_unsubscribe(world, halfway_id), ENGINE_ERROR_NOT_FOUND);
    CHECK_STATUS(g_api->script_event_unsubscribe(world, anything_id), ENGINE_OK);

    if (with_threads)
    {
        CheckThreads(engine, world);
    }
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    return hash;
}

int main(void)
{
    /* The ordinary run compares one partition with four (the thread-count independence the sample proves); the soak
       form runs 1, 2, 4 and 8. */
    static const char* const kPartitions[] = {"1", "4", "2", "8"};
    const int scenarios = g_frame_count == FRAME_COUNT_SOAK ? 4 : 2;
    uint64_t first = 0;
    int i;

    host_quiet_failures();
    if (getenv("ENGINE_TEST_SOAK") != NULL && getenv("ENGINE_TEST_SOAK")[0] == '1')
    {
        g_frame_count = FRAME_COUNT_SOAK;
    }
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &g_api) != ENGINE_OK)
    {
        printf("script_sample: no function table\n");
        return 2;
    }
    for (i = 0; i < scenarios; ++i)
    {
        const uint64_t hash = RunScenario(kPartitions[i], i == 1);
        printf("script_sample: partitions %s: state hash %016llx\n", kPartitions[i], (unsigned long long)hash);
        if (i == 0)
        {
            first = hash;
        }
        CHECK(hash == first);
    }
    if (g_failures != 0)
    {
        printf("script_sample: %d failures\n", g_failures);
        return 1;
    }
    printf("script_sample: ok\n");
    return 0;
}
