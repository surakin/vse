/* C API script physics (2026-11-20): a Lua behavior in a C API world uses character.* on the world's one character set.
   Over an imported, spawned ground (a `-col` quad), the behavior makes a character on its own entity, jumps it at 5 m/s
   and teleports it with character.set_position, and reports what it sees through a host function. The program checks:
   it lands; is_grounded goes false then true; the apex is v^2 / (2 g); the script's view agrees with the C API's
   (character_jump with speed 0); the teleport puts it at the new place with its vertical speed reset; a character made
   on an entity spawned in the same tick is made at that tick's merge; the layer names are the engine's
   script.physics_layers setting. Then the C door alone: a held character_move
   walks the same distance in a second at 30, 60 and 120 frames per second. Plain C. */
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
            printf("host_script_character: line %d: CHECK failed: %s\n", __LINE__, #condition);                        \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

#define CHECK_STATUS(call, expected)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status got_ = (call);                                                                             \
        if (got_ != (expected))                                                                                        \
        {                                                                                                              \
            printf("host_script_character: line %d: %s returned %d, expected %d\n", __LINE__, #call, (int)got_,        \
                   (int)(expected));                                                                                   \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

#define TICKS 260
#define JUMP_TICK 90
#define JUMP2_TICK 200
#define TELEPORT_TICK 205

/* The ground of tests/engine_library/host_import.c: a 10 x 10 m quad at y = 0, named as a collider. */
static const char kGround[] =
    "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
    "\"nodes\":[{\"name\":\"ground-col\",\"mesh\":0}],"
    "\"meshes\":[{\"name\":\"ground\",\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
    "\"accessors\":["
    "{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\",\"min\":[-5,0,-5],\"max\":[5,0,5]},"
    "{\"bufferView\":1,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"}],"
    "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":48},{\"buffer\":0,\"byteOffset\":96,"
    "\"byteLength\":12}],"
    "\"buffers\":[{\"byteLength\":110,\"uri\":\"data:application/octet-stream;base64,"
    "AACgwAAAAAAAAKDAAACgQAAAAAAAAKDAAACgQAAAAAAAAKBAAACgwAAAAAAAAKBAAABAQAAAAAAAAIC/AABAQAAAAEAAAIC/AABAQAAAAEAAAIA/"
    "AABAQAAAAAAAAIA/AAACAAEAAAADAAIAAAA=\"}]}";

/* The spawned body is placed away from the player: characters collide with each other, so a body left at the
   origin would hold the player up on top of its capsule. */
static const char kScript
    [] = "behavior('player', {\n"
         "  on_create = function(self)\n"
         "    self.me = entity.self()\n"
         "    character.create(self.me, {radius = 0.25, height = 1.2, step_height = 0.3, min_ground_y = 0.64,\n"
         "                               skin_width = 0.01, gravity_scale = 1})\n"
         "    self.t = 0\n"
         "    self.body = entity.spawn(nil)\n"
         "    transform.set_position(self.body, vec3.new(-3, 1, 3))\n"
         "    self.same_tick = pcall(character.create, self.body, nil)\n"
         "  end,\n"
         "  on_update = function(self, dt)\n"
         "    self.t = self.t + 1\n"
         "    if not character.exists(self.me) then return end -- made at this tick's merge: from the next tick on\n"
         "    host.report(self.t, character.is_grounded(self.me) and 1 or 0, character.vertical_speed(self.me),\n"
         "                self.same_tick and 1 or 0, physics.layer('player'))\n"
         "    if self.t == 90 or self.t == 200 then character.jump(self.me, 5) end\n"
         "    if self.t == 205 then character.set_position(self.me, vec3.new(2, 3, 2)) end\n"
         "  end,\n"
         "})\n";

typedef struct Report
{
    int seen;
    int grounded;
    double vertical;
    int same_tick;
    int player_layer;
} Report;

static Report g_reports[TICKS + 2];

static engine_status HostReport(void* user, const engine_value_pool* args, engine_value_pool* results)
{
    const int64_t t = args->items[0].tag == ENGINE_VALUE_INT ? args->items[0].i : (int64_t)args->items[0].f;
    (void)user;
    if (t >= 0 && t <= TICKS)
    {
        Report* r = &g_reports[t];
        r->seen = 1;
        r->grounded = (int)(args->items[1].tag == ENGINE_VALUE_INT ? args->items[1].i : (int64_t)args->items[1].f);
        r->vertical = args->items[2].tag == ENGINE_VALUE_NUMBER ? args->items[2].f : (double)args->items[2].i;
        r->same_tick = (int)(args->items[3].tag == ENGINE_VALUE_INT ? args->items[3].i : (int64_t)args->items[3].f);
        r->player_layer = (int)(args->items[4].tag == ENGINE_VALUE_INT ? args->items[4].i : (int64_t)args->items[4].f);
    }
    results->root_count = 0;
    results->item_count = 0;
    results->byte_count = 0;
    return ENGINE_OK;
}

static engine_string Str(const char* text)
{
    engine_string s;
    s.data = text;
    s.length = strlen(text);
    return s;
}

static void Frame(engine_handle engine, double dt)
{
    engine_frame_info frame;
    memset(&frame, 0, sizeof(frame));
    frame.struct_size = sizeof(frame);
    frame.delta_seconds = dt;
    CHECK_STATUS(g_api->frame(engine, &frame, NULL), ENGINE_OK);
}

/* A world with the ground spawned and made solid. */
static engine_handle GroundWorld(engine_handle engine, uint64_t level)
{
    engine_handle world;
    uint64_t root = 0;
    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    CHECK_STATUS(g_api->level_spawn(world, level, NULL, &root, NULL, 0, NULL), ENGINE_OK);
    return world;
}

static uint64_t ImportGround(engine_handle engine)
{
    engine_import_options options;
    engine_import_result result;
    uint64_t level = 0;
    uint32_t i;
    memset(&options, 0, sizeof(options));
    options.struct_size = sizeof(options);
    options.flags = ENGINE_IMPORT_EMIT_LEVEL;
    options.id_seed = 5151;
    memset(&result, 0, sizeof(result));
    result.struct_size = sizeof(result);
    CHECK_STATUS(g_api->import_gltf(engine, kGround, sizeof(kGround) - 1, &options, &result), ENGINE_OK);
    for (i = 0; i < result.resource_count; ++i)
    {
        uint32_t type;
        memcpy(&type, (const char*)result.types.data + i * sizeof(type), sizeof(type));
        if (type == ENGINE_RESOURCE_LEVEL)
        {
            memcpy(&level, (const char*)result.ids.data + i * sizeof(level), sizeof(level));
        }
    }
    engine_buffer_release(&result.ids);
    engine_buffer_release(&result.types);
    engine_buffer_release(&result.paths);
    engine_buffer_release(&result.warnings);
    engine_buffer_release(&result.manifest);
    engine_buffer_release(&result.summary);
    return level;
}

static float PositionOf(engine_handle world, uint64_t key, int axis)
{
    engine_transform t;
    memset(&t, 0, sizeof(t));
    t.struct_size = sizeof(t);
    CHECK_STATUS(g_api->component_get(world, key, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
    return (float)t.position[axis];
}

static uint64_t PlaceEntity(engine_handle world, float x, float y, float z)
{
    uint64_t key = 0;
    engine_transform t;
    CHECK_STATUS(g_api->entity_create(world, &key), ENGINE_OK);
    memset(&t, 0, sizeof(t));
    t.struct_size = sizeof(t);
    t.position[0] = x;
    t.position[1] = y;
    t.position[2] = z;
    t.rotation[3] = 1.0f;
    t.scale[0] = t.scale[1] = t.scale[2] = 1.0f;
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
    return key;
}

/* The scripted player. */
static void TestScriptedCharacter(engine_handle engine, uint64_t level)
{
    engine_handle world = GroundWorld(engine, level);
    engine_script_load load;
    engine_script_component component;
    engine_character_state c_state[TICKS + 2];
    uint64_t script = 0;
    uint64_t player;
    float rest = 0.0f;
    float apex = -1000.0f;
    int t;
    int air_seen = 0;
    int landed_again = 0;
    memset(g_reports, 0, sizeof(g_reports));
    memset(c_state, 0, sizeof(c_state));
    CHECK_STATUS(g_api->script_register_function(world, Str("host.report"), 5, 0, &HostReport, NULL), ENGINE_OK);
    memset(&load, 0, sizeof(load));
    load.struct_size = sizeof(load);
    load.name = Str("player");
    load.source = Str(kScript);
    CHECK_STATUS(g_api->script_load(world, &load, &script), ENGINE_OK);
    Frame(engine, 1.0 / 60.0);
    Frame(engine, 1.0 / 60.0); /* the ground's colliders, then its bodies */
    player = PlaceEntity(world, 0.0f, 1.5f, 0.0f);
    memset(&component, 0, sizeof(component));
    component.struct_size = sizeof(component);
    component.enabled = 1;
    strcpy(component.behavior, "player");
    CHECK_STATUS(g_api->component_set(world, player, ENGINE_COMPONENT_SCRIPT, &component), ENGINE_OK);
    for (t = 1; t <= TICKS; ++t)
    {
        engine_character_state* s = &c_state[t];
        Frame(engine, 1.0 / 60.0);
        s->struct_size = sizeof(*s);
        if (g_api->character_jump(world, player, 0.0f, s) != ENGINE_OK)
        {
            memset(s, 0, sizeof(*s));
        }
        if (t == JUMP_TICK - 1)
        {
            rest = PositionOf(world, player, 1);
        }
        if (t >= JUMP_TICK && t < JUMP2_TICK)
        {
            const float y = PositionOf(world, player, 1);
            apex = y > apex ? y : apex;
        }
    }
    /* the behavior ran and reported every tick after its first */
    CHECK(g_reports[10].seen && g_reports[TICKS - 1].seen);
    /* a character on an entity spawned in the same tick is made (the merge creates the entity first): two entities of
       the world have a character, the player and its spawned body */
    CHECK(g_reports[10].same_tick == 1);
    CHECK(g_reports[10].player_layer == 2); /* "default,world,player" */
    {
        uint64_t keys[64];
        size_t total = 0;
        size_t k;
        int with_character = 0;
        size_t with_component = 0;
        CHECK_STATUS(g_api->query(world, 0, keys, 64, &total), ENGINE_OK);
        for (k = 0; k < total && k < 64; ++k)
        {
            engine_character_state probe;
            engine_character config;
            int has = 0;
            memset(&probe, 0, sizeof(probe));
            probe.struct_size = sizeof(probe);
            with_character += g_api->character_jump(world, keys[k], 0.0f, &probe) == ENGINE_OK ? 1 : 0;
            /* a script-made character is also seen as the component (has, get and queries agree) */
            CHECK_STATUS(g_api->component_has(world, keys[k], ENGINE_COMPONENT_CHARACTER, &has), ENGINE_OK);
            memset(&config, 0, sizeof(config));
            config.struct_size = sizeof(config);
            CHECK((g_api->component_get(world, keys[k], ENGINE_COMPONENT_CHARACTER, &config) == ENGINE_OK) ==
                  (has != 0));
        }
        CHECK(with_character == 2);
        CHECK_STATUS(g_api->query(world, ENGINE_COMPONENT_BIT(ENGINE_COMPONENT_CHARACTER), keys, 64, &with_component),
                     ENGINE_OK);
        CHECK(with_component == 2);
    }
    /* it landed before the jump, and the script and the C API agree (the script sees the state its tick started with:
       the C state read after the previous frame) */
    CHECK(g_reports[JUMP_TICK - 1].grounded == 1);
    /* The rise: v^2 / (2 g) = 1.2742 m in continuous time; the fixed 60 Hz step integrates gravity before it moves
       (semi-implicit Euler), so the exact discrete apex is dt * (n v - g dt n (n + 1) / 2) with n = floor(v / (g dt)):
       1.2329 m for 5 m/s. */
    {
        const double v = 5.0;
        const double g = 9.81;
        const double dt = 1.0 / 60.0;
        const double n = floor(v / (g * dt));
        const double discrete = dt * (n * v - g * dt * n * (n + 1.0) / 2.0);
        printf("host_script_character: resting height %.4f, rise %.4f (discrete %.4f, continuous %.4f)\n", (double)rest,
               (double)(apex - rest), discrete, v * v / (2.0 * g));
        CHECK(fabs((apex - rest) - discrete) < 0.005);
    }
    for (t = 2; t < TICKS; ++t)
    {
        if (g_reports[t].seen)
        {
            CHECK(g_reports[t].grounded == (int)c_state[t - 1].on_ground);
            CHECK(fabs(g_reports[t].vertical - (double)c_state[t - 1].vertical_speed) < 1e-4);
        }
        if (t > JUMP_TICK && t < JUMP2_TICK && g_reports[t].seen)
        {
            air_seen = air_seen || g_reports[t].grounded == 0;
            landed_again = landed_again || (air_seen && g_reports[t].grounded == 1);
        }
    }
    CHECK(air_seen);     /* is_grounded went false ... */
    CHECK(landed_again); /* ... then true */
    /* the teleport at tick 205 (mid-jump, rising at about 4 m/s): at the new place, its vertical speed reset */
    CHECK(g_reports[TELEPORT_TICK].vertical > 3.0);
    CHECK(fabs(g_reports[TELEPORT_TICK + 1].vertical) < 0.5);
    CHECK(fabsf(PositionOf(world, player, 0) - 2.0f) < 0.05f);
    CHECK(fabsf(PositionOf(world, player, 2) - 2.0f) < 0.05f);
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
}

/* The C door: a held character_move walks the same distance whatever the frame rate. */
static float WalkOneSecond(engine_handle engine, uint64_t level, int frames_per_second)
{
    engine_handle world = GroundWorld(engine, level);
    engine_character c;
    const float wish[3] = {1.5f, 0.0f, 0.0f};
    uint64_t key;
    float start;
    float end;
    int f;
    Frame(engine, 1.0 / 60.0);
    Frame(engine, 1.0 / 60.0);
    key = PlaceEntity(world, -3.0f, 0.62f, 0.0f);
    memset(&c, 0, sizeof(c));
    c.struct_size = sizeof(c);
    c.radius = 0.25f;
    c.height = 1.2f;
    c.step_height = 0.3f;
    c.min_ground_y = 0.64f;
    c.skin_width = 0.01f;
    c.gravity_scale = 1.0f;
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_CHARACTER, &c), ENGINE_OK);
    for (f = 0; f < 30; ++f)
    {
        Frame(engine, 1.0 / 60.0); /* it settles on the ground */
    }
    start = PositionOf(world, key, 0);
    CHECK_STATUS(g_api->character_move(world, key, wish, 1.0f / 60.0f, NULL), ENGINE_OK); /* held from now on */
    for (f = 0; f < frames_per_second; ++f)
    {
        Frame(engine, 1.0 / (double)frames_per_second);
    }
    end = PositionOf(world, key, 0);
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
    return end - start;
}

int main(void)
{
    engine_create_info info;
    engine_handle engine;
    engine_info about;
    engine_setting_entry layers;
    uint64_t level;
    host_quiet_failures(); /* Windows: report failures on stderr, never in a dialog */
    setvbuf(stdout, NULL, _IONBF, 0);
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &g_api) != ENGINE_OK)
    {
        printf("host_script_character: engine_get_api failed\n");
        return 2;
    }
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.flags = ENGINE_CREATE_LOG_SUPPRESS_CONSOLE;
    layers.key = Str("script.physics_layers"); /* the scripts' layer names come from the setting */
    layers.value = Str("default,world,player");
    info.settings = &layers;
    info.setting_count = 1;
    memset(&engine, 0, sizeof(engine));
    CHECK_STATUS(g_api->create(&info, &engine), ENGINE_OK);
    memset(&about, 0, sizeof(about));
    about.struct_size = sizeof(about);
    CHECK_STATUS(g_api->get_info(engine, &about), ENGINE_OK);
    if ((about.caps & ENGINE_CAP_IMPORT_GLTF) == 0 || (about.caps & ENGINE_CAP_SCRIPT) == 0)
    {
        printf("host_script_character: skipped: the library has no importer or no scripts\n");
        CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
        return 0;
    }
    level = ImportGround(engine);
    CHECK(level != 0);
    TestScriptedCharacter(engine, level);
    {
        const float d30 = WalkOneSecond(engine, level, 30);
        const float d60 = WalkOneSecond(engine, level, 60);
        const float d120 = WalkOneSecond(engine, level, 120);
        printf("host_script_character: walked %.4f m at 30 fps, %.4f at 60, %.4f at 120\n", (double)d30, (double)d60,
               (double)d120);
        CHECK(fabsf(d60 - 1.5f) < 0.05f);
        CHECK(fabsf(d30 - d60) < 0.01f);
        CHECK(fabsf(d120 - d60) < 0.01f);
    }
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    g_api->thread_detach();
    if (g_failures != 0)
    {
        printf("host_script_character: %d checks failed\n", g_failures);
        return 1;
    }
    printf("host_script_character: ok\n");
    return 0;
}
