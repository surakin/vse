/* Phase 26 (C API update): sounds on entities. A C program opens the engine's host-driven audio, puts an audio source
   and a listener on entities of a world, runs frames, and renders the mix itself: the sound is heard at the source's
   place, follows it, stops when the component goes, and survives audio being closed and opened again. Also: a stale
   handle in audio_render, a bus effect retuned more often than the mixer has effect slots, and audio_close while
   another thread calls the table. */
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
            printf("host_audio_world: line %d: CHECK failed: %s\n", __LINE__, #condition);                             \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

#define CHECK_STATUS(call, expected)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status got_ = (call);                                                                             \
        if (got_ != (expected))                                                                                        \
        {                                                                                                              \
            printf("host_audio_world: line %d: %s returned %d, expected %d\n", __LINE__, #call, (int)got_,             \
                   (int)(expected));                                                                                   \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

static void Frame(engine_handle engine)
{
    engine_frame_info info;
    engine_frame_stats stats;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.delta_seconds = 1.0 / 60.0;
    memset(&stats, 0, sizeof(stats));
    stats.struct_size = sizeof(stats);
    CHECK_STATUS(g_api->frame(engine, &info, &stats), ENGINE_OK);
}

/* The peak of `blocks` rendered blocks of stereo, after the frame's commands had a block to apply. */
static float Peak(engine_handle engine, int blocks)
{
    float out[256 * 2];
    float peak = 0.0f;
    int b;
    uint32_t i;
    for (b = 0; b < blocks; ++b)
    {
        CHECK_STATUS(g_api->audio_render(engine, 256, out), ENGINE_OK);
        for (i = 0; i < 256 * 2; ++i)
        {
            const float a = fabsf(out[i]);
            peak = a > peak ? a : peak;
        }
    }
    return peak;
}

static engine_transform At(float x, float y, float z)
{
    engine_transform t;
    memset(&t, 0, sizeof(t));
    t.struct_size = sizeof(t);
    t.position[0] = x;
    t.position[1] = y;
    t.position[2] = z;
    t.rotation[3] = 1.0f;
    t.scale[0] = t.scale[1] = t.scale[2] = 1.0f;
    return t;
}

static uint64_t MakeTone(engine_handle engine)
{
    float samples[4800];
    engine_audio_clip_info info;
    uint64_t clip = 0;
    int i;
    for (i = 0; i < 4800; ++i)
    {
        samples[i] = 0.5f * sinf(0.1f * (float)i);
    }
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.sample_rate = 48000;
    info.channels = 1;
    info.format = ENGINE_AUDIO_FORMAT_F32;
    CHECK_STATUS(g_api->audio_clip_create(engine, &info, samples, sizeof(samples), &clip), ENGINE_OK);
    return clip;
}

static engine_audio_source Source(uint64_t clip)
{
    engine_audio_source s;
    memset(&s, 0, sizeof(s));
    s.struct_size = sizeof(s);
    s.clip = clip;
    s.gain = 1.0f;
    s.pitch = 1.0f;
    s.flags = ENGINE_AUDIO_SOURCE_LOOP | ENGINE_AUDIO_SOURCE_PLAY_ON_START | ENGINE_AUDIO_SOURCE_SPATIAL;
    s.model = ENGINE_AUDIO_MODEL_INVERSE;
    s.min_distance = 1.0f;
    s.max_distance = 100.0f;
    s.rolloff = 1.0f;
    s.cone_inner = 6.2831853f;
    s.cone_outer = 6.2831853f;
    return s;
}

typedef struct closer
{
    engine_handle engine;
    int rounds;
} closer;

static void CallerThread(void* arg)
{
    closer* c = (closer*)arg;
    int i;
    engine_audio_state state;
    for (i = 0; i < c->rounds; ++i)
    {
        memset(&state, 0, sizeof(state));
        state.struct_size = sizeof(state);
        /* open or closed: either answer is fine, a crash is not */
        (void)g_api->audio_state(c->engine, &state);
    }
}

int main(void)
{
    engine_create_info create;
    engine_handle engine, world;
    engine_audio_info info;
    engine_audio_source src, back;
    engine_audio_listener ear, ear_back;
    engine_transform t;
    engine_audio_effect fx;
    uint64_t source_entity = 0, listener_entity = 0, clip = 0;
    uint32_t bus = 0;
    float near_peak, far_peak, quiet_peak;
    int i;

    host_quiet_failures();
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &g_api) != ENGINE_OK)
    {
        printf("host_audio_world: no function table\n");
        return 2;
    }
    memset(&create, 0, sizeof(create));
    create.struct_size = sizeof(create);
    memset(&engine, 0, sizeof(engine));
    CHECK_STATUS(g_api->create(&create, &engine), ENGINE_OK);
    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    CHECK_STATUS(g_api->entity_create(world, &source_entity), ENGINE_OK);
    CHECK_STATUS(g_api->entity_create(world, &listener_entity), ENGINE_OK);
    t = At(0.0f, 0.0f, -3.0f);
    CHECK_STATUS(g_api->component_set(world, source_entity, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
    t = At(0.0f, 0.0f, 0.0f);
    CHECK_STATUS(g_api->component_set(world, listener_entity, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);

    /* Without audio open a source with a clip is refused (there is no clip), one without a clip is accepted. */
    src = Source(1);
    CHECK_STATUS(g_api->component_set(world, source_entity, ENGINE_COMPONENT_AUDIO_SOURCE, &src),
                 ENGINE_ERROR_NOT_READY);
    src = Source(0);
    CHECK_STATUS(g_api->component_set(world, source_entity, ENGINE_COMPONENT_AUDIO_SOURCE, &src), ENGINE_OK);

    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.sample_rate = 48000;
    info.channels = 2;
    info.max_voices = 16;
    CHECK_STATUS(g_api->audio_open(engine, &info), ENGINE_OK);
    clip = MakeTone(engine);

    /* The source and the listener, validated. */
    src = Source(clip);
    src.gain = 40.0f;
    CHECK_STATUS(g_api->component_set(world, source_entity, ENGINE_COMPONENT_AUDIO_SOURCE, &src),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    src = Source(clip);
    src.max_distance = 0.5f; /* below min_distance */
    CHECK_STATUS(g_api->component_set(world, source_entity, ENGINE_COMPONENT_AUDIO_SOURCE, &src),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    src = Source(clip);
    src.struct_size = 8;
    CHECK_STATUS(g_api->component_set(world, source_entity, ENGINE_COMPONENT_AUDIO_SOURCE, &src),
                 ENGINE_ERROR_STRUCT_SIZE);
    src = Source(clip + 7777);
    CHECK_STATUS(g_api->component_set(world, source_entity, ENGINE_COMPONENT_AUDIO_SOURCE, &src),
                 ENGINE_ERROR_STALE_HANDLE);
    src = Source(clip);
    CHECK_STATUS(g_api->component_set(world, source_entity, ENGINE_COMPONENT_AUDIO_SOURCE, &src), ENGINE_OK);
    memset(&back, 0, sizeof(back));
    back.struct_size = sizeof(back);
    CHECK_STATUS(g_api->component_get(world, source_entity, ENGINE_COMPONENT_AUDIO_SOURCE, &back), ENGINE_OK);
    CHECK(back.clip == clip);
    CHECK(back.flags == src.flags && back.gain == 1.0f && back.max_distance == 100.0f);
    memset(&ear, 0, sizeof(ear));
    ear.struct_size = sizeof(ear);
    ear.gain = 9.0f;
    ear.active = 1;
    CHECK_STATUS(g_api->component_set(world, listener_entity, ENGINE_COMPONENT_AUDIO_LISTENER, &ear),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    ear.gain = 1.0f;
    CHECK_STATUS(g_api->component_set(world, listener_entity, ENGINE_COMPONENT_AUDIO_LISTENER, &ear), ENGINE_OK);
    memset(&ear_back, 0, sizeof(ear_back));
    ear_back.struct_size = sizeof(ear_back);
    CHECK_STATUS(g_api->component_get(world, listener_entity, ENGINE_COMPONENT_AUDIO_LISTENER, &ear_back), ENGINE_OK);
    CHECK(ear_back.gain == 1.0f && ear_back.active == 1);

    /* Heard: a few frames place the voice, a few blocks play it. */
    for (i = 0; i < 4; ++i)
    {
        Frame(engine);
        Peak(engine, 2);
    }
    near_peak = Peak(engine, 4);
    CHECK(near_peak > 0.01f);

    /* Farther away is quieter; beyond max_distance nothing is heard. */
    t = At(0.0f, 0.0f, -60.0f);
    CHECK_STATUS(g_api->component_set(world, source_entity, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
    for (i = 0; i < 4; ++i)
    {
        Frame(engine);
        Peak(engine, 4);
    }
    far_peak = Peak(engine, 4);
    CHECK(far_peak < near_peak * 0.5f);
    t = At(0.0f, 0.0f, -300.0f);
    CHECK_STATUS(g_api->component_set(world, source_entity, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
    for (i = 0; i < 4; ++i)
    {
        Frame(engine);
        Peak(engine, 8);
    }
    CHECK(Peak(engine, 4) < 1e-4f);

    /* Back in range, then the component is removed: the voice fades out. */
    t = At(0.0f, 0.0f, -3.0f);
    CHECK_STATUS(g_api->component_set(world, source_entity, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
    for (i = 0; i < 4; ++i)
    {
        Frame(engine);
        Peak(engine, 4);
    }
    CHECK(Peak(engine, 4) > 0.01f);
    CHECK_STATUS(g_api->component_remove(world, source_entity, ENGINE_COMPONENT_AUDIO_SOURCE), ENGINE_OK);
    CHECK_STATUS(g_api->component_get(world, source_entity, ENGINE_COMPONENT_AUDIO_SOURCE, &back),
                 ENGINE_ERROR_NOT_FOUND);
    for (i = 0; i < 4; ++i)
    {
        Frame(engine);
        Peak(engine, 8);
    }
    quiet_peak = Peak(engine, 4);
    CHECK(quiet_peak < 1e-4f);

    /* Audio closed and opened again: the world keeps its source, the new mixer plays it once the clip exists. */
    CHECK_STATUS(g_api->component_set(world, source_entity, ENGINE_COMPONENT_AUDIO_SOURCE, &src), ENGINE_OK);
    for (i = 0; i < 3; ++i)
    {
        Frame(engine);
    }
    CHECK_STATUS(g_api->audio_close(engine), ENGINE_OK);
    Frame(engine); /* the world lets go of the closed host */
    CHECK_STATUS(g_api->audio_open(engine, &info), ENGINE_OK);
    clip = MakeTone(engine);
    src = Source(clip);
    CHECK_STATUS(g_api->component_set(world, source_entity, ENGINE_COMPONENT_AUDIO_SOURCE, &src), ENGINE_OK);
    for (i = 0; i < 4; ++i)
    {
        Frame(engine);
        Peak(engine, 4);
    }
    CHECK(Peak(engine, 4) > 0.01f);

    /* A bus effect retuned far more often than the mixer has effect slots. */
    CHECK_STATUS(g_api->audio_bus_create(engine, (engine_string){"fx", 2}, 0, &bus), ENGINE_OK);
    memset(&fx, 0, sizeof(fx));
    fx.struct_size = sizeof(fx);
    fx.kind = ENGINE_AUDIO_EFFECT_FILTER;
    fx.params[0] = 0.0f;
    fx.params[2] = 0.7071f;
    for (i = 0; i < 200; ++i)
    {
        fx.params[1] = 500.0f + (float)i * 10.0f;
        CHECK_STATUS(g_api->audio_bus_effect_set(engine, bus, 0, &fx), ENGINE_OK);
    }
    fx.kind = ENGINE_AUDIO_EFFECT_REVERB;
    fx.params[0] = 1.0f;
    fx.params[1] = 1.5f;
    fx.params[2] = 0.5f;
    fx.params[3] = 0.3f;
    fx.params[4] = 1.0f;
    for (i = 0; i < 200; ++i)
    {
        fx.params[3] = 0.1f + (float)(i % 5) * 0.1f;
        CHECK_STATUS(g_api->audio_bus_effect_set(engine, bus, 1, &fx), ENGINE_OK);
    }

    /* audio_render with a handle that names nothing does not touch the engine. */
    {
        engine_handle stale = engine;
        float out[256 * 2];
        stale.value += 0x100000000ull;
        CHECK_STATUS(g_api->audio_render(stale, 256, out), ENGINE_ERROR_STALE_HANDLE);
    }

    /* audio_close while another thread calls the table (no crash, whatever the answers). */
    {
        closer c;
        host_thread thread;
        c.engine = engine;
        c.rounds = 20000;
        CHECK(host_thread_start(&thread, CallerThread, &c) == 0);
        for (i = 0; i < 20; ++i)
        {
            (void)g_api->audio_close(engine);
            (void)g_api->audio_open(engine, &info);
        }
        host_thread_join(&thread);
    }

    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
    (void)g_api->audio_close(engine);
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    if (g_failures != 0)
    {
        printf("host_audio_world: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("host_audio_world: ok\n");
    return 0;
}
