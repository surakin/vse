/* Maintenance (2026-11-18): live voices. A C program stands for a call: it opens push streams, feeds them as its audio
   arrives (from another thread too), and sets them as the sound of entities, so that the engine's spatial mixer places
   them in the world. Checks: a live tone is heard like the same tone as a clip (level and balance, at several places),
   a host that stops feeding gets silence and a voice that stays alive, an overrun drops the oldest and counts, the
   delay from push to output, every order of destroying the entity, removing the source and closing the stream, a stream
   with two sources, the older struct without a stream field, and the snapshot refusing a live voice. */
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
            printf("host_audio_live: line %d: CHECK failed: %s\n", __LINE__, #condition);                              \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

#define CHECK_STATUS(call, expected)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status got_ = (call);                                                                             \
        if (got_ != (expected))                                                                                        \
        {                                                                                                              \
            printf("host_audio_live: line %d: %s returned %d, expected %d\n", __LINE__, #call, (int)got_,              \
                   (int)(expected));                                                                                   \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

#define PI 3.14159265358979f
#define BLOCK 256

static engine_handle g_engine;
static engine_handle g_world;

static void Frame(void)
{
    engine_frame_info info;
    engine_frame_stats stats;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.delta_seconds = 1.0 / 60.0;
    memset(&stats, 0, sizeof(stats));
    stats.struct_size = sizeof(stats);
    CHECK_STATUS(g_api->frame(g_engine, &info, &stats), ENGINE_OK);
}

/* The 1 kHz tone at 48 kHz: sample `pos`. */
static float Tone(long pos)
{
    return 0.5f * sinf(2.0f * PI * 1000.0f * (float)(pos % 48) / 48000.0f);
}

typedef struct feeder
{
    uint64_t stream; /* 0: no feeding */
    long pos;
} feeder;

static void Feed(feeder* f, int frames)
{
    float data[BLOCK * 8];
    int i;
    if (f->stream == 0)
    {
        return;
    }
    for (i = 0; i < frames; ++i)
    {
        data[i] = Tone(f->pos++);
    }
    CHECK_STATUS(g_api->audio_stream_push(g_engine, f->stream, data, (uint32_t)frames, NULL), ENGINE_OK);
}

/* Frames, feeding and rendering `blocks` blocks; returns the RMS of each channel and the peak of the last block. */
static void Run(feeder* f, int blocks, float* rms_l, float* rms_r, float* last_peak)
{
    float out[BLOCK * 2];
    double sl = 0.0, sr = 0.0;
    int b, i;
    for (b = 0; b < blocks; ++b)
    {
        float peak = 0.0f;
        Frame();
        Feed(f, BLOCK);
        CHECK_STATUS(g_api->audio_render(g_engine, BLOCK, out), ENGINE_OK);
        for (i = 0; i < BLOCK; ++i)
        {
            sl += (double)out[2 * i] * out[2 * i];
            sr += (double)out[2 * i + 1] * out[2 * i + 1];
            peak = fmaxf(peak, fmaxf(fabsf(out[2 * i]), fabsf(out[2 * i + 1])));
        }
        if (last_peak != NULL)
        {
            *last_peak = peak;
        }
    }
    if (rms_l != NULL)
    {
        *rms_l = (float)sqrt(sl / (double)(blocks * BLOCK));
    }
    if (rms_r != NULL)
    {
        *rms_r = (float)sqrt(sr / (double)(blocks * BLOCK));
    }
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

static engine_audio_source Source(uint64_t clip, uint64_t stream)
{
    engine_audio_source s;
    memset(&s, 0, sizeof(s));
    s.struct_size = sizeof(s);
    s.clip = clip;
    s.stream = stream;
    s.gain = 1.0f;
    s.pitch = 1.0f;
    s.flags = ENGINE_AUDIO_SOURCE_LOOP | ENGINE_AUDIO_SOURCE_PLAY_ON_START | ENGINE_AUDIO_SOURCE_SPATIAL;
    s.model = ENGINE_AUDIO_MODEL_INVERSE;
    s.min_distance = 1.0f;
    s.max_distance = 30.0f;
    s.rolloff = 1.0f;
    s.cone_inner = 6.2831853f;
    s.cone_outer = 6.2831853f;
    return s;
}

static uint64_t OpenPush(uint32_t prefill, uint32_t capacity)
{
    engine_audio_push_info info;
    uint64_t stream = 0;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.sample_rate = 48000;
    info.channels = 1;
    info.capacity_frames = capacity;
    info.prefill_frames = prefill;
    CHECK_STATUS(g_api->audio_stream_open_push(g_engine, &info, &stream), ENGINE_OK);
    return stream;
}

static uint64_t MakeToneClip(void)
{
    float samples[4800];
    engine_audio_clip_info info;
    uint64_t clip = 0;
    int i;
    for (i = 0; i < 4800; ++i)
    {
        samples[i] = Tone(i);
    }
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.sample_rate = 48000;
    info.channels = 1;
    info.format = ENGINE_AUDIO_FORMAT_F32;
    CHECK_STATUS(g_api->audio_clip_create(g_engine, &info, samples, sizeof(samples), &clip), ENGINE_OK);
    return clip;
}

static int Near(float a, float b)
{
    return fabsf(a - b) <= 0.03f * fmaxf(a, b) + 1e-4f;
}

static void TestEquivalence(uint64_t listener, uint64_t emitter, uint64_t clip)
{
    static const float places[5][3] = {{0, 0, -1}, {0, 0, -5}, {0, 0, -25}, {-5, 0, 0}, {0, 0, 5}};
    int p;
    for (p = 0; p < 5; ++p)
    {
        float cl, cr, pl, pr;
        engine_audio_source src = Source(clip, 0);
        engine_transform t = At(places[p][0], places[p][1], places[p][2]);
        feeder none = {0, 0};
        feeder live;
        (void)listener;
        CHECK_STATUS(g_api->component_set(g_world, emitter, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
        CHECK_STATUS(g_api->component_set(g_world, emitter, ENGINE_COMPONENT_AUDIO_SOURCE, &src), ENGINE_OK);
        Run(&none, 12, NULL, NULL, NULL);
        Run(&none, 9, &cl, &cr, NULL); /* 9 blocks = 48 cycles of the tone */
        CHECK_STATUS(g_api->component_remove(g_world, emitter, ENGINE_COMPONENT_AUDIO_SOURCE), ENGINE_OK);
        Run(&none, 12, NULL, NULL, NULL);

        live.stream = OpenPush(0, 9600);
        live.pos = 0;
        src = Source(0, live.stream);
        CHECK_STATUS(g_api->component_set(g_world, emitter, ENGINE_COMPONENT_AUDIO_SOURCE, &src), ENGINE_OK);
        Run(&live, 12, NULL, NULL, NULL);
        Run(&live, 9, &pl, &pr, NULL);
        CHECK_STATUS(g_api->component_remove(g_world, emitter, ENGINE_COMPONENT_AUDIO_SOURCE), ENGINE_OK);
        Run(&none, 12, NULL, NULL, NULL);
        CHECK_STATUS(g_api->audio_stream_close(g_engine, live.stream), ENGINE_OK);
        printf("host_audio_live: place %d: clip L %.5f R %.5f, live L %.5f R %.5f\n", p, cl, cr, pl, pr);
        CHECK(Near(cl, pl) && Near(cr, pr));
    }
}

static void TestUnderrun(uint64_t emitter)
{
    feeder live;
    engine_audio_source src;
    engine_audio_stream_stats stats;
    engine_audio_event events[64];
    size_t total = 0, i;
    float peak = 0.0f;
    int finished = 0;
    engine_transform t = At(0, 0, -1);
    CHECK_STATUS(g_api->component_set(g_world, emitter, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
    live.stream = OpenPush(0, 9600);
    live.pos = 0;
    src = Source(0, live.stream);
    CHECK_STATUS(g_api->component_set(g_world, emitter, ENGINE_COMPONENT_AUDIO_SOURCE, &src), ENGINE_OK);
    Run(&live, 20, NULL, NULL, &peak);
    CHECK(peak > 0.01f);
    for (i = 0; i < 64; ++i)
    {
        events[i].struct_size = sizeof(events[i]);
    }
    CHECK_STATUS(g_api->audio_events(g_engine, events, 64, &total), ENGINE_OK); /* clears the queue */
    live.stream = 0;                                                            /* the host stops feeding: 200 ms */
    Run(&live, 38, NULL, NULL, &peak);
    CHECK(peak < 1e-4f);
    for (i = 0; i < 64; ++i)
    {
        events[i].struct_size = sizeof(events[i]);
    }
    CHECK_STATUS(g_api->audio_events(g_engine, events, 64, &total), ENGINE_OK);
    for (i = 0; i < total && i < 64; ++i)
    {
        finished += events[i].kind == ENGINE_AUDIO_EVENT_FINISHED || events[i].kind == ENGINE_AUDIO_EVENT_STOPPED;
    }
    CHECK(finished == 0); /* the voice is alive */
    /* find the stream again through the source */
    {
        engine_audio_source back;
        memset(&back, 0, sizeof(back));
        back.struct_size = sizeof(back);
        CHECK_STATUS(g_api->component_get(g_world, emitter, ENGINE_COMPONENT_AUDIO_SOURCE, &back), ENGINE_OK);
        CHECK(back.stream != 0 && back.clip == 0);
        live.stream = back.stream;
        memset(&stats, 0, sizeof(stats));
        stats.struct_size = sizeof(stats);
        CHECK_STATUS(g_api->audio_stream_stats(g_engine, back.stream, &stats), ENGINE_OK);
        CHECK(stats.is_push == 1 && stats.underruns > 30 && stats.frames_pushed == 20 * BLOCK);
        printf("host_audio_live: after 200 ms of silence: %llu underruns, %llu frames pushed\n",
               (unsigned long long)stats.underruns, (unsigned long long)stats.frames_pushed);
    }
    Run(&live, 20, NULL, NULL, &peak); /* and it resumes */
    CHECK(peak > 0.01f);
    CHECK_STATUS(g_api->component_remove(g_world, emitter, ENGINE_COMPONENT_AUDIO_SOURCE), ENGINE_OK);
    Run(&live, 12, NULL, NULL, NULL);
    live.stream = 0;
}

static void TestOverrun(void)
{
    engine_audio_stream_stats stats;
    float data[4800];
    uint64_t stream = OpenPush(0, 4800);
    int i;
    for (i = 0; i < 4800; ++i)
    {
        data[i] = Tone(i);
    }
    for (i = 0; i < 5; ++i)
    {
        uint32_t accepted = 0;
        CHECK_STATUS(g_api->audio_stream_push(g_engine, stream, data, 4800, &accepted), ENGINE_OK);
        CHECK(accepted == 4800);
    }
    memset(&stats, 0, sizeof(stats));
    stats.struct_size = sizeof(stats);
    CHECK_STATUS(g_api->audio_stream_stats(g_engine, stream, &stats), ENGINE_OK);
    CHECK(stats.frames_pushed == 24000 && stats.frames_dropped == 19200 && stats.buffered_frames == 4800);
    CHECK_STATUS(g_api->audio_stream_close(g_engine, stream), ENGINE_OK);
    CHECK_STATUS(g_api->audio_stream_push(g_engine, stream, data, 10, NULL), ENGINE_ERROR_STALE_HANDLE);
}

/* The frames between a push and the first sound of it in the output, steady state, 10 ms blocks, at eight different
 * phases of the mixer's 256-frame blocks against the host's 480-frame calls: the worst of them. */
static void TestLatency(void)
{
    engine_audio_play play;
    uint64_t voice = 0;
    uint64_t stream = OpenPush(0, 9600);
    float block[480];
    float out[480 * 2];
    int i, round, phase, worst = 0;
    memset(&play, 0, sizeof(play));
    play.struct_size = sizeof(play);
    play.gain = 1.0f;
    play.pitch = 1.0f;
    CHECK_STATUS(g_api->audio_stream_play(g_engine, stream, &play, &voice), ENGINE_OK);
    for (phase = 0; phase < 8; ++phase)
    {
        int first = -1;
        for (round = 0; round < 10 + phase; ++round) /* silence in steady state */
        {
            for (i = 0; i < 480; ++i)
            {
                block[i] = 0.0f;
            }
            CHECK_STATUS(g_api->audio_stream_push(g_engine, stream, block, 480, NULL), ENGINE_OK);
            CHECK_STATUS(g_api->audio_render(g_engine, 480, out), ENGINE_OK);
        }
        for (i = 0; i < 480; ++i)
        {
            block[i] = 0.8f; /* the marker */
        }
        CHECK_STATUS(g_api->audio_stream_push(g_engine, stream, block, 480, NULL), ENGINE_OK);
        for (round = 0; round < 3 && first < 0; ++round)
        {
            CHECK_STATUS(g_api->audio_render(g_engine, 480, out), ENGINE_OK);
            for (i = 0; i < 480 && first < 0; ++i)
            {
                if (fabsf(out[2 * i]) > 0.1f || fabsf(out[2 * i + 1]) > 0.1f)
                {
                    first = round * 480 + i;
                }
            }
        }
        worst = first > worst ? first : worst;
        CHECK(first >= 0);
        for (round = 0; round < 3; ++round) /* let the marker pass */
        {
            CHECK_STATUS(g_api->audio_render(g_engine, 480, out), ENGINE_OK);
        }
    }
    printf(
        "host_audio_live: latency: a marker pushed just before a 480-frame render reaches the output after at most %d "
        "frames (%.2f ms)\n",
        worst, worst * 1000.0 / 48000.0);
    CHECK(worst <= 2 * BLOCK);
    CHECK_STATUS(g_api->audio_stream_close(g_engine, stream), ENGINE_OK);
    for (round = 0; round < 4; ++round)
    {
        CHECK_STATUS(g_api->audio_render(g_engine, 480, out), ENGINE_OK);
    }
}

static void TestLifecycle(uint64_t emitter)
{
    engine_audio_source src, back;
    uint64_t other = 0, stream, stream2;
    feeder live;
    float peak = 0.0f;
    int order;
    engine_transform t = At(0, 0, -2);
    CHECK_STATUS(g_api->entity_create(g_world, &other), ENGINE_OK);
    CHECK_STATUS(g_api->component_set(g_world, other, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
    CHECK_STATUS(g_api->component_set(g_world, emitter, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);

    stream = OpenPush(0, 9600);
    src = Source(0, stream);
    CHECK_STATUS(g_api->component_set(g_world, emitter, ENGINE_COMPONENT_AUDIO_SOURCE, &src), ENGINE_OK);
    CHECK_STATUS(g_api->component_set(g_world, other, ENGINE_COMPONENT_AUDIO_SOURCE, &src),
                 ENGINE_ERROR_INVALID_ARGUMENT); /* one reader per stream */
    src = Source(MakeToneClip(), stream);
    CHECK_STATUS(g_api->component_set(g_world, other, ENGINE_COMPONENT_AUDIO_SOURCE, &src),
                 ENGINE_ERROR_INVALID_ARGUMENT); /* a clip and a stream */
    src = Source(0, stream + 0x100000000ull);
    CHECK_STATUS(g_api->component_set(g_world, other, ENGINE_COMPONENT_AUDIO_SOURCE, &src), ENGINE_ERROR_STALE_HANDLE);
    src = Source(0, stream);
    CHECK_STATUS(g_api->component_set(g_world, emitter, ENGINE_COMPONENT_AUDIO_SOURCE, &src), ENGINE_OK); /* again */

    for (order = 0; order < 6; ++order)
    {
        /* the three ends of a live voice, in each order: entity destroyed, source removed, stream closed */
        uint64_t e = 0;
        uint64_t s = OpenPush(0, 9600);
        engine_audio_source es = Source(0, s);
        int step;
        static const int orders[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
        CHECK_STATUS(g_api->entity_create(g_world, &e), ENGINE_OK);
        CHECK_STATUS(g_api->component_set(g_world, e, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
        CHECK_STATUS(g_api->component_set(g_world, e, ENGINE_COMPONENT_AUDIO_SOURCE, &es), ENGINE_OK);
        live.stream = s;
        live.pos = 0;
        Run(&live, 6, NULL, NULL, &peak);
        for (step = 0; step < 3; ++step)
        {
            const int what = orders[order][step];
            if (what == 0)
            {
                CHECK_STATUS(g_api->entity_destroy(g_world, e), step < 1 ? ENGINE_OK : ENGINE_OK);
            }
            else if (what == 1)
            {
                const engine_status st = g_api->component_remove(g_world, e, ENGINE_COMPONENT_AUDIO_SOURCE);
                CHECK(st == ENGINE_OK || st == ENGINE_ERROR_STALE_HANDLE || st == ENGINE_ERROR_NOT_FOUND);
            }
            else
            {
                CHECK_STATUS(g_api->audio_stream_close(g_engine, s), ENGINE_OK);
                live.stream = 0;
            }
            Run(&live, 3, NULL, NULL, NULL);
        }
        Run(&live, 12, NULL, NULL, &peak);
        CHECK(peak < 1e-4f); /* every voice of this round is gone */
    }

    /* the stream outlives its entity and is set on another: a participant who leaves and comes back */
    stream2 = OpenPush(0, 9600);
    live.stream = stream2;
    live.pos = 0;
    src = Source(0, stream2);
    CHECK_STATUS(g_api->component_set(g_world, other, ENGINE_COMPONENT_AUDIO_SOURCE, &src), ENGINE_OK);
    Run(&live, 8, NULL, NULL, &peak);
    CHECK(peak > 0.01f);
    CHECK_STATUS(g_api->entity_destroy(g_world, other), ENGINE_OK);
    Run(&live, 12, NULL, NULL, &peak);
    CHECK(peak < 1e-4f);
    {
        engine_audio_stream_stats stats;
        memset(&stats, 0, sizeof(stats));
        stats.struct_size = sizeof(stats);
        CHECK_STATUS(g_api->audio_stream_stats(g_engine, stream2, &stats), ENGINE_OK); /* still open */
    }
    CHECK_STATUS(g_api->entity_create(g_world, &other), ENGINE_OK);
    CHECK_STATUS(g_api->component_set(g_world, other, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
    CHECK_STATUS(g_api->component_set(g_world, other, ENGINE_COMPONENT_AUDIO_SOURCE, &src), ENGINE_OK);
    Run(&live, 8, NULL, NULL, &peak);
    CHECK(peak > 0.01f);

    /* the first version of the struct has no stream: accepted, plays nothing */
    memset(&back, 0, sizeof(back));
    src = Source(0, 0);
    src.struct_size = 64;
    CHECK_STATUS(g_api->component_set(g_world, emitter, ENGINE_COMPONENT_AUDIO_SOURCE, &src), ENGINE_OK);
    back.struct_size = 64;
    CHECK_STATUS(g_api->component_get(g_world, emitter, ENGINE_COMPONENT_AUDIO_SOURCE, &back), ENGINE_OK);
    CHECK(back.stream == 0 && back.struct_size == 64);

    /* a voice playing a live stream cannot be saved; once it is gone, the snapshot works */
    {
        engine_buffer snapshot;
        memset(&snapshot, 0, sizeof(snapshot));
        CHECK_STATUS(g_api->audio_snapshot(g_engine, &snapshot), ENGINE_ERROR_UNSUPPORTED);
        CHECK_STATUS(g_api->component_remove(g_world, other, ENGINE_COMPONENT_AUDIO_SOURCE), ENGINE_OK);
        live.stream = 0;
        Run(&live, 20, NULL, NULL, NULL);
        CHECK_STATUS(g_api->audio_snapshot(g_engine, &snapshot), ENGINE_OK);
        engine_buffer_release(&snapshot);
    }
    CHECK_STATUS(g_api->audio_stream_close(g_engine, stream), ENGINE_OK);
    CHECK_STATUS(g_api->audio_stream_close(g_engine, stream2), ENGINE_OK);
}

static void TestArguments(void)
{
    engine_audio_push_info info;
    uint64_t stream = 0, clip_stream = 0, clip;
    engine_audio_stream_stats stats;
    float data[16] = {0};
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.sample_rate = 48000;
    info.channels = 3;
    info.capacity_frames = 1000;
    CHECK_STATUS(g_api->audio_stream_open_push(g_engine, &info, &stream), ENGINE_ERROR_INVALID_ARGUMENT);
    info.channels = 1;
    info.sample_rate = 44100;
    CHECK_STATUS(g_api->audio_stream_open_push(g_engine, &info, &stream), ENGINE_ERROR_INVALID_ARGUMENT);
    info.sample_rate = 48000;
    info.capacity_frames = 10;
    CHECK_STATUS(g_api->audio_stream_open_push(g_engine, &info, &stream), ENGINE_ERROR_INVALID_ARGUMENT);
    info.capacity_frames = 1000;
    info.prefill_frames = 1001;
    CHECK_STATUS(g_api->audio_stream_open_push(g_engine, &info, &stream), ENGINE_ERROR_INVALID_ARGUMENT);
    info.prefill_frames = 0;
    info.struct_size = 8;
    CHECK_STATUS(g_api->audio_stream_open_push(g_engine, &info, &stream), ENGINE_ERROR_STRUCT_SIZE);
    info.struct_size = sizeof(info);
    CHECK_STATUS(g_api->audio_stream_open_push(g_engine, NULL, &stream), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->audio_stream_open_push(g_engine, &info, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->audio_stream_open_push(g_engine, &info, &stream), ENGINE_OK);
    CHECK_STATUS(g_api->audio_stream_push(g_engine, stream, NULL, 4, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->audio_stream_push(g_engine, 0, data, 4, NULL), ENGINE_ERROR_STALE_HANDLE);
    clip = MakeToneClip();
    CHECK_STATUS(g_api->audio_stream_open_clip(g_engine, clip, &clip_stream), ENGINE_OK);
    CHECK_STATUS(g_api->audio_stream_push(g_engine, clip_stream, data, 4, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    memset(&stats, 0, sizeof(stats));
    stats.struct_size = sizeof(stats);
    CHECK_STATUS(g_api->audio_stream_stats(g_engine, clip_stream, &stats), ENGINE_OK);
    CHECK(stats.is_push == 0);
    stats.struct_size = 8;
    CHECK_STATUS(g_api->audio_stream_stats(g_engine, stream, &stats), ENGINE_ERROR_STRUCT_SIZE);
    CHECK_STATUS(g_api->audio_stream_close(g_engine, clip_stream), ENGINE_OK);
    CHECK_STATUS(g_api->audio_stream_close(g_engine, stream), ENGINE_OK);
}

/* A pusher thread delivering 10 ms blocks in real time while the main thread renders and runs frames. */
typedef struct pusher
{
    uint64_t stream;
    int blocks;
    int pushed_all;
} pusher;

static void PusherThread(void* arg)
{
    pusher* p = (pusher*)arg;
    float block[480];
    int b, i;
    long pos = 0;
    p->pushed_all = 1;
    for (b = 0; b < p->blocks; ++b)
    {
        uint32_t accepted = 0;
        for (i = 0; i < 480; ++i)
        {
            block[i] = Tone(pos++);
        }
        if (g_api->audio_stream_push(g_engine, p->stream, block, 480, &accepted) != ENGINE_OK || accepted != 480)
        {
            p->pushed_all = 0;
        }
        host_sleep_ms(10);
    }
}

static void TestThreaded(uint64_t emitter)
{
    pusher p;
    host_thread thread;
    engine_audio_source src;
    engine_audio_stream_stats stats;
    float out[480 * 2], peak = 0.0f;
    int b, i;
    engine_transform t = At(0, 0, -2);
    CHECK_STATUS(g_api->component_set(g_world, emitter, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
    p.stream = OpenPush(480, 9600);
    p.blocks = 60;
    p.pushed_all = 0;
    src = Source(0, p.stream);
    CHECK_STATUS(g_api->component_set(g_world, emitter, ENGINE_COMPONENT_AUDIO_SOURCE, &src), ENGINE_OK);
    CHECK(host_thread_start(&thread, PusherThread, &p) == 0);
    for (b = 0; b < 70; ++b)
    {
        Frame();
        CHECK_STATUS(g_api->audio_render(g_engine, 480, out), ENGINE_OK);
        for (i = 0; i < 480 * 2; ++i)
        {
            peak = fmaxf(peak, fabsf(out[i]));
        }
        host_sleep_ms(10);
    }
    host_thread_join(&thread);
    CHECK(p.pushed_all == 1);
    CHECK(peak > 0.01f);
    memset(&stats, 0, sizeof(stats));
    stats.struct_size = sizeof(stats);
    CHECK_STATUS(g_api->audio_stream_stats(g_engine, p.stream, &stats), ENGINE_OK);
    CHECK(stats.frames_pushed == 60u * 480u && stats.frames_dropped == 0);
    printf("host_audio_live: threaded: %llu frames pushed, %llu underruns\n", (unsigned long long)stats.frames_pushed,
           (unsigned long long)stats.underruns);
    CHECK_STATUS(g_api->component_remove(g_world, emitter, ENGINE_COMPONENT_AUDIO_SOURCE), ENGINE_OK);
    CHECK_STATUS(g_api->audio_stream_close(g_engine, p.stream), ENGINE_OK);
    for (b = 0; b < 6; ++b)
    {
        Frame();
        CHECK_STATUS(g_api->audio_render(g_engine, 480, out), ENGINE_OK);
    }
}

int main(void)
{
    engine_create_info create;
    engine_audio_info info;
    engine_audio_listener ear;
    engine_transform t;
    engine_info einfo;
    uint64_t emitter = 0, listener = 0, clip;

    host_quiet_failures();
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &g_api) != ENGINE_OK)
    {
        printf("host_audio_live: no function table\n");
        return 2;
    }
    memset(&create, 0, sizeof(create));
    create.struct_size = sizeof(create);
    memset(&g_engine, 0, sizeof(g_engine));
    CHECK_STATUS(g_api->create(&create, &g_engine), ENGINE_OK);
    memset(&einfo, 0, sizeof(einfo));
    einfo.struct_size = sizeof(einfo);
    CHECK_STATUS(g_api->get_info(g_engine, &einfo), ENGINE_OK);
    CHECK((einfo.caps & ENGINE_CAP_AUDIO_PUSH_STREAMS) != 0);
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.sample_rate = 48000;
    info.channels = 2;
    info.max_voices = 16;
    {
        engine_audio_stream_stats stats;
        uint64_t stream = 0;
        engine_audio_push_info push;
        memset(&push, 0, sizeof(push));
        push.struct_size = sizeof(push);
        push.sample_rate = 48000;
        push.channels = 1;
        push.capacity_frames = 1000;
        CHECK_STATUS(g_api->audio_stream_open_push(g_engine, &push, &stream),
                     ENGINE_ERROR_NOT_READY); /* no audio yet */
        (void)stats;
    }
    CHECK_STATUS(g_api->audio_open(g_engine, &info), ENGINE_OK);
    memset(&g_world, 0, sizeof(g_world));
    CHECK_STATUS(g_api->world_create(g_engine, &g_world), ENGINE_OK);
    CHECK_STATUS(g_api->entity_create(g_world, &emitter), ENGINE_OK);
    CHECK_STATUS(g_api->entity_create(g_world, &listener), ENGINE_OK);
    t = At(0, 0, 0);
    CHECK_STATUS(g_api->component_set(g_world, listener, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
    memset(&ear, 0, sizeof(ear));
    ear.struct_size = sizeof(ear);
    ear.gain = 1.0f;
    ear.active = 1;
    CHECK_STATUS(g_api->component_set(g_world, listener, ENGINE_COMPONENT_AUDIO_LISTENER, &ear), ENGINE_OK);
    CHECK_STATUS(g_api->component_set(g_world, emitter, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
    clip = MakeToneClip();

    TestArguments();
    TestEquivalence(listener, emitter, clip);
    TestUnderrun(emitter);
    TestOverrun();
    TestLatency();
    TestLifecycle(emitter);
    TestThreaded(emitter);

    CHECK_STATUS(g_api->world_destroy(g_world), ENGINE_OK);
    CHECK_STATUS(g_api->audio_close(g_engine), ENGINE_OK);
    CHECK_STATUS(g_api->destroy(g_engine), ENGINE_OK);
    if (g_failures != 0)
    {
        printf("host_audio_live: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("host_audio_live: ok\n");
    return 0;
}
