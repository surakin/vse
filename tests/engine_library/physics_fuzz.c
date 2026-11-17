/* Phase 25 Day 412: a fuzz of the physics part of the C API. A fixed seed drives thousands of random calls (entities
   made and destroyed, components with random numbers in them, forces, queries, characters, events, snapshots that are
   restored whole and with bytes flipped, frames): whatever the arguments, no call may crash, and under the sanitizer
   flavors none may touch memory it should not. A result is never checked, only that the library survives. */
#include <stdio.h>
#include <stdlib.h>
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
            printf("physics_fuzz: line %d: CHECK failed: %s\n", __LINE__, #condition);                                 \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

#define CHECK_STATUS(call, expected)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status got_ = (call);                                                                             \
        if (got_ != (expected))                                                                                        \
        {                                                                                                              \
            printf("physics_fuzz: line %d: %s returned %d, expected %d\n", __LINE__, #call, (int)got_,                 \
                   (int)(expected));                                                                                   \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

static engine_handle NewEngine(void)
{
    engine_create_info info;
    engine_handle engine;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    memset(&engine, 0, sizeof(engine));
    CHECK_STATUS(g_api->create(&info, &engine), ENGINE_OK);
    return engine;
}

static uint64_t g_state = 0x853c49e6748fea9bull;

static uint32_t Rand(void)
{
    g_state = g_state * 6364136223846793005ull + 1442695040888963407ull;
    return (uint32_t)(g_state >> 33);
}

/* A 32-bit word that is a float, a small integer or noise: the fields of a random struct come from here. */
static uint32_t Word(void)
{
    static const uint32_t palette[] = {0x00000000u, 0x3f800000u, 0x3f000000u, 0x40000000u, 0xbf800000u,
                                       0x00000001u, 0x00000002u, 0x00000003u, 0x7fc00000u, 0x7f800000u,
                                       0x501502f9u, 0x3c23d70au, 0x42c80000u, 0x41200000u};
    const uint32_t r = Rand();
    if (r % 5 == 0)
    {
        return Rand();
    }
    return palette[r % (sizeof(palette) / sizeof(palette[0]))];
}

static float F(void)
{
    uint32_t w = Word();
    float f;
    memcpy(&f, &w, sizeof(f));
    return f;
}

#define KEYS 48
static uint64_t g_keys[KEYS];

static uint64_t Key(void)
{
    const uint32_t r = Rand() % 20;
    if (r == 0)
    {
        return 0;
    }
    if (r == 1)
    {
        return ~0ull;
    }
    return g_keys[Rand() % KEYS];
}

static void Fuzz(engine_handle engine, engine_handle world, int calls)
{
    static const uint32_t kinds[] = {ENGINE_COMPONENT_TRANSFORM,
                                     ENGINE_COMPONENT_MESH_REFERENCE,
                                     ENGINE_COMPONENT_CAMERA,
                                     ENGINE_COMPONENT_LIGHT,
                                     ENGINE_COMPONENT_VISIBILITY,
                                     ENGINE_COMPONENT_RIGID_BODY,
                                     ENGINE_COMPONENT_COLLIDER,
                                     ENGINE_COMPONENT_JOINT,
                                     ENGINE_COMPONENT_CHARACTER,
                                     ENGINE_COMPONENT_WORLD_TRANSFORM,
                                     0u,
                                     99u};
    static const uint32_t sizes[] = {4u, 8u, 40u, 64u, 96u, 160u, 512u, 512u, 512u};
    uint32_t buffer[128];
    engine_buffer snap;
    int have_snap = 0;
    int i;
    int k;
    memset(&snap, 0, sizeof(snap));
    for (i = 0; i < calls; ++i)
    {
        float v[4];
        float w[4];
        uint64_t keys[8];
        size_t total = 0;
        engine_physics_event events[8];
        engine_character_state state;
        int has = 0;
        for (k = 0; k < 4; ++k)
        {
            v[k] = F();
            w[k] = F();
        }
        switch (Rand() % 24)
        {
        case 0:
        case 1:
        {
            uint64_t key = 0;
            const uint32_t slot = Rand() % KEYS;
            if (g_api->entity_create(world, &key) == ENGINE_OK)
            {
                g_keys[slot] = key;
            }
            break;
        }
        case 2: g_api->entity_destroy(world, Key()); break;
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        {
            const uint32_t kind = kinds[Rand() % (sizeof(kinds) / sizeof(kinds[0]))];
            for (k = 0; k < 128; ++k)
            {
                buffer[k] = Word();
            }
            buffer[0] = sizes[Rand() % (sizeof(sizes) / sizeof(sizes[0]))];
            g_api->component_set(world, Key(), kind, buffer);
            break;
        }
        case 9:
        {
            memset(buffer, 0, sizeof(buffer));
            buffer[0] = sizes[Rand() % (sizeof(sizes) / sizeof(sizes[0]))];
            g_api->component_get(world, Key(), kinds[Rand() % (sizeof(kinds) / sizeof(kinds[0]))], buffer);
            break;
        }
        case 10: g_api->component_remove(world, Key(), kinds[Rand() % (sizeof(kinds) / sizeof(kinds[0]))]); break;
        case 11:
            g_api->component_has(world, Key(), kinds[Rand() % (sizeof(kinds) / sizeof(kinds[0]))], &has);
            g_api->query(world, Rand() & 0x7FFFu, keys, 8, &total);
            break;
        case 12:
        case 13:
        {
            engine_frame_info info;
            memset(&info, 0, sizeof(info));
            info.struct_size = sizeof(info);
            info.delta_seconds = (double)(Rand() % 5) * 0.01;
            g_api->frame(engine, &info, NULL);
            break;
        }
        case 14:
            g_api->body_apply_force(world, Key(), v, (Rand() & 1) ? w : NULL);
            g_api->body_apply_impulse(world, Key(), v, (Rand() & 1) ? w : NULL);
            break;
        case 15:
            g_api->body_set_velocity(world, Key(), v, (Rand() & 1) ? w : NULL);
            g_api->body_get_velocity(world, Key(), v, w);
            break;
        case 16: g_api->body_teleport(world, Key(), v, (Rand() & 1) ? w : NULL); break;
        case 17:
        {
            engine_ray_hit hit;
            int found = 0;
            memset(&hit, 0, sizeof(hit));
            hit.struct_size = sizeof(hit);
            g_api->physics_raycast(world, v, w, F(), Rand(), &hit, &found);
            g_api->physics_overlap_sphere(world, v, F(), Rand(), keys, 8, &total);
            g_api->physics_overlap_box(world, v, w, Rand(), keys, 8, &total);
            g_api->physics_set_gravity(world, v);
            break;
        }
        case 18: g_api->joint_reaction(world, Key(), v, w); break;
        case 19:
            memset(&state, 0, sizeof(state));
            state.struct_size = sizeof(state);
            g_api->character_move(world, Key(), v, F(), &state);
            g_api->character_jump(world, Key(), F(), &state);
            break;
        case 20:
            memset(events, 0, sizeof(events));
            for (k = 0; k < 8; ++k)
            {
                events[k].struct_size = sizeof(engine_physics_event);
            }
            g_api->physics_events(world, events, 8, &total);
            break;
        case 21:
            if (have_snap)
            {
                engine_buffer_release(&snap);
                have_snap = 0;
            }
            have_snap = g_api->physics_snapshot(world, &snap) == ENGINE_OK;
            break;
        case 22:
            if (have_snap && snap.size > 0)
            {
                g_api->physics_restore(world, snap.data, snap.size);
            }
            break;
        default:
            if (have_snap && snap.size > 0)
            {
                /* the snapshot with a few bytes changed, or cut short: refused, never a crash */
                unsigned char* copy = (unsigned char*)malloc(snap.size);
                size_t len = snap.size;
                if (copy != NULL)
                {
                    memcpy(copy, snap.data, snap.size);
                    for (k = 0; k < 1 + (int)(Rand() % 4); ++k)
                    {
                        copy[Rand() % snap.size] = (unsigned char)Rand();
                    }
                    if (Rand() % 3 == 0)
                    {
                        len = Rand() % snap.size;
                    }
                    g_api->physics_restore(world, copy, len);
                    free(copy);
                }
            }
            break;
        }
    }
    if (have_snap)
    {
        engine_buffer_release(&snap);
    }
}

int main(void)
{
    engine_handle engine;
    engine_handle world;
    int round;
    host_quiet_failures();
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &g_api) != ENGINE_OK)
    {
        printf("physics_fuzz: no function table\n");
        return 2;
    }
    engine = NewEngine();
    for (round = 0; round < 6; ++round)
    {
        memset(&world, 0, sizeof(world));
        CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
        memset(g_keys, 0, sizeof(g_keys));
        Fuzz(engine, world, 4000);
        CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
    }
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    g_api->thread_detach();
    if (g_failures != 0)
    {
        printf("physics_fuzz: %d check(s) failed\n", g_failures);
        return 1;
    }
    printf("physics_fuzz: ok\n");
    return 0;
}
