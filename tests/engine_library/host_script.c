/* Phase 27 Day 471: scripts through the C API. A C program loads a script into a world, calls its functions with values
   in and out, registers a host function the script calls, attaches a behavior to entities with the script component,
   runs frames, and checks the failures: bad pools, budgets, a busy world, a host function that is called while a frame
   runs, and stale handles. */
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
            printf("host_script: line %d: CHECK failed: %s\n", __LINE__, #condition);                                  \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

#define CHECK_STATUS(call, expected)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status got_ = (call);                                                                             \
        if (got_ != (expected))                                                                                        \
        {                                                                                                              \
            printf("host_script: line %d: %s returned %d, expected %d\n", __LINE__, #call, (int)got_,                  \
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

/* A pool with room: the host fills `items` and `bytes` for an input, the library writes them for an output. */
typedef struct pool_storage
{
    engine_value items[64];
    uint8_t bytes[512];
    engine_value_pool pool;
} pool_storage;

static void PoolInit(pool_storage* p)
{
    memset(p, 0, sizeof(*p));
    p->pool.struct_size = sizeof(p->pool);
    p->pool.items = p->items;
    p->pool.item_capacity = 64;
    p->pool.bytes = p->bytes;
    p->pool.byte_capacity = 512;
}

static void PoolInt(pool_storage* p, int64_t v)
{
    engine_value x;
    memset(&x, 0, sizeof(x));
    x.tag = ENGINE_VALUE_INT;
    x.i = v;
    p->items[p->pool.item_count++] = x;
    p->pool.root_count++;
}

/* The pool is "in": root_count and item_count are what the host wrote. */
static void PoolSealed(pool_storage* p)
{
    p->pool.item_capacity = p->pool.item_count;
    p->pool.byte_capacity = p->pool.byte_count;
}

static size_t PutBytes(pool_storage* p, const char* text)
{
    const size_t at = p->pool.byte_count;
    const size_t n = strlen(text);
    memcpy(p->bytes + at, text, n);
    p->pool.byte_count += n;
    return at;
}

static void SetString(engine_value* v, pool_storage* p, const char* text)
{
    memset(v, 0, sizeof(*v));
    v->tag = ENGINE_VALUE_STRING;
    v->i = (int64_t)PutBytes(p, text);
    v->count = (uint32_t)strlen(text);
}

/* The value stored under `key` in the map at item `map`, or NULL. */
static const engine_value* MapFind(const pool_storage* p, const engine_value* map, const char* key)
{
    uint32_t k;
    for (k = 0; k < map->count; ++k)
    {
        const engine_value* name = &p->items[map->i + (int64_t)k * 2];
        if (name->count == strlen(key) && memcmp(p->bytes + name->i, key, name->count) == 0)
        {
            return &p->items[map->i + (int64_t)k * 2 + 1];
        }
    }
    return NULL;
}

/* ---- host functions */

static int g_notes = 0;
static int64_t g_last_note = 0;
static int g_busy_checked = 0;
static engine_handle g_world;
static engine_status g_busy_status = ENGINE_OK;

static engine_status HostAdd(void* user, const engine_value_pool* args, engine_value_pool* results)
{
    int64_t sum = 0;
    uint32_t i;
    (void)user;
    for (i = 0; i < args->root_count; ++i)
    {
        sum += args->items[i].i;
    }
    memset(&results->items[0], 0, sizeof(engine_value));
    results->items[0].tag = ENGINE_VALUE_INT;
    results->items[0].i = sum;
    results->root_count = 1;
    results->item_count = 1;
    results->byte_count = 0;
    return ENGINE_OK;
}

static void CallFromAnotherThread(void* arg)
{
    pool_storage in, out;
    (void)arg;
    PoolInit(&in);
    PoolInit(&out);
    PoolInt(&in, 1);
    PoolInt(&in, 2);
    PoolSealed(&in);
    g_busy_status = g_api->script_call(g_world, Str("add3"), &in.pool, &out.pool);
}

static engine_status HostNote(void* user, const engine_value_pool* args, engine_value_pool* results)
{
    (void)user;
    ++g_notes;
    g_last_note = args->items[0].i;
    results->root_count = 0;
    results->item_count = 0;
    results->byte_count = 0;
    if (!g_busy_checked)
    {
        /* A frame is running: another thread's script_call finds the world busy and does not wait. */
        host_thread t;
        g_busy_checked = 1;
        CHECK(host_thread_start(&t, &CallFromAnotherThread, NULL) == 0);
        host_thread_join(&t);
    }
    return ENGINE_OK;
}

static engine_status HostFail(void* user, const engine_value_pool* args, engine_value_pool* results)
{
    (void)user;
    (void)args;
    (void)results;
    return ENGINE_ERROR_INVALID_ARGUMENT;
}

static const char kScript
    [] = "function add3(a, b, c) return host.add(host.add(a, b), c or 0) end\n"
         "function describe(t)\n"
         "  return { total = t.list[1] + t.list[2], name = t.name, flag = true, ratio = 0.5, v = vec3.new(1, 2, 3) }\n"
         "end\n"
         "function boom() error('nope') end\n"
         "function slow() local n = 0 while true do n = n + 1 end end\n"
         "function use_fail() return host.fail() end\n"
         "behavior('counter', {\n"
         "  on_create = function(self) self.n = 0 end,\n"
         "  on_update = function(self, dt) self.n = self.n + 1 host.note(self.n + self.params.start) end,\n"
         "})\n";

int main(void)
{
    engine_create_info create;
    engine_handle engine, world;
    engine_info info;
    engine_script_load load;
    engine_script_component component, back;
    engine_script_budget budget;
    engine_script_stats stats;
    engine_frame_info frame;
    uint64_t script = 0, entity = 0, entities[20];
    pool_storage in, out;
    engine_value_pool small;
    engine_value tiny[1];
    const engine_value* found;
    int i, has = 0;

    host_quiet_failures();
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &g_api) != ENGINE_OK)
    {
        printf("host_script: no function table\n");
        return 2;
    }
    memset(&create, 0, sizeof(create));
    create.struct_size = sizeof(create);
    memset(&engine, 0, sizeof(engine));
    CHECK_STATUS(g_api->create(&create, &engine), ENGINE_OK);
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    CHECK_STATUS(g_api->get_info(engine, &info), ENGINE_OK);
    CHECK((info.caps & ENGINE_CAP_SCRIPT) != 0);
    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    g_world = world;

    /* Before any script: stats are zero, a call has nothing to run. */
    memset(&stats, 0, sizeof(stats));
    stats.struct_size = sizeof(stats);
    CHECK_STATUS(g_api->script_stats(world, &stats), ENGINE_OK);
    CHECK(stats.scripts_loaded == 0 && stats.host_functions == 0 && stats.partitions == 0);
    PoolInit(&in);
    PoolInit(&out);
    CHECK_STATUS(g_api->script_call(world, Str("add3"), &in.pool, &out.pool), ENGINE_ERROR_NOT_READY);

    /* Host functions: names and arities are checked, a name is used once. */
    CHECK_STATUS(g_api->script_register_function(world, Str("host.add"), 2, 0, &HostAdd, NULL), ENGINE_OK);
    CHECK_STATUS(g_api->script_register_function(world, Str("host.note"), 1, 0, &HostNote, NULL), ENGINE_OK);
    CHECK_STATUS(g_api->script_register_function(world, Str("host.fail"), 0, 0, &HostFail, NULL), ENGINE_OK);
    CHECK_STATUS(g_api->script_register_function(world, Str("host.add"), 2, 0, &HostAdd, NULL),
                 ENGINE_ERROR_ALREADY_EXISTS);
    CHECK_STATUS(g_api->script_register_function(world, Str("math.sin2"), 1, 0, &HostAdd, NULL),
                 ENGINE_ERROR_ALREADY_EXISTS);
    CHECK_STATUS(g_api->script_register_function(world, Str("nodot"), 1, 0, &HostAdd, NULL),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->script_register_function(world, Str("Host.Add"), 1, 0, &HostAdd, NULL),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->script_register_function(world, Str("host.big"), 17, 0, &HostAdd, NULL),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->script_register_function(world, Str("host.flag"), 1, 2, &HostAdd, NULL),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->script_register_function(world, Str("host.null"), 1, 0, NULL, NULL),
                 ENGINE_ERROR_INVALID_ARGUMENT);

    /* Loading: the arguments are checked, the first load makes the runtime. */
    memset(&load, 0, sizeof(load));
    load.struct_size = sizeof(load);
    CHECK_STATUS(g_api->script_load(world, &load, &script), ENGINE_ERROR_INVALID_ARGUMENT); /* an empty source */
    load.name = Str("host_script.lua");
    load.source.data = kScript;
    load.source.length = sizeof(kScript) - 1;
    load.flags = 1;
    CHECK_STATUS(g_api->script_load(world, &load, &script), ENGINE_ERROR_INVALID_ARGUMENT);
    load.flags = 0;
    CHECK_STATUS(g_api->script_load(world, NULL, &script), ENGINE_ERROR_INVALID_ARGUMENT);
    load.struct_size = 4;
    CHECK_STATUS(g_api->script_load(world, &load, &script), ENGINE_ERROR_STRUCT_SIZE);
    load.struct_size = sizeof(load);
    CHECK_STATUS(g_api->script_load(world, &load, &script), ENGINE_OK);
    CHECK(script == 1);
    CHECK_STATUS(g_api->script_register_function(world, Str("host.late"), 1, 0, &HostAdd, NULL),
                 ENGINE_ERROR_NOT_READY);

    /* A call: integers in and out. */
    PoolInit(&in);
    PoolInit(&out);
    PoolInt(&in, 1);
    PoolInt(&in, 2);
    PoolInt(&in, 3);
    PoolSealed(&in);
    CHECK_STATUS(g_api->script_call(world, Str("add3"), &in.pool, &out.pool), ENGINE_OK);
    CHECK(out.pool.root_count == 1 && out.items[0].tag == ENGINE_VALUE_INT && out.items[0].i == 6);

    /* The same with the result dropped, and with no arguments (c is nil: 0). */
    CHECK_STATUS(g_api->script_call(world, Str("add3"), &in.pool, NULL), ENGINE_OK);

    /* A map with an array and a string in, a map out; an output with no room says how much it needs. */
    PoolInit(&in);
    {
        engine_value* v = in.items;
        memset(v, 0, 7 * sizeof(engine_value));
        v[0].tag = ENGINE_VALUE_MAP;
        v[0].count = 2;
        v[0].i = 1;
        SetString(&v[1], &in, "name");
        SetString(&v[2], &in, "abc");
        SetString(&v[3], &in, "list");
        v[4].tag = ENGINE_VALUE_ARRAY;
        v[4].count = 2;
        v[4].i = 5;
        v[5].tag = ENGINE_VALUE_INT;
        v[5].i = 3;
        v[6].tag = ENGINE_VALUE_INT;
        v[6].i = 4;
        in.pool.item_count = 7;
        in.pool.root_count = 1;
        PoolSealed(&in);
    }
    PoolInit(&out);
    CHECK_STATUS(g_api->script_call(world, Str("describe"), &in.pool, &out.pool), ENGINE_OK);
    CHECK(out.pool.root_count == 1 && out.items[0].tag == ENGINE_VALUE_MAP && out.items[0].count == 5);
    found = MapFind(&out, &out.items[0], "total");
    CHECK(found != NULL && found->tag == ENGINE_VALUE_INT && found->i == 7);
    found = MapFind(&out, &out.items[0], "name");
    CHECK(found != NULL && found->tag == ENGINE_VALUE_STRING && found->count == 3 &&
          memcmp(out.bytes + found->i, "abc", 3) == 0);
    found = MapFind(&out, &out.items[0], "flag");
    CHECK(found != NULL && found->tag == ENGINE_VALUE_BOOL && found->i == 1);
    found = MapFind(&out, &out.items[0], "ratio");
    CHECK(found != NULL && found->tag == ENGINE_VALUE_NUMBER && found->f == 0.5);
    found = MapFind(&out, &out.items[0], "v"); /* a vec3 comes out as an array of three numbers */
    CHECK(found != NULL && found->tag == ENGINE_VALUE_ARRAY && found->count == 3 &&
          out.items[found->i + 2].tag == ENGINE_VALUE_NUMBER && out.items[found->i + 2].f == 3.0);
    memset(&small, 0, sizeof(small));
    small.struct_size = sizeof(small);
    small.items = tiny;
    small.item_capacity = 1;
    CHECK_STATUS(g_api->script_call(world, Str("describe"), &in.pool, &small), ENGINE_ERROR_TOO_LARGE);
    CHECK(small.item_count > 1 && small.byte_count > 0);

    /* Bad pools: a string outside the bytes, an unknown tag, a map key that is not a string, a small struct. */
    PoolInit(&in);
    in.items[0].tag = ENGINE_VALUE_STRING;
    in.items[0].i = 100;
    in.items[0].count = 50;
    in.pool.item_count = 1;
    in.pool.root_count = 1;
    PoolSealed(&in);
    CHECK_STATUS(g_api->script_call(world, Str("add3"), &in.pool, &out.pool), ENGINE_ERROR_INVALID_ARGUMENT);
    in.items[0].tag = 99;
    CHECK_STATUS(g_api->script_call(world, Str("add3"), &in.pool, &out.pool), ENGINE_ERROR_INVALID_ARGUMENT);
    in.items[0].tag = ENGINE_VALUE_ARRAY;
    in.items[0].i = 0; /* an array that holds itself: the depth limit stops it */
    in.items[0].count = 1;
    CHECK_STATUS(g_api->script_call(world, Str("add3"), &in.pool, &out.pool), ENGINE_ERROR_INVALID_ARGUMENT);
    in.pool.struct_size = 4;
    CHECK_STATUS(g_api->script_call(world, Str("add3"), &in.pool, &out.pool), ENGINE_ERROR_STRUCT_SIZE);
    CHECK_STATUS(g_api->script_call(world, Str(""), NULL, NULL), ENGINE_ERROR_INVALID_ARGUMENT);

    /* Script errors: a raised error, a missing function, a host function that fails. */
    CHECK_STATUS(g_api->script_call(world, Str("boom"), NULL, NULL), ENGINE_ERROR_INTERNAL);
    CHECK_STATUS(g_api->script_call(world, Str("no_such_function"), NULL, NULL), ENGINE_ERROR_INTERNAL);
    CHECK_STATUS(g_api->script_call(world, Str("use_fail"), NULL, NULL), ENGINE_ERROR_INTERNAL);

    /* With no budget set, a loop still ends: the default is a limit, not "unlimited". */
    CHECK_STATUS(g_api->script_call(world, Str("slow"), NULL, NULL), ENGINE_ERROR_LIMIT_EXCEEDED);

    /* A budget stops a loop; a call after it works. */
    memset(&budget, 0, sizeof(budget));
    budget.struct_size = sizeof(budget);
    budget.instructions = 100000;
    CHECK_STATUS(g_api->script_set_budget(world, &budget), ENGINE_OK);
    CHECK_STATUS(g_api->script_call(world, Str("slow"), NULL, NULL), ENGINE_ERROR_LIMIT_EXCEEDED);
    PoolInit(&in);
    PoolInt(&in, 4);
    PoolInt(&in, 5);
    PoolSealed(&in);
    PoolInit(&out);
    CHECK_STATUS(g_api->script_call(world, Str("add3"), &in.pool, &out.pool), ENGINE_OK);
    CHECK(out.items[0].i == 9);
    budget.struct_size = 4;
    CHECK_STATUS(g_api->script_set_budget(world, &budget), ENGINE_ERROR_STRUCT_SIZE);
    CHECK_STATUS(g_api->script_set_budget(world, NULL), ENGINE_ERROR_INVALID_ARGUMENT);

    /* A script component: params are a map, the behavior name is checked. */
    CHECK_STATUS(g_api->entity_create(world, &entity), ENGINE_OK);
    memset(&component, 0, sizeof(component));
    component.struct_size = sizeof(component);
    component.enabled = 1;
    CHECK_STATUS(g_api->component_set(world, entity, ENGINE_COMPONENT_SCRIPT, &component),
                 ENGINE_ERROR_INVALID_ARGUMENT); /* no behavior name */
    strcpy(component.behavior, "counter");
    PoolInit(&in);
    {
        engine_value* v = in.items;
        memset(v, 0, 2 * sizeof(engine_value));
        v[0].tag = ENGINE_VALUE_MAP;
        v[0].count = 1;
        v[0].i = 1;
        SetString(&v[1], &in, "start");
        /* the value of the key goes after it: two items per pair */
        memset(&v[2], 0, sizeof(engine_value));
        v[2].tag = ENGINE_VALUE_INT;
        v[2].i = 100;
        in.pool.item_count = 3;
        in.pool.root_count = 1;
        PoolSealed(&in);
    }
    component.params = &in.pool;
    CHECK_STATUS(g_api->component_set(world, entity, ENGINE_COMPONENT_SCRIPT, &component), ENGINE_OK);
    CHECK_STATUS(g_api->component_has(world, entity, ENGINE_COMPONENT_SCRIPT, &has), ENGINE_OK);
    CHECK(has == 1);
    memset(&back, 0, sizeof(back));
    back.struct_size = sizeof(back);
    CHECK_STATUS(g_api->component_get(world, entity, ENGINE_COMPONENT_SCRIPT, &back), ENGINE_OK);
    CHECK(strcmp(back.behavior, "counter") == 0 && back.enabled == 1 && back.params == NULL);

    memset(&frame, 0, sizeof(frame));
    frame.struct_size = sizeof(frame);
    frame.delta_seconds = 1.0 / 60.0;
    for (i = 0; i < 10; ++i)
    {
        CHECK_STATUS(g_api->frame(engine, &frame, NULL), ENGINE_OK);
    }
    CHECK(g_notes == 10);
    CHECK(g_last_note == 110); /* n counts from 1 and the params said 100 */
    CHECK(g_busy_checked == 1);
    CHECK(g_busy_status == ENGINE_ERROR_NOT_READY); /* the call from another thread found the frame running */
    CHECK_STATUS(g_api->script_stats(world, &stats), ENGINE_OK);
    CHECK(stats.instances == 1 && stats.updates >= 10 && stats.partitions > 0 && stats.host_calls >= 10);
    CHECK(stats.scripts_loaded == 1 && stats.host_functions == 3 && stats.errors == 0);

    /* Disabled, the instance stops; enabled again it goes on; removed, it is closed. */
    component.enabled = 0;
    CHECK_STATUS(g_api->component_set(world, entity, ENGINE_COMPONENT_SCRIPT, &component), ENGINE_OK);
    CHECK_STATUS(g_api->frame(engine, &frame, NULL), ENGINE_OK);
    CHECK_STATUS(g_api->frame(engine, &frame, NULL), ENGINE_OK);
    CHECK(g_notes == 10);
    component.enabled = 1;
    CHECK_STATUS(g_api->component_set(world, entity, ENGINE_COMPONENT_SCRIPT, &component), ENGINE_OK);
    CHECK_STATUS(g_api->frame(engine, &frame, NULL), ENGINE_OK);
    CHECK(g_notes == 11);
    CHECK_STATUS(g_api->component_remove(world, entity, ENGINE_COMPONENT_SCRIPT), ENGINE_OK);
    CHECK_STATUS(g_api->component_remove(world, entity, ENGINE_COMPONENT_SCRIPT), ENGINE_ERROR_NOT_FOUND);
    CHECK_STATUS(g_api->frame(engine, &frame, NULL), ENGINE_OK);
    CHECK(g_notes == 11);
    CHECK_STATUS(g_api->script_stats(world, &stats), ENGINE_OK);
    CHECK(stats.instances == 0);

    /* 1,000 frames of 20 scripted entities. */
    for (i = 0; i < 20; ++i)
    {
        CHECK_STATUS(g_api->entity_create(world, &entities[i]), ENGINE_OK);
    }
    component.params = &in.pool;
    for (i = 0; i < 20; ++i)
    {
        CHECK_STATUS(g_api->component_set(world, entities[i], ENGINE_COMPONENT_SCRIPT, &component), ENGINE_OK);
    }
    {
        const int before = g_notes;
        for (i = 0; i < 1000; ++i)
        {
            CHECK_STATUS(g_api->frame(engine, &frame, NULL), ENGINE_OK);
        }
        CHECK(g_notes - before == 20 * 1000);
    }
    CHECK_STATUS(g_api->script_stats(world, &stats), ENGINE_OK);
    CHECK(stats.instances == 20 && stats.errors == 0);
    {
        uint64_t hits[32];
        size_t total = 0;
        CHECK_STATUS(g_api->query(world, ENGINE_COMPONENT_BIT(ENGINE_COMPONENT_SCRIPT), hits, 32, &total), ENGINE_OK);
        CHECK(total == 20);
        /* Every component bit at once (the query's list of ids once held 14 for 16 kinds: a stack overrun). */
        CHECK_STATUS(g_api->query(world, ENGINE_COMPONENT_MASK_ALL, hits, 32, &total), ENGINE_OK);
        CHECK(total == 0);
    }
    /* A params pool from a host whose struct is smaller than ours is refused before it is read. */
    in.pool.struct_size = 4;
    CHECK_STATUS(g_api->component_set(world, entities[0], ENGINE_COMPONENT_SCRIPT, &component),
                 ENGINE_ERROR_STRUCT_SIZE);
    in.pool.struct_size = sizeof(in.pool);

    /* A destroyed world's handle is stale for every script function. */
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
    PoolInit(&in);
    CHECK_STATUS(g_api->script_call(world, Str("add3"), &in.pool, &out.pool), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(g_api->script_load(world, &load, &script), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(g_api->script_stats(world, &stats), ENGINE_ERROR_STALE_HANDLE);
    budget.struct_size = sizeof(budget);
    CHECK_STATUS(g_api->script_set_budget(world, &budget), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);

    if (g_failures != 0)
    {
        printf("host_script: %d failures\n", g_failures);
        return 1;
    }
    printf("host_script: ok\n");
    return 0;
}
