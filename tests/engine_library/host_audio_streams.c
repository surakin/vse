/* Phase 26 Day 439: the stream, music, effect and snapshot functions of the C API: every refusal, a clip stream, a host
   callback stream, a music cross-fade, an effect and a snapshot that is restored. */
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
            printf("host_audio_streams: line %d: CHECK failed: %s\n", __LINE__, #condition);                           \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

#define CHECK_STATUS(call, expected)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status got_ = (call);                                                                             \
        if (got_ != (expected))                                                                                        \
        {                                                                                                              \
            printf("host_audio_streams: line %d: %s returned %d, expected %d\n", __LINE__, #call, (int)got_,           \
                   (int)(expected));                                                                                   \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

typedef struct tone
{
    uint64_t position;
    uint64_t length;
    uint32_t reads;
} tone;

static uint32_t ToneRead(void* user, float* out, uint32_t frames)
{
    tone* t = (tone*)user;
    uint32_t i;
    ++t->reads;
    for (i = 0; i < frames && t->position < t->length; ++i, ++t->position)
    {
        out[2 * i] = 0.25f * sinf(0.05f * (float)t->position);
        out[2 * i + 1] = out[2 * i];
    }
    return i;
}

static uint32_t NanRead(void* user, float* out, uint32_t frames)
{
    uint32_t i;
    (void)user;
    for (i = 0; i < 2 * frames; ++i)
    {
        out[i] = (i & 1) ? INFINITY : NAN;
    }
    return frames;
}

static int ToneSeek(void* user, uint64_t frame)
{
    tone* t = (tone*)user;
    t->position = frame < t->length ? frame : t->length;
    return 0;
}

static engine_audio_stream_source Source(tone* t)
{
    engine_audio_stream_source s;
    memset(&s, 0, sizeof(s));
    s.struct_size = sizeof(s);
    s.sample_rate = 48000;
    s.channels = 2;
    s.frames = t->length;
    s.read = ToneRead;
    s.seek = ToneSeek;
    s.user = t;
    return s;
}

static float Rms(const float* samples, uint32_t count)
{
    double sum = 0.0;
    uint32_t i;
    for (i = 0; i < count; ++i)
    {
        sum += (double)samples[i] * samples[i];
    }
    return (float)sqrt(sum / count);
}

int main(void)
{
    engine_create_info create;
    engine_handle engine;
    engine_audio_info info;
    engine_audio_clip_info clip_info;
    engine_audio_stream_source bad;
    engine_audio_effect fx;
    engine_audio_tempo tempo;
    engine_audio_play play;
    engine_buffer snap;
    tone a = {0, 100000, 0};
    tone b = {0, 100000, 0};
    float samples[512];
    float out[256 * 2];
    float out2[256 * 2];
    uint64_t clip = 0, stream = 0, stream_b = 0, voice = 0, none = 0;
    uint32_t bus = 0;
    int i;

    host_quiet_failures();
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &g_api) != ENGINE_OK)
    {
        printf("host_audio_streams: no function table\n");
        return 2;
    }
    memset(&create, 0, sizeof(create));
    create.struct_size = sizeof(create);
    memset(&engine, 0, sizeof(engine));
    CHECK_STATUS(g_api->create(&create, &engine), ENGINE_OK);

    /* Before open everything is NOT_READY. */
    CHECK_STATUS(g_api->audio_stream_open_clip(engine, 1, &stream), ENGINE_ERROR_NOT_READY);
    CHECK_STATUS(g_api->audio_update(engine, 0), ENGINE_ERROR_NOT_READY);
    CHECK_STATUS(g_api->audio_music_stop(engine, 0), ENGINE_ERROR_NOT_READY);
    memset(&snap, 0, sizeof(snap));
    CHECK_STATUS(g_api->audio_snapshot(engine, &snap), ENGINE_ERROR_NOT_READY);

    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.sample_rate = 48000;
    info.channels = 2;
    info.max_voices = 16;
    CHECK_STATUS(g_api->audio_open(engine, &info), ENGINE_OK);
    CHECK_STATUS(g_api->audio_bus_create(engine, (engine_string){"music", 5}, 0, &bus), ENGINE_OK);

    for (i = 0; i < 512; ++i)
    {
        samples[i] = 0.3f * sinf(0.1f * (float)i);
    }
    memset(&clip_info, 0, sizeof(clip_info));
    clip_info.struct_size = sizeof(clip_info);
    clip_info.sample_rate = 48000;
    clip_info.channels = 1;
    clip_info.format = ENGINE_AUDIO_FORMAT_F32;
    CHECK_STATUS(g_api->audio_clip_create(engine, &clip_info, samples, sizeof(samples), &clip), ENGINE_OK);

    /* Refusals. */
    CHECK_STATUS(g_api->audio_stream_open_clip(engine, clip, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->audio_stream_open_clip(engine, 0, &stream), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(g_api->audio_stream_open(engine, NULL, &stream), ENGINE_ERROR_INVALID_ARGUMENT);
    bad = Source(&a);
    bad.struct_size = 8;
    CHECK_STATUS(g_api->audio_stream_open(engine, &bad, &stream), ENGINE_ERROR_STRUCT_SIZE);
    bad = Source(&a);
    bad.read = NULL;
    CHECK_STATUS(g_api->audio_stream_open(engine, &bad, &stream), ENGINE_ERROR_INVALID_ARGUMENT);
    bad = Source(&a);
    bad.channels = 3;
    CHECK_STATUS(g_api->audio_stream_open(engine, &bad, &stream), ENGINE_ERROR_INVALID_ARGUMENT);
    bad = Source(&a);
    bad.frames = 0;
    CHECK_STATUS(g_api->audio_stream_open(engine, &bad, &stream), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->audio_stream_close(engine, 0), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(g_api->audio_music_play(engine, 0, bus, 0, NULL), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(g_api->audio_music_stinger(engine, 0, 1.0f, 0, NULL), ENGINE_ERROR_STALE_HANDLE);
    memset(&fx, 0, sizeof(fx));
    fx.struct_size = 4;
    CHECK_STATUS(g_api->audio_bus_effect_set(engine, bus, 0, &fx), ENGINE_ERROR_STRUCT_SIZE);
    fx.struct_size = sizeof(fx);
    fx.kind = 99;
    CHECK_STATUS(g_api->audio_bus_effect_set(engine, bus, 0, &fx), ENGINE_ERROR_INVALID_ARGUMENT);
    fx.kind = ENGINE_AUDIO_EFFECT_FILTER;
    CHECK_STATUS(g_api->audio_bus_effect_set(engine, bus, 9, &fx), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->audio_bus_effect_set(engine, 1000, 0, &fx), ENGINE_ERROR_INVALID_ARGUMENT);
    for (i = 0; i < 8; ++i)
    {
        CHECK_STATUS(g_api->audio_render(engine, 256, out), ENGINE_OK);
    }
    CHECK_STATUS(g_api->audio_restore(engine, NULL, 10), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->audio_restore(engine, samples, 16), ENGINE_ERROR_CORRUPT);

    /* A stream over a clip, and one over the host's callbacks. */
    CHECK_STATUS(g_api->audio_stream_open_clip(engine, clip, &stream), ENGINE_OK);
    bad = Source(&b);
    CHECK_STATUS(g_api->audio_stream_open(engine, &bad, &stream_b), ENGINE_OK);
    memset(&play, 0, sizeof(play));
    play.struct_size = sizeof(play);
    play.bus = bus;
    play.gain = 1.0f;
    play.pitch = 1.0f;
    CHECK_STATUS(g_api->audio_stream_play(engine, stream_b, &play, &voice), ENGINE_OK);
    CHECK_STATUS(g_api->audio_update(engine, 1), ENGINE_OK);
    CHECK_STATUS(g_api->audio_render(engine, 256, out), ENGINE_OK);
    CHECK(b.reads > 0);
    CHECK_STATUS(g_api->audio_render(engine, 256, out), ENGINE_OK);
    CHECK(Rms(out, 512) > 0.01f);

    /* An effect on the bus and a snapshot that comes back. */
    fx.kind = ENGINE_AUDIO_EFFECT_FILTER;
    fx.params[0] = 0;
    fx.params[1] = 2000.0f;
    fx.params[2] = 0.7f;
    CHECK_STATUS(g_api->audio_bus_effect_set(engine, bus, 0, &fx), ENGINE_OK);
    for (i = 0; i < 8; ++i)
    {
        CHECK_STATUS(g_api->audio_update(engine, 1), ENGINE_OK);
        CHECK_STATUS(g_api->audio_render(engine, 256, out), ENGINE_OK);
    }
    CHECK_STATUS(g_api->audio_snapshot(engine, &snap), ENGINE_OK);
    CHECK(snap.size > 100);
    CHECK_STATUS(g_api->audio_update(engine, 1), ENGINE_OK);
    CHECK_STATUS(g_api->audio_render(engine, 256, out), ENGINE_OK);
    CHECK_STATUS(g_api->audio_restore(engine, snap.data, snap.size), ENGINE_OK);
    CHECK_STATUS(g_api->audio_update(engine, 1), ENGINE_OK);
    CHECK_STATUS(g_api->audio_render(engine, 256, out2), ENGINE_OK);
    CHECK(memcmp(out, out2, sizeof(out)) == 0);
    CHECK_STATUS(g_api->audio_restore(engine, snap.data, snap.size - 7), ENGINE_ERROR_CORRUPT);
    if (snap.release)
    {
        snap.release(&snap);
    }

    /* Music: play a track, cross-fade to another. */
    tempo.struct_size = sizeof(tempo);
    tempo.beats_per_minute = 0.0f;
    tempo.beats_per_bar = 4;
    CHECK_STATUS(g_api->audio_music_play(engine, stream, bus, 0, &tempo), ENGINE_ERROR_INVALID_ARGUMENT);
    tempo.beats_per_minute = 120.0f;
    CHECK_STATUS(g_api->audio_music_play(engine, stream_b, bus, 480, &tempo), ENGINE_OK);
    CHECK_STATUS(g_api->audio_update(engine, 1), ENGINE_OK);
    CHECK_STATUS(g_api->audio_render(engine, 256, out), ENGINE_OK);
    CHECK_STATUS(g_api->audio_music_crossfade(engine, stream, 960, 0, &tempo), ENGINE_OK);
    for (i = 0; i < 8; ++i)
    {
        CHECK_STATUS(g_api->audio_update(engine, 1), ENGINE_OK);
        CHECK_STATUS(g_api->audio_render(engine, 256, out), ENGINE_OK);
    }
    CHECK_STATUS(g_api->audio_music_stop(engine, 480), ENGINE_OK);

    /* The music stays on the bus of its first call. */
    {
        uint32_t other = 0;
        CHECK_STATUS(g_api->audio_bus_create(engine, (engine_string){"other", 5}, 0, &other), ENGINE_OK);
        CHECK_STATUS(g_api->audio_music_play(engine, stream, other, 0, &tempo), ENGINE_ERROR_INVALID_ARGUMENT);
    }

    /* Samples that are not finite play as silence. */
    {
        engine_audio_stream_source nan_source = Source(&a);
        uint64_t nan_stream = 0, nan_voice = 0;
        nan_source.read = NanRead;
        CHECK_STATUS(g_api->audio_stream_open(engine, &nan_source, &nan_stream), ENGINE_OK);
        CHECK_STATUS(g_api->audio_stream_play(engine, nan_stream, &play, &nan_voice), ENGINE_OK);
        for (i = 0; i < 4; ++i)
        {
            int j;
            CHECK_STATUS(g_api->audio_update(engine, 1), ENGINE_OK);
            CHECK_STATUS(g_api->audio_render(engine, 256, out), ENGINE_OK);
            for (j = 0; j < 512; ++j)
            {
                CHECK(isfinite(out[j]));
            }
        }
        CHECK_STATUS(g_api->audio_stream_close(engine, nan_stream), ENGINE_OK);
    }

    /* Closing a stream, twice, and after the engine closes: the source is not read again once close returned. */
    {
        uint32_t reads_before_close;
        CHECK_STATUS(g_api->audio_stream_close(engine, stream_b), ENGINE_OK);
        reads_before_close = b.reads;
        for (i = 0; i < 40; ++i)
        {
            CHECK_STATUS(g_api->audio_update(engine, 1), ENGINE_OK);
            CHECK_STATUS(g_api->audio_render(engine, 256, out), ENGINE_OK);
        }
        CHECK(b.reads == reads_before_close);
    }
    CHECK_STATUS(g_api->audio_stream_close(engine, stream_b), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(g_api->audio_stream_close(engine, stream), ENGINE_OK);
    CHECK_STATUS(g_api->audio_close(engine), ENGINE_OK);
    CHECK_STATUS(g_api->audio_stream_close(engine, none), ENGINE_ERROR_NOT_READY);
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    g_api->thread_detach();
    if (g_failures != 0)
    {
        printf("host_audio_streams: %d check(s) failed\n", g_failures);
        return 1;
    }
    printf("host_audio_streams: ok\n");
    return 0;
}
