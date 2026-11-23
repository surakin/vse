/* glTF colliders Day 7: a character walks on an imported, spawned world through the C API. The file has a ground
   (`ground-col`, a 10 x 10 m quad), a wall, a step of exactly step_height, one 5 cm higher and a ramp steeper than
   min_ground_y (OMI_physics_* boxes). A character dropped above the ground lands and stands; walking into the wall
   stops it; the low step is climbed, the high one and the ramp stop it; walking off the ground's edge it falls. Two
   worlds built the same way give the same positions (a hash, printed for the other machines to compare). Plain C. */
#include <math.h>
#include <stdint.h>
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
            printf("host_character_world: line %d: CHECK failed: %s\n", __LINE__, #condition);                         \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

#define CHECK_STATUS(call, expected)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status got_ = (call);                                                                             \
        if (got_ != (expected))                                                                                        \
        {                                                                                                              \
            printf("host_character_world: line %d: %s returned %d, expected %d\n", __LINE__, #call, (int)got_,         \
                   (int)(expected));                                                                                   \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

#define STEP_HEIGHT 0.3f
#define RADIUS 0.3f
#define HEIGHT 1.8f
#define DT (1.0f / 60.0f)

/* The ground quad of tests/engine_library/host_import.c (positions at 0, indices at 96), the wall, the steps and the
   ramp as OMI boxes. The ramp is a box turned 60 degrees about Z, its low end on the ground near x = 2: the face the
   character walks onto has a normal with y = cos(60) = 0.5, below min_ground_y 0.64, so it is a wall. */
static const char kWorld[] =
    "{\"asset\":{\"version\":\"2.0\"},\"extensionsUsed\":[\"OMI_physics_body\",\"OMI_physics_shape\"],"
    "\"extensions\":{\"OMI_physics_shape\":{\"shapes\":["
    "{\"type\":\"box\",\"box\":{\"size\":[0.2,3,2]}},"
    "{\"type\":\"box\",\"box\":{\"size\":[2,0.3,2]}},"
    "{\"type\":\"box\",\"box\":{\"size\":[2,0.35,2]}},"
    "{\"type\":\"box\",\"box\":{\"size\":[4,0.2,2]}}]}},"
    "\"scene\":0,\"scenes\":[{\"nodes\":[0,1,2,3,4]}],"
    "\"nodes\":[{\"name\":\"ground-col\",\"mesh\":0},"
    "{\"name\":\"wall\",\"translation\":[4,1.5,0],\"extensions\":{\"OMI_physics_body\":{\"collider\":{\"shape\":0}}}},"
    "{\"name\":\"step\",\"translation\":[3,0.15,3],\"extensions\":{\"OMI_physics_body\":{\"collider\":{\"shape\":1}}}},"
    "{\"name\":\"high\",\"translation\":[3,0.175,-3],\"extensions\":{\"OMI_physics_body\":{\"collider\":{\"shape\":2}}}},"
    "{\"name\":\"ramp\",\"translation\":[3,1.732,4.8],\"rotation\":[0,0,0.5,0.8660254],"
    "\"extensions\":{\"OMI_physics_body\":{\"collider\":{\"shape\":3}}}}],"
    "\"meshes\":[{\"name\":\"ground\",\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
    "\"accessors\":["
    "{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\",\"min\":[-5,0,-5],\"max\":[5,0,5]},"
    "{\"bufferView\":1,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"}],"
    "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":48},{\"buffer\":0,\"byteOffset\":96,"
    "\"byteLength\":12}],"
    "\"buffers\":[{\"byteLength\":110,\"uri\":\"data:application/octet-stream;base64,"
    "AACgwAAAAAAAAKDAAACgQAAAAAAAAKDAAACgQAAAAAAAAKBAAACgwAAAAAAAAKBAAABAQAAAAAAAAIC/AABAQAAAAEAAAIC/AABAQAAAAEAAAIA/"
    "AABAQAAAAAAAAIA/AAACAAEAAAADAAIAAAA=\"}]}";

typedef struct Lane
{
    float x;
    float y;
    float z;
    int grounded;
} Lane;

static void Frame(engine_handle engine)
{
    engine_frame_info frame;
    memset(&frame, 0, sizeof(frame));
    frame.struct_size = sizeof(frame);
    frame.delta_seconds = DT;
    CHECK_STATUS(g_api->frame(engine, &frame, NULL), ENGINE_OK);
}

static uint64_t MakeCharacter(engine_handle world, float x, float y, float z)
{
    uint64_t key = 0;
    engine_transform t;
    engine_character c;
    CHECK_STATUS(g_api->entity_create(world, &key), ENGINE_OK);
    memset(&t, 0, sizeof(t));
    t.struct_size = sizeof(t);
    t.position[0] = x;
    t.position[1] = y;
    t.position[2] = z;
    t.rotation[3] = 1.0f;
    t.scale[0] = t.scale[1] = t.scale[2] = 1.0f;
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
    memset(&c, 0, sizeof(c));
    c.struct_size = sizeof(c);
    c.radius = RADIUS;
    c.height = HEIGHT;
    c.step_height = STEP_HEIGHT;
    c.min_ground_y = 0.64f;
    c.skin_width = 0.01f;
    c.gravity_scale = 1.0f;
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_CHARACTER, &c), ENGINE_OK);
    return key;
}

/* Walks a character at `velocity` (x, z) for `frames` frames; returns where it ends and whether it stands. */
static Lane Walk(engine_handle engine, engine_handle world, uint64_t key, float vx, float vz, int frames,
                 unsigned long long* hash)
{
    Lane lane;
    int f;
    engine_character_state state;
    engine_transform t;
    memset(&lane, 0, sizeof(lane));
    for (f = 0; f < frames; ++f)
    {
        const float velocity[3] = {vx, 0.0f, vz};
        memset(&state, 0, sizeof(state));
        state.struct_size = sizeof(state);
        CHECK_STATUS(g_api->character_move(world, key, velocity, DT, &state), ENGINE_OK);
        Frame(engine);
        memset(&t, 0, sizeof(t));
        t.struct_size = sizeof(t);
        CHECK_STATUS(g_api->component_get(world, key, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
        if (hash != NULL)
        {
            const unsigned char* bytes = (const unsigned char*)t.position;
            size_t i;
            for (i = 0; i < sizeof(t.position); ++i)
            {
                *hash = (*hash ^ bytes[i]) * 1099511628211ull;
            }
        }
        lane.x = t.position[0];
        lane.y = t.position[1];
        lane.z = t.position[2];
    }
    memset(&state, 0, sizeof(state));
    state.struct_size = sizeof(state);
    CHECK_STATUS(g_api->character_jump(world, key, 0.0f, &state), ENGINE_OK); /* reads the state */
    lane.grounded = (int)state.on_ground;
    return lane;
}

/* One world: import, spawn, the five checks. Returns the hash of every position the characters went through. */
static unsigned long long RunWorld(engine_handle engine, uint64_t seed, int report)
{
    engine_import_options options;
    engine_import_result result;
    engine_handle world;
    uint64_t level = 0;
    uint64_t root = 0;
    uint32_t i;
    unsigned long long hash = 1469598103934665603ull;
    Lane lane;
    float rest = 0.0f;
    char prefix[32];
    memset(&options, 0, sizeof(options));
    options.struct_size = sizeof(options);
    options.flags = ENGINE_IMPORT_EMIT_LEVEL;
    options.id_seed = seed;
    snprintf(prefix, sizeof(prefix), "/world%llu", (unsigned long long)seed);
    options.path_prefix.data = prefix;
    options.path_prefix.length = strlen(prefix);
    memset(&result, 0, sizeof(result));
    result.struct_size = sizeof(result);
    CHECK_STATUS(g_api->import_gltf(engine, kWorld, sizeof(kWorld) - 1, &options, &result), ENGINE_OK);
    for (i = 0; i < result.resource_count; ++i)
    {
        uint32_t type;
        memcpy(&type, (const char*)result.types.data + i * sizeof(type), sizeof(type));
        if (type == ENGINE_RESOURCE_LEVEL)
        {
            memcpy(&level, (const char*)result.ids.data + i * sizeof(level), sizeof(level));
        }
    }
    CHECK(level != 0);
    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    CHECK_STATUS(g_api->level_spawn(world, level, NULL, &root, NULL, 0, NULL), ENGINE_OK);
    /* the double precision series: a second spawn 1,000 km away puts its root at the exact place */
    {
        engine_spawn_params far_spawn;
        engine_transform root_at;
        engine_handle far_world;
        uint64_t far_root = 0;
        uint64_t unused = 0;
        memset(&far_world, 0, sizeof(far_world));
        CHECK_STATUS(g_api->world_create(engine, &far_world), ENGINE_OK);
        memset(&far_spawn, 0, sizeof(far_spawn));
        far_spawn.struct_size = sizeof(far_spawn);
        far_spawn.position[0] = 1.0e6 + 0.25;
        far_spawn.position[1] = 0.5;
        far_spawn.position[2] = -1.0e6 - 0.125;
        far_spawn.scale = 1.0f;
        CHECK_STATUS(g_api->level_spawn(far_world, level, &far_spawn, &far_root, NULL, 0, NULL), ENGINE_OK);
        memset(&root_at, 0, sizeof(root_at));
        root_at.struct_size = sizeof(root_at);
        CHECK_STATUS(g_api->component_get(far_world, far_root, ENGINE_COMPONENT_TRANSFORM, &root_at), ENGINE_OK);
        CHECK(root_at.position[0] == 1.0e6 + 0.25 && root_at.position[1] == 0.5 &&
              root_at.position[2] == -1.0e6 - 0.125);
        far_spawn.flags = 0x80u;
        CHECK_STATUS(g_api->level_spawn(far_world, level, &far_spawn, &unused, NULL, 0, NULL),
                     ENGINE_ERROR_INVALID_ARGUMENT);
        far_spawn.flags = 0;
        far_spawn.position[0] = 3.0e12;
        CHECK_STATUS(g_api->level_spawn(far_world, level, &far_spawn, &unused, NULL, 0, NULL),
                     ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->world_destroy(far_world), ENGINE_OK);
    }
    Frame(engine);
    Frame(engine); /* the colliders, then the bodies */

    /* dropped from 2 m above the ground, it lands and stands (its position is the capsule's centre) */
    {
        const uint64_t c = MakeCharacter(world, -2.0f, 3.0f, 0.0f);
        lane = Walk(engine, world, c, 0.0f, 0.0f, 120, &hash);
        CHECK(lane.grounded == 1);
        rest = lane.y;
        if (report)
        {
            printf("host_character_world: resting height %.4f\n", (double)rest);
        }
        CHECK(fabsf(rest - HEIGHT * 0.5f) < 0.03f);
        /* into the wall (its face at x = 3.9): it stops a radius before it */
        lane = Walk(engine, world, c, 2.0f, 0.0f, 180, &hash);
        CHECK(lane.x < 3.9f - RADIUS + 0.03f && lane.x > 3.9f - RADIUS - 0.1f);
        CHECK(lane.grounded == 1);
    }
    /* the step of exactly step_height (top at 0.3, from x = 2) is climbed */
    {
        const uint64_t c = MakeCharacter(world, 0.0f, rest + 0.05f, 3.0f);
        lane = Walk(engine, world, c, 2.0f, 0.0f, 120, &hash);
        CHECK(lane.x > 2.5f);
        CHECK(fabsf(lane.y - (rest + STEP_HEIGHT)) < 0.03f);
        CHECK(lane.grounded == 1);
    }
    /* one 5 cm higher stops it */
    {
        const uint64_t c = MakeCharacter(world, 0.0f, rest + 0.05f, -3.0f);
        lane = Walk(engine, world, c, 2.0f, 0.0f, 120, &hash);
        CHECK(lane.x < 2.0f - RADIUS + 0.05f);
        CHECK(fabsf(lane.y - rest) < 0.03f);
    }
    /* the ramp steeper than min_ground_y stops it (it slides down what it climbs) */
    {
        const uint64_t c = MakeCharacter(world, 0.0f, rest + 0.05f, 4.8f);
        lane = Walk(engine, world, c, 2.0f, 0.0f, 180, &hash);
        CHECK(lane.y < rest + STEP_HEIGHT + 0.05f);
        CHECK(lane.x < 2.5f);
    }
    /* off the ground's edge (x = -5): it no longer stands, and falls */
    {
        const uint64_t c = MakeCharacter(world, -3.0f, rest + 0.05f, -1.0f);
        lane = Walk(engine, world, c, -3.0f, 0.0f, 90, &hash);
        CHECK(lane.grounded == 0);
        CHECK(lane.y < rest - 0.5f);
    }
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
    engine_buffer_release(&result.ids);
    engine_buffer_release(&result.types);
    engine_buffer_release(&result.paths);
    engine_buffer_release(&result.warnings);
    engine_buffer_release(&result.manifest);
    engine_buffer_release(&result.summary);
    return hash;
}

int main(void)
{
    engine_create_info info;
    engine_handle engine;
    engine_info about;
    unsigned long long first;
    unsigned long long second;
    host_quiet_failures(); /* Windows: report failures on stderr, never in a dialog */
    setvbuf(stdout, NULL, _IONBF, 0);
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &g_api) != ENGINE_OK)
    {
        printf("host_character_world: engine_get_api failed\n");
        return 2;
    }
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.flags = ENGINE_CREATE_LOG_SUPPRESS_CONSOLE;
    memset(&engine, 0, sizeof(engine));
    CHECK_STATUS(g_api->create(&info, &engine), ENGINE_OK);
    memset(&about, 0, sizeof(about));
    about.struct_size = sizeof(about);
    CHECK_STATUS(g_api->get_info(engine, &about), ENGINE_OK);
    if ((about.caps & ENGINE_CAP_IMPORT_GLTF) == 0)
    {
        printf("host_character_world: skipped: the library has no importer\n");
        CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
        return 0;
    }
    first = RunWorld(engine, 7001, 1);
    second = RunWorld(engine, 7002, 0);
    printf("host_character_world: position hash %016llx\n", first);
    CHECK(first == second); /* two worlds built the same way move the same */
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    g_api->thread_detach();
    if (g_failures != 0)
    {
        printf("host_character_world: %d checks failed\n", g_failures);
        return 1;
    }
    printf("host_character_world: ok\n");
    return 0;
}
