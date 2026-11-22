/* A display that glows and reflects (maintenance, 2026-11-22): a 2 x 2 m quad with a material named "Screen" (smooth
   dielectric, white), drawn by an off-screen surface in a world with no light at all (ambient 0, the default sun gone
   through world_set_environment), so only what a surface emits or reflects shows. The program checks:
   - emissive_from_albedo 0 (the default) draws the quad black; 1 draws the picture of its albedo texture; the glow
     follows albedo_uv (a texture of a red and a green half, shifted) and the black bars of albedo_uv_outside stay
   black;
   - reflection_intensity 0 (the default) leaves the quad black; above 0 a smooth dielectric seen at a glancing angle
     shows much more of sky_color than the same quad seen head on;
   - the first struct_size of engine_environment (without the field) still works and means 0.
   Skipped when the build has no off-screen surfaces or no device can be made. Plain C. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "engine/engine_c.h"
#include "host_surface_api.h"

#define W 64u
#define H 48u

static int g_failures = 0;
static const engine_api* g_api = NULL;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("host_glass_screen: line %d: CHECK failed: %s\n", __LINE__, #condition);                            \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

#define CHECK_STATUS(call, expected)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status got_ = (call);                                                                             \
        if (got_ != (expected))                                                                                        \
        {                                                                                                              \
            printf("host_glass_screen: line %d: %s returned %d, expected %d\n", __LINE__, #call, (int)got_,            \
                   (int)(expected));                                                                                   \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

static const char kQuad[] =
    "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"name\":\"Screen\",\"mesh\":0}],\"materials\""
    ":[{\"name\":\"Screen\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,1,1,1],\"metallicFactor\":0,\"roughnessFactor\":0."
    "05}}],\"meshes\":[{\"name\":\"quad\",\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2},\"indices\":"
    "3,\"material\":0}]}],\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\",\"min\":[-1,-1,0],\""
    "max\":[1,1,0]},{\"bufferView\":1,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},{\"bufferView\":2,\"componentType\":51"
    "26,\"count\":4,\"type\":\"VEC2\"},{\"bufferView\":3,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"}],\"bufferViews\":[{\""
    "buffer\":0,\"byteOffset\":0,\"byteLength\":48},{\"buffer\":0,\"byteOffset\":48,\"byteLength\":48},{\"buffer\":0,\"byteOffset"
    "\":96,\"byteLength\":32},{\"buffer\":0,\"byteOffset\":128,\"byteLength\":12}],\"buffers\":[{\"byteLength\":140,\"uri\":\"data:"
    "application/octet-stream;base64,AACAvwAAgL8AAAAAAACAPwAAgL8AAAAAAACAPwAAgD8AAAAAAACAvwAAgD8AAAAAAAAAAAAAAAAAAI"
    "A/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAgD8AAIA/AACAPwAAgD8AAAAAAAAAAAAAAAAAAAEAAgAAAAIAAwA="
    "\"}]}";

static engine_string S(const char* text)
{
    engine_string s;
    s.data = text;
    s.length = strlen(text);
    return s;
}

static uint64_t ImportQuad(engine_handle engine)
{
    engine_import_options options;
    engine_import_result result;
    uint64_t level = 0;
    uint32_t i;
    memset(&options, 0, sizeof(options));
    options.struct_size = sizeof(options);
    options.flags = ENGINE_IMPORT_EMIT_LEVEL;
    options.id_seed = 6161;
    memset(&result, 0, sizeof(result));
    result.struct_size = sizeof(result);
    CHECK_STATUS(g_api->import_gltf(engine, kQuad, sizeof(kQuad) - 1, &options, &result), ENGINE_OK);
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

static void SetEnvironment(engine_handle world, float sky_r, float sky_g, float sky_b, float reflection, int old_size)
{
    engine_environment e;
    memset(&e, 0, sizeof(e));
    e.struct_size = old_size ? (uint32_t)offsetof(engine_environment, reflection_intensity) : sizeof(e);
    e.sky_color[0] = sky_r;
    e.sky_color[1] = sky_g;
    e.sky_color[2] = sky_b;
    e.exposure = 1.0f;
    e.reflection_intensity = reflection;
    CHECK_STATUS(g_api->world_set_environment(world, &e), ENGINE_OK);
}

static void SetFloat(engine_handle world, uint64_t key, const char* slot, float v)
{
    engine_slot_override o;
    memset(&o, 0, sizeof(o));
    o.struct_size = sizeof(o);
    o.kind = ENGINE_SLOT_FLOAT;
    o.value[0] = v;
    CHECK_STATUS(g_api->entity_set_slot_override(world, key, S(slot), &o), ENGINE_OK);
}

static void SetVec(engine_handle world, uint64_t key, const char* slot, uint32_t kind, float a, float b, float c,
                   float d)
{
    engine_slot_override o;
    memset(&o, 0, sizeof(o));
    o.struct_size = sizeof(o);
    o.kind = kind;
    o.value[0] = a;
    o.value[1] = b;
    o.value[2] = c;
    o.value[3] = d;
    CHECK_STATUS(g_api->entity_set_slot_override(world, key, S(slot), &o), ENGINE_OK);
}

static engine_handle MakeTexture(engine_handle engine, const unsigned char* rgba, int w, int h)
{
    engine_texture_info info;
    engine_update_info update;
    engine_handle texture;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.flags = ENGINE_TEXTURE_SRGB;
    info.width = (uint32_t)w;
    info.height = (uint32_t)h;
    info.name = S("glass test");
    memset(&texture, 0, sizeof(texture));
    CHECK_STATUS(g_api->texture_create(engine, &info, &texture), ENGINE_OK);
    memset(&update, 0, sizeof(update));
    update.struct_size = sizeof(update);
    CHECK_STATUS(g_api->texture_update(texture, rgba, (uint32_t)w * 4u, &update), ENGINE_OK);
    return texture;
}

static void Rotate(engine_handle world, uint64_t key, float degrees_about_y)
{
    engine_transform t;
    const float half = degrees_about_y * 3.14159265f / 360.0f;
    memset(&t, 0, sizeof(t));
    t.struct_size = sizeof(t);
    CHECK_STATUS(g_api->component_get(world, key, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
    t.rotation[0] = 0.0f;
    t.rotation[1] = (float)sin(half);
    t.rotation[2] = 0.0f;
    t.rotation[3] = (float)cos(half);
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
}

static void Frame(engine_handle engine)
{
    engine_frame_info frame;
    memset(&frame, 0, sizeof(frame));
    frame.struct_size = sizeof(frame);
    frame.delta_seconds = 1.0 / 60.0;
    CHECK_STATUS(g_api->frame(engine, &frame, NULL), ENGINE_OK);
}

/* Draws until the changes made so far show, then reads the pixel (x, y) as rgb. */
static void PixelAt(engine_handle engine, engine_handle surface, unsigned x, unsigned y, int rgb[3])
{
    engine_handle ticket;
    engine_pixels pixels;
    const unsigned char* p;
    int i;
    for (i = 0; i < 10; ++i)
    {
        Frame(engine);
    }
    memset(&ticket, 0, sizeof(ticket));
    CHECK_STATUS(g_api->surface_read_pixels(surface, &ticket), ENGINE_OK);
    Frame(engine);
    memset(&pixels, 0, sizeof(pixels));
    pixels.struct_size = sizeof(pixels);
    CHECK_STATUS(g_api->pixels_finish(ticket, &pixels), ENGINE_OK);
    CHECK_STATUS(g_api->pixels_destroy(ticket), ENGINE_OK);
    p = (const unsigned char*)pixels.data.data + (size_t)y * pixels.stride + (size_t)x * 4;
    rgb[0] = p[0];
    rgb[1] = p[1];
    rgb[2] = p[2];
    engine_buffer_release(&pixels.data);
}

int main(void)
{
    engine_create_info create;
    engine_handle engine;
    engine_info about;
    engine_surface_info info;
    engine_handle surface;
    engine_handle world;
    engine_spawn_params spawn;
    engine_camera camera;
    engine_transform at;
    engine_handle picture;
    uint64_t level;
    uint64_t root = 0;
    uint64_t camera_entity = 0;
    uint64_t screen = 0;
    int c[3];
    int head[3];
    int glancing[3];
    unsigned char halves[2 * 4] = {255, 0, 0, 255, 0, 255, 0, 255};
    unsigned char flat[4 * 4 * 4];
    int i;

    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &g_api) != ENGINE_OK)
    {
        printf("host_glass_screen: no function table\n");
        return 2;
    }
    memset(&create, 0, sizeof(create));
    create.struct_size = sizeof(create);
    create.flags = ENGINE_CREATE_LOG_SUPPRESS_CONSOLE;
    memset(&engine, 0, sizeof(engine));
    CHECK_STATUS(g_api->create(&create, &engine), ENGINE_OK);
    memset(&about, 0, sizeof(about));
    about.struct_size = sizeof(about);
    CHECK_STATUS(g_api->get_info(engine, &about), ENGINE_OK);
    if ((about.caps & ENGINE_CAP_SURFACE_OFFSCREEN) == 0 || (about.caps & ENGINE_CAP_IMPORT_GLTF) == 0)
    {
        printf("host_glass_screen: skipped: this build has no off-screen surfaces or no importer\n");
        CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
        return 0;
    }
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.kind = ENGINE_SURFACE_OFFSCREEN;
    info.width = W;
    info.height = H;
    info.scale = 1.0f;
    info.flags = host_surface_api_flags();
    memset(&surface, 0, sizeof(surface));
    if (getenv("HOST_GLASS_SCREEN_SKIP") != NULL)
    {
        printf("host_glass_screen: skipped: ThreadSanitizer cannot run the GPU driver's threads\n");
        CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
        return 0;
    }
    if (g_api->surface_create(engine, &info, &surface) != ENGINE_OK)
    {
        printf("host_glass_screen: skipped: no device could be made\n");
        CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
        return 0;
    }
    level = ImportQuad(engine);
    CHECK(level != 0);
    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    CHECK_STATUS(g_api->surface_set_world(surface, world), ENGINE_OK);
    memset(&spawn, 0, sizeof(spawn));
    spawn.struct_size = sizeof(spawn);
    spawn.rotation[3] = 1.0f;
    spawn.scale = 1.0f;
    CHECK_STATUS(g_api->level_spawn(world, level, &spawn, &root, NULL, 0, NULL), ENGINE_OK);
    CHECK_STATUS(g_api->entity_find(world, S("Screen"), &screen), ENGINE_OK);

    /* the camera at (0, 0, 3) looks down -Z at the quad: 2 m wide, filling 28 of the 64 columns */
    CHECK_STATUS(g_api->entity_create(world, &camera_entity), ENGINE_OK);
    memset(&at, 0, sizeof(at));
    at.struct_size = sizeof(at);
    at.position[2] = 3.0f;
    at.rotation[3] = 1.0f;
    at.scale[0] = at.scale[1] = at.scale[2] = 1.0f;
    CHECK_STATUS(g_api->component_set(world, camera_entity, ENGINE_COMPONENT_TRANSFORM, &at), ENGINE_OK);
    memset(&camera, 0, sizeof(camera));
    camera.struct_size = sizeof(camera);
    camera.flags = ENGINE_CAMERA_FIXED_ASPECT;
    camera.fov_y = 1.0471976f;
    camera.near_plane = 0.1f;
    camera.far_plane = 100.0f;
    camera.aspect = (float)W / (float)H;
    CHECK_STATUS(g_api->component_set(world, camera_entity, ENGINE_COMPONENT_CAMERA, &camera), ENGINE_OK);
    CHECK_STATUS(g_api->surface_set_camera(surface, camera_entity), ENGINE_OK);

    /* a world with no light at all: only emission and reflection show */
    SetEnvironment(world, 0.0f, 0.0f, 0.0f, 0.0f, 0);

    /* 1. the glow of the albedo */
    memset(flat, 0, sizeof(flat));
    for (i = 0; i < 16; ++i)
    {
        flat[i * 4 + 0] = 200;
        flat[i * 4 + 1] = 100;
        flat[i * 4 + 2] = 50;
        flat[i * 4 + 3] = 255;
    }
    picture = MakeTexture(engine, flat, 4, 4);
    {
        engine_slot_override o;
        memset(&o, 0, sizeof(o));
        o.struct_size = sizeof(o);
        o.kind = ENGINE_SLOT_HOST_TEXTURE;
        o.host_texture = picture;
        CHECK_STATUS(g_api->entity_set_slot_override(world, screen, S("albedo_tex"), &o), ENGINE_OK);
    }
    PixelAt(engine, surface, W / 2, H / 2, c);
    CHECK(c[0] < 8 && c[1] < 8 && c[2] < 8); /* slot at its default 0: black in a dark world */
    SetFloat(world, screen, "emissive_from_albedo", 1.0f);
    PixelAt(engine, surface, W / 2, H / 2, c);
    CHECK(c[0] > 60 && c[0] > c[1] + 20 && c[1] > c[2] + 10); /* the picture's colour */
    SetFloat(world, screen, "emissive_from_albedo", 0.0f);
    PixelAt(engine, surface, W / 2, H / 2, c);
    CHECK(c[0] < 8 && c[1] < 8 && c[2] < 8);
    CHECK_STATUS(g_api->entity_clear_slot_override(world, screen, S("emissive_from_albedo")), ENGINE_OK);

    /* 2. the glow follows albedo_uv and the bars stay black: a red and a green half */
    CHECK_STATUS(g_api->texture_destroy(picture), ENGINE_OK);
    picture = MakeTexture(engine, halves, 2, 1);
    {
        engine_slot_override o;
        memset(&o, 0, sizeof(o));
        o.struct_size = sizeof(o);
        o.kind = ENGINE_SLOT_HOST_TEXTURE;
        o.host_texture = picture;
        CHECK_STATUS(g_api->entity_set_slot_override(world, screen, S("albedo_tex"), &o), ENGINE_OK);
    }
    SetFloat(world, screen, "emissive_from_albedo", 1.0f);
    SetVec(world, screen, "albedo_uv", ENGINE_SLOT_VEC4, 0.5f, 1.0f, 0.0f, 0.0f); /* only the left half */
    PixelAt(engine, surface, W / 2, H / 2, c);
    CHECK(c[0] > 100 && c[0] > c[1] * 4);
    SetVec(world, screen, "albedo_uv", ENGINE_SLOT_VEC4, 0.5f, 1.0f, 0.5f, 0.0f); /* only the right half */
    PixelAt(engine, surface, W / 2, H / 2, c);
    CHECK(c[1] > 100 && c[1] > c[0] * 4);
    SetVec(world, screen, "albedo_uv", ENGINE_SLOT_VEC4, 4.0f, 4.0f, -1.5f, -1.5f); /* the picture is a quarter size */
    SetVec(world, screen, "albedo_uv_outside", ENGINE_SLOT_COLOR, 0.0f, 0.0f, 0.0f, 1.0f);
    PixelAt(engine, surface, W / 2, H / 2, c);
    CHECK(c[0] + c[1] > 60); /* the middle is still picture */
    PixelAt(engine, surface, W / 2 - 11, H / 2, c);
    CHECK(c[0] < 8 && c[1] < 8 && c[2] < 8); /* a bar: black, though it is emissive */
    CHECK_STATUS(g_api->entity_clear_slot_override(world, screen, S("albedo_uv")), ENGINE_OK);
    CHECK_STATUS(g_api->entity_clear_slot_override(world, screen, S("albedo_uv_outside")), ENGINE_OK);
    CHECK_STATUS(g_api->entity_clear_slot_override(world, screen, S("emissive_from_albedo")), ENGINE_OK);
    CHECK_STATUS(g_api->entity_clear_slot_override(world, screen, S("albedo_tex")), ENGINE_OK);
    CHECK_STATUS(g_api->texture_destroy(picture), ENGINE_OK);

    /* 3. the sky reflection: a smooth dielectric, head on and at a glancing angle */
    SetEnvironment(world, 0.0f, 0.5f, 0.0f, 0.0f, 0);
    PixelAt(engine, surface, W / 2, H / 2, head);
    PixelAt(engine, surface, W / 2, H / 2, glancing);
    CHECK(head[1] == glancing[1]); /* intensity 0: the quad is black either way ... */
    Rotate(world, root, 78.0f);
    PixelAt(engine, surface, W / 2, H / 2, c);
    CHECK(c[1] == head[1]); /* ... and the angle changes nothing */
    SetEnvironment(world, 0.0f, 0.5f, 0.0f, 1.0f, 0);
    PixelAt(engine, surface, W / 2, H / 2, glancing);
    Rotate(world, root, 0.0f);
    PixelAt(engine, surface, W / 2, H / 2, head);
    {
        printf("host_glass_screen: head on green %d, glancing green %d\n", head[1], glancing[1]);
    }
    CHECK(glancing[1] > head[1] * 3 && glancing[1] > 40);
    CHECK(glancing[0] < 10 && glancing[2] < 10); /* the sky is green: nothing else is reflected */
    /* a host built before the field: struct_size ends at it, and the reflection is 0 */
    SetEnvironment(world, 0.0f, 0.5f, 0.0f, 5.0f, 1);
    Rotate(world, root, 78.0f);
    PixelAt(engine, surface, W / 2, H / 2, c);
    CHECK(c[1] == head[1] || c[1] < 10);

    CHECK_STATUS(g_api->surface_destroy(surface), ENGINE_OK);
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    if (g_failures == 0)
    {
        printf("host_glass_screen: ok\n");
        return 0;
    }
    printf("host_glass_screen: %d failure(s)\n", g_failures);
    return 1;
}
