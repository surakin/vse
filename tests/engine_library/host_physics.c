/* Phase 25 Day 408: a C program puts physical entities in a world through the function table: a rigid body and a
   collider on entities, the frame that steps them, forces and impulses, velocities and teleports, ray casts and
   overlaps, the physics settings, and every refusal the API documents. */
#include <math.h>
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
            printf("host_physics: line %d: CHECK failed: %s\n", __LINE__, #condition);                                 \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

#define CHECK_STATUS(call, expected)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status got_ = (call);                                                                             \
        if (got_ != (expected))                                                                                        \
        {                                                                                                              \
            printf("host_physics: line %d: %s returned %d, expected %d\n", __LINE__, #call, (int)got_,                 \
                   (int)(expected));                                                                                   \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

static engine_string S(const char* text)
{
    engine_string s;
    s.data = text;
    s.length = strlen(text);
    return s;
}

static engine_handle NewEngine(const engine_setting_entry* entries, size_t count)
{
    engine_create_info info;
    engine_handle engine;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.settings = entries;
    info.setting_count = count;
    memset(&engine, 0, sizeof(engine));
    CHECK_STATUS(g_api->create(&info, &engine), ENGINE_OK);
    return engine;
}

static void Frames(engine_handle engine, int count, double dt)
{
    engine_frame_info info;
    int i;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.delta_seconds = dt;
    for (i = 0; i < count; ++i)
    {
        CHECK_STATUS(g_api->frame(engine, &info, NULL), ENGINE_OK);
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

static float PositionY(engine_handle world, uint64_t key)
{
    engine_transform t;
    memset(&t, 0, sizeof(t));
    t.struct_size = sizeof(t);
    CHECK_STATUS(g_api->component_get(world, key, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
    return t.position[1];
}

static void SetBody(engine_handle world, uint64_t key, uint32_t type, float mass)
{
    engine_rigid_body b;
    memset(&b, 0, sizeof(b));
    b.struct_size = sizeof(b);
    b.type = type;
    b.mass = mass;
    b.angular_damping = 0.05f;
    b.gravity_scale = 1.0f;
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_RIGID_BODY, &b), ENGINE_OK);
}

static engine_collider MakeCollider(uint32_t shape)
{
    engine_collider c;
    memset(&c, 0, sizeof(c));
    c.struct_size = sizeof(c);
    c.shape = shape;
    c.radius = 0.5f;
    c.half_height = 0.5f;
    c.half_extents[0] = c.half_extents[1] = c.half_extents[2] = 0.5f;
    c.rotation[3] = 1.0f;
    c.friction = 0.6f;
    c.restitution = 0.0f;
    c.density = 1000.0f;
    c.group = 1u;
    c.mask = 0xFFFFFFFFu;
    return c;
}

static uint64_t MakeFloor(engine_handle world)
{
    uint64_t key = 0;
    engine_collider c = MakeCollider(ENGINE_SHAPE_BOX);
    CHECK_STATUS(g_api->entity_create(world, &key), ENGINE_OK);
    SetTransform(world, key, 0.0f, -0.5f, 0.0f);
    SetBody(world, key, ENGINE_BODY_STATIC, 0.0f);
    c.half_extents[0] = 20.0f;
    c.half_extents[1] = 0.5f;
    c.half_extents[2] = 20.0f;
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_COLLIDER, &c), ENGINE_OK);
    return key;
}

static uint64_t MakeBall(engine_handle world, float y, float restitution)
{
    uint64_t key = 0;
    engine_collider c = MakeCollider(ENGINE_SHAPE_SPHERE);
    CHECK_STATUS(g_api->entity_create(world, &key), ENGINE_OK);
    SetTransform(world, key, 0.0f, y, 0.0f);
    SetBody(world, key, ENGINE_BODY_DYNAMIC, 0.0f);
    c.restitution = restitution;
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_COLLIDER, &c), ENGINE_OK);
    return key;
}

static void TestArguments(engine_handle engine)
{
    engine_handle world;
    engine_handle counter;
    uint64_t key = 0;
    uint64_t gone = 0;
    engine_rigid_body b;
    engine_collider c;
    float v3[3] = {0.0f, 1.0f, 0.0f};
    float nan3[3];
    float q[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    engine_ray_hit hit;
    int found = 7;
    size_t total = 9;
    uint64_t keys[4];

    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    CHECK_STATUS(g_api->entity_create(world, &key), ENGINE_OK);
    CHECK_STATUS(g_api->entity_create(world, &gone), ENGINE_OK);
    CHECK_STATUS(g_api->entity_destroy(world, gone), ENGINE_OK);
    nan3[0] = nanf("");
    nan3[1] = 0.0f;
    nan3[2] = 0.0f;

    /* the components */
    memset(&b, 0, sizeof(b));
    b.struct_size = sizeof(b);
    b.type = ENGINE_BODY_DYNAMIC;
    b.gravity_scale = 1.0f;
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_RIGID_BODY, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    b.struct_size = 4;
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_RIGID_BODY, &b), ENGINE_ERROR_STRUCT_SIZE);
    b.struct_size = sizeof(b);
    b.type = 3;
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_RIGID_BODY, &b), ENGINE_ERROR_INVALID_ARGUMENT);
    b.type = ENGINE_BODY_DYNAMIC;
    b.mass = nanf("");
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_RIGID_BODY, &b), ENGINE_ERROR_INVALID_ARGUMENT);
    b.mass = 1.0e-30f;
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_RIGID_BODY, &b), ENGINE_ERROR_INVALID_ARGUMENT);
    b.mass = -1.0f;
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_RIGID_BODY, &b), ENGINE_ERROR_INVALID_ARGUMENT);
    b.mass = 2.0f;
    b.flags = 0x80000000u;
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_RIGID_BODY, &b), ENGINE_ERROR_INVALID_ARGUMENT);
    b.flags = 0;
    CHECK_STATUS(g_api->component_set(world, gone, ENGINE_COMPONENT_RIGID_BODY, &b), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_RIGID_BODY, &b), ENGINE_OK);
    memset(&b, 0, sizeof(b));
    b.struct_size = sizeof(b);
    CHECK_STATUS(g_api->component_get(world, key, ENGINE_COMPONENT_RIGID_BODY, &b), ENGINE_OK);
    CHECK(b.type == ENGINE_BODY_DYNAMIC && b.mass == 2.0f);
    b.struct_size = 4;
    CHECK_STATUS(g_api->component_get(world, key, ENGINE_COMPONENT_RIGID_BODY, &b), ENGINE_ERROR_STRUCT_SIZE);
    memset(&c, 0, sizeof(c));
    c.struct_size = 4;
    CHECK_STATUS(g_api->component_get(world, key, ENGINE_COMPONENT_COLLIDER, &c), ENGINE_ERROR_STRUCT_SIZE);

    c = MakeCollider(ENGINE_SHAPE_SPHERE);
    c.radius = 0.0f;
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_COLLIDER, &c), ENGINE_ERROR_INVALID_ARGUMENT);
    c = MakeCollider(ENGINE_SHAPE_BOX);
    c.half_extents[1] = nanf("");
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_COLLIDER, &c), ENGINE_ERROR_INVALID_ARGUMENT);
    c = MakeCollider(ENGINE_SHAPE_CAPSULE);
    c.layer = 32;
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_COLLIDER, &c), ENGINE_ERROR_INVALID_ARGUMENT);
    c = MakeCollider(ENGINE_SHAPE_CAPSULE);
    c.restitution = 2.0f;
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_COLLIDER, &c), ENGINE_ERROR_INVALID_ARGUMENT);
    c = MakeCollider(ENGINE_SHAPE_CAPSULE);
    c.rotation[3] = 0.0f;
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_COLLIDER, &c), ENGINE_ERROR_INVALID_ARGUMENT);
    c = MakeCollider(ENGINE_SHAPE_CAPSULE);
    c.is_trigger = 2;
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_COLLIDER, &c), ENGINE_ERROR_INVALID_ARGUMENT);
    c = MakeCollider(9);
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_COLLIDER, &c), ENGINE_ERROR_UNSUPPORTED);
    c = MakeCollider(ENGINE_SHAPE_CAPSULE);
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_COLLIDER, &c), ENGINE_OK);
    memset(&c, 0, sizeof(c));
    c.struct_size = sizeof(c);
    CHECK_STATUS(g_api->component_get(world, key, ENGINE_COMPONENT_COLLIDER, &c), ENGINE_OK);
    CHECK(c.shape == ENGINE_SHAPE_CAPSULE && c.radius == 0.5f && c.half_height == 0.5f && c.density == 1000.0f);

    /* query on the new bits */
    CHECK_STATUS(g_api->query(world, ENGINE_COMPONENT_BIT(ENGINE_COMPONENT_RIGID_BODY), keys, 4, &total), ENGINE_OK);
    CHECK(total == 1 && keys[0] == key);
    CHECK_STATUS(g_api->query(world, ENGINE_COMPONENT_BIT(ENGINE_COMPONENT_COLLIDER), keys, 4, &total), ENGINE_OK);
    CHECK(total == 1);
    CHECK_STATUS(g_api->query(world, 0x4000u, keys, 4, &total), ENGINE_ERROR_INVALID_ARGUMENT);
    {
        int has = 0;
        CHECK_STATUS(g_api->component_has(world, key, ENGINE_COMPONENT_COLLIDER, &has), ENGINE_OK);
        CHECK(has == 1);
        CHECK_STATUS(g_api->component_remove(world, key, ENGINE_COMPONENT_COLLIDER), ENGINE_OK);
        CHECK_STATUS(g_api->component_has(world, key, ENGINE_COMPONENT_COLLIDER, &has), ENGINE_OK);
        CHECK(has == 0);
        CHECK_STATUS(g_api->component_remove(world, key, ENGINE_COMPONENT_COLLIDER), ENGINE_ERROR_NOT_FOUND);
    }

    /* an entity with no body: the body functions say so */
    CHECK_STATUS(g_api->body_apply_force(world, key, v3, NULL), ENGINE_ERROR_NOT_FOUND);
    CHECK_STATUS(g_api->body_apply_impulse(world, key, v3, NULL), ENGINE_ERROR_NOT_FOUND);
    CHECK_STATUS(g_api->body_set_velocity(world, key, v3, NULL), ENGINE_ERROR_NOT_FOUND);
    CHECK_STATUS(g_api->body_get_velocity(world, key, v3, NULL), ENGINE_ERROR_NOT_FOUND);
    CHECK_STATUS(g_api->body_teleport(world, key, v3, q), ENGINE_ERROR_NOT_FOUND);

    /* arguments */
    CHECK_STATUS(g_api->physics_set_gravity(world, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->physics_set_gravity(world, nan3), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->body_apply_force(world, key, NULL, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->body_apply_force(world, key, nan3, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->body_apply_force(world, key, v3, nan3), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->body_apply_impulse(world, key, nan3, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->body_set_velocity(world, key, nan3, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->body_teleport(world, key, nan3, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->body_apply_force(world, gone, v3, NULL), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(g_api->body_apply_force(world, 0, v3, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    memset(&hit, 0, sizeof(hit));
    hit.struct_size = sizeof(hit);
    CHECK_STATUS(g_api->physics_raycast(world, NULL, v3, 10.0f, 0xFFFFFFFFu, &hit, &found),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->physics_raycast(world, v3, nan3, 10.0f, 0xFFFFFFFFu, &hit, &found),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    {
        const float zero[3] = {0.0f, 0.0f, 0.0f};
        CHECK_STATUS(g_api->physics_raycast(world, v3, zero, 10.0f, 0xFFFFFFFFu, &hit, &found),
                     ENGINE_ERROR_INVALID_ARGUMENT);
    }
    CHECK_STATUS(g_api->physics_raycast(world, v3, v3, 0.0f, 0xFFFFFFFFu, &hit, &found), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->physics_raycast(world, v3, v3, 10.0f, 0xFFFFFFFFu, NULL, &found),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    hit.struct_size = 4;
    CHECK_STATUS(g_api->physics_raycast(world, v3, v3, 10.0f, 0xFFFFFFFFu, &hit, &found), ENGINE_ERROR_STRUCT_SIZE);
    CHECK_STATUS(g_api->physics_overlap_sphere(world, v3, 1.0f, 0xFFFFFFFFu, keys, 4, NULL),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->physics_overlap_sphere(world, v3, -1.0f, 0xFFFFFFFFu, keys, 4, &total),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->physics_overlap_sphere(world, v3, 1.0f, 0xFFFFFFFFu, NULL, 4, &total),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->physics_overlap_box(world, v3, nan3, 0xFFFFFFFFu, keys, 4, &total),
                 ENGINE_ERROR_INVALID_ARGUMENT);

    /* handles of the wrong kind or no longer valid */
    memset(&counter, 0, sizeof(counter));
    CHECK_STATUS(g_api->counter_create(engine, &counter), ENGINE_OK);
    CHECK_STATUS(g_api->physics_set_gravity(counter, v3), ENGINE_ERROR_TYPE_MISMATCH);
    CHECK_STATUS(g_api->body_apply_force(counter, key, v3, NULL), ENGINE_ERROR_TYPE_MISMATCH);
    CHECK_STATUS(g_api->physics_overlap_sphere(counter, v3, 1.0f, 0xFFFFFFFFu, keys, 4, &total),
                 ENGINE_ERROR_TYPE_MISMATCH);
    CHECK_STATUS(g_api->counter_destroy(counter), ENGINE_OK);
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
    CHECK_STATUS(g_api->physics_set_gravity(world, v3), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(g_api->body_apply_force(world, key, v3, NULL), ENGINE_ERROR_STALE_HANDLE);
    hit.struct_size = sizeof(hit);
    CHECK_STATUS(g_api->physics_raycast(world, v3, v3, 1.0f, 1u, &hit, &found), ENGINE_ERROR_STALE_HANDLE);
}

static void TestBallBounces(engine_handle engine)
{
    engine_handle world;
    uint64_t floor_key;
    uint64_t ball;
    int frame;
    float lowest = 100.0f;
    float highest_after = 0.0f;
    int bounced = 0;
    float previous;
    engine_frame_stats stats;
    engine_frame_info info;

    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    floor_key = MakeFloor(world);
    ball = MakeBall(world, 3.0f, 0.8f);
    { /* the floor needs bounce too: restitution combines */
        engine_collider c;
        memset(&c, 0, sizeof(c));
        c.struct_size = sizeof(c);
        CHECK_STATUS(g_api->component_get(world, floor_key, ENGINE_COMPONENT_COLLIDER, &c), ENGINE_OK);
        c.restitution = 0.8f;
        CHECK_STATUS(g_api->component_set(world, floor_key, ENGINE_COMPONENT_COLLIDER, &c), ENGINE_OK);
    }
    previous = PositionY(world, ball);
    for (frame = 0; frame < 240; ++frame)
    {
        float y;
        Frames(engine, 1, 1.0 / 60.0);
        y = PositionY(world, ball);
        if (y < lowest)
        {
            lowest = y;
        }
        if (y > previous && lowest < 0.6f)
        {
            bounced = 1;
            if (y > highest_after)
            {
                highest_after = y;
            }
        }
        previous = y;
    }
    CHECK(lowest > 0.4f && lowest < 0.56f); /* the ball rests on the floor with its radius of 0.5 */
    CHECK(bounced == 1);
    CHECK(highest_after > 1.0f); /* it rose again after the first hit */

    /* the frame reports the physics */
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.delta_seconds = 1.0 / 60.0;
    memset(&stats, 0, sizeof(stats));
    stats.struct_size = sizeof(stats);
    CHECK_STATUS(g_api->frame(engine, &info, &stats), ENGINE_OK);
    CHECK(stats.physics_bodies == 2);
    CHECK(stats.physics_steps == 1);
    /* a host built before the physics fields passes the older size and is served */
    memset(&stats, 0, sizeof(stats));
    stats.struct_size = 32;
    CHECK_STATUS(g_api->frame(engine, &info, &stats), ENGINE_OK);
    CHECK(stats.struct_size == 32 && stats.worlds_run == 1);

    /* destroying the ball takes its body away */
    CHECK_STATUS(g_api->entity_destroy(world, ball), ENGINE_OK);
    Frames(engine, 1, 1.0 / 60.0);
    memset(&stats, 0, sizeof(stats));
    stats.struct_size = sizeof(stats);
    CHECK_STATUS(g_api->frame(engine, &info, &stats), ENGINE_OK);
    CHECK(stats.physics_bodies == 1);
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
}

static void TestImpulseAndQueries(engine_handle engine)
{
    engine_handle world;
    uint64_t floor_key;
    uint64_t box = 0;
    engine_collider c = MakeCollider(ENGINE_SHAPE_BOX);
    float no_gravity[3] = {0.0f, 0.0f, 0.0f};
    float impulse[3] = {5.0f, 0.0f, 0.0f};
    float linear[3] = {0.0f, 0.0f, 0.0f};
    float angular[3] = {0.0f, 0.0f, 0.0f};
    float down[3] = {0.0f, -1.0f, 0.0f};
    float origin[3] = {0.0f, 5.0f, 0.0f};
    float centre[3] = {0.0f, 0.5f, 0.0f};
    float half[3] = {0.25f, 0.25f, 0.25f};
    float to[3] = {7.0f, 0.5f, 0.0f};
    engine_ray_hit hit;
    int found = 0;
    size_t total = 0;
    uint64_t keys[4];
    float pos[3] = {3.0f, 2.0f, 1.0f};
    engine_transform t;
    engine_frame_info info;
    engine_frame_stats stats;

    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    floor_key = MakeFloor(world);
    CHECK_STATUS(g_api->entity_create(world, &box), ENGINE_OK);
    SetTransform(world, box, 0.0f, 0.5f, 0.0f);
    SetBody(world, box, ENGINE_BODY_DYNAMIC, 2.0f);
    CHECK_STATUS(g_api->component_set(world, box, ENGINE_COMPONENT_COLLIDER, &c), ENGINE_OK);

    /* the impulse changes the velocity at once (the body is made by the first call that needs it) */
    CHECK_STATUS(g_api->physics_set_gravity(world, no_gravity), ENGINE_OK);
    CHECK_STATUS(g_api->body_apply_impulse(world, box, impulse, NULL), ENGINE_OK);
    CHECK_STATUS(g_api->body_get_velocity(world, box, linear, angular), ENGINE_OK);
    CHECK(fabsf(linear[0] - 2.5f) < 1.0e-4f && fabsf(linear[1]) < 1.0e-4f);
    Frames(engine, 30, 1.0 / 60.0);
    memset(&t, 0, sizeof(t));
    t.struct_size = sizeof(t);
    CHECK_STATUS(g_api->component_get(world, box, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
    CHECK(t.position[0] > 1.0f && t.position[0] < 1.5f); /* about 2.5 m/s for half a second, a little damped */

    /* a force pushes over the following steps; a velocity set is what the body has */
    {
        const float zero[3] = {0.0f, 0.0f, 0.0f};
        const float force[3] = {0.0f, 40.0f, 0.0f};
        CHECK_STATUS(g_api->body_set_velocity(world, box, zero, zero), ENGINE_OK);
        CHECK_STATUS(g_api->body_apply_force(world, box, force, NULL), ENGINE_OK);
        Frames(engine, 1, 1.0 / 60.0);
        CHECK_STATUS(g_api->body_get_velocity(world, box, linear, NULL), ENGINE_OK);
        CHECK(linear[1] > 0.2f && linear[1] < 0.4f); /* 40 N on 2 kg for 1/60 s: 0.33 m/s */
    }
    /* a force on a static body is refused */
    CHECK_STATUS(g_api->body_apply_force(world, floor_key, impulse, NULL), ENGINE_ERROR_UNSUPPORTED);

    /* a teleport moves the body and the entity at once */
    CHECK_STATUS(g_api->body_teleport(world, box, pos, NULL), ENGINE_OK);
    CHECK_STATUS(g_api->component_get(world, box, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
    CHECK(t.position[0] == 3.0f && t.position[1] == 2.0f && t.position[2] == 1.0f);
    CHECK_STATUS(g_api->body_teleport(world, box, centre, NULL), ENGINE_OK);
    CHECK_STATUS(g_api->body_set_velocity(world, box, no_gravity, no_gravity), ENGINE_OK);

    /* a ray from above hits the top of the floor at 5 m, with the normal up */
    memset(&hit, 0, sizeof(hit));
    hit.struct_size = sizeof(hit);
    centre[1] = 3.0f; /* out of the ray's way: the box sits at 0.5 on x=0, so cast beside it */
    origin[0] = 4.0f;
    CHECK_STATUS(g_api->physics_raycast(world, origin, down, 100.0f, 0xFFFFFFFFu, &hit, &found), ENGINE_OK);
    CHECK(found == 1 && hit.entity == floor_key);
    CHECK(fabsf(hit.distance - 5.0f) < 1.0e-3f && fabsf(hit.point[1]) < 1.0e-3f &&
          fabsf(hit.normal[1] - 1.0f) < 1.0e-4f);
    CHECK_STATUS(g_api->physics_raycast(world, origin, down, 2.0f, 0xFFFFFFFFu, &hit, &found), ENGINE_OK);
    CHECK(found == 0); /* too short */
    CHECK_STATUS(g_api->physics_raycast(world, origin, down, 100.0f, 0x2u, &hit, &found), ENGINE_OK);
    CHECK(found == 0); /* the colliders are on layer 0 */
    origin[0] = 0.0f;  /* straight down onto the box */
    CHECK_STATUS(g_api->physics_raycast(world, origin, down, 100.0f, 0xFFFFFFFFu, &hit, &found), ENGINE_OK);
    CHECK(found == 1 && hit.entity == box && fabsf(hit.distance - 4.0f) < 1.0e-3f);

    /* overlaps list the right entities, and the total counts past the capacity */
    centre[1] = 0.5f;
    CHECK_STATUS(g_api->physics_overlap_sphere(world, centre, 0.2f, 0xFFFFFFFFu, keys, 4, &total), ENGINE_OK);
    CHECK(total == 1 && keys[0] == box);
    centre[1] = 0.0f;
    CHECK_STATUS(g_api->physics_overlap_sphere(world, centre, 0.3f, 0xFFFFFFFFu, NULL, 0, &total), ENGINE_OK);
    CHECK(total == 2); /* the box and the floor */
    to[1] = 0.1f;      /* a small box on the floor's top, far from the box */
    CHECK_STATUS(g_api->physics_overlap_box(world, to, half, 0xFFFFFFFFu, keys, 4, &total), ENGINE_OK);
    CHECK(total == 1 && keys[0] == floor_key);
    to[1] = 10.0f;
    CHECK_STATUS(g_api->physics_overlap_box(world, to, half, 0xFFFFFFFFu, keys, 4, &total), ENGINE_OK);
    CHECK(total == 0);

    /* the world destroyed with bodies in it, and the engine goes on */
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.delta_seconds = 1.0 / 60.0;
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
    memset(&stats, 0, sizeof(stats));
    stats.struct_size = sizeof(stats);
    CHECK_STATUS(g_api->frame(engine, &info, &stats), ENGINE_OK);
    CHECK(stats.worlds_run == 0 && stats.physics_bodies == 0);
}

static void TestSettings(void)
{
    engine_setting_entry entries[2];
    engine_handle engine;
    engine_create_info info;
    uint64_t key = 0;
    engine_handle world;
    float y;

    /* a low gravity is a setting; the ball falls slower than 9.81 */
    entries[0].key = S("physics.gravity_y");
    entries[0].value = S("-1.0");
    engine = NewEngine(entries, 1);
    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    key = MakeBall(world, 10.0f, 0.0f);
    Frames(engine, 60, 1.0 / 60.0);
    y = PositionY(world, key);
    CHECK(y < 9.8f && y > 9.2f); /* a fall of about half a metre in a second at 1 m/s^2 */
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);

    /* an unknown physics setting is refused as any other, and a value out of range too */
    entries[0].key = S("physics.no_such_setting");
    entries[0].value = S("1");
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.settings = entries;
    info.setting_count = 1;
    memset(&engine, 0, sizeof(engine));
    CHECK_STATUS(g_api->create(&info, &engine), ENGINE_ERROR_NOT_FOUND);
    entries[0].key = S("physics.step_hz");
    entries[0].value = S("100000");
    CHECK(g_api->create(&info, &engine) != ENGINE_OK);
}

int main(void)
{
    engine_handle engine;
    host_quiet_failures();
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &g_api) != ENGINE_OK)
    {
        printf("host_physics: no function table\n");
        return 2;
    }
    engine = NewEngine(NULL, 0);
    TestArguments(engine);
    TestBallBounces(engine);
    TestImpulseAndQueries(engine);
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    TestSettings();
    g_api->thread_detach();
    if (g_failures != 0)
    {
        printf("host_physics: %d check(s) failed\n", g_failures);
        return 1;
    }
    printf("host_physics: ok\n");
    return 0;
}
