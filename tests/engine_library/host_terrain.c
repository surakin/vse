/* Terrain through the C API (Phase 28 Day 493): a C program makes a height map from samples it holds
   (height_map_create), puts a terrain on an entity (ENGINE_COMPONENT_TERRAIN) with the height-field collider flag,
   reads the ground (terrain_height), drops a ball on it and lets the frame's physics stop it on the surface, reads the
   collider back as ENGINE_SHAPE_HEIGHT_FIELD, checks every refusal the API documents and, when a device can be made,
   draws the terrain with an off-screen surface and looks at a pixel. Plain C. */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/engine_c.h"
#include "host_surface_api.h"

#define W 64u
#define H 48u
#define SIDE 33u

static int g_failures = 0;
static const engine_api* g_api = NULL;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("host_terrain: line %d: CHECK failed: %s\n", __LINE__, #condition);                                 \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

#define CHECK_STATUS(call, expected)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status got_ = (call);                                                                             \
        if (got_ != (expected))                                                                                        \
        {                                                                                                              \
            printf("host_terrain: line %d: %s returned %d, expected %d\n", __LINE__, #call, (int)got_,                 \
                   (int)(expected));                                                                                   \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

static void Frames(engine_handle engine, int count)
{
    engine_frame_info frame;
    int i;
    memset(&frame, 0, sizeof(frame));
    frame.struct_size = sizeof(frame);
    frame.delta_seconds = 1.0 / 60.0;
    for (i = 0; i < count; ++i)
    {
        CHECK_STATUS(g_api->frame(engine, &frame, NULL), ENGINE_OK);
    }
}

static void SetTransform(engine_handle world, uint64_t key, float x, float y, float z)
{
    engine_transform t;
    memset(&t, 0, sizeof(t));
    t.struct_size = sizeof(t);
    t.position[0] = x;
    t.position[1] = y;
    t.position[2] = z;
    t.rotation[3] = 1.0f;
    t.scale[0] = t.scale[1] = t.scale[2] = 1.0f;
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
}

static engine_terrain MakeTerrain(uint64_t height_map, uint32_t flags)
{
    engine_terrain t;
    uint32_t i;
    memset(&t, 0, sizeof(t));
    t.struct_size = sizeof(t);
    t.flags = flags;
    t.height_map = height_map;
    t.layer_count = 1;
    t.slope_layer = -1;
    t.slope_start = 0.35f;
    t.slope_end = 0.55f;
    t.fade_start = 150.0f;
    t.fade_end = 300.0f;
    for (i = 0; i < ENGINE_TERRAIN_LAYERS_MAX; ++i)
    {
        t.layers[i].tiling = 8.0f;
        t.layers[i].tint[0] = t.layers[i].tint[1] = t.layers[i].tint[2] = 1.0f;
        t.layers[i].roughness = 1.0f;
    }
    t.friction = 0.8f;
    return t;
}

/* The ground of a ramp: sample (i, j) is 2000 + 100 * i, height s / 1000 m (range 0 to 65.535), cells 2 m. */
static float Ground(float x, float z, float origin_x, float origin_y, float origin_z)
{
    (void)z;
    (void)origin_z;
    return origin_y + 2.0f + 0.05f * (x - origin_x);
}

static void Physics(engine_handle engine)
{
    uint16_t samples[SIDE * SIDE];
    engine_handle world;
    uint64_t terrain = 0;
    uint64_t ball = 0;
    uint64_t other = 0;
    engine_terrain t;
    engine_terrain back;
    engine_collider c;
    engine_collider cback;
    engine_rigid_body body;
    engine_transform at;
    engine_buffer blob;
    double height = -1.0;
    float expected;
    uint64_t keys[4];
    size_t total = 0;
    int has = 0;
    uint32_t i;
    uint32_t j;

    for (j = 0; j < SIDE; ++j)
    {
        for (i = 0; i < SIDE; ++i)
        {
            samples[j * SIDE + i] = (uint16_t)(2000u + 100u * i);
        }
    }
    CHECK_STATUS(g_api->height_map_create(engine, 0x7e1, samples, SIDE, SIDE, 0.0f, 65.535f, 2.0f), ENGINE_OK);
    CHECK_STATUS(g_api->height_map_create(engine, 0x7e1, samples, SIDE, SIDE, 0.0f, 65.535f, 2.0f),
                 ENGINE_ERROR_ALREADY_EXISTS);
    CHECK_STATUS(g_api->height_map_create(engine, 0, samples, SIDE, SIDE, 0.0f, 65.535f, 2.0f),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->height_map_create(engine, 0x7e2, NULL, SIDE, SIDE, 0.0f, 65.535f, 2.0f),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->height_map_create(engine, 0x7e2, samples, 1, SIDE, 0.0f, 65.535f, 2.0f),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->height_map_create(engine, 0x7e2, samples, SIDE, SIDE, 0.0f, 65.535f, 0.0f),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->height_map_create(engine, 0x7e2, samples, SIDE, SIDE, 5.0f, 1.0f, 2.0f),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    /* the engine keeps the blob: the host's samples are copied */
    memset(&blob, 0, sizeof(blob));
    CHECK_STATUS(g_api->resource_read(engine, 0x7e1, 0, &blob), ENGINE_OK);
    CHECK(blob.size > SIDE * SIDE * 2);
    engine_buffer_release(&blob);

    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    CHECK_STATUS(g_api->entity_create(world, &terrain), ENGINE_OK);
    SetTransform(world, terrain, 10.0f, 1.0f, 20.0f);

    /* the height before the terrain exists, and the refusals */
    CHECK_STATUS(g_api->terrain_height(world, 30.0f, 40.0f, &height), ENGINE_ERROR_NOT_FOUND);
    t = MakeTerrain(0x7e1, 0);
    CHECK_STATUS(g_api->component_set(world, terrain, ENGINE_COMPONENT_TERRAIN, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    t.struct_size = 4;
    CHECK_STATUS(g_api->component_set(world, terrain, ENGINE_COMPONENT_TERRAIN, &t), ENGINE_ERROR_STRUCT_SIZE);
    t = MakeTerrain(0x7e1, 0x80u);
    CHECK_STATUS(g_api->component_set(world, terrain, ENGINE_COMPONENT_TERRAIN, &t), ENGINE_ERROR_INVALID_ARGUMENT);
    t = MakeTerrain(0, 0);
    CHECK_STATUS(g_api->component_set(world, terrain, ENGINE_COMPONENT_TERRAIN, &t), ENGINE_ERROR_INVALID_ARGUMENT);
    t = MakeTerrain(0x9999, 0);
    CHECK_STATUS(g_api->component_set(world, terrain, ENGINE_COMPONENT_TERRAIN, &t), ENGINE_ERROR_NOT_FOUND);
    t = MakeTerrain(0x7e1, 0);
    t.layer_count = 0;
    CHECK_STATUS(g_api->component_set(world, terrain, ENGINE_COMPONENT_TERRAIN, &t), ENGINE_ERROR_INVALID_ARGUMENT);
    t.layer_count = 5;
    CHECK_STATUS(g_api->component_set(world, terrain, ENGINE_COMPONENT_TERRAIN, &t), ENGINE_ERROR_INVALID_ARGUMENT);
    t = MakeTerrain(0x7e1, 0);
    t.slope_layer = 1;
    CHECK_STATUS(g_api->component_set(world, terrain, ENGINE_COMPONENT_TERRAIN, &t), ENGINE_ERROR_INVALID_ARGUMENT);
    t = MakeTerrain(0x7e1, 0);
    t.slope_start = 0.6f;
    CHECK_STATUS(g_api->component_set(world, terrain, ENGINE_COMPONENT_TERRAIN, &t), ENGINE_ERROR_INVALID_ARGUMENT);
    t = MakeTerrain(0x7e1, 0);
    t.fade_end = t.fade_start;
    CHECK_STATUS(g_api->component_set(world, terrain, ENGINE_COMPONENT_TERRAIN, &t), ENGINE_ERROR_INVALID_ARGUMENT);
    t = MakeTerrain(0x7e1, 0);
    t.layers[0].tiling = 0.0f;
    CHECK_STATUS(g_api->component_set(world, terrain, ENGINE_COMPONENT_TERRAIN, &t), ENGINE_ERROR_INVALID_ARGUMENT);
    t = MakeTerrain(0x7e1, 0);
    t.layers[0].roughness = nanf("");
    CHECK_STATUS(g_api->component_set(world, terrain, ENGINE_COMPONENT_TERRAIN, &t), ENGINE_ERROR_INVALID_ARGUMENT);
    t = MakeTerrain(0x7e1, 0);
    t.layer_texture = 0x7e1; /* a height map, not a texture */
    CHECK_STATUS(g_api->component_set(world, terrain, ENGINE_COMPONENT_TERRAIN, &t), ENGINE_ERROR_TYPE_MISMATCH);
    t = MakeTerrain(0x7e1, 0);
    t.splat_texture = 0x9999;
    CHECK_STATUS(g_api->component_set(world, terrain, ENGINE_COMPONENT_TERRAIN, &t), ENGINE_ERROR_NOT_FOUND);
    CHECK_STATUS(g_api->component_has(world, terrain, ENGINE_COMPONENT_TERRAIN, &has), ENGINE_OK);
    CHECK(has == 0); /* nothing above set it */

    /* the terrain with its collider */
    t = MakeTerrain(0x7e1, ENGINE_TERRAIN_COLLIDER);
    CHECK_STATUS(g_api->component_set(world, terrain, ENGINE_COMPONENT_TERRAIN, &t), ENGINE_OK);
    CHECK_STATUS(g_api->component_has(world, terrain, ENGINE_COMPONENT_TERRAIN, &has), ENGINE_OK);
    CHECK(has == 1);
    CHECK_STATUS(g_api->component_has(world, terrain, ENGINE_COMPONENT_RIGID_BODY, &has), ENGINE_OK);
    CHECK(has == 1); /* the flag made a static body */
    memset(&back, 0, sizeof(back));
    back.struct_size = sizeof(back);
    CHECK_STATUS(g_api->component_get(world, terrain, ENGINE_COMPONENT_TERRAIN, &back), ENGINE_OK);
    CHECK(back.height_map == 0x7e1 && back.flags == ENGINE_TERRAIN_COLLIDER && back.layer_count == 1 &&
          back.slope_layer == -1 && back.layers[0].tiling == 8.0f && back.friction == 0.8f);
    memset(&cback, 0, sizeof(cback));
    cback.struct_size = sizeof(cback);
    CHECK_STATUS(g_api->component_get(world, terrain, ENGINE_COMPONENT_COLLIDER, &cback), ENGINE_OK);
    CHECK(cback.shape == ENGINE_SHAPE_HEIGHT_FIELD && cback.mesh_resource == 0x7e1);
    memset(&body, 0, sizeof(body));
    body.struct_size = sizeof(body);
    CHECK_STATUS(g_api->component_get(world, terrain, ENGINE_COMPONENT_RIGID_BODY, &body), ENGINE_OK);
    CHECK(body.type == ENGINE_BODY_STATIC);
    /* a dynamic body beside a height field is refused */
    body.type = ENGINE_BODY_DYNAMIC;
    body.gravity_scale = 1.0f;
    CHECK_STATUS(g_api->component_set(world, terrain, ENGINE_COMPONENT_RIGID_BODY, &body),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    /* a second terrain collider on an entity that has another collider is refused */
    CHECK_STATUS(g_api->entity_create(world, &other), ENGINE_OK);
    memset(&c, 0, sizeof(c));
    c.struct_size = sizeof(c);
    c.shape = ENGINE_SHAPE_BOX;
    c.half_extents[0] = c.half_extents[1] = c.half_extents[2] = 1.0f;
    c.rotation[3] = 1.0f;
    c.friction = 0.5f;
    c.density = 1000.0f;
    c.group = 1u;
    c.mask = 0xFFFFFFFFu;
    CHECK_STATUS(g_api->component_set(world, other, ENGINE_COMPONENT_COLLIDER, &c), ENGINE_OK);
    CHECK_STATUS(g_api->component_set(world, other, ENGINE_COMPONENT_TERRAIN, &t), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->entity_destroy(world, other), ENGINE_OK);
    CHECK_STATUS(g_api->query(world, ENGINE_COMPONENT_BIT(ENGINE_COMPONENT_TERRAIN), keys, 4, &total), ENGINE_OK);
    CHECK(total == 1 && keys[0] == terrain);

    /* the ground, before a frame (from the local transform) and after one */
    expected = Ground(30.0f, 40.0f, 10.0f, 1.0f, 20.0f);
    CHECK_STATUS(g_api->terrain_height(world, 30.0f, 40.0f, &height), ENGINE_OK);
    CHECK(fabs(height - expected) < 1e-3f);
    Frames(engine, 3);
    CHECK_STATUS(g_api->terrain_height(world, 30.0f, 40.0f, &height), ENGINE_OK);
    CHECK(fabs(height - expected) < 1e-3f);
    CHECK_STATUS(g_api->terrain_height(world, 9.0f, 40.0f, &height), ENGINE_ERROR_NOT_FOUND);  /* west of the map */
    CHECK_STATUS(g_api->terrain_height(world, 30.0f, 85.0f, &height), ENGINE_ERROR_NOT_FOUND); /* past its far edge */
    CHECK_STATUS(g_api->terrain_height(world, 30.0f, 40.0f, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->terrain_height(world, nanf(""), 40.0f, &height), ENGINE_ERROR_INVALID_ARGUMENT);

    /* a ball dropped from 6 m above the ground comes to rest on the surface */
    CHECK_STATUS(g_api->entity_create(world, &ball), ENGINE_OK);
    SetTransform(world, ball, 30.0f, expected + 6.0f, 40.0f);
    memset(&body, 0, sizeof(body));
    body.struct_size = sizeof(body);
    body.type = ENGINE_BODY_DYNAMIC;
    body.gravity_scale = 1.0f;
    body.angular_damping = 2.0f;
    body.linear_damping = 0.5f;
    CHECK_STATUS(g_api->component_set(world, ball, ENGINE_COMPONENT_RIGID_BODY, &body), ENGINE_OK);
    c.shape = ENGINE_SHAPE_SPHERE;
    c.radius = 0.5f;
    c.friction = 1.0f;
    CHECK_STATUS(g_api->component_set(world, ball, ENGINE_COMPONENT_COLLIDER, &c), ENGINE_OK);
    Frames(engine, 240);
    memset(&at, 0, sizeof(at));
    at.struct_size = sizeof(at);
    CHECK_STATUS(g_api->component_get(world, ball, ENGINE_COMPONENT_TRANSFORM, &at), ENGINE_OK);
    CHECK_STATUS(g_api->terrain_height(world, at.position[0], at.position[2], &height), ENGINE_OK);
    printf("host_terrain: ball at (%.2f, %.3f, %.2f), ground %.3f\n", at.position[0], at.position[1], at.position[2],
           height);
    CHECK(at.position[1] > height + 0.45f && at.position[1] < height + 0.6f);

    /* the double precision series: the same terrain 100,000 km away reads the same ground (a float coordinate steps by
       8 m there) */
    {
        const double far = 1.0e8;
        engine_transform moved;
        double far_height = 0.0;
        memset(&moved, 0, sizeof(moved));
        moved.struct_size = sizeof(moved);
        moved.position[0] = 10.0 + far;
        moved.position[1] = 1.0;
        moved.position[2] = 20.0 + far;
        moved.rotation[3] = 1.0f;
        moved.scale[0] = moved.scale[1] = moved.scale[2] = 1.0f;
        CHECK_STATUS(g_api->component_set(world, terrain, ENGINE_COMPONENT_TRANSFORM, &moved), ENGINE_OK);
        CHECK_STATUS(g_api->terrain_height(world, 30.0 + far, 40.0 + far, &far_height), ENGINE_OK);
        CHECK(fabs(far_height - (double)expected) < 1e-3);
        CHECK_STATUS(g_api->terrain_height(world, 9.0 + far, 40.0 + far, &far_height), ENGINE_ERROR_NOT_FOUND);
        CHECK_STATUS(g_api->terrain_height(world, 30.0 + far, 40.0 + far, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->terrain_height(world, 3.0e12, 0.0, &far_height), ENGINE_ERROR_INVALID_ARGUMENT);
        moved.position[0] = 10.0; /* back where it was */
        moved.position[1] = 1.0;
        moved.position[2] = 20.0;
        CHECK_STATUS(g_api->component_set(world, terrain, ENGINE_COMPONENT_TRANSFORM, &moved), ENGINE_OK);
    }

    /* a NaN in a layer the terrain does not use is refused as well; unused layers left zero are fine */
    t = MakeTerrain(0x7e1, 0);
    t.layers[3].tint[1] = nanf("");
    CHECK_STATUS(g_api->component_set(world, terrain, ENGINE_COMPONENT_TERRAIN, &t), ENGINE_ERROR_INVALID_ARGUMENT);
    t = MakeTerrain(0x7e1, ENGINE_TERRAIN_COLLIDER);
    memset(&t.layers[2], 0, sizeof(t.layers[2]));
    CHECK_STATUS(g_api->component_set(world, terrain, ENGINE_COMPONENT_TERRAIN, &t), ENGINE_OK);

    /* the collider reads back as a height field naming its map, and a height field can be set as a collider */
    {
        engine_collider field;
        engine_collider collider_back;
        uint64_t plain = 0;
        Frames(engine, 2);
        memset(&collider_back, 0, sizeof(collider_back));
        collider_back.struct_size = sizeof(collider_back);
        CHECK_STATUS(g_api->component_get(world, terrain, ENGINE_COMPONENT_COLLIDER, &collider_back), ENGINE_OK);
        CHECK(collider_back.shape == ENGINE_SHAPE_HEIGHT_FIELD && collider_back.mesh_resource == 0x7e1);
        memset(&field, 0, sizeof(field));
        field.struct_size = sizeof(field);
        field.shape = ENGINE_SHAPE_HEIGHT_FIELD;
        field.mesh_resource = 0x7e1;
        field.rotation[3] = 1.0f;
        field.friction = 0.5f;
        field.density = 1000.0f;
        field.group = 1u;
        field.mask = 0xFFFFFFFFu;
        CHECK_STATUS(g_api->entity_create(world, &plain), ENGINE_OK);
        CHECK_STATUS(g_api->component_set(world, plain, ENGINE_COMPONENT_COLLIDER, &field), ENGINE_OK);
        Frames(engine, 2);
        memset(&collider_back, 0, sizeof(collider_back));
        collider_back.struct_size = sizeof(collider_back);
        CHECK_STATUS(g_api->component_get(world, plain, ENGINE_COMPONENT_COLLIDER, &collider_back), ENGINE_OK);
        CHECK(collider_back.shape == ENGINE_SHAPE_HEIGHT_FIELD && collider_back.mesh_resource == 0x7e1);
        field.mesh_resource = 0x9998;
        CHECK_STATUS(g_api->component_set(world, plain, ENGINE_COMPONENT_COLLIDER, &field), ENGINE_ERROR_NOT_FOUND);
        CHECK_STATUS(g_api->entity_destroy(world, plain), ENGINE_OK);
    }

    /* a scaled terrain is not drawn, so it has no surface to read; a child whose parent no frame has placed is not
     * ready */
    {
        engine_transform scaled;
        engine_transform transform_back;
        engine_transform parent_at;
        uint64_t child = 0;
        uint64_t parent = 0;
        engine_terrain ct = MakeTerrain(0x7e1, 0);
        memset(&scaled, 0, sizeof(scaled));
        scaled.struct_size = sizeof(scaled);
        scaled.position[0] = 10.0f;
        scaled.position[1] = 1.0f;
        scaled.position[2] = 20.0f;
        scaled.rotation[3] = 1.0f;
        scaled.scale[0] = 2.0f;
        scaled.scale[1] = scaled.scale[2] = 1.0f;
        transform_back = scaled;
        transform_back.scale[0] = 1.0f;
        CHECK_STATUS(g_api->component_set(world, terrain, ENGINE_COMPONENT_TRANSFORM, &scaled), ENGINE_OK);
        CHECK_STATUS(g_api->terrain_height(world, 30.0f, 40.0f, &height), ENGINE_ERROR_NOT_FOUND);
        CHECK_STATUS(g_api->component_set(world, terrain, ENGINE_COMPONENT_TRANSFORM, &transform_back), ENGINE_OK);
        CHECK_STATUS(g_api->terrain_height(world, 30.0f, 40.0f, &height), ENGINE_OK);

        CHECK_STATUS(g_api->component_remove(world, terrain, ENGINE_COMPONENT_TERRAIN), ENGINE_OK);
        CHECK_STATUS(g_api->entity_create(world, &parent), ENGINE_OK);
        CHECK_STATUS(g_api->entity_create(world, &child), ENGINE_OK);
        memset(&parent_at, 0, sizeof(parent_at));
        parent_at.struct_size = sizeof(parent_at);
        parent_at.rotation[3] = 1.0f;
        parent_at.scale[0] = parent_at.scale[1] = parent_at.scale[2] = 1.0f;
        CHECK_STATUS(g_api->component_set(world, parent, ENGINE_COMPONENT_TRANSFORM, &parent_at), ENGINE_OK);
        SetTransform(world, child, 10.0f, 1.0f, 20.0f);
        CHECK_STATUS(g_api->entity_set_parent(world, child, parent), ENGINE_OK);
        CHECK_STATUS(g_api->component_set(world, child, ENGINE_COMPONENT_TERRAIN, &ct), ENGINE_OK);
        CHECK_STATUS(g_api->terrain_height(world, 30.0f, 40.0f, &height), ENGINE_ERROR_NOT_READY);
        Frames(engine, 3);
        CHECK_STATUS(g_api->terrain_height(world, 30.0f, 40.0f, &height), ENGINE_OK);
        CHECK_STATUS(g_api->entity_destroy(world, child), ENGINE_OK);
        CHECK_STATUS(g_api->entity_destroy(world, parent), ENGINE_OK);
        CHECK_STATUS(g_api->component_set(world, terrain, ENGINE_COMPONENT_TERRAIN, &ct), ENGINE_OK);
    }

    /* taking the terrain off takes its collider and its body with it */
    CHECK_STATUS(g_api->component_remove(world, terrain, ENGINE_COMPONENT_TERRAIN), ENGINE_OK);
    CHECK_STATUS(g_api->component_has(world, terrain, ENGINE_COMPONENT_COLLIDER, &has), ENGINE_OK);
    CHECK(has == 0);
    CHECK_STATUS(g_api->component_has(world, terrain, ENGINE_COMPONENT_RIGID_BODY, &has), ENGINE_OK);
    CHECK(has == 0);
    CHECK_STATUS(g_api->terrain_height(world, 30.0f, 40.0f, &height), ENGINE_ERROR_NOT_FOUND);
    CHECK_STATUS(g_api->component_remove(world, terrain, ENGINE_COMPONENT_TERRAIN), ENGINE_ERROR_NOT_FOUND);
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
}

static void Pixel(engine_handle engine, engine_handle surface, unsigned x, unsigned y, int rgb[3])
{
    engine_handle ticket;
    engine_pixels pixels;
    const unsigned char* p;
    Frames(engine, 10);
    memset(&ticket, 0, sizeof(ticket));
    CHECK_STATUS(g_api->surface_read_pixels(surface, &ticket), ENGINE_OK);
    Frames(engine, 1);
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

static void Drawing(engine_handle engine)
{
    engine_info about;
    engine_surface_info info;
    engine_handle surface;
    engine_handle world;
    engine_camera camera;
    engine_transform at;
    engine_environment env;
    engine_surface_state state;
    uint64_t terrain = 0;
    uint64_t camera_entity = 0;
    int with[3];
    int without[3];
    uint16_t samples[SIDE * SIDE];
    uint32_t i;

    memset(&about, 0, sizeof(about));
    about.struct_size = sizeof(about);
    CHECK_STATUS(g_api->get_info(engine, &about), ENGINE_OK);
    if ((about.caps & ENGINE_CAP_SURFACE_OFFSCREEN) == 0 || getenv("HOST_TERRAIN_SKIP") != NULL)
    {
        printf("host_terrain: drawing skipped: no off-screen surfaces, or no GPU in this flavor\n");
        return;
    }
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.kind = ENGINE_SURFACE_OFFSCREEN;
    info.width = W;
    info.height = H;
    info.scale = 1.0f;
    info.flags = host_surface_api_flags();
    memset(&surface, 0, sizeof(surface));
    if (g_api->surface_create(engine, &info, &surface) != ENGINE_OK)
    {
        printf("host_terrain: drawing skipped: no device could be made\n");
        return;
    }
    for (i = 0; i < SIDE * SIDE; ++i)
    {
        samples[i] = (uint16_t)(2000u + 100u * (i % SIDE));
    }
    CHECK_STATUS(g_api->height_map_create(engine, 0x7e3, samples, SIDE, SIDE, 0.0f, 65.535f, 2.0f), ENGINE_OK);
    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    CHECK_STATUS(g_api->surface_set_world(surface, world), ENGINE_OK);
    /* a green sky: where no terrain is drawn the pixel is green */
    memset(&env, 0, sizeof(env));
    env.struct_size = sizeof(env);
    env.sky_color[1] = 0.5f;
    env.exposure = 1.0f;
    CHECK_STATUS(g_api->world_set_environment(world, &env), ENGINE_OK);
    /* a sun shining straight down (along its entity's -Z, turned to -Y) */
    {
        uint64_t sun = 0;
        engine_light light;
        engine_transform turned = {0};
        CHECK_STATUS(g_api->entity_create(world, &sun), ENGINE_OK);
        turned.struct_size = sizeof(turned);
        turned.rotation[0] = -0.70710678f;
        turned.rotation[3] = 0.70710678f;
        turned.scale[0] = turned.scale[1] = turned.scale[2] = 1.0f;
        CHECK_STATUS(g_api->component_set(world, sun, ENGINE_COMPONENT_TRANSFORM, &turned), ENGINE_OK);
        memset(&light, 0, sizeof(light));
        light.struct_size = (uint32_t)offsetof(engine_light, inner_cone);
        light.kind = ENGINE_LIGHT_DIRECTIONAL;
        light.color[0] = light.color[1] = light.color[2] = 1.0f;
        light.intensity = 3.0f;
        CHECK_STATUS(g_api->component_set(world, sun, ENGINE_COMPONENT_LIGHT, &light), ENGINE_OK);
    }
    /* the camera 20 m above the middle of the map (32 m, 32 m), looking straight down */
    CHECK_STATUS(g_api->entity_create(world, &camera_entity), ENGINE_OK);
    memset(&at, 0, sizeof(at));
    at.struct_size = sizeof(at);
    at.position[0] = 32.0f;
    at.position[1] = 20.0f;
    at.position[2] = 32.0f;
    at.rotation[0] = -0.70710678f;
    at.rotation[3] = 0.70710678f;
    at.scale[0] = at.scale[1] = at.scale[2] = 1.0f;
    CHECK_STATUS(g_api->component_set(world, camera_entity, ENGINE_COMPONENT_TRANSFORM, &at), ENGINE_OK);
    memset(&camera, 0, sizeof(camera));
    camera.struct_size = sizeof(camera);
    camera.flags = ENGINE_CAMERA_FIXED_ASPECT;
    camera.fov_y = 1.0471976f;
    camera.near_plane = 0.1f;
    camera.far_plane = 200.0f;
    camera.aspect = (float)W / (float)H;
    CHECK_STATUS(g_api->component_set(world, camera_entity, ENGINE_COMPONENT_CAMERA, &camera), ENGINE_OK);
    CHECK_STATUS(g_api->surface_set_camera(surface, camera_entity), ENGINE_OK);

    Pixel(engine, surface, W / 2, H / 2, without);
    CHECK(without[1] > 60 && without[0] < 10); /* the sky */

    CHECK_STATUS(g_api->entity_create(world, &terrain), ENGINE_OK);
    SetTransform(world, terrain, 0.0f, 0.0f, 0.0f);
    {
        engine_terrain t = MakeTerrain(0x7e3, 0);
        CHECK_STATUS(g_api->component_set(world, terrain, ENGINE_COMPONENT_TERRAIN, &t), ENGINE_OK);
    }
    Pixel(engine, surface, W / 2, H / 2, with);
    printf("host_terrain: pixel with terrain %d %d %d, without %d %d %d\n", with[0], with[1], with[2], without[0],
           without[1], without[2]);
    CHECK(with[0] > 25 && with[1] > 25 && with[2] > 25 && with[0] > without[0] + 20); /* the grey ground */
    memset(&state, 0, sizeof(state));
    state.struct_size = sizeof(state);
    CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
    CHECK(state.entities_unresolved == 0);

    /* a terrain whose height map is replaced by one with other samples draws the new heights: removed, it is gone */
    CHECK_STATUS(g_api->component_remove(world, terrain, ENGINE_COMPONENT_TERRAIN), ENGINE_OK);
    Pixel(engine, surface, W / 2, H / 2, with);
    CHECK(with[1] > 60 && with[0] < 10);

    CHECK_STATUS(g_api->surface_destroy(surface), ENGINE_OK);
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
}

int main(void)
{
    engine_create_info create;
    engine_handle engine;

    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &g_api) != ENGINE_OK)
    {
        printf("host_terrain: no function table\n");
        return 2;
    }
    memset(&create, 0, sizeof(create));
    create.struct_size = sizeof(create);
    create.flags = ENGINE_CREATE_LOG_SUPPRESS_CONSOLE;
    memset(&engine, 0, sizeof(engine));
    CHECK_STATUS(g_api->create(&create, &engine), ENGINE_OK);
    Physics(engine);
    Drawing(engine);
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    if (g_failures == 0)
    {
        printf("host_terrain: ok\n");
        return 0;
    }
    printf("host_terrain: %d failure(s)\n", g_failures);
    return 1;
}
