/* Phase 26 Day 438: a C program opens the engine's host-driven audio, makes a clip from PCM it owns, plays and controls
   voices, renders the mix itself from "its audio callback" (also from a second thread), polls the events and checks
   every refusal the API documents. */
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
            printf("host_audio: line %d: CHECK failed: %s\n", __LINE__, #condition);                                   \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

#define CHECK_STATUS(call, expected)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status got_ = (call);                                                                             \
        if (got_ != (expected))                                                                                        \
        {                                                                                                              \
            printf("host_audio: line %d: %s returned %d, expected %d\n", __LINE__, #call, (int)got_, (int)(expected)); \
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

static engine_audio_info Info(uint32_t channels)
{
    engine_audio_info info;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.sample_rate = 48000;
    info.channels = channels;
    info.max_voices = 16;
    return info;
}

/* A ramp of `frames` mono frames: sample i is (i + 1) / 4096, as 32-bit floats. */
static void MakeRamp(float* out, uint32_t frames)
{
    uint32_t i;
    for (i = 0; i < frames; ++i)
    {
        out[i] = (float)(i + 1) / 4096.0f;
    }
}

static engine_audio_clip_info ClipInfo(uint32_t channels, uint32_t format)
{
    engine_audio_clip_info info;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.sample_rate = 48000;
    info.channels = channels;
    info.format = format;
    return info;
}

static engine_audio_play Play(uint64_t clip)
{
    engine_audio_play play;
    memset(&play, 0, sizeof(play));
    play.struct_size = sizeof(play);
    play.clip = clip;
    play.gain = 1.0f;
    play.pitch = 1.0f;
    return play;
}

static void TestArguments(engine_handle engine)
{
    engine_audio_info info = Info(2);
    engine_audio_clip_info clip_info = ClipInfo(1, ENGINE_AUDIO_FORMAT_F32);
    engine_audio_play play = Play(0);
    engine_audio_bus_params bus;
    engine_audio_voice_params voice;
    float samples[64];
    float out[512];
    uint64_t clip = 0;
    uint64_t voice_id = 0;
    uint32_t bus_id = 0;
    engine_handle none;
    memset(&none, 0, sizeof(none));
    memset(samples, 0, sizeof(samples));

    /* Before audio_open everything but open says NOT_READY. */
    CHECK_STATUS(g_api->audio_render(engine, 256, out), ENGINE_ERROR_NOT_READY);
    CHECK_STATUS(g_api->audio_clip_create(engine, &clip_info, samples, sizeof(samples), &clip), ENGINE_ERROR_NOT_READY);
    CHECK_STATUS(g_api->audio_close(engine), ENGINE_ERROR_NOT_READY);
    CHECK_STATUS(g_api->audio_open(none, &info), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->audio_open(engine, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    info.struct_size = 8;
    CHECK_STATUS(g_api->audio_open(engine, &info), ENGINE_ERROR_STRUCT_SIZE);
    info = Info(3);
    CHECK_STATUS(g_api->audio_open(engine, &info), ENGINE_ERROR_UNSUPPORTED);
    info = Info(2);
    info.sample_rate = 100;
    CHECK_STATUS(g_api->audio_open(engine, &info), ENGINE_ERROR_INVALID_ARGUMENT);
    info = Info(2);
    info.max_voices = 5000;
    CHECK_STATUS(g_api->audio_open(engine, &info), ENGINE_ERROR_INVALID_ARGUMENT);
    info = Info(2);
    CHECK_STATUS(g_api->audio_open(engine, &info), ENGINE_OK);
    CHECK_STATUS(g_api->audio_open(engine, &info), ENGINE_ERROR_ALREADY_EXISTS);

    /* Clips. */
    CHECK_STATUS(g_api->audio_clip_create(engine, &clip_info, samples, sizeof(samples), NULL),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->audio_clip_create(engine, NULL, samples, sizeof(samples), &clip),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->audio_clip_create(engine, &clip_info, NULL, sizeof(samples), &clip),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->audio_clip_create(engine, &clip_info, samples, 0, &clip), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->audio_clip_create(engine, &clip_info, samples, 7, &clip), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->audio_clip_create(engine, &clip_info, samples, (size_t)1 << 40, &clip), ENGINE_ERROR_TOO_LARGE);
    clip_info.channels = 3;
    CHECK_STATUS(g_api->audio_clip_create(engine, &clip_info, samples, sizeof(samples), &clip),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    clip_info = ClipInfo(1, 9);
    CHECK_STATUS(g_api->audio_clip_create(engine, &clip_info, samples, sizeof(samples), &clip),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    clip_info = ClipInfo(1, ENGINE_AUDIO_FORMAT_F32);
    clip_info.loop_start = 10;
    clip_info.loop_end = 5;
    CHECK_STATUS(g_api->audio_clip_create(engine, &clip_info, samples, sizeof(samples), &clip),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    clip_info.loop_start = 0;
    clip_info.loop_end = 65;
    CHECK_STATUS(g_api->audio_clip_create(engine, &clip_info, samples, sizeof(samples), &clip),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    samples[3] = (float)NAN;
    clip_info = ClipInfo(1, ENGINE_AUDIO_FORMAT_F32);
    CHECK_STATUS(g_api->audio_clip_create(engine, &clip_info, samples, sizeof(samples), &clip),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    samples[3] = 0.0f;
    CHECK_STATUS(g_api->audio_clip_create(engine, &clip_info, samples, sizeof(samples), &clip), ENGINE_OK);
    CHECK(clip != 0);
    CHECK_STATUS(g_api->audio_clip_destroy(engine, clip), ENGINE_OK);
    CHECK_STATUS(g_api->audio_clip_destroy(engine, clip), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(g_api->audio_clip_destroy(engine, 0), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(g_api->audio_clip_create(engine, &clip_info, samples, sizeof(samples), &clip), ENGINE_OK);

    /* Voices. */
    play = Play(clip);
    CHECK_STATUS(g_api->audio_play(engine, &play, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->audio_play(engine, NULL, &voice_id), ENGINE_ERROR_INVALID_ARGUMENT);
    play.struct_size = 4;
    CHECK_STATUS(g_api->audio_play(engine, &play, &voice_id), ENGINE_ERROR_STRUCT_SIZE);
    play = Play(clip + 1000);
    CHECK_STATUS(g_api->audio_play(engine, &play, &voice_id), ENGINE_ERROR_STALE_HANDLE);
    play = Play(clip);
    play.gain = (float)NAN;
    CHECK_STATUS(g_api->audio_play(engine, &play, &voice_id), ENGINE_ERROR_INVALID_ARGUMENT);
    play = Play(clip);
    play.pitch = 100.0f;
    CHECK_STATUS(g_api->audio_play(engine, &play, &voice_id), ENGINE_ERROR_INVALID_ARGUMENT);
    play = Play(clip);
    play.pan = 2.0f;
    CHECK_STATUS(g_api->audio_play(engine, &play, &voice_id), ENGINE_ERROR_INVALID_ARGUMENT);
    play = Play(clip);
    play.bus = 9;
    CHECK(g_api->audio_play(engine, &play, &voice_id) != ENGINE_OK);
    play = Play(clip);
    CHECK_STATUS(g_api->audio_play(engine, &play, &voice_id), ENGINE_OK);
    CHECK(voice_id != 0);
    memset(&voice, 0, sizeof(voice));
    voice.struct_size = sizeof(voice);
    voice.mask = ENGINE_AUDIO_VOICE_GAIN;
    voice.gain = 0.5f;
    CHECK_STATUS(g_api->audio_voice_set(engine, voice_id, &voice), ENGINE_OK);
    CHECK_STATUS(g_api->audio_voice_set(engine, voice_id, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    voice.mask = 64;
    CHECK_STATUS(g_api->audio_voice_set(engine, voice_id, &voice), ENGINE_ERROR_INVALID_ARGUMENT);
    voice.mask = ENGINE_AUDIO_VOICE_PAN;
    voice.pan = 5.0f;
    CHECK_STATUS(g_api->audio_voice_set(engine, voice_id, &voice), ENGINE_ERROR_INVALID_ARGUMENT);
    voice.struct_size = 4;
    CHECK_STATUS(g_api->audio_voice_set(engine, voice_id, &voice), ENGINE_ERROR_STRUCT_SIZE);
    voice.struct_size = sizeof(voice);
    voice.mask = ENGINE_AUDIO_VOICE_GAIN;
    CHECK_STATUS(g_api->audio_voice_set(engine, 0, &voice), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(g_api->audio_voice_set(engine, voice_id + ((uint64_t)7 << 32), &voice), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(g_api->audio_voice_stop(engine, 0, 0), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(g_api->audio_voice_stop(engine, voice_id, 0), ENGINE_OK);

    /* Buses. */
    CHECK_STATUS(g_api->audio_bus_create(engine, (engine_string){"music", 5}, 0, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->audio_bus_create(engine, (engine_string){NULL, 5}, 0, &bus_id), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->audio_bus_create(engine, (engine_string){"", 0}, 0, &bus_id), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK(g_api->audio_bus_create(engine, (engine_string){"orphan", 6}, 77, &bus_id) != ENGINE_OK);
    CHECK_STATUS(g_api->audio_bus_create(engine, (engine_string){"music", 5}, 0, &bus_id), ENGINE_OK);
    CHECK(bus_id != 0);
    memset(&bus, 0, sizeof(bus));
    bus.struct_size = sizeof(bus);
    bus.mask = ENGINE_AUDIO_BUS_GAIN | ENGINE_AUDIO_BUS_MUTE;
    bus.gain = 0.5f;
    CHECK_STATUS(g_api->audio_bus_set(engine, bus_id, &bus), ENGINE_OK);
    CHECK(g_api->audio_bus_set(engine, 30, &bus) != ENGINE_OK);
    bus.mask = ENGINE_AUDIO_BUS_LIMITER;
    bus.limiter = 1;
    CHECK_STATUS(g_api->audio_bus_set(engine, bus_id, &bus), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->audio_bus_set(engine, 0, &bus), ENGINE_OK);
    bus.mask = 128;
    CHECK_STATUS(g_api->audio_bus_set(engine, 0, &bus), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->audio_bus_set(engine, 0, NULL), ENGINE_ERROR_INVALID_ARGUMENT);

    /* Render and events. */
    CHECK_STATUS(g_api->audio_render(engine, 256, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->audio_render(none, 256, out), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(g_api->audio_render(engine, 256, out), ENGINE_OK);
    {
        engine_audio_event events[4];
        size_t total = 99;
        memset(events, 0, sizeof(events));
        CHECK_STATUS(g_api->audio_events(engine, events, 4, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->audio_events(engine, NULL, 4, &total), ENGINE_ERROR_INVALID_ARGUMENT);
        events[0].struct_size = 4;
        CHECK_STATUS(g_api->audio_events(engine, events, 4, &total), ENGINE_ERROR_STRUCT_SIZE);
        CHECK_STATUS(g_api->audio_events(engine, NULL, 0, &total), ENGINE_OK);
    }
    {
        engine_audio_state state;
        memset(&state, 0, sizeof(state));
        CHECK_STATUS(g_api->audio_state(engine, &state), ENGINE_ERROR_STRUCT_SIZE);
        state.struct_size = sizeof(state);
        CHECK_STATUS(g_api->audio_state(engine, &state), ENGINE_OK);
        CHECK(state.sample_rate == 48000 && state.channels == 2 && state.blocks_rendered >= 1);
    }

    CHECK_STATUS(g_api->audio_close(engine), ENGINE_OK);
    CHECK_STATUS(g_api->audio_close(engine), ENGINE_ERROR_NOT_READY);
    CHECK_STATUS(g_api->audio_play(engine, &play, &voice_id), ENGINE_ERROR_NOT_READY);
}

/* A host-supplied clip plays through audio_render and gives the samples back, at any callback size. */
static void TestRender(engine_handle engine)
{
    engine_audio_info info = Info(1);
    engine_audio_clip_info clip_info = ClipInfo(1, ENGINE_AUDIO_FORMAT_F32);
    engine_audio_play play;
    float ramp[600];
    float out[2048];
    float again[2048];
    uint64_t clip = 0;
    uint64_t voice = 0;
    uint32_t i;
    unsigned char s16[8];

    MakeRamp(ramp, 600);
    CHECK_STATUS(g_api->audio_open(engine, &info), ENGINE_OK);
    CHECK_STATUS(g_api->audio_clip_create(engine, &clip_info, ramp, sizeof(ramp), &clip), ENGINE_OK);
    play = Play(clip);
    CHECK_STATUS(g_api->audio_play(engine, &play, &voice), ENGINE_OK);
    /* The command takes effect after the engine's lead (a block or two); the ramp then appears, whole, once. */
    for (i = 0; i < 2048; i += 128)
    {
        CHECK_STATUS(g_api->audio_render(engine, 128, out + i), ENGINE_OK);
    }
    {
        uint32_t start = 0;
        while (start < 2048 && out[start] == 0.0f)
        {
            start += 1;
        }
        CHECK(start < 1024);
        for (i = 0; i < 600 && start + i < 2048; ++i)
        {
            CHECK(out[start + i] == ramp[i]);
        }
        CHECK(start + 600 <= 2048 && out[start + 600] == 0.0f);
    }
    /* A second engine run with another callback size gives the same samples. */
    CHECK_STATUS(g_api->audio_close(engine), ENGINE_OK);
    CHECK_STATUS(g_api->audio_open(engine, &info), ENGINE_OK);
    CHECK_STATUS(g_api->audio_clip_create(engine, &clip_info, ramp, sizeof(ramp), &clip), ENGINE_OK);
    play = Play(clip);
    CHECK_STATUS(g_api->audio_play(engine, &play, &voice), ENGINE_OK);
    for (i = 0; i < 2048; i += 1024)
    {
        CHECK_STATUS(g_api->audio_render(engine, 1024, again + i), ENGINE_OK);
    }
    CHECK(memcmp(out, again, sizeof(out)) == 0);
    /* The finished event, the stale id after it, and signed 16-bit PCM. */
    {
        engine_audio_event events[8];
        size_t total = 0;
        memset(events, 0, sizeof(events));
        for (i = 0; i < 8; ++i)
        {
            events[i].struct_size = sizeof(events[i]);
        }
        CHECK_STATUS(g_api->audio_events(engine, events, 8, &total), ENGINE_OK);
        CHECK(total >= 1 && events[0].kind == ENGINE_AUDIO_EVENT_FINISHED && events[0].voice == voice);
    }
    clip_info = ClipInfo(1, ENGINE_AUDIO_FORMAT_S16);
    s16[0] = 0x00;
    s16[1] = 0x40; /* 16384 / 32768 = 0.5 */
    s16[2] = 0x00;
    s16[3] = 0xC0; /* -16384 / 32768 = -0.5 */
    s16[4] = 0x00;
    s16[5] = 0x40;
    s16[6] = 0x00;
    s16[7] = 0xC0;
    CHECK_STATUS(g_api->audio_clip_create(engine, &clip_info, s16, 8, &clip), ENGINE_OK);
    play = Play(clip);
    CHECK_STATUS(g_api->audio_play(engine, &play, &voice), ENGINE_OK);
    CHECK_STATUS(g_api->audio_render(engine, 512, out), ENGINE_OK);
    {
        uint32_t seen = 0;
        for (i = 0; i < 512; ++i)
        {
            if (out[i] == 0.5f)
            {
                seen += 1;
            }
        }
        CHECK(seen == 2);
    }
    CHECK_STATUS(g_api->audio_close(engine), ENGINE_OK);
}

typedef struct render_thread
{
    engine_handle engine;
    int blocks;
    int failed;
} render_thread;

static void RenderLoop(void* arg)
{
    render_thread* t = (render_thread*)arg;
    float out[256];
    int i;
    for (i = 0; i < t->blocks; ++i)
    {
        if (g_api->audio_render(t->engine, 128, out) != ENGINE_OK)
        {
            t->failed += 1;
        }
    }
}

/* The control calls run from this thread while another thread renders: no failure, no crash (the sanitizer flavors
   check the rest). */
static void TestThreads(engine_handle engine)
{
    engine_audio_info info = Info(2);
    engine_audio_clip_info clip_info = ClipInfo(2, ENGINE_AUDIO_FORMAT_F32);
    float samples[2000];
    uint64_t clip = 0;
    int i;
    render_thread t;
    host_thread thread;
    for (i = 0; i < 2000; ++i)
    {
        samples[i] = (float)sin(0.05 * i) * 0.3f;
    }
    CHECK_STATUS(g_api->audio_open(engine, &info), ENGINE_OK);
    CHECK_STATUS(g_api->audio_clip_create(engine, &clip_info, samples, sizeof(samples), &clip), ENGINE_OK);
    t.engine = engine;
    t.blocks = 2000;
    t.failed = 0;
    CHECK(host_thread_start(&thread, RenderLoop, &t) == 0);
    for (i = 0; i < 400; ++i)
    {
        engine_audio_play play = Play(clip);
        uint64_t voice = 0;
        play.loop = i & 1;
        if (g_api->audio_play(engine, &play, &voice) == ENGINE_OK && (i % 3) == 0)
        {
            g_api->audio_voice_stop(engine, voice, 0);
        }
    }
    host_thread_join(&thread);
    CHECK(t.failed == 0);
    CHECK_STATUS(g_api->audio_close(engine), ENGINE_OK);
}

int main(void)
{
    engine_handle engine;
    host_quiet_failures();
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &g_api) != ENGINE_OK)
    {
        printf("host_audio: no function table\n");
        return 2;
    }
    engine = NewEngine();
    TestArguments(engine);
    TestRender(engine);
    TestThreads(engine);
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    g_api->thread_detach();
    if (g_failures != 0)
    {
        printf("host_audio: %d check(s) failed\n", g_failures);
        return 1;
    }
    printf("host_audio: ok\n");
    return 0;
}
