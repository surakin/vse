/* Scripts set the sky and the lights of their world (maintenance, 2026-11-22): a Lua behavior in a C API world uses
   environment.set, environment.get, light.create and light.set. The program checks: a light made by the script is the
   one component_get reads back; a subset call keeps the other fields (of the environment and of a light); the change
   is applied at the sync point (the script reads the old value in its own tick, the new one in the next); unknown
   fields, wrong types, negative intensities, a spot light with its cones the wrong way round and a light call on an
   entity with no light are errors naming the field; a light made on an entity spawned in the same tick exists from the
   next tick; a host's own world_set_environment between frames replaces the script's values, and a script's later
   call wins over it. Then, when this build has off-screen surfaces and a device can be made, the picture of an empty
   world follows the sky colour the script set. Plain C. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_os.h"
#include "engine/engine_c.h"
#include "host_surface_api.h"

static int g_failures = 0;
static const engine_api* g_api = NULL;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("host_script_scene: line %d: CHECK failed: %s\n", __LINE__, #condition);                            \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

#define CHECK_STATUS(call, expected)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status got_ = (call);                                                                             \
        if (got_ != (expected))                                                                                        \
        {                                                                                                              \
            printf("host_script_scene: line %d: %s returned %d, expected %d\n", __LINE__, #call, (int)got_,            \
                   (int)(expected));                                                                                   \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

#define MAX_TICKS 16
#define MAX_VALUES 12

/* What the script reported at each tick: a row of numbers per tick. */
typedef struct Row
{
    int seen;
    double v[MAX_VALUES];
} Row;

static Row g_rows[MAX_TICKS + 1];

static double NumberOf(const engine_value* value)
{
    if (value->tag == ENGINE_VALUE_INT)
    {
        return (double)value->i;
    }
    if (value->tag == ENGINE_VALUE_NUMBER)
    {
        return value->f;
    }
    return -1000.0;
}

/* host.report(tick, v1, ..., v9): ten arguments, the script pads. */
static engine_status HostReport(void* user, const engine_value_pool* args, engine_value_pool* results)
{
    const int64_t t = (int64_t)NumberOf(&args->items[0]);
    uint32_t i;
    (void)user;
    if (t >= 0 && t <= MAX_TICKS)
    {
        g_rows[t].seen = 1;
        for (i = 1; i < args->root_count && i <= MAX_VALUES; ++i)
        {
            g_rows[t].v[i - 1] = NumberOf(&args->items[i]);
        }
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

static void Frame(engine_handle engine)
{
    engine_frame_info frame;
    memset(&frame, 0, sizeof(frame));
    frame.struct_size = sizeof(frame);
    frame.delta_seconds = 1.0 / 60.0;
    CHECK_STATUS(g_api->frame(engine, &frame, NULL), ENGINE_OK);
}

static int Near(double a, double b)
{
    return fabs(a - b) < 1e-4;
}

/* The tick's script. Tick 1 makes the light and changes the sky; tick 2 reads what tick 1 asked for; tick 3 changes a
   subset; tick 4 reads it; ticks 5 and 6 are the errors; tick 7 and later are for the host's own call. The numbers
   reported (columns) are listed where the program reads them. */
static const char kScript[] =
    "local function fails(f, ...)\n"
    "  local ok, message = pcall(f, ...)\n"
    "  return (not ok) and tostring(message) or ''\n"
    "end\n"
    "local function report(t, a, b, c, d, e, f, g, h, i)\n"
    "  host.report(t, a or 0, b or 0, c or 0, d or 0, e or 0, f or 0, g or 0, h or 0, i or 0)\n"
    "end\n"
    "local function has(message, text) return string.find(message, text, 1, true) ~= nil and 1 or 0 end\n"
    "behavior('sky', {\n"
    "  on_create = function(self)\n"
    "    self.me = entity.self()\n"
    "    self.t = 0\n"
    "  end,\n"
    "  on_update = function(self, dt)\n"
    "    self.t = self.t + 1\n"
    "    local t = self.t\n"
    "    local env = environment.get()\n"
    "    if t == 1 then\n"
    "      -- the defaults, then a light and a sky: nothing of it is visible in this tick\n"
    "      report(1, env.ambient_color.x, env.ambient_intensity, env.sky_color.z, env.exposure, env.reflection_intensity)\n"
    "      light.create(self.me, 'directional', {color = vec3.new(1, 0.5, 0.25), intensity = 2})\n"
    "      environment.set({sky_color = vec3.new(0.1, 0.2, 0.3), exposure = 2})\n"
    "      self.child = entity.spawn(nil)\n"
    "      light.create(self.child, 'point', {radius = 5, color = vec3.new(0, 1, 0), intensity = 3})\n"
    "    elseif t == 2 then\n"
    "      -- the sky is set (the sync point of tick 1) and the other fields kept\n"
    "      report(2, env.sky_color.x, env.sky_color.y, env.sky_color.z, env.exposure, env.ambient_color.x,\n"
    "                  env.ambient_intensity)\n"
    "      light.set(self.me, {intensity = 0.5})\n"
    "    elseif t == 3 then\n"
    "      environment.set({ambient_intensity = 0.7, reflection_intensity = 0.25, sky_mode = 2,\n"
    "                       sky_zenith = vec3.new(0.4, 0.5, 0.6), sun_size = 0.1, sky_exposure = 1.5,\n"
    "                       sky_ambient_intensity = 0.5})\n"
    "    elseif t == 4 then\n"
    "      report(4, env.sky_color.y, env.exposure, env.ambient_intensity, env.ambient_color.x, env.reflection_intensity)\n"
    "      report(10, env.sky_mode, env.sky_zenith.x, env.sky_zenith.z, env.sun_size, env.sky_exposure,\n"
    "                 env.sky_ambient_intensity, env.sky_horizon.y, env.sky_horizon_blend)\n"
    "    elseif t == 5 then\n"
    "      -- the errors\n"
    "      local a = fails(environment.set, {bogus = 1})\n"
    "      local b = fails(environment.set, {exposure = -1})\n"
    "      local c = fails(environment.set, {sky_color = 1})\n"
    "      report(11, has(fails(environment.set, {sky_mode = 3}), 'sky_mode'),\n"
    "                 has(fails(environment.set, {sun_size = 0}), 'sun_size'),\n"
    "                 has(fails(environment.set, {sun_direction = vec3.new(0, 0, 0)}), 'sun_direction'))\n"
    "      local d = fails(light.set, self.me, {intensity = -1})\n"
    "      local e = fails(light.set, self.me, {color = vec3.new(1, -1, 1)})\n"
    "      local f = fails(light.set, self.me, {nope = 1})\n"
    "      local g = fails(light.create, self.me, 'point', {})\n"
    "      local h = fails(light.create, entity.spawn(nil), 'cone', {})\n"
    "      local i = fails(light.set, entity.find('nothing') or self.me, {radius = 'wide'})\n"
    "      report(5, has(a, 'bogus'), has(b, 'exposure'), has(c, 'sky_color'), has(d, 'intensity'), has(e, 'color'),\n"
    "                  has(f, 'nope'), has(g, 'already has a light'), has(h, 'kind'), has(i, 'radius'))\n"
    "      -- a spot light made the wrong way round is refused, naming the cones\n"
    "      local spot = entity.spawn(nil)\n"
    "      self.bad_spot = has(fails(light.create, spot, 'spot', {inner_cone = 1.0, outer_cone = 0.5}), 'inner_cone')\n"
    "    elseif t == 6 then\n"
    "      report(6, self.bad_spot)\n"
    "    elseif t == 8 then\n"
    "      -- the host replaced the whole environment between frames 7 and 8\n"
    "      report(8, env.ambient_color.x, env.ambient_intensity, env.sky_color.x, env.exposure)\n"
    "      environment.set({exposure = 3})\n"
    "    elseif t == 9 then\n"
    "      report(9, env.exposure, env.sky_color.x, env.ambient_intensity)\n"
    "    end\n"
    "  end,\n"
    "})\n";

static void SetEnvironment(engine_handle world, float ar, float ag, float ab, float ai, float sr, float sg, float sb,
                           float exposure)
{
    engine_environment environment;
    memset(&environment, 0, sizeof(environment));
    environment.struct_size = sizeof(environment);
    environment.ambient_color[0] = ar;
    environment.ambient_color[1] = ag;
    environment.ambient_color[2] = ab;
    environment.ambient_intensity = ai;
    environment.sky_color[0] = sr;
    environment.sky_color[1] = sg;
    environment.sky_color[2] = sb;
    environment.exposure = exposure;
    CHECK_STATUS(g_api->world_set_environment(world, &environment), ENGINE_OK);
}

static void ReadLight(engine_handle world, uint64_t key, engine_light* light, engine_status expected)
{
    memset(light, 0, sizeof(*light));
    light->struct_size = sizeof(*light);
    CHECK_STATUS(g_api->component_get(world, key, ENGINE_COMPONENT_LIGHT, light), expected);
}

static uint64_t PlaceEntity(engine_handle world)
{
    uint64_t key = 0;
    engine_transform t;
    CHECK_STATUS(g_api->entity_create(world, &key), ENGINE_OK);
    memset(&t, 0, sizeof(t));
    t.struct_size = sizeof(t);
    t.position[1] = 1.0f;
    t.rotation[3] = 1.0f;
    t.scale[0] = t.scale[1] = t.scale[2] = 1.0f;
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
    return key;
}

/* The scripted world: the state checks. Returns the world (the picture part uses another one). */
static void TestState(engine_handle engine)
{
    engine_handle world;
    engine_script_load load;
    engine_script_component component;
    engine_light light;
    uint64_t script = 0;
    uint64_t me;
    uint64_t keys[16];
    size_t total = 0;
    size_t lights_found = 0;
    size_t k;
    int t;
    memset(g_rows, 0, sizeof(g_rows));
    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    CHECK_STATUS(g_api->script_register_function(world, Str("host.report"), 10, 0, &HostReport, NULL), ENGINE_OK);
    memset(&load, 0, sizeof(load));
    load.struct_size = sizeof(load);
    load.name = Str("sky");
    load.source = Str(kScript);
    CHECK_STATUS(g_api->script_load(world, &load, &script), ENGINE_OK);
    me = PlaceEntity(world);
    memset(&component, 0, sizeof(component));
    component.struct_size = sizeof(component);
    component.enabled = 1;
    strcpy(component.behavior, "sky");
    CHECK_STATUS(g_api->component_set(world, me, ENGINE_COMPONENT_SCRIPT, &component), ENGINE_OK);
    ReadLight(world, me, &light, ENGINE_ERROR_NOT_FOUND); /* no light yet */

    for (t = 1; t <= 7; ++t)
    {
        Frame(engine);
        if (t == 1)
        {
            /* the script made a directional light on its own entity at the sync point */
            ReadLight(world, me, &light, ENGINE_OK);
            CHECK(light.kind == ENGINE_LIGHT_DIRECTIONAL);
            CHECK(Near(light.color[0], 1.0) && Near(light.color[1], 0.5) && Near(light.color[2], 0.25));
            CHECK(Near(light.intensity, 2.0));
        }
        if (t == 2)
        {
            ReadLight(world, me, &light, ENGINE_OK); /* light.set kept the colour and changed the intensity */
            CHECK(light.kind == ENGINE_LIGHT_DIRECTIONAL);
            CHECK(Near(light.color[0], 1.0) && Near(light.color[1], 0.5) && Near(light.color[2], 0.25));
            CHECK(Near(light.intensity, 0.5));
        }
    }
    /* tick 1: the defaults (white ambient at 0.2, a black sky, exposure 1) */
    CHECK(g_rows[1].seen && Near(g_rows[1].v[0], 1.0) && Near(g_rows[1].v[1], 0.2) && Near(g_rows[1].v[2], 0.0) &&
          Near(g_rows[1].v[3], 1.0) && Near(g_rows[1].v[4], 0.0));
    /* tick 2: the sky is set, the ambient light kept; the child's light.set before its light existed was an error */
    CHECK(g_rows[2].seen && Near(g_rows[2].v[0], 0.1) && Near(g_rows[2].v[1], 0.2) && Near(g_rows[2].v[2], 0.3) &&
          Near(g_rows[2].v[3], 2.0) && Near(g_rows[2].v[4], 1.0) && Near(g_rows[2].v[5], 0.2));
    /* tick 4: a one-field call kept the rest */
    CHECK(g_rows[4].seen && Near(g_rows[4].v[0], 0.2) && Near(g_rows[4].v[1], 2.0) && Near(g_rows[4].v[2], 0.7) &&
          Near(g_rows[4].v[3], 1.0) && Near(g_rows[4].v[4], 0.25));
    /* tick 5: every error named its field (or its reason) */
    CHECK(g_rows[5].seen);
    for (k = 0; k < 9; ++k)
    {
        if (!Near(g_rows[5].v[k], 1.0))
        {
            printf("host_script_scene: error %zu of tick 5 did not name its field\n", k);
        }
        CHECK(Near(g_rows[5].v[k], 1.0));
    }
    /* the sky fields: set at tick 3, read at tick 4; the defaults of the others */
    CHECK(g_rows[10].seen && Near(g_rows[10].v[0], 2.0) && Near(g_rows[10].v[1], 0.4) && Near(g_rows[10].v[2], 0.6) &&
          Near(g_rows[10].v[3], 0.1) && Near(g_rows[10].v[4], 1.5) && Near(g_rows[10].v[5], 0.5) &&
          Near(g_rows[10].v[6], 0.75) && Near(g_rows[10].v[7], 0.1));
    CHECK(g_rows[11].seen && Near(g_rows[11].v[0], 1.0) && Near(g_rows[11].v[1], 1.0) && Near(g_rows[11].v[2], 1.0));
    CHECK(g_rows[6].seen && Near(g_rows[6].v[0], 1.0));

    /* the child's light (made in tick 1 on an entity spawned in that tick) is the one tick 2 set */
    CHECK_STATUS(g_api->query(world, 0, keys, 16, &total), ENGINE_OK);
    for (k = 0; k < total && k < 16; ++k)
    {
        engine_light other;
        if (keys[k] == me)
        {
            continue;
        }
        memset(&other, 0, sizeof(other));
        other.struct_size = sizeof(other);
        if (g_api->component_get(world, keys[k], ENGINE_COMPONENT_LIGHT, &other) == ENGINE_OK)
        {
            ++lights_found;
            CHECK(other.kind == ENGINE_LIGHT_POINT);
            CHECK(Near(other.radius, 5.0) && Near(other.intensity, 3.0));
            CHECK(Near(other.color[0], 0.0) && Near(other.color[1], 1.0) && Near(other.color[2], 0.0));
        }
    }
    CHECK(lights_found == 1); /* the failed creations (a cone, a spot the wrong way round) made no light */

    /* the host replaces the whole environment between frames; the script sees it in its next tick, and its own
       later call keeps the other fields of the host's */
    SetEnvironment(world, 0.4f, 0.5f, 0.6f, 0.9f, 0.8f, 0.0f, 0.0f, 1.5f);
    Frame(engine); /* tick 8: the script reads the host's values, then asks for exposure 3 */
    Frame(engine); /* tick 9: it reads exposure 3 over the host's other fields */
    CHECK(g_rows[8].seen && Near(g_rows[8].v[0], 0.4) && Near(g_rows[8].v[1], 0.9) && Near(g_rows[8].v[2], 0.8) &&
          Near(g_rows[8].v[3], 1.5));
    CHECK(g_rows[9].seen && Near(g_rows[9].v[0], 3.0) && Near(g_rows[9].v[1], 0.8) && Near(g_rows[9].v[2], 0.9));
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
}

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

/* entity.find and entity.name in a C API world: a node of an imported level and an entity the host named. A script
   finds each by name, moves it, and reads its name; the host reads the moved position back. */
static const char kFindScript[] =
    "behavior('finder', {\n"
    "  on_update = function(self, dt)\n"
    "    local ground = entity.find('ground-col')\n"
    "    local probe = entity.find('Probe')\n"
    "    local nothing = entity.find('no-such-entity')\n"
    "    if ground ~= nil and probe ~= nil and nothing == nil then\n"
    "      transform.set_position(probe, vec3.new(1, 2, 3))\n"
    "      transform.set_position(ground, vec3.new(0, 7, 0))\n"
    "      host.report(1, entity.name(ground) == 'ground-col' and 1 or 0, entity.name(probe) == 'Probe' and 1 or 0)\n"
    "    end\n"
    "  end,\n"
    "})\n";

static void TestFind(engine_handle engine)
{
    engine_handle world;
    engine_script_load load;
    engine_script_component component;
    engine_transform t;
    engine_spawn_params spawn;
    uint64_t level = ImportGround(engine);
    uint64_t root = 0;
    uint64_t script = 0;
    uint64_t probe = 0;
    uint64_t holder = 0;
    uint64_t ground = 0;
    memset(g_rows, 0, sizeof(g_rows));
    CHECK(level != 0);
    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    memset(&spawn, 0, sizeof(spawn));
    spawn.struct_size = sizeof(spawn);
    spawn.rotation[3] = 1.0f;
    spawn.scale = 1.0f;
    CHECK_STATUS(g_api->level_spawn(world, level, &spawn, &root, NULL, 0, NULL), ENGINE_OK);
    CHECK_STATUS(g_api->entity_find(world, Str("ground-col"), &ground), ENGINE_OK);
    probe = PlaceEntity(world);
    CHECK_STATUS(g_api->entity_name_set(world, probe, Str("Probe")), ENGINE_OK);
    CHECK_STATUS(g_api->script_register_function(world, Str("host.report"), 3, 0, &HostReport, NULL), ENGINE_OK);
    memset(&load, 0, sizeof(load));
    load.struct_size = sizeof(load);
    load.name = Str("finder");
    load.source = Str(kFindScript);
    CHECK_STATUS(g_api->script_load(world, &load, &script), ENGINE_OK);
    holder = PlaceEntity(world);
    memset(&component, 0, sizeof(component));
    component.struct_size = sizeof(component);
    component.enabled = 1;
    strcpy(component.behavior, "finder");
    CHECK_STATUS(g_api->component_set(world, holder, ENGINE_COMPONENT_SCRIPT, &component), ENGINE_OK);
    Frame(engine);
    Frame(engine);
    CHECK(g_rows[1].seen && Near(g_rows[1].v[0], 1.0) && Near(g_rows[1].v[1], 1.0)); /* found, and entity.name agrees */
    memset(&t, 0, sizeof(t));
    t.struct_size = sizeof(t);
    CHECK_STATUS(g_api->component_get(world, probe, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
    CHECK(Near(t.position[0], 1.0) && Near(t.position[1], 2.0) && Near(t.position[2], 3.0));
    CHECK_STATUS(g_api->component_get(world, ground, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
    CHECK(Near(t.position[1], 7.0));
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
}

/* An empty world has the sky as its picture: a script that sets sky_color changes it. */
static void TestPicture(engine_handle engine)
{
    engine_surface_info info;
    engine_handle surface;
    engine_handle world;
    engine_handle ticket;
    engine_pixels pixels;
    engine_script_load load;
    engine_script_component component;
    uint64_t script = 0;
    uint64_t holder;
    engine_info about;
    const unsigned char* centre;
    unsigned char before[4];
    static const char kSky[] = "behavior('dawn', {\n"
                               "  on_update = function(self, dt)\n"
                               "    environment.set({sky_color = vec3.new(0.5, 0.0, 0.0), exposure = 1})\n"
                               "  end,\n"
                               "})\n";

    memset(&about, 0, sizeof(about));
    about.struct_size = sizeof(about);
    CHECK_STATUS(g_api->get_info(engine, &about), ENGINE_OK);
    if ((about.caps & ENGINE_CAP_SURFACE_OFFSCREEN) == 0)
    {
        printf("host_script_scene: the picture part is skipped: this build has no off-screen surfaces\n");
        return;
    }
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.kind = ENGINE_SURFACE_OFFSCREEN;
    info.width = 64;
    info.height = 48;
    info.scale = 1.0f;
    info.flags = host_surface_api_flags();
    memset(&surface, 0, sizeof(surface));
    if (g_api->surface_create(engine, &info, &surface) != ENGINE_OK)
    {
        printf("host_script_scene: the picture part is skipped: no device could be made\n");
        return;
    }
    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    CHECK_STATUS(g_api->surface_set_world(surface, world), ENGINE_OK);
    SetEnvironment(world, 1.0f, 1.0f, 1.0f, 0.2f, 0.0f, 0.0f, 0.0f, 1.0f);
    holder = PlaceEntity(world);

    memset(&ticket, 0, sizeof(ticket));
    CHECK_STATUS(g_api->surface_read_pixels(surface, &ticket), ENGINE_OK);
    Frame(engine);
    memset(&pixels, 0, sizeof(pixels));
    pixels.struct_size = sizeof(pixels);
    CHECK_STATUS(g_api->pixels_finish(ticket, &pixels), ENGINE_OK);
    CHECK_STATUS(g_api->pixels_destroy(ticket), ENGINE_OK);
    centre = (const unsigned char*)pixels.data.data + (size_t)(pixels.height / 2) * pixels.stride +
             (size_t)(pixels.width / 2) * 4;
    memcpy(before, centre, 4);
    CHECK(before[0] < 10 && before[1] < 10 && before[2] < 10); /* the black sky */
    engine_buffer_release(&pixels.data);

    memset(&load, 0, sizeof(load));
    load.struct_size = sizeof(load);
    load.name = Str("dawn");
    load.source = Str(kSky);
    CHECK_STATUS(g_api->script_load(world, &load, &script), ENGINE_OK);
    memset(&component, 0, sizeof(component));
    component.struct_size = sizeof(component);
    component.enabled = 1;
    strcpy(component.behavior, "dawn");
    CHECK_STATUS(g_api->component_set(world, holder, ENGINE_COMPONENT_SCRIPT, &component), ENGINE_OK);
    Frame(engine); /* the script runs and asks for the sky */
    memset(&ticket, 0, sizeof(ticket));
    CHECK_STATUS(g_api->surface_read_pixels(surface, &ticket), ENGINE_OK);
    Frame(engine); /* drawn with the sky the merge set */
    memset(&pixels, 0, sizeof(pixels));
    pixels.struct_size = sizeof(pixels);
    CHECK_STATUS(g_api->pixels_finish(ticket, &pixels), ENGINE_OK);
    CHECK_STATUS(g_api->pixels_destroy(ticket), ENGINE_OK);
    centre = (const unsigned char*)pixels.data.data + (size_t)(pixels.height / 2) * pixels.stride +
             (size_t)(pixels.width / 2) * 4;
    CHECK(centre[0] > 60 && centre[1] < 20 && centre[2] < 20); /* red now */
    engine_buffer_release(&pixels.data);
    CHECK_STATUS(g_api->surface_destroy(surface), ENGINE_OK);
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
}

/* Scripts find a joint and turn it (maintenance, 2026-11-24): skeleton.find_joint and animation.set_joint_rotation on a
   skeleton entity, driven by events the host sends to it. The hero skeleton has a root and an "arm" one above it; the
   idle clip holds the arm at 1.5 (speed 0), so a quarter turn of the root about Z lays it along -X. */
typedef struct FileBlob
{
    unsigned char* data;
    size_t size;
} FileBlob;

static FileBlob g_blobs[2];

static FileBlob ReadBlob(const char* name)
{
    char path[1024];
    FileBlob blob = {NULL, 0};
    FILE* f;
    snprintf(path, sizeof(path), "%s/%s", RES_DATA_DIR, name);
    f = fopen(path, "rb");
    if (f == NULL)
    {
        printf("host_script_scene: cannot open %s\n", path);
        ++g_failures;
        return blob;
    }
    fseek(f, 0, SEEK_END);
    blob.size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    blob.data = (unsigned char*)malloc(blob.size);
    if (fread(blob.data, 1, blob.size, f) != blob.size)
    {
        ++g_failures;
    }
    fclose(f);
    return blob;
}

static void AddBlobAs(engine_handle engine, uint64_t id, const FileBlob* blob)
{
    engine_resource_add_info info;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.id = id;
    info.bytes = blob->data;
    info.size = blob->size;
    CHECK_STATUS(g_api->resource_add(engine, &info), ENGINE_OK);
}

static const char kJointScript[] =
    "local function fails(f, ...)\n"
    "  local ok, message = pcall(f, ...)\n"
    "  return (not ok) and tostring(message) or ''\n"
    "end\n"
    "local function has(message, text) return string.find(message, text, 1, true) ~= nil and 1 or 0 end\n"
    "local function report(t, a, b, c, d) host.report(t, a or 0, b or 0, c or 0, d or 0, 0, 0, 0, 0, 0) end\n"
    "behavior('tilt', {\n"
    "  on_create = function(self)\n"
    "    local me = entity.self()\n"
    "    local plain = entity.find('plain')\n"
    "    report(1, skeleton.find_joint(me, 'arm') or -1, skeleton.find_joint(me, 'tail') == nil and 1 or 0,\n"
    "           skeleton.find_joint(plain, 'arm') == nil and 1 or 0)\n"
    "    event.on('turn', function(_, angle)\n"
    "      animation.set_joint_rotation(me, 0, quat.from_axis_angle(vec3.new(0, 0, 1), angle))\n"
    "    end)\n"
    "    event.on('probe', function()\n"
    "      local up = quat.from_axis_angle(vec3.new(0, 0, 1), 1)\n"
    "      report(2, has(fails(animation.set_joint_rotation, me, 7, up), 'joint'),\n"
    "                has(fails(animation.set_joint_rotation, me, 0, quat.from_axis_angle(vec3.new(0, 0, 1), 0 / 0)), 'finite'),\n"
    "                has(fails(animation.set_joint_rotation, plain, 0, up), 'skeleton'))\n"
    "    end)\n"
    "  end,\n"
    "})\n";

static void JointXY(engine_handle world, uint64_t key, uint32_t joint, double* x, double* y)
{
    float matrix[16];
    memset(matrix, 0, sizeof(matrix));
    CHECK_STATUS(g_api->skeleton_joint_transform(world, key, joint, matrix), ENGINE_OK);
    *x = matrix[12];
    *y = matrix[13];
}

static void EmitTurn(engine_handle world, const char* name, double angle, uint64_t target)
{
    engine_value item;
    engine_value_pool pool;
    memset(&item, 0, sizeof(item));
    memset(&pool, 0, sizeof(pool));
    item.tag = ENGINE_VALUE_NUMBER;
    item.f = angle;
    pool.struct_size = sizeof(pool);
    pool.root_count = 1;
    pool.item_count = 1;
    pool.item_capacity = 1;
    pool.items = &item;
    CHECK_STATUS(g_api->script_event_emit(world, Str(name), &pool, target), ENGINE_OK);
}

static void TestJoints(engine_handle engine)
{
    engine_handle world;
    engine_script_load load;
    engine_script_component component;
    engine_skeleton_instance instance;
    engine_play_params play;
    uint64_t script = 0;
    uint64_t hero;
    uint64_t plain;
    double x = 0.0;
    double y = 0.0;
    memset(g_rows, 0, sizeof(g_rows));
    g_blobs[0] = ReadBlob("hero.skel.blob");
    g_blobs[1] = ReadBlob("idle.anim.blob");
    if (g_blobs[0].data == NULL || g_blobs[1].data == NULL)
    {
        return;
    }
    AddBlobAs(engine, 0xa01ull, &g_blobs[0]);
    AddBlobAs(engine, 0xc01ull, &g_blobs[1]);
    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    CHECK_STATUS(g_api->script_register_function(world, Str("host.report"), 10, 0, &HostReport, NULL), ENGINE_OK);
    plain = PlaceEntity(world);
    CHECK_STATUS(g_api->entity_name_set(world, plain, Str("plain")), ENGINE_OK);
    hero = PlaceEntity(world);
    memset(&instance, 0, sizeof(instance));
    instance.struct_size = sizeof(instance);
    instance.skeleton = 0xa01ull;
    CHECK_STATUS(g_api->component_set(world, hero, ENGINE_COMPONENT_SKELETON_INSTANCE, &instance), ENGINE_OK);
    memset(&play, 0, sizeof(play));
    play.struct_size = sizeof(play);
    play.flags = ENGINE_PLAY_LOOP;
    play.speed = 1.0f;
    play.start_time = 0.5f;
    CHECK_STATUS(g_api->animation_play(world, hero, 0xc01ull, &play), ENGINE_OK);
    CHECK_STATUS(g_api->animation_set_speed(world, hero, 0.0f), ENGINE_OK);
    memset(&load, 0, sizeof(load));
    load.struct_size = sizeof(load);
    load.name = Str("tilt");
    load.source = Str(kJointScript);
    CHECK_STATUS(g_api->script_load(world, &load, &script), ENGINE_OK);
    memset(&component, 0, sizeof(component));
    component.struct_size = sizeof(component);
    component.enabled = 1;
    strcpy(component.behavior, "tilt");
    CHECK_STATUS(g_api->component_set(world, hero, ENGINE_COMPONENT_SCRIPT, &component), ENGINE_OK);
    Frame(engine);
    Frame(engine);
    /* the script found `arm`, not `tail`, and nothing on an entity with no skeleton */
    CHECK(g_rows[1].seen && Near(g_rows[1].v[0], 1.0) && Near(g_rows[1].v[1], 1.0) && Near(g_rows[1].v[2], 1.0));
    JointXY(world, hero, 1, &x, &y);
    CHECK(Near(x, 0.0) && fabs(y - 1.5) < 0.01);
    /* a quarter turn of the root, sent as an event to the entity: the arm lies along -X */
    EmitTurn(world, "turn", 1.5707963, hero);
    Frame(engine);
    Frame(engine);
    Frame(engine);
    JointXY(world, hero, 1, &x, &y);
    CHECK(fabs(x + 1.5) < 0.01 && fabs(y) < 0.01);
    /* the identity (an angle of 0) takes it away: the clip's matrix again */
    EmitTurn(world, "turn", 0.0, hero);
    Frame(engine);
    Frame(engine);
    Frame(engine);
    JointXY(world, hero, 1, &x, &y);
    CHECK(fabs(x) < 0.01 && fabs(y - 1.5) < 0.01);
    /* a joint the skeleton does not have, a rotation that is not finite and an entity with no skeleton are errors */
    EmitTurn(world, "probe", 0.0, hero);
    Frame(engine);
    Frame(engine);
    CHECK(g_rows[2].seen && Near(g_rows[2].v[0], 1.0) && Near(g_rows[2].v[1], 1.0) && Near(g_rows[2].v[2], 1.0));
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
}

/* Clips and bytes from scripts (maintenance, 2026-11-24): animation.clip / play / set_speed / stop / time on a skeleton
   entity driven by events, and bytes.pack / unpack / size, with bytes going through events both ways. */
static void AddBlobPath(engine_handle engine, uint64_t id, const FileBlob* blob, const char* path)
{
    engine_resource_add_info info;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.id = id;
    info.path = Str(path);
    info.bytes = blob->data;
    info.size = blob->size;
    CHECK_STATUS(g_api->resource_add(engine, &info), ENGINE_OK);
}

static const char kClipScript[] =
    "local function fails(f, ...)\n"
    "  local ok, message = pcall(f, ...)\n"
    "  return (not ok) and tostring(message) or ''\n"
    "end\n"
    "local function has(message, text) return string.find(message, text, 1, true) ~= nil and 1 or 0 end\n"
    "local function report(t, a, b, c, d, e) host.report(t, a or 0, b or 0, c or 0, d or 0, e or 0, 0, 0, 0, 0) end\n"
    "behavior('clips', {\n"
    "  on_create = function(self)\n"
    "    local me = entity.self()\n"
    "    local plain = entity.find('plain')\n"
    "    local idle = animation.clip('/clips/idle')\n"
    "    local hold = animation.clip('clips/hold')\n"
    "    local other = animation.clip('/clips/other')\n"
    "    report(1, idle and 1 or 0, hold and 1 or 0, animation.clip('/clips/missing') == nil and 1 or 0,\n"
    "           animation.time(me) == nil and 1 or 0, (idle ~= hold) and 1 or 0)\n"
    "    event.on('go', function(_, what)\n"
    "      if what == 1 then animation.play(me, idle, {speed = 1})\n"
    "      elseif what == 2 then animation.play(me, hold, {fade_seconds = 0.5, loop = false})\n"
    "      elseif what == 3 then animation.set_speed(me, 0)\n"
    "      elseif what == 4 then animation.stop(me)\n"
    "      elseif what == 5 then report(3, animation.time(me) or -1)\n"
    "      elseif what == 6 then animation.play(me, hold, {loop = false, start_time = 1000})\n"
    "      elseif what == 7 then animation.play(me, hold, {loop = false})\n"
    "      end\n"
    "    end)\n"
    "    event.on('probe', function()\n"
    "      report(2, has(fails(animation.play, me, other, nil), 'skeleton'),\n"
    "                has(fails(animation.play, me, '/clips/nothing', nil), 'not_found'),\n"
    "                has(fails(animation.set_speed, plain, 1), 'no animation'),\n"
    "                has(fails(animation.stop, plain), 'no animation'),\n"
    "                has(fails(animation.play, me, idle, {speed = 'fast'}), 'speed'))\n"
    "      report(4, has(fails(animation.play, me, 4096, nil), 'clip handle'),\n"
    "                (resource.find('/clips/idle') == idle) and 1 or 0, (resource.find('/clips/nope') == nil) and 1 or 0,\n"
    "                (type(idle) == 'userdata' and has(tostring(idle), 'resource(') == 1) and 1 or 0,\n"
    "                (getmetatable(idle) == false) and 1 or 0)\n"
    "    end)\n"
    "  end,\n"
    "})\n";

static void ClipState(engine_handle world, uint64_t key, engine_animation_state* state)
{
    memset(state, 0, sizeof(*state));
    state->struct_size = sizeof(*state);
    CHECK_STATUS(g_api->component_get(world, key, ENGINE_COMPONENT_ANIMATION_PLAYER, state), ENGINE_OK);
}

static void Frames(engine_handle engine, int count)
{
    int i;
    for (i = 0; i < count; ++i)
    {
        Frame(engine);
    }
}

static void TestClips(engine_handle engine)
{
    engine_handle world;
    engine_script_load load;
    engine_script_component component;
    engine_skeleton_instance instance;
    engine_animation_state state;
    FileBlob blobs[5];
    uint64_t script = 0;
    uint64_t hero;
    uint64_t plain;
    double t1 = 0.0;
    double t2 = 0.0;
    memset(g_rows, 0, sizeof(g_rows));
    blobs[0] = ReadBlob("hero.skel.blob");
    blobs[1] = ReadBlob("other.skel.blob");
    blobs[2] = ReadBlob("idle.anim.blob");
    blobs[3] = ReadBlob("hold.anim.blob");
    blobs[4] = ReadBlob("other.anim.blob");
    if (blobs[0].data == NULL || blobs[1].data == NULL || blobs[2].data == NULL || blobs[3].data == NULL ||
        blobs[4].data == NULL)
    {
        return;
    }
    CHECK_STATUS(g_api->resource_remove(engine, 0xa01ull, ENGINE_RESOURCE_FORCE, NULL), ENGINE_OK); /* TestJoints' */
    CHECK_STATUS(g_api->resource_remove(engine, 0xc01ull, ENGINE_RESOURCE_FORCE, NULL), ENGINE_OK);
    AddBlobAs(engine, 0xa01ull, &blobs[0]);
    AddBlobAs(engine, 0xa02ull, &blobs[1]);
    AddBlobPath(engine, 0xc01ull, &blobs[2], "/clips/idle");
    AddBlobPath(engine, 0xc03ull, &blobs[3], "/clips/hold");
    AddBlobPath(engine, 0xc02ull, &blobs[4], "/clips/other");
    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    CHECK_STATUS(g_api->script_register_function(world, Str("host.report"), 10, 0, &HostReport, NULL), ENGINE_OK);
    plain = PlaceEntity(world);
    CHECK_STATUS(g_api->entity_name_set(world, plain, Str("plain")), ENGINE_OK);
    hero = PlaceEntity(world);
    memset(&instance, 0, sizeof(instance));
    instance.struct_size = sizeof(instance);
    instance.skeleton = 0xa01ull;
    CHECK_STATUS(g_api->component_set(world, hero, ENGINE_COMPONENT_SKELETON_INSTANCE, &instance), ENGINE_OK);
    memset(&load, 0, sizeof(load));
    load.struct_size = sizeof(load);
    load.name = Str("clips");
    load.source = Str(kClipScript);
    CHECK_STATUS(g_api->script_load(world, &load, &script), ENGINE_OK);
    memset(&component, 0, sizeof(component));
    component.struct_size = sizeof(component);
    component.enabled = 1;
    strcpy(component.behavior, "clips");
    CHECK_STATUS(g_api->component_set(world, hero, ENGINE_COMPONENT_SCRIPT, &component), ENGINE_OK);
    Frames(engine, 2);
    /* the clips are found by path (with and without the slash), a missing one is nil, nothing plays yet */
    CHECK(g_rows[1].seen && Near(g_rows[1].v[0], 1.0) && Near(g_rows[1].v[1], 1.0) && Near(g_rows[1].v[2], 1.0) &&
          Near(g_rows[1].v[3], 1.0) && Near(g_rows[1].v[4], 1.0));
    CHECK_STATUS(g_api->animation_time(world, hero, &t1), ENGINE_ERROR_NOT_FOUND);
    /* play: the idle clip loops at speed 1 */
    EmitTurn(world, "go", 1.0, hero);
    Frames(engine, 4);
    ClipState(world, hero, &state);
    CHECK(state.clip == 0xc01ull && state.playing == 1 && state.looping == 1 && state.fading == 0);
    CHECK_STATUS(g_api->animation_time(world, hero, &t1), ENGINE_OK);
    CHECK(t1 > 0.0);
    /* the same clip again does not restart it */
    EmitTurn(world, "go", 1.0, hero);
    Frames(engine, 4);
    CHECK_STATUS(g_api->animation_time(world, hero, &t2), ENGINE_OK);
    CHECK(t2 > t1);
    /* another clip, faded to, not looping */
    EmitTurn(world, "go", 2.0, hero);
    Frames(engine, 3);
    ClipState(world, hero, &state);
    CHECK(state.clip == 0xc03ull && state.playing == 1 && state.looping == 0 && state.fading == 1);
    /* pause with a speed of 0: the time stands still */
    EmitTurn(world, "go", 3.0, hero);
    Frames(engine, 3);
    ClipState(world, hero, &state);
    CHECK(state.speed == 0.0f);
    CHECK_STATUS(g_api->animation_time(world, hero, &t1), ENGINE_OK);
    Frames(engine, 3);
    CHECK_STATUS(g_api->animation_time(world, hero, &t2), ENGINE_OK);
    CHECK(t1 == t2);
    /* the script reads the time as of its tick */
    EmitTurn(world, "go", 5.0, hero);
    Frames(engine, 3);
    CHECK(g_rows[3].seen && fabs(g_rows[3].v[0] - t1) < 0.05);
    /* stop */
    EmitTurn(world, "go", 4.0, hero);
    Frames(engine, 3);
    ClipState(world, hero, &state);
    CHECK(state.playing == 0);
    /* a clip that ran to its end plays again from the start when asked for again */
    EmitTurn(world, "go", 1.0, hero);
    Frames(engine, 3);
    EmitTurn(world, "go", 6.0, hero);
    Frames(engine, 4);
    ClipState(world, hero, &state);
    CHECK(state.clip == 0xc03ull && state.finished == 1);
    EmitTurn(world, "go", 7.0, hero);
    Frames(engine, 3);
    ClipState(world, hero, &state);
    CHECK(state.clip == 0xc03ull && state.finished == 0 && state.time < 100.0f);
    /* a clip of another skeleton, an unknown id, nothing started and a bad parameter are errors */
    EmitTurn(world, "probe", 0.0, hero);
    Frames(engine, 3);
    CHECK(g_rows[2].seen && Near(g_rows[2].v[0], 1.0) && Near(g_rows[2].v[1], 1.0) && Near(g_rows[2].v[2], 1.0) &&
          Near(g_rows[2].v[3], 1.0) && Near(g_rows[2].v[4], 1.0));
    /* a clip id is a handle: raw integers are refused, resource.find gives the same handle, it is opaque and protected
     */
    CHECK(g_rows[4].seen && Near(g_rows[4].v[0], 1.0) && Near(g_rows[4].v[1], 1.0) && Near(g_rows[4].v[2], 1.0) &&
          Near(g_rows[4].v[3], 1.0) && Near(g_rows[4].v[4], 1.0));
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
    CHECK_STATUS(g_api->resource_remove(engine, 0xc01ull, ENGINE_RESOURCE_FORCE, NULL), ENGINE_OK);
    CHECK_STATUS(g_api->resource_remove(engine, 0xc02ull, ENGINE_RESOURCE_FORCE, NULL), ENGINE_OK);
    CHECK_STATUS(g_api->resource_remove(engine, 0xc03ull, ENGINE_RESOURCE_FORCE, NULL), ENGINE_OK);
    CHECK_STATUS(g_api->resource_remove(engine, 0xa01ull, ENGINE_RESOURCE_FORCE, NULL), ENGINE_OK);
    CHECK_STATUS(g_api->resource_remove(engine, 0xa02ull, ENGINE_RESOURCE_FORCE, NULL), ENGINE_OK);
    {
        int i;
        for (i = 0; i < 5; ++i)
        {
            free(blobs[i].data);
        }
    }
}

/* A clip whose id is above 2^53 (a seeded import) is a handle in a script, found by path or by animation.clip, never a
   number that loses its low bits (maintenance, 2026-11-24). */
static const char kBigClipGltf[] =
    "{\"asset\":{\"version\":\"2.0\"},\"extensionsUsed\":[\"KHR_lights_punctual\"],"
    "\"extensions\":{\"KHR_lights_punctual\":{\"lights\":["
    "{\"name\":\"sun\",\"type\":\"directional\",\"color\":[1,0.5,0.25],\"intensity\":2},"
    "{\"name\":\"bulb\",\"type\":\"point\",\"intensity\":10,\"range\":4},"
    "{\"name\":\"torch\",\"type\":\"spot\",\"intensity\":10,\"range\":6,"
    "\"spot\":{\"innerConeAngle\":0.2,\"outerConeAngle\":0.6}}]}},"
    "\"scene\":0,\"scenes\":[{\"nodes\":[0,1,2,3]}],"
    "\"nodes\":[{\"name\":\"face\",\"mesh\":0},"
    "{\"name\":\"sun\",\"extensions\":{\"KHR_lights_punctual\":{\"light\":0}}},"
    "{\"name\":\"bulb\",\"translation\":[0,2,0],\"extensions\":{\"KHR_lights_punctual\":{\"light\":1}}},"
    "{\"name\":\"torch\",\"extensions\":{\"KHR_lights_punctual\":{\"light\":2}}}],"
    "\"materials\":[{\"name\":\"skin\"}],"
    "\"meshes\":[{\"name\":\"face\",\"weights\":[0.5],\"primitives\":[{\"attributes\":{\"POSITION\":0},"
    "\"targets\":[{\"POSITION\":1}],\"material\":0}]}],"
    "\"animations\":[{\"name\":\"open\",\"samplers\":[{\"input\":2,\"output\":3,\"interpolation\":\"LINEAR\"}],"
    "\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"weights\"}}]}],"
    "\"accessors\":["
    "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[1,1,0]},"
    "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,1],\"max\":[0,0,1]},"
    "{\"bufferView\":2,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\",\"min\":[0],\"max\":[1]},"
    "{\"bufferView\":3,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"}],"
    "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,"
    "\"byteLength\":36},{\"buffer\":0,\"byteOffset\":72,\"byteLength\":8},{\"buffer\":0,\"byteOffset\":80,"
    "\"byteLength\":8}],"
    "\"buffers\":[{\"byteLength\":88,\"uri\":\"data:application/octet-stream;base64,"
    "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAgD8AAAAAAACAPw=="
    "\"}]}";

static const char kBigClipScript[] =
    "local function fails(f, ...)\n"
    "  local ok, message = pcall(f, ...)\n"
    "  return (not ok) and tostring(message) or ''\n"
    "end\n"
    "local function has(message, text) return string.find(message, text, 1, true) ~= nil and 1 or 0 end\n"
    "behavior('big', {\n"
    "  on_create = function(self)\n"
    "    local me = entity.self()\n"
    "    local clip = animation.clip('/big/clips/open_face')\n"
    "    local found = resource.find('/big/clips/open_face')\n"
    "    host.report(1, clip and 1 or 0, (clip == found) and 1 or 0, 0, 0, 0, 0, 0, 0, 0)\n"
    "    event.on('go', function(_, what)\n"
    "      if what == 1 then animation.play(me, clip, {loop = false})\n"
    "      elseif what == 2 then animation.play(me, '/big/clips/open_face', {loop = false})\n"
    "      elseif what == 3 then\n"
    "        host.report(2, has(fails(animation.play, me, '/big/clips/none', nil), '/big/clips/none'), 0, 0, 0, 0, 0, 0, 0, 0)\n"
    "      end\n"
    "    end)\n"
    "  end,\n"
    "})\n";

static void TestBigClipId(engine_handle engine)
{
    engine_import_options options;
    engine_import_result result;
    engine_handle world;
    engine_script_load load;
    engine_script_component component;
    engine_animation_state state;
    uint64_t script = 0;
    uint64_t face;
    uint64_t clip = 0;
    uint32_t i;
    memset(g_rows, 0, sizeof(g_rows));
    memset(&options, 0, sizeof(options));
    options.struct_size = sizeof(options);
    options.id_seed = 0x5eed;
    options.path_prefix = Str("/big");
    memset(&result, 0, sizeof(result));
    result.struct_size = sizeof(result);
    CHECK_STATUS(g_api->import_gltf(engine, kBigClipGltf, sizeof(kBigClipGltf) - 1, &options, &result), ENGINE_OK);
    for (i = 0; i < result.resource_count; ++i)
    {
        uint32_t type;
        memcpy(&type, result.types.data + i * sizeof(type), sizeof(type));
        if (type == ENGINE_RESOURCE_ANIMATION)
        {
            memcpy(&clip, result.ids.data + i * sizeof(clip), sizeof(clip));
        }
    }
    CHECK(clip > (1ull << 53)); /* a Lua number could not hold it */
    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    CHECK_STATUS(g_api->script_register_function(world, Str("host.report"), 10, 0, &HostReport, NULL), ENGINE_OK);
    face = PlaceEntity(world);
    memset(&load, 0, sizeof(load));
    load.struct_size = sizeof(load);
    load.name = Str("big");
    load.source = Str(kBigClipScript);
    CHECK_STATUS(g_api->script_load(world, &load, &script), ENGINE_OK);
    memset(&component, 0, sizeof(component));
    component.struct_size = sizeof(component);
    component.enabled = 1;
    strcpy(component.behavior, "big");
    CHECK_STATUS(g_api->component_set(world, face, ENGINE_COMPONENT_SCRIPT, &component), ENGINE_OK);
    Frames(engine, 2);
    CHECK(g_rows[1].seen && Near(g_rows[1].v[0], 1.0) && Near(g_rows[1].v[1], 1.0));
    EmitTurn(world, "go", 1.0, face);
    Frames(engine, 3);
    ClipState(world, face, &state);
    CHECK(state.clip == clip && state.playing == 1);
    CHECK_STATUS(g_api->animation_stop(world, face), ENGINE_OK);
    EmitTurn(world, "go", 2.0, face);
    Frames(engine, 3);
    ClipState(world, face, &state);
    CHECK(state.clip == clip && state.playing == 1);
    EmitTurn(world, "go", 3.0, face);
    Frames(engine, 3);
    CHECK(g_rows[2].seen && Near(g_rows[2].v[0], 1.0));
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
    for (i = 0; i < result.resource_count; ++i)
    {
        uint64_t id;
        memcpy(&id, result.ids.data + i * sizeof(id), sizeof(id));
        CHECK_STATUS(g_api->resource_remove(engine, id, ENGINE_RESOURCE_FORCE, NULL), ENGINE_OK);
    }
    engine_buffer_release(&result.ids);
    engine_buffer_release(&result.types);
    engine_buffer_release(&result.paths);
    engine_buffer_release(&result.warnings);
    engine_buffer_release(&result.manifest);
    engine_buffer_release(&result.summary);
}

static const char kBytesScript[] =
    "local function fails(f, ...)\n"
    "  local ok, message = pcall(f, ...)\n"
    "  return (not ok) and tostring(message) or ''\n"
    "end\n"
    "local function has(message, text) return string.find(message, text, 1, true) ~= nil and 1 or 0 end\n"
    "local function r(t, ...)\n"
    "  local v = {...}\n"
    "  host.report(t, v[1] or 0, v[2] or 0, v[3] or 0, v[4] or 0, v[5] or 0, v[6] or 0, v[7] or 0, v[8] or 0, v[9] or 0)\n"
    "end\n"
    "behavior('packer', {\n"
    "  on_create = function(self)\n"
    "    local fmt = 'B b H h I i f d'\n"
    "    local b = bytes.pack(fmt, 255, -128, 65535, -32768, 4294967295, -2147483648, 1.5, -0.0)\n"
    "    local a, bb, c, d, e, f, g, h, nxt = bytes.unpack(b, fmt, nil)\n"
    "    r(1, bytes.size('B B H I I f f f H h H H'), bytes.size(fmt), a, bb, c, d, e, f)\n"
    "    r(2, g, (1 / h < 0) and 1 or 0, nxt, select('#', bytes.unpack(b, '', 26)),\n"
    "      has(fails(bytes.unpack, b, 'B', 26), 'offset 26'), has(fails(bytes.unpack, b, 'B', 27), 'offset 27'),\n"
    "      has(fails(bytes.unpack, b, 'x', nil), '`x`'), bytes.size('s1') == nil and 1 or 0)\n"
    "    local n = bytes.pack('f', 0 / 0)\n"
    "    local nan = bytes.unpack(n, 'f', nil)\n"
    "    local str = bytes.pack('c3 s1 B', 'abc', 'hi', 0)\n"
    "    local s3, s1, zero, end_at = bytes.unpack(str, 'c3 s1 B', nil)\n"
    "    local second = bytes.unpack(b, 'B', 1)\n"
    "    r(3, (nan ~= nan) and 1 or 0, (s3 == 'abc') and 1 or 0, (s1 == 'hi') and 1 or 0, zero, end_at, second + 0,\n"
    "      has(fails(bytes.pack, 'B', 256), 'value 1'), has(fails(bytes.pack, 'c2', 'abc'), 'exactly'))\n"
    "    event.on('pose', function(_, payload)\n"
    "      local t1, t2, flags, p, q, x, y, z, hh, ii, jj, kk = bytes.unpack(payload, 'B B H I I f f f H h H H', nil)\n"
    "      r(4, t1, t2, flags, p, q, x, y, z)\n"
    "      event.emit('echo', bytes.pack('H I', hh, 99))\n"
    "    end)\n"
    "  end,\n"
    "})\n";

static int g_echo_ok = 0;

static void OnEcho(void* user, engine_string name, uint64_t sender, const engine_value_pool* payload)
{
    static const unsigned char kExpect[6] = {0x78, 0x56, 0x63, 0x00, 0x00, 0x00};
    (void)user;
    (void)name;
    (void)sender;
    if (payload != NULL && payload->root_count == 1 && payload->items[0].tag == ENGINE_VALUE_BYTES &&
        payload->items[0].count == 6 && memcmp(payload->bytes + payload->items[0].i, kExpect, 6) == 0)
    {
        g_echo_ok = 1;
    }
}

static void TestBytes(engine_handle engine)
{
    engine_handle world;
    engine_script_load load;
    engine_script_component component;
    engine_value item;
    engine_value_pool pool;
    uint32_t subscription = 0;
    unsigned char pose[32];
    uint64_t script = 0;
    uint64_t holder;
    float f;
    memset(g_rows, 0, sizeof(g_rows));
    g_echo_ok = 0;
    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    CHECK_STATUS(g_api->script_register_function(world, Str("host.report"), 10, 0, &HostReport, NULL), ENGINE_OK);
    CHECK_STATUS(g_api->script_event_subscribe(world, Str("echo"), &OnEcho, NULL, &subscription), ENGINE_OK);
    holder = PlaceEntity(world);
    memset(&load, 0, sizeof(load));
    load.struct_size = sizeof(load);
    load.name = Str("packer");
    load.source = Str(kBytesScript);
    CHECK_STATUS(g_api->script_load(world, &load, &script), ENGINE_OK);
    memset(&component, 0, sizeof(component));
    component.struct_size = sizeof(component);
    component.enabled = 1;
    strcpy(component.behavior, "packer");
    CHECK_STATUS(g_api->component_set(world, holder, ENGINE_COMPONENT_SCRIPT, &component), ENGINE_OK);
    Frames(engine, 2);
    /* sizes and every letter round-trip: the report holds what came back */
    CHECK(g_rows[1].seen && Near(g_rows[1].v[0], 32.0) && Near(g_rows[1].v[1], 26.0) && Near(g_rows[1].v[2], 255.0) &&
          Near(g_rows[1].v[3], -128.0) && Near(g_rows[1].v[4], 65535.0) && Near(g_rows[1].v[5], -32768.0) &&
          Near(g_rows[1].v[6], 4294967295.0) && Near(g_rows[1].v[7], -2147483648.0));
    /* a float, a negative zero, the next offset, the exact end (one result: the offset), and the errors */
    CHECK(g_rows[2].seen && Near(g_rows[2].v[0], 1.5) && Near(g_rows[2].v[1], 1.0) && Near(g_rows[2].v[2], 26.0) &&
          Near(g_rows[2].v[3], 1.0) && Near(g_rows[2].v[4], 1.0) && Near(g_rows[2].v[5], 1.0) &&
          Near(g_rows[2].v[6], 1.0) && Near(g_rows[2].v[7], 1.0));
    /* a NaN, strings, a zero byte, one past the end naming, a byte read at an offset, errors of pack */
    CHECK(g_rows[3].seen && Near(g_rows[3].v[0], 1.0) && Near(g_rows[3].v[1], 1.0) && Near(g_rows[3].v[2], 1.0) &&
          Near(g_rows[3].v[3], 0.0) && Near(g_rows[3].v[4], 7.0) && Near(g_rows[3].v[5], 128.0) &&
          Near(g_rows[3].v[6], 1.0) && Near(g_rows[3].v[7], 1.0));
    /* bytes in through an event: a 32-byte pose frame; bytes out through another */
    memset(pose, 0, sizeof(pose));
    pose[0] = 7;    /* B */
    pose[1] = 9;    /* B */
    pose[2] = 0x34; /* H = 0x1234 */
    pose[3] = 0x12;
    pose[4] = 1000 & 0xff; /* I = 1000 */
    pose[5] = 1000 >> 8;
    pose[8] = 2; /* I = 2 */
    f = 0.5f;
    memcpy(pose + 12, &f, 4); /* f */
    f = -2.0f;
    memcpy(pose + 16, &f, 4); /* f */
    f = 8.0f;
    memcpy(pose + 20, &f, 4); /* f */
    pose[24] = 0x78;          /* H = 0x5678 */
    pose[25] = 0x56;
    memset(&item, 0, sizeof(item));
    item.tag = ENGINE_VALUE_BYTES;
    item.count = sizeof(pose);
    item.i = 0;
    memset(&pool, 0, sizeof(pool));
    pool.struct_size = sizeof(pool);
    pool.root_count = 1;
    pool.item_count = 1;
    pool.item_capacity = 1;
    pool.items = &item;
    pool.byte_count = sizeof(pose);
    pool.byte_capacity = sizeof(pose);
    pool.bytes = pose;
    CHECK_STATUS(g_api->script_event_emit(world, Str("pose"), &pool, holder), ENGINE_OK);
    Frames(engine, 4);
    CHECK(g_rows[4].seen && Near(g_rows[4].v[0], 7.0) && Near(g_rows[4].v[1], 9.0) && Near(g_rows[4].v[2], 0x1234) &&
          Near(g_rows[4].v[3], 1000.0) && Near(g_rows[4].v[4], 2.0) && Near(g_rows[4].v[5], 0.5) &&
          Near(g_rows[4].v[6], -2.0) && Near(g_rows[4].v[7], 8.0));
    CHECK(g_echo_ok == 1);
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
}

int main(void)
{
    engine_create_info create;
    engine_handle engine;
    engine_info info;

    host_quiet_failures();
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &g_api) != ENGINE_OK)
    {
        printf("host_script_scene: no function table\n");
        return 2;
    }
    memset(&create, 0, sizeof(create));
    create.struct_size = sizeof(create);
    create.flags = ENGINE_CREATE_LOG_SUPPRESS_CONSOLE;
    memset(&engine, 0, sizeof(engine));
    CHECK_STATUS(g_api->create(&create, &engine), ENGINE_OK);
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    CHECK_STATUS(g_api->get_info(engine, &info), ENGINE_OK);
    if ((info.caps & ENGINE_CAP_SCRIPT) == 0)
    {
        printf("host_script_scene: skipped: this build has no scripts\n");
        CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
        return 0;
    }
    TestState(engine);
    if ((info.caps & ENGINE_CAP_IMPORT_GLTF) != 0)
    {
        TestFind(engine);
    }
    TestJoints(engine);
    TestClips(engine);
    if ((info.caps & ENGINE_CAP_IMPORT_GLTF) != 0)
    {
        TestBigClipId(engine);
    }
    TestBytes(engine);
    if (getenv("HOST_SCRIPT_SCENE_NO_PICTURE") == NULL) /* ThreadSanitizer cannot run the GPU driver's threads */
    {
        TestPicture(engine);
    }
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    free(g_blobs[0].data);
    free(g_blobs[1].data);
    if (g_failures == 0)
    {
        printf("host_script_scene: ok\n");
        return 0;
    }
    printf("host_script_scene: %d failure(s)\n", g_failures);
    return 1;
}
