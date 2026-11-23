/* Phase 25 Day 409: a C program ties bodies with joints, walks a character, reads the physics events and rewinds the
   physics with a snapshot, all through the function table, and checks every refusal the API documents. */
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
            printf("host_joints: line %d: CHECK failed: %s\n", __LINE__, #condition);                                  \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

#define CHECK_STATUS(call, expected)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status got_ = (call);                                                                             \
        if (got_ != (expected))                                                                                        \
        {                                                                                                              \
            printf("host_joints: line %d: %s returned %d, expected %d\n", __LINE__, #call, (int)got_,                  \
                   (int)(expected));                                                                                   \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

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
    return (float)t.position[1];
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

static engine_joint MakeJoint(uint32_t kind, uint64_t a, uint64_t b, float x, float y, float z)
{
    engine_joint j;
    memset(&j, 0, sizeof(j));
    j.struct_size = sizeof(j);
    j.kind = kind;
    j.entity_a = a;
    j.entity_b = b;
    j.point_a[0] = j.point_b[0] = x;
    j.point_a[1] = j.point_b[1] = y;
    j.point_a[2] = j.point_b[2] = z;
    j.axis[2] = 1.0f;
    j.min_length = -1.0f;
    j.max_length = -1.0f;
    return j;
}

static engine_character MakeCharacter(void)
{
    engine_character c;
    memset(&c, 0, sizeof(c));
    c.struct_size = sizeof(c);
    c.radius = 0.4f;
    c.height = 1.8f;
    c.step_height = 0.3f;
    c.min_ground_y = 0.64f;
    c.skin_width = 0.02f;
    c.gravity_scale = 1.0f;
    return c;
}

static uint64_t MakeBox(engine_handle world, uint32_t type, float x, float y, float z, float half)
{
    uint64_t key = 0;
    engine_collider c = MakeCollider(ENGINE_SHAPE_BOX);
    CHECK_STATUS(g_api->entity_create(world, &key), ENGINE_OK);
    SetTransform(world, key, x, y, z);
    SetBody(world, key, type, 0.0f);
    c.half_extents[0] = c.half_extents[1] = c.half_extents[2] = half;
    CHECK_STATUS(g_api->component_set(world, key, ENGINE_COMPONENT_COLLIDER, &c), ENGINE_OK);
    return key;
}

static float Length3(const float* v)
{
    return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static void TestArguments(engine_handle engine)
{
    engine_handle world;
    uint64_t a = 0;
    uint64_t b = 0;
    uint64_t gone = 0;
    engine_joint j;
    engine_character c;
    engine_character_state st;
    float v3[3] = {1.0f, 0.0f, 0.0f};
    float nan3[3];
    float force[3];
    int has = 7;
    size_t total = 9;
    uint64_t keys[4];

    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    CHECK_STATUS(g_api->entity_create(world, &a), ENGINE_OK);
    CHECK_STATUS(g_api->entity_create(world, &b), ENGINE_OK);
    CHECK_STATUS(g_api->entity_create(world, &gone), ENGINE_OK);
    CHECK_STATUS(g_api->entity_destroy(world, gone), ENGINE_OK);
    nan3[0] = nanf("");
    nan3[1] = nan3[2] = 0.0f;

    /* the joint component */
    j = MakeJoint(ENGINE_JOINT_BALL, a, b, 0.0f, 1.0f, 0.0f);
    CHECK_STATUS(g_api->component_set(world, a, ENGINE_COMPONENT_JOINT, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    j.struct_size = 4;
    CHECK_STATUS(g_api->component_set(world, a, ENGINE_COMPONENT_JOINT, &j), ENGINE_ERROR_STRUCT_SIZE);
    j = MakeJoint(9, a, b, 0.0f, 1.0f, 0.0f);
    CHECK_STATUS(g_api->component_set(world, a, ENGINE_COMPONENT_JOINT, &j), ENGINE_ERROR_INVALID_ARGUMENT);
    j = MakeJoint(ENGINE_JOINT_BALL, a, b, 0.0f, 1.0f, 0.0f);
    j.flags = 0x80u;
    CHECK_STATUS(g_api->component_set(world, a, ENGINE_COMPONENT_JOINT, &j), ENGINE_ERROR_INVALID_ARGUMENT);
    j = MakeJoint(ENGINE_JOINT_BALL, 0, 0, 0.0f, 1.0f, 0.0f);
    CHECK_STATUS(g_api->component_set(world, a, ENGINE_COMPONENT_JOINT, &j), ENGINE_ERROR_INVALID_ARGUMENT);
    j = MakeJoint(ENGINE_JOINT_BALL, a, gone, 0.0f, 1.0f, 0.0f);
    CHECK_STATUS(g_api->component_set(world, a, ENGINE_COMPONENT_JOINT, &j), ENGINE_ERROR_STALE_HANDLE);
    j = MakeJoint(ENGINE_JOINT_BALL, a, a, 0.0f, 1.0f, 0.0f);
    CHECK_STATUS(g_api->component_set(world, a, ENGINE_COMPONENT_JOINT, &j), ENGINE_ERROR_INVALID_ARGUMENT);
    j = MakeJoint(ENGINE_JOINT_HINGE, a, b, 0.0f, 1.0f, 0.0f);
    j.axis[2] = 0.0f;
    CHECK_STATUS(g_api->component_set(world, a, ENGINE_COMPONENT_JOINT, &j), ENGINE_ERROR_INVALID_ARGUMENT);
    j = MakeJoint(ENGINE_JOINT_BALL, a, b, 0.0f, 1.0f, 0.0f);
    j.hertz = nanf("");
    CHECK_STATUS(g_api->component_set(world, a, ENGINE_COMPONENT_JOINT, &j), ENGINE_ERROR_INVALID_ARGUMENT);
    j = MakeJoint(ENGINE_JOINT_HINGE, a, b, 0.0f, 1.0f, 0.0f);
    j.flags = ENGINE_JOINT_LIMIT | ENGINE_JOINT_MOTOR | ENGINE_JOINT_COLLIDE_CONNECTED;
    j.lower_angle = -0.5f;
    j.upper_angle = 0.75f;
    j.motor_speed = 2.0f;
    j.max_motor_torque = 9.0f;
    j.break_force = 100.0f;
    CHECK_STATUS(g_api->component_set(world, a, ENGINE_COMPONENT_JOINT, &j), ENGINE_OK);
    memset(&j, 0, sizeof(j));
    j.struct_size = sizeof(j);
    CHECK_STATUS(g_api->component_get(world, a, ENGINE_COMPONENT_JOINT, &j), ENGINE_OK);
    CHECK(j.kind == ENGINE_JOINT_HINGE && j.entity_a == a && j.entity_b == b && j.upper_angle == 0.75f &&
          j.motor_speed == 2.0f && j.break_force == 100.0f && j.point_a[1] == 1.0f && j.axis[2] == 1.0f &&
          j.flags == (ENGINE_JOINT_LIMIT | ENGINE_JOINT_MOTOR | ENGINE_JOINT_COLLIDE_CONNECTED));
    CHECK_STATUS(g_api->component_get(world, b, ENGINE_COMPONENT_JOINT, &j), ENGINE_ERROR_NOT_FOUND);
    CHECK_STATUS(g_api->component_has(world, a, ENGINE_COMPONENT_JOINT, &has), ENGINE_OK);
    CHECK(has == 1);
    CHECK_STATUS(g_api->query(world, ENGINE_COMPONENT_BIT(ENGINE_COMPONENT_JOINT), keys, 4, &total), ENGINE_OK);
    CHECK(total == 1 && keys[0] == a);
    /* a joint whose bodies do not exist has no reaction yet */
    CHECK_STATUS(g_api->joint_reaction(world, a, force, NULL), ENGINE_ERROR_NOT_FOUND);
    CHECK_STATUS(g_api->joint_reaction(world, gone, force, NULL), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(g_api->joint_reaction(world, 0, force, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->component_remove(world, a, ENGINE_COMPONENT_JOINT), ENGINE_OK);
    CHECK_STATUS(g_api->component_remove(world, a, ENGINE_COMPONENT_JOINT), ENGINE_ERROR_NOT_FOUND);

    /* the character component */
    c = MakeCharacter();
    CHECK_STATUS(g_api->component_set(world, a, ENGINE_COMPONENT_CHARACTER, &c), ENGINE_ERROR_MISSING_DEPENDENCY);
    SetTransform(world, a, 0.0f, 1.0f, 0.0f);
    CHECK_STATUS(g_api->component_set(world, a, ENGINE_COMPONENT_CHARACTER, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    c.struct_size = 4;
    CHECK_STATUS(g_api->component_set(world, a, ENGINE_COMPONENT_CHARACTER, &c), ENGINE_ERROR_STRUCT_SIZE);
    c = MakeCharacter();
    c.height = 0.5f;
    CHECK_STATUS(g_api->component_set(world, a, ENGINE_COMPONENT_CHARACTER, &c), ENGINE_ERROR_INVALID_ARGUMENT);
    c = MakeCharacter();
    c.radius = nanf("");
    CHECK_STATUS(g_api->component_set(world, a, ENGINE_COMPONENT_CHARACTER, &c), ENGINE_ERROR_INVALID_ARGUMENT);
    c = MakeCharacter();
    CHECK_STATUS(g_api->component_has(world, a, ENGINE_COMPONENT_CHARACTER, &has), ENGINE_OK);
    CHECK(has == 0);
    memset(&st, 0, sizeof(st));
    st.struct_size = sizeof(st);
    CHECK_STATUS(g_api->character_move(world, a, v3, 0.016f, &st), ENGINE_ERROR_NOT_FOUND);
    CHECK_STATUS(g_api->component_set(world, a, ENGINE_COMPONENT_CHARACTER, &c), ENGINE_OK);
    c.radius = 0.0f;
    memset(&c, 0, sizeof(c));
    c.struct_size = sizeof(c);
    CHECK_STATUS(g_api->component_get(world, a, ENGINE_COMPONENT_CHARACTER, &c), ENGINE_OK);
    CHECK(c.radius == 0.4f && c.height == 1.8f && c.step_height == 0.3f);
    CHECK_STATUS(g_api->character_move(world, a, NULL, 0.016f, &st), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->character_move(world, a, nan3, 0.016f, &st), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->character_move(world, a, v3, 0.0f, &st), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->character_move(world, a, v3, 2.0f, &st), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->character_move(world, a, v3, nanf(""), &st), ENGINE_ERROR_INVALID_ARGUMENT);
    st.struct_size = 4;
    CHECK_STATUS(g_api->character_move(world, a, v3, 0.016f, &st), ENGINE_ERROR_STRUCT_SIZE);
    st.struct_size = sizeof(st);
    CHECK_STATUS(g_api->character_move(world, gone, v3, 0.016f, &st), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(g_api->character_move(world, a, v3, 0.016f, NULL), ENGINE_OK);
    CHECK_STATUS(g_api->character_jump(world, a, -1.0f, &st), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->character_jump(world, a, nanf(""), &st), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->character_jump(world, a, 0.0f, &st), ENGINE_OK);
    CHECK_STATUS(g_api->query(world, ENGINE_COMPONENT_BIT(ENGINE_COMPONENT_CHARACTER), keys, 4, &total), ENGINE_OK);
    CHECK(total == 1 && keys[0] == a);
    CHECK_STATUS(g_api->component_remove(world, a, ENGINE_COMPONENT_CHARACTER), ENGINE_OK);
    CHECK_STATUS(g_api->character_move(world, a, v3, 0.016f, &st), ENGINE_ERROR_NOT_FOUND);
    CHECK_STATUS(g_api->component_remove(world, a, ENGINE_COMPONENT_CHARACTER), ENGINE_ERROR_NOT_FOUND);
    Frames(engine, 2, 1.0 / 60.0);

    /* events and snapshots */
    {
        engine_physics_event ev[2];
        engine_buffer buffer;
        memset(ev, 0, sizeof(ev));
        CHECK_STATUS(g_api->physics_events(world, ev, 2, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->physics_events(world, NULL, 2, &total), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->physics_events(world, NULL, 0, &total), ENGINE_OK);
        CHECK(total == 0);
        CHECK_STATUS(g_api->physics_snapshot(world, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->physics_restore(world, NULL, 4), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->physics_restore(world, "nope", 4), ENGINE_ERROR_CORRUPT);
        CHECK_STATUS(g_api->physics_restore(world, "", 0), ENGINE_ERROR_CORRUPT);
        CHECK_STATUS(g_api->physics_snapshot(world, &buffer), ENGINE_OK);
        CHECK(buffer.size > 8 && buffer.data != NULL);
        CHECK_STATUS(g_api->physics_restore(world, buffer.data, buffer.size - 1), ENGINE_ERROR_CORRUPT);
        CHECK_STATUS(g_api->physics_restore(world, buffer.data, buffer.size), ENGINE_OK);
        engine_buffer_release(&buffer);
        CHECK_STATUS(g_api->physics_snapshot(world, &buffer), ENGINE_OK);
        engine_buffer_release(&buffer);
    }
    CHECK_STATUS(g_api->physics_events((engine_handle){0}, NULL, 0, &total), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
    CHECK_STATUS(g_api->physics_snapshot(world, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    (void)force;
}

static void TestPendulumAndBreak(engine_handle engine)
{
    engine_handle world;
    uint64_t anchor;
    uint64_t ball;
    uint64_t pivot = 0;
    engine_joint j;
    float force[3];
    float torque[3];
    float dist = 0.0f;
    float peak = 0.0f;
    int i;
    int broke = 0;
    int has = 7;
    size_t total = 0;
    engine_physics_event ev[16];
    engine_transform t;

    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    anchor = MakeBox(world, ENGINE_BODY_STATIC, 0.0f, 5.0f, 0.0f, 0.2f);
    ball = MakeBall(world, 5.0f, 0.0f);
    SetTransform(world, ball, 2.0f, 5.0f, 0.0f);
    CHECK_STATUS(g_api->entity_create(world, &pivot), ENGINE_OK);
    j = MakeJoint(ENGINE_JOINT_BALL, anchor, ball, 0.0f, 5.0f, 0.0f);
    CHECK_STATUS(g_api->component_set(world, pivot, ENGINE_COMPONENT_JOINT, &j), ENGINE_OK);
    CHECK_STATUS(g_api->joint_reaction(world, pivot, force, torque), ENGINE_OK); /* made by the call: no force yet */
    CHECK(Length3(force) == 0.0f);
    for (i = 0; i < 120; ++i)
    {
        Frames(engine, 1, 1.0 / 60.0);
        memset(&t, 0, sizeof(t));
        t.struct_size = sizeof(t);
        CHECK_STATUS(g_api->component_get(world, ball, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
        dist = sqrtf((float)(t.position[0] * t.position[0] + (t.position[1] - 5.0f) * (t.position[1] - 5.0f) +
                     t.position[2] * t.position[2]));
        if (dist > peak + 0.0f && i > 5)
        {
            peak = dist;
        }
        CHECK(dist < 2.2f && t.position[1] < 5.05f);
    }
    CHECK(peak > 1.8f); /* the ball swings on a rope of 2 metres: it never comes closer than that */
    CHECK_STATUS(g_api->joint_reaction(world, pivot, force, torque), ENGINE_OK);
    CHECK(Length3(force) > 1.0f);
    CHECK_STATUS(g_api->joint_reaction(world, pivot, NULL, NULL), ENGINE_OK);
    CHECK_STATUS(g_api->joint_reaction(world, ball, force, NULL), ENGINE_ERROR_NOT_FOUND); /* not a joint */

    /* a weak joint breaks: an event, the component goes, the reaction is gone */
    j.break_force = 0.5f;
    CHECK_STATUS(g_api->component_set(world, pivot, ENGINE_COMPONENT_JOINT, &j), ENGINE_OK); /* made again */
    for (i = 0; i < 120 && !broke; ++i)
    {
        size_t n;
        size_t k;
        Frames(engine, 1, 1.0 / 60.0);
        memset(ev, 0, sizeof(ev));
        for (k = 0; k < 16; ++k)
        {
            ev[k].struct_size = sizeof(engine_physics_event);
        }
        CHECK_STATUS(g_api->physics_events(world, ev, 16, &total), ENGINE_OK);
        n = total < 16 ? total : 16;
        for (k = 0; k < n; ++k)
        {
            if (ev[k].kind == ENGINE_PHYSICS_JOINT_BREAK)
            {
                CHECK(ev[k].entity_a == anchor && ev[k].entity_b == ball);
                broke = 1;
            }
        }
    }
    CHECK(broke == 1);
    CHECK_STATUS(g_api->component_has(world, pivot, ENGINE_COMPONENT_JOINT, &has), ENGINE_OK);
    CHECK(has == 0);
    CHECK_STATUS(g_api->joint_reaction(world, pivot, force, torque), ENGINE_ERROR_NOT_FOUND);
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
}

static void TestCharacter(engine_handle engine)
{
    engine_handle world;
    uint64_t floor_key;
    uint64_t hero = 0;
    engine_character c = MakeCharacter();
    engine_character_state st;
    engine_transform t;
    float walk[3] = {2.0f, 0.0f, 0.0f};
    float still[3] = {0.0f, 0.0f, 0.0f};
    float peak = 0.0f;
    int i;

    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    floor_key = MakeFloor(world);
    CHECK_STATUS(g_api->entity_create(world, &hero), ENGINE_OK);
    SetTransform(world, hero, 0.0f, 1.2f, 0.0f);
    CHECK_STATUS(g_api->component_set(world, hero, ENGINE_COMPONENT_CHARACTER, &c), ENGINE_OK);
    memset(&st, 0, sizeof(st));
    st.struct_size = sizeof(st);
    for (i = 0; i < 90; ++i)
    {
        CHECK_STATUS(g_api->character_move(world, hero, walk, 1.0f / 60.0f, &st), ENGINE_OK);
        Frames(engine, 1, 1.0 / 60.0);
    }
    memset(&t, 0, sizeof(t));
    t.struct_size = sizeof(t);
    CHECK_STATUS(g_api->component_get(world, hero, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
    CHECK(t.position[0] > 2.5f && t.position[0] < 3.2f); /* 1.5 s at 2 m/s */
    CHECK(fabs(t.position[1] - 0.9f) < 0.1f);            /* the capsule's centre, 0.9 above the floor */
    CHECK_STATUS(g_api->character_move(world, hero, still, 1.0f / 60.0f, &st), ENGINE_OK);
    CHECK(st.on_ground == 1 && st.ground_entity == floor_key && st.ground_normal[1] > 0.99f);
    /* a jump leaves the ground and comes back */
    CHECK_STATUS(g_api->character_jump(world, hero, 5.0f, &st), ENGINE_OK);
    for (i = 0; i < 90; ++i)
    {
        CHECK_STATUS(g_api->character_move(world, hero, still, 1.0f / 60.0f, &st), ENGINE_OK);
        Frames(engine, 1, 1.0 / 60.0);
        CHECK_STATUS(g_api->component_get(world, hero, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
        if (t.position[1] > peak)
        {
            peak = (float)t.position[1];
        }
    }
    CHECK(peak > 1.5f);
    CHECK(fabs(t.position[1] - 0.9f) < 0.1f);
    CHECK_STATUS(g_api->character_move(world, hero, still, 1.0f / 60.0f, &st), ENGINE_OK);
    CHECK(st.on_ground == 1);
    /* destroying the entity gives the capsule back: the next frame runs without it */
    CHECK_STATUS(g_api->entity_destroy(world, hero), ENGINE_OK);
    Frames(engine, 2, 1.0 / 60.0);
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
}

static void TestEventsAndSnapshot(engine_handle engine)
{
    engine_handle world;
    uint64_t floor_key;
    uint64_t ball;
    uint64_t sensor = 0;
    engine_collider c;
    engine_physics_event ev[32];
    size_t total = 0;
    size_t k;
    size_t n;
    int i;
    int began = 0;
    int entered = 0;
    int ended = 0;
    engine_buffer snap;
    float before;
    float after_first;
    float after_second;

    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    floor_key = MakeFloor(world);
    ball = MakeBall(world, 3.0f, 0.2f);
    CHECK_STATUS(g_api->entity_create(world, &sensor), ENGINE_OK);
    SetTransform(world, sensor, 0.0f, 1.5f, 0.0f);
    SetBody(world, sensor, ENGINE_BODY_STATIC, 0.0f);
    c = MakeCollider(ENGINE_SHAPE_BOX);
    c.is_trigger = 1;
    CHECK_STATUS(g_api->component_set(world, sensor, ENGINE_COMPONENT_COLLIDER, &c), ENGINE_OK);
    for (i = 0; i < 150; ++i)
    {
        Frames(engine, 1, 1.0 / 60.0);
        CHECK_STATUS(g_api->physics_events(world, NULL, 0, &total), ENGINE_OK);
        n = total < 32 ? total : 32;
        for (k = 0; k < 32; ++k)
        {
            memset(&ev[k], 0, sizeof(ev[k]));
            ev[k].struct_size = sizeof(engine_physics_event);
        }
        CHECK_STATUS(g_api->physics_events(world, ev, 32, &total), ENGINE_OK);
        for (k = 0; k < n; ++k)
        {
            if (ev[k].kind == ENGINE_PHYSICS_CONTACT_BEGIN)
            {
                CHECK((ev[k].entity_a == ball && ev[k].entity_b == floor_key) ||
                      (ev[k].entity_a == floor_key && ev[k].entity_b == ball));
                CHECK(fabsf(ev[k].normal[1]) > 0.9f);
                ++began;
            }
            if (ev[k].kind == ENGINE_PHYSICS_CONTACT_END)
            {
                ++ended;
            }
            if (ev[k].kind == ENGINE_PHYSICS_TRIGGER_ENTER)
            {
                CHECK(ev[k].entity_a == sensor && ev[k].entity_b == ball);
                ++entered;
            }
        }
        if (i == 5)
        {
            ev[0].struct_size = 4; /* a bad element is refused when something is written to it */
            CHECK_STATUS(g_api->physics_events(world, ev, 1, &total),
                         total == 0 ? ENGINE_OK : ENGINE_ERROR_STRUCT_SIZE);
        }
    }
    CHECK(began >= 1 && entered >= 1 && ended >= 1);

    /* a snapshot rewinds: the same frames give the same bits */
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    MakeFloor(world);
    ball = MakeBall(world, 4.0f, 0.4f);
    Frames(engine, 40, 1.0 / 60.0);
    before = PositionY(world, ball);
    CHECK_STATUS(g_api->physics_snapshot(world, &snap), ENGINE_OK);
    Frames(engine, 100, 1.0 / 60.0);
    after_first = PositionY(world, ball);
    CHECK_STATUS(g_api->physics_restore(world, snap.data, snap.size), ENGINE_OK);
    CHECK(fabsf(PositionY(world, ball) - before) < 1.0e-3f);
    Frames(engine, 100, 1.0 / 60.0);
    after_second = PositionY(world, ball);
    CHECK(memcmp(&after_first, &after_second, sizeof(float)) == 0);
    CHECK(fabsf(after_first - before) > 0.5f);
    engine_buffer_release(&snap);
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
}

int main(void)
{
    engine_handle engine;
    host_quiet_failures();
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &g_api) != ENGINE_OK)
    {
        printf("host_joints: no function table\n");
        return 2;
    }
    engine = NewEngine(NULL, 0);
    TestArguments(engine);
    TestPendulumAndBreak(engine);
    TestCharacter(engine);
    TestEventsAndSnapshot(engine);
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    g_api->thread_detach();
    if (g_failures != 0)
    {
        printf("host_joints: %d check(s) failed\n", g_failures);
        return 1;
    }
    printf("host_joints: ok\n");
    return 0;
}
