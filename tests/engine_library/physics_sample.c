/* Phase 25 Day 409: the physics sample. A host with no window builds a small world through the C API (a floor, a stack
   of boxes, a pendulum on a ball joint, a character that walks and jumps, a trigger), runs 600 frames, rewinds to frame
   300 with a snapshot and runs the rest again: the two runs end in the same bits, and the checksum of the last frame is
   printed (it is the same on every build and machine). */
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
            printf("physics_sample: line %d: CHECK failed: %s\n", __LINE__, #condition);                               \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

#define CHECK_STATUS(call, expected)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status got_ = (call);                                                                             \
        if (got_ != (expected))                                                                                        \
        {                                                                                                              \
            printf("physics_sample: line %d: %s returned %d, expected %d\n", __LINE__, #call, (int)got_,               \
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

static uint32_t Fnv(uint32_t h, const void* data, size_t size)
{
    const unsigned char* p = (const unsigned char*)data;
    size_t i;
    for (i = 0; i < size; ++i)
    {
        h = (h ^ p[i]) * 16777619u;
    }
    return h;
}

/* A checksum of every entity's transform, in the order the query lists them. */
static uint32_t Checksum(engine_handle world)
{
    uint64_t keys[64];
    size_t total = 0;
    size_t i;
    uint32_t h = 2166136261u;
    CHECK_STATUS(g_api->query(world, ENGINE_COMPONENT_BIT(ENGINE_COMPONENT_TRANSFORM), keys, 64, &total), ENGINE_OK);
    CHECK(total <= 64);
    for (i = 0; i < total && i < 64; ++i)
    {
        engine_transform t;
        memset(&t, 0, sizeof(t));
        t.struct_size = sizeof(t);
        CHECK_STATUS(g_api->component_get(world, keys[i], ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
        h = Fnv(h, t.position, sizeof(t.position));
        h = Fnv(h, t.rotation, sizeof(t.rotation));
    }
    return h;
}

/* The checksum of the last frame, recorded on Linux (clang and GCC, unity and not, debug and release agree). Recorded
   again on 2026-11-20, when character_move became a held move carried out in each fixed step and the character's
   transform the interpolated position. */
static const uint32_t kGoldenChecksum = 0x7e65470fu;

static uint32_t g_contacts = 0;
static uint32_t g_triggers = 0;

static void Drive(engine_handle engine, engine_handle world, uint64_t hero, int first, int last)
{
    float walk[3] = {1.5f, 0.0f, 0.0f};
    int f;
    for (f = first; f < last; ++f)
    {
        engine_physics_event ev[64];
        size_t total = 0;
        size_t k;
        size_t n;
        walk[2] = (f / 100) % 2 == 0 ? 0.5f : -0.5f;
        CHECK_STATUS(g_api->character_move(world, hero, walk, 1.0f / 60.0f, NULL), ENGINE_OK);
        if (f % 200 == 150)
        {
            CHECK_STATUS(g_api->character_jump(world, hero, 4.0f, NULL), ENGINE_OK);
        }
        Frames(engine, 1, 1.0 / 60.0);
        for (k = 0; k < 64; ++k)
        {
            memset(&ev[k], 0, sizeof(ev[k]));
            ev[k].struct_size = sizeof(engine_physics_event);
        }
        CHECK_STATUS(g_api->physics_events(world, ev, 64, &total), ENGINE_OK);
        n = total < 64 ? total : 64;
        for (k = 0; k < n; ++k)
        {
            g_contacts += ev[k].kind == ENGINE_PHYSICS_CONTACT_BEGIN ? 1u : 0u;
            g_triggers += ev[k].kind == ENGINE_PHYSICS_TRIGGER_ENTER ? 1u : 0u;
        }
    }
}

int main(void)
{
    engine_handle engine;
    engine_handle world;
    uint64_t anchor;
    uint64_t ball;
    uint64_t pivot = 0;
    uint64_t hero = 0;
    uint64_t sensor = 0;
    engine_joint j;
    engine_character ch;
    engine_collider c;
    engine_buffer snap;
    uint32_t first;
    uint32_t second;
    int i;

    host_quiet_failures();
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &g_api) != ENGINE_OK)
    {
        printf("physics_sample: no function table\n");
        return 2;
    }
    engine = NewEngine(NULL, 0);
    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    MakeFloor(world);
    for (i = 0; i < 5; ++i)
    {
        MakeBox(world, ENGINE_BODY_DYNAMIC, 6.0f, 0.5f + 1.05f * (float)i, 0.0f, 0.5f);
    }
    anchor = MakeBox(world, ENGINE_BODY_STATIC, -4.0f, 6.0f, 0.0f, 0.2f);
    ball = MakeBall(world, 6.0f, 0.3f);
    SetTransform(world, ball, -2.0f, 6.0f, 0.0f);
    CHECK_STATUS(g_api->entity_create(world, &pivot), ENGINE_OK);
    j = MakeJoint(ENGINE_JOINT_BALL, anchor, ball, -4.0f, 6.0f, 0.0f);
    CHECK_STATUS(g_api->component_set(world, pivot, ENGINE_COMPONENT_JOINT, &j), ENGINE_OK);
    CHECK_STATUS(g_api->entity_create(world, &sensor), ENGINE_OK);
    SetTransform(world, sensor, 3.0f, 1.0f, 0.0f);
    SetBody(world, sensor, ENGINE_BODY_STATIC, 0.0f);
    c = MakeCollider(ENGINE_SHAPE_BOX);
    c.is_trigger = 1;
    c.half_extents[0] = c.half_extents[1] = c.half_extents[2] = 0.8f;
    CHECK_STATUS(g_api->component_set(world, sensor, ENGINE_COMPONENT_COLLIDER, &c), ENGINE_OK);
    CHECK_STATUS(g_api->entity_create(world, &hero), ENGINE_OK);
    SetTransform(world, hero, 0.0f, 1.0f, 0.0f);
    ch = MakeCharacter();
    CHECK_STATUS(g_api->component_set(world, hero, ENGINE_COMPONENT_CHARACTER, &ch), ENGINE_OK);

    Drive(engine, world, hero, 0, 300);
    CHECK_STATUS(g_api->physics_snapshot(world, &snap), ENGINE_OK);
    Drive(engine, world, hero, 300, 600);
    first = Checksum(world);
    CHECK_STATUS(g_api->physics_restore(world, snap.data, snap.size), ENGINE_OK);
    Drive(engine, world, hero, 300, 600);
    second = Checksum(world);
    CHECK(first == second);
    CHECK(second == kGoldenChecksum); /* the same on every build and machine (Linux, Windows and macOS x86-64) */
    CHECK(g_contacts > 0);
    engine_buffer_release(&snap);
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    g_api->thread_detach();
    if (g_failures != 0)
    {
        printf("physics_sample: %d check(s) failed (checksum %08x)\n", g_failures, (unsigned)second);
        return 1;
    }
    printf("physics_sample: checksum %08x contacts %u triggers %u\n", (unsigned)second, (unsigned)g_contacts,
           (unsigned)g_triggers);
    printf("physics_sample: ok\n");
    return 0;
}
