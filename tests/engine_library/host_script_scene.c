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
    "      report(1, env.ambient_color.x, env.ambient_intensity, env.sky_color.z, env.exposure)\n"
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
    "      environment.set({ambient_intensity = 0.7})\n"
    "    elseif t == 4 then\n"
    "      report(4, env.sky_color.y, env.exposure, env.ambient_intensity, env.ambient_color.x)\n"
    "    elseif t == 5 then\n"
    "      -- the errors\n"
    "      local a = fails(environment.set, {bogus = 1})\n"
    "      local b = fails(environment.set, {exposure = -1})\n"
    "      local c = fails(environment.set, {sky_color = 1})\n"
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
          Near(g_rows[1].v[3], 1.0));
    /* tick 2: the sky is set, the ambient light kept; the child's light.set before its light existed was an error */
    CHECK(g_rows[2].seen && Near(g_rows[2].v[0], 0.1) && Near(g_rows[2].v[1], 0.2) && Near(g_rows[2].v[2], 0.3) &&
          Near(g_rows[2].v[3], 2.0) && Near(g_rows[2].v[4], 1.0) && Near(g_rows[2].v[5], 0.2));
    /* tick 4: a one-field call kept the rest */
    CHECK(g_rows[4].seen && Near(g_rows[4].v[0], 0.2) && Near(g_rows[4].v[1], 2.0) && Near(g_rows[4].v[2], 0.7) &&
          Near(g_rows[4].v[3], 1.0));
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
    if (getenv("HOST_SCRIPT_SCENE_NO_PICTURE") == NULL) /* ThreadSanitizer cannot run the GPU driver's threads */
    {
        TestPicture(engine);
    }
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    if (g_failures == 0)
    {
        printf("host_script_scene: ok\n");
        return 0;
    }
    printf("host_script_scene: %d failure(s)\n", g_failures);
    return 1;
}
