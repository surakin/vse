/* Scripts move instances (Phase 28 Day 501): a Lua behavior on each of 100 entities moves one instance of a set every
   tick (instances.set), adds one at its first tick (instances.add), removes none; one more entity swings a node of an
   instance tree (tree.set_local). The program runs the whole scenario for 1, 8 and 64 script partitions and compares
   the FNV-1a hash of every instance and of the swung node (read back with instance_set_get_transform): it is the same
   for every number of partitions. It also checks that a script's unknown set, a bad rotation and an unknown node are
   errors, and that the set holds the 100 + 100 instances. Plain C. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/engine_c.h"

#define COUNT 100u
#define FRAMES 120

static int g_failures = 0;
static const engine_api* g_api = NULL;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("host_script_instances: line %d: CHECK failed: %s\n", __LINE__, #condition);                        \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

#define CHECK_STATUS(call, expected)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status got_ = (call);                                                                             \
        if (got_ != (expected))                                                                                        \
        {                                                                                                              \
            printf("host_script_instances: line %d: %s returned %d, expected %d\n", __LINE__, #call, (int)got_,        \
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

/* The map {set = a, id = b, index = c, tree = d}. */
static void PoolMap(pool_storage* p, int64_t set, int64_t id, int64_t index, int64_t tree)
{
    static const char* const names[4] = {"set", "id", "index", "tree"};
    const int64_t values[4] = {set, id, index, tree};
    size_t offset = 0;
    int i;
    memset(p, 0, sizeof(*p));
    p->pool.struct_size = sizeof(p->pool);
    p->pool.items = p->items;
    p->pool.bytes = p->bytes;
    p->items[0].tag = ENGINE_VALUE_MAP;
    p->items[0].count = 4;
    p->items[0].i = 1;
    for (i = 0; i < 4; ++i)
    {
        const size_t length = strlen(names[i]);
        p->items[1 + 2 * i].tag = ENGINE_VALUE_STRING;
        p->items[1 + 2 * i].i = (int64_t)offset;
        p->items[1 + 2 * i].count = (uint32_t)length;
        memcpy(p->bytes + offset, names[i], length);
        offset += length;
        p->items[2 + 2 * i].tag = ENGINE_VALUE_INT;
        p->items[2 + 2 * i].i = values[i];
    }
    p->pool.root_count = 1;
    p->pool.item_count = 9;
    p->pool.item_capacity = 9;
    p->pool.byte_count = offset;
    p->pool.byte_capacity = offset;
}

static const char kScript[] =
    "behavior('mover', {\n"
    "  on_create = function(self) self.step = 0 end,\n"
    "  on_update = function(self, dt)\n"
    "    self.step = self.step + 1\n"
    "    local p = self.params\n"
    "    local i = p.index\n"
    "    instances.set(p.set, p.id, vec3.new(i * 2.0 + self.step * 0.01, 0.5, 0), quat.identity(), vec3.new(1, 1, 1))\n"
    "    if self.step == 1 then\n"
    "      instances.add(p.set, vec3.new(i * 2.0, 5, 0), quat.identity(), vec3.new(1, 1, 1), vec4.new(1, 0, 0, 1))\n"
    "      if i == 0 then\n"
    "        local ok = pcall(instances.add, 999999, vec3.new(0, 0, 0), quat.identity(), vec3.new(1, 1, 1), nil)\n"
    "        local bad = pcall(instances.add, p.set, vec3.new(0, 0, 0), quat.identity(), vec3.new(-1, 1, 1), nil)\n"
    "        local node = pcall(tree.set_local, p.tree, 77, vec3.new(0, 0, 0), quat.identity(), vec3.new(1, 1, 1))\n"
    "        event.emit('errors', (ok and 1 or 0) + (bad and 10 or 0) + (node and 100 or 0))\n"
    "      end\n"
    "    end\n"
    "    if i == 1 then\n"
    "      tree.set_local(p.tree, 1, vec3.new(0, 1, 0), quat.from_axis_angle(vec3.new(1, 0, 0), self.step * 0.01),\n"
    "                     vec3.new(1, 1, 1))\n"
    "    end\n"
    "  end,\n"
    "})\n";

static int g_errors_seen = -1;

static void OnErrors(void* user, engine_string name, uint64_t sender, const engine_value_pool* payload)
{
    (void)user;
    (void)name;
    (void)sender;
    if (payload->root_count == 1 && payload->items[0].tag == ENGINE_VALUE_INT)
    {
        g_errors_seen = (int)payload->items[0].i;
    }
}

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

static uint64_t RunScenario(const char* partitions)
{
    engine_create_info create;
    engine_setting_entry entry;
    engine_handle engine, world;
    engine_script_load load;
    engine_script_component component;
    engine_frame_info frame;
    engine_instance_placement placement;
    engine_instance items[COUNT];
    engine_instance extra;
    engine_tree_node nodes[2];
    uint64_t ids[COUNT];
    uint64_t entities[COUNT];
    uint64_t set = 0, tree = 0, script = 0, hash = 14695981039346656037ull;
    uint64_t meshes[1] = {1};
    float bounds[6] = {-1, -1, -1, 1, 1, 1};
    uint32_t event_id = 0;
    pool_storage params;
    uint32_t i;
    int f;

    g_errors_seen = -1;
    entry.key = Str("script.partitions");
    entry.value = Str(partitions);
    memset(&create, 0, sizeof(create));
    create.struct_size = sizeof(create);
    create.flags = ENGINE_CREATE_LOG_SUPPRESS_CONSOLE;
    create.settings = &entry;
    create.setting_count = 1;
    memset(&engine, 0, sizeof(engine));
    CHECK_STATUS(g_api->create(&create, &engine), ENGINE_OK);
    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    CHECK_STATUS(g_api->script_event_subscribe(world, Str("errors"), &OnErrors, NULL, &event_id), ENGINE_OK);

    memset(&placement, 0, sizeof(placement));
    placement.struct_size = sizeof(placement);
    placement.rotation[3] = 1.0f;
    placement.scale = 1.0f;
    CHECK_STATUS(g_api->instance_set_create(world, &placement, 1, 2 * COUNT, &set), ENGINE_OK);
    for (i = 0; i < COUNT; ++i)
    {
        memset(&items[i], 0, sizeof(items[i]));
        items[i].position[0] = (double)i * 2.0;
        items[i].rotation[3] = 1.0f;
        items[i].scale[0] = items[i].scale[1] = items[i].scale[2] = 1.0f;
        items[i].tint[0] = items[i].tint[1] = items[i].tint[2] = items[i].tint[3] = 1.0f;
    }
    CHECK_STATUS(g_api->instance_set_add_many(world, set, items, COUNT, sizeof(engine_instance), ids), ENGINE_OK);
    memset(nodes, 0, sizeof(nodes));
    nodes[0].rotation[3] = 1.0f;
    nodes[0].scale[0] = nodes[0].scale[1] = nodes[0].scale[2] = 1.0f;
    nodes[0].tint[0] = nodes[0].tint[1] = nodes[0].tint[2] = nodes[0].tint[3] = 1.0f;
    nodes[0].parent = -1;
    nodes[1] = nodes[0];
    nodes[1].parent = 0;
    nodes[1].position[1] = 1.0;
    CHECK_STATUS(
        g_api->instance_tree_create(world, &placement, meshes, bounds, 1, nodes, 2, sizeof(engine_tree_node), &tree),
        ENGINE_OK);

    memset(&load, 0, sizeof(load));
    load.struct_size = sizeof(load);
    load.name = Str("instances.lua");
    load.source = Str(kScript);
    CHECK_STATUS(g_api->script_load(world, &load, &script), ENGINE_OK);
    for (i = 0; i < COUNT; ++i)
    {
        engine_transform transform;
        CHECK_STATUS(g_api->entity_create(world, &entities[i]), ENGINE_OK);
        memset(&transform, 0, sizeof(transform));
        transform.struct_size = sizeof(transform);
        transform.rotation[3] = 1.0f;
        transform.scale[0] = transform.scale[1] = transform.scale[2] = 1.0f;
        CHECK_STATUS(g_api->component_set(world, entities[i], ENGINE_COMPONENT_TRANSFORM, &transform), ENGINE_OK);
        PoolMap(&params, (int64_t)set, (int64_t)ids[i], (int64_t)i, (int64_t)tree);
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
    for (f = 0; f < FRAMES; ++f)
    {
        CHECK_STATUS(g_api->frame(engine, &frame, NULL), ENGINE_OK);
    }

    /* the script's errors: the unknown set, the bad rotation and the unknown node all raised */
    CHECK(g_errors_seen == 0);
    /* 100 + the 100 the scripts added: the set (capacity 200) is full */
    CHECK_STATUS(g_api->instance_set_add_many(world, set, items, 1, sizeof(engine_instance), NULL),
                 ENGINE_ERROR_LIMIT_EXCEEDED);
    for (i = 0; i < COUNT; ++i)
    {
        memset(&extra, 0, sizeof(extra));
        CHECK_STATUS(g_api->instance_set_get_transform(world, set, ids[i], &extra, sizeof(extra)), ENGINE_OK);
        hash = Fnv(hash, extra.position, sizeof(extra.position));
        CHECK(extra.position[1] == 0.5);
    }
    CHECK_STATUS(g_api->instance_set_get_transform(world, set, ids[0], &extra, 8), ENGINE_ERROR_STRUCT_SIZE);
    CHECK_STATUS(g_api->instance_set_get_transform(world, 12345, ids[0], &extra, sizeof(extra)),
                 ENGINE_ERROR_NOT_FOUND);
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    return hash;
}

int main(void)
{
    static const char* const kPartitions[] = {"1", "8", "64"};
    uint64_t first = 0;
    size_t i;
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &g_api) != ENGINE_OK)
    {
        printf("host_script_instances: no function table\n");
        return 2;
    }
    for (i = 0; i < 3; ++i)
    {
        const uint64_t hash = RunScenario(kPartitions[i]);
        printf("host_script_instances: partitions %s: hash %016llx\n", kPartitions[i], (unsigned long long)hash);
        if (i == 0)
        {
            first = hash;
        }
        CHECK(hash == first);
    }
    if (g_failures == 0)
    {
        printf("host_script_instances: ok\n");
        return 0;
    }
    printf("host_script_instances: %d checks failed\n", g_failures);
    return 1;
}
