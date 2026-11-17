/* Phase 26 Day 439: the audio sample. A C host renders sound through the table, offline: two streamed music tracks made
   by host callbacks, a cross-fade (the music player keeps its own schedule outside a snapshot, so every music action
   ends before block 300), a stinger on the beat, a dialogue voice that ducks the music, a reverb on a send, 600 blocks,
   a snapshot at block 300 and a restore that replays blocks 300 to 599 bit for bit. It prints the checksum of the
   render for this build and a table of levels (other machines compare the table within a tolerance, not the checksum).
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "host_os.h"
#include "engine/engine_c.h"

#define BLOCKS 600
#define SNAP_AT 300
#define BLOCK_FRAMES 256

static int g_failures = 0;
static const engine_api* g_api = NULL;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("audio_sample: line %d: CHECK failed: %s\n", __LINE__, #condition);                                 \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

#define CHECK_STATUS(call, expected)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status got_ = (call);                                                                             \
        if (got_ != (expected))                                                                                        \
        {                                                                                                              \
            printf("audio_sample: line %d: %s returned %d, expected %d\n", __LINE__, #call, (int)got_,                 \
                   (int)(expected));                                                                                   \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

typedef struct tone
{
    uint64_t position;
    float w;
    uint64_t length;
} tone;

static uint32_t ToneRead(void* user, float* out, uint32_t frames)
{
    tone* t = (tone*)user;
    uint32_t i;
    for (i = 0; i < frames && t->position < t->length; ++i, ++t->position)
    {
        const float v = 0.2f * sinf(t->w * (float)t->position);
        out[2 * i] = v;
        out[2 * i + 1] = 0.8f * v;
    }
    return i;
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

static uint64_t Hash(uint64_t h, const float* samples, uint32_t count)
{
    uint32_t i;
    for (i = 0; i < count; ++i)
    {
        uint32_t bits;
        memcpy(&bits, &samples[i], 4);
        h = (h ^ bits) * 1099511628211ull;
    }
    return h;
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

static float g_audio[BLOCKS][BLOCK_FRAMES * 2];
static float g_replay[BLOCKS][BLOCK_FRAMES * 2];

static void RenderBlock(engine_handle engine, float* out)
{
    CHECK_STATUS(g_api->audio_update(engine, 1), ENGINE_OK);
    CHECK_STATUS(g_api->audio_render(engine, BLOCK_FRAMES, out), ENGINE_OK);
}

int main(void)
{
    engine_create_info create;
    engine_handle engine;
    engine_audio_info info;
    engine_audio_clip_info clip_info;
    engine_audio_play play;
    engine_audio_effect reverb;
    engine_audio_tempo tempo;
    tone a = {0, 0.031f, 400000};
    tone b = {0, 0.047f, 400000};
    float blip[3000];
    uint64_t clip = 0, stream_a = 0, stream_b = 0, voice = 0, stinger_frame = 0;
    uint32_t music_bus = 0, fx_bus = 0, dialogue_bus = 0;
    engine_buffer snapshot;
    uint64_t checksum = 1469598103934665603ull;
    int block, i;

    host_quiet_failures();
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &g_api) != ENGINE_OK)
    {
        printf("audio_sample: no function table\n");
        return 2;
    }
    memset(&create, 0, sizeof(create));
    create.struct_size = sizeof(create);
    memset(&engine, 0, sizeof(engine));
    CHECK_STATUS(g_api->create(&create, &engine), ENGINE_OK);
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.sample_rate = 48000;
    info.channels = 2;
    info.max_voices = 16;
    info.limiter = 1;
    CHECK_STATUS(g_api->audio_open(engine, &info), ENGINE_OK);
    CHECK_STATUS(g_api->audio_bus_create(engine, (engine_string){"music", 5}, 0, &music_bus), ENGINE_OK);
    CHECK_STATUS(g_api->audio_bus_create(engine, (engine_string){"fx", 2}, 0, &fx_bus), ENGINE_OK);
    CHECK_STATUS(g_api->audio_bus_create(engine, (engine_string){"dialogue", 8}, 0, &dialogue_bus), ENGINE_OK);
    memset(&reverb, 0, sizeof(reverb));
    reverb.struct_size = sizeof(reverb);
    reverb.kind = ENGINE_AUDIO_EFFECT_REVERB;
    reverb.params[0] = 1.0f;
    reverb.params[1] = 1.2f;
    reverb.params[2] = 0.4f;
    reverb.params[3] = 0.5f;
    reverb.params[4] = 0.0f;
    CHECK_STATUS(g_api->audio_bus_effect_set(engine, fx_bus, 0, &reverb), ENGINE_OK);
    CHECK_STATUS(g_api->audio_bus_send(engine, music_bus, 0, fx_bus, 0.3f, 0), ENGINE_OK);
    CHECK_STATUS(g_api->audio_bus_duck(engine, music_bus, dialogue_bus, 0.4f, 0.05f, 9600.0f), ENGINE_OK);

    for (i = 0; i < 3000; ++i)
    {
        blip[i] = 0.5f * sinf(0.3f * (float)i) * (1.0f - (float)i / 3000.0f);
    }
    memset(&clip_info, 0, sizeof(clip_info));
    clip_info.struct_size = sizeof(clip_info);
    clip_info.sample_rate = 48000;
    clip_info.channels = 1;
    clip_info.format = ENGINE_AUDIO_FORMAT_F32;
    CHECK_STATUS(g_api->audio_clip_create(engine, &clip_info, blip, sizeof(blip), &clip), ENGINE_OK);
    {
        engine_audio_stream_source sa = Source(&a);
        engine_audio_stream_source sb = Source(&b);
        CHECK_STATUS(g_api->audio_stream_open(engine, &sa, &stream_a), ENGINE_OK);
        CHECK_STATUS(g_api->audio_stream_open(engine, &sb, &stream_b), ENGINE_OK);
    }
    tempo.struct_size = sizeof(tempo);
    tempo.beats_per_minute = 120.0f;
    tempo.beats_per_bar = 4;
    CHECK_STATUS(g_api->audio_music_play(engine, stream_a, music_bus, 4800, &tempo), ENGINE_OK);

    memset(&snapshot, 0, sizeof(snapshot));
    for (block = 0; block < BLOCKS; ++block)
    {
        if (block == 100)
        {
            CHECK_STATUS(g_api->audio_music_crossfade(engine, stream_b, 20000, 0, &tempo), ENGINE_OK);
        }
        if (block == 150)
        {
            CHECK_STATUS(g_api->audio_music_stinger(engine, clip, 0.6f, 1, &stinger_frame), ENGINE_OK);
        }
        if (block == 200)
        {
            memset(&play, 0, sizeof(play));
            play.struct_size = sizeof(play);
            play.clip = clip;
            play.bus = dialogue_bus;
            play.gain = 0.5f;
            play.pitch = 1.0f;
            play.loop = 1;
            CHECK_STATUS(g_api->audio_play(engine, &play, &voice), ENGINE_OK);
        }
        if (block == 260)
        {
            CHECK_STATUS(g_api->audio_voice_stop(engine, voice, 2400), ENGINE_OK);
        }
        if (block == SNAP_AT)
        {
            CHECK_STATUS(g_api->audio_snapshot(engine, &snapshot), ENGINE_OK);
            CHECK(snapshot.size > 1000);
        }
        RenderBlock(engine, g_audio[block]);
        checksum = Hash(checksum, g_audio[block], BLOCK_FRAMES * 2);
    }
    CHECK(stinger_frame > 150 * BLOCK_FRAMES);

    /* The restore at block 300 replays blocks 300 to 599 bit for bit. */
    CHECK_STATUS(g_api->audio_restore(engine, snapshot.data, snapshot.size), ENGINE_OK);
    CHECK(g_api->audio_restore(engine, snapshot.data, snapshot.size / 2) != ENGINE_OK);
    for (block = SNAP_AT; block < BLOCKS; ++block)
    {
        RenderBlock(engine, g_replay[block]);
    }
    for (block = SNAP_AT; block < BLOCKS; ++block)
    {
        if (memcmp(g_audio[block], g_replay[block], sizeof(g_audio[0])) != 0)
        {
            printf("audio_sample: first replay difference at block %d (rms %f vs %f)\n", block,
                   Rms(g_audio[block], BLOCK_FRAMES * 2), Rms(g_replay[block], BLOCK_FRAMES * 2));
            break;
        }
    }
    CHECK(memcmp(g_audio[SNAP_AT], g_replay[SNAP_AT], sizeof(g_audio[0]) * (BLOCKS - SNAP_AT)) == 0);
    if (snapshot.release)
    {
        snapshot.release(&snapshot);
    }

    printf("audio_sample: checksum 0x%016llx\n", (unsigned long long)checksum);
    for (block = 0; block < BLOCKS; block += 50)
    {
        printf("audio_sample: block %3d rms %.4f\n", block, Rms(g_audio[block], BLOCK_FRAMES * 2));
    }
    CHECK(Rms(g_audio[40], BLOCK_FRAMES * 2) > 0.01f);  /* the first track is sounding */
    CHECK(Rms(g_audio[400], BLOCK_FRAMES * 2) > 0.01f); /* the second track is */
    CHECK_STATUS(g_api->audio_close(engine), ENGINE_OK);
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    g_api->thread_detach();
    if (g_failures != 0)
    {
        printf("audio_sample: %d check(s) failed\n", g_failures);
        return 1;
    }
    printf("audio_sample: ok\n");
    return 0;
}
