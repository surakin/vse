// The review findings of the audio module, each with the case that showed it: hostile snapshot values (non-finite and
// out-of-range floats, a bus graph that differs from the mixer's own), a long fade in a snapshot, a paused virtual
// voice, a rate change with inserts, and the spatial system's lost stops, stolen loops and stale velocities.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "audio/components.h"
#include "audio/filter.h"
#include "audio/mixer.h"
#include "audio/snapshot.h"
#include "audio/spatial_audio.h"
#include "bytes/byte_writer.h"
#include "containers/vector.h"
#include "cstr/mem_ops.h"
#include "ecs/transform.h"
#include "lang/scalar.h"

using namespace vse;
using namespace vse::audio;
using Catch::Approx;

namespace
{

AudioClipPtr HdClip(const char* name, u32 frames, bool loop_points = false)
{
    Vector<f32> samples;
    for (u32 i = 0; i < frames; ++i)
    {
        samples.Push(0.25f * Sin(0.05f * static_cast<f32>(i)) + 0.1f);
    }
    ClipDesc desc;
    desc.format.sample_rate = 48000;
    desc.format.layout = ChannelLayout::Mono;
    desc.format.format = SampleFormat::F32;
    desc.frame_count = frames;
    if (loop_points)
    {
        desc.loop_start = 10;
        desc.loop_end = frames - 10;
    }
    AudioClipPtr clip;
    REQUIRE(AudioClip::CreateCopy(desc, Span<const f32>(samples.Data(), samples.Size()), name, &clip) ==
            ClipStatus::Ok);
    return clip;
}

class HdResolver final : public SnapshotResolver
{
public:
    Vector<AudioClipConstPtr> clips;

    AudioClipConstPtr FindClip(StringView name, u32 frames) override
    {
        for (usize i = 0; i < clips.Size(); ++i)
        {
            if (StringView(clips[i]->Name()) == name && clips[i]->Frames() == frames)
            {
                return clips[i];
            }
        }
        return AudioClipConstPtr();
    }
    StreamBufferPtr FindStream(StringView) override { return StreamBufferPtr(); }
};

MixerConfig HdConfig(ChannelLayout layout = ChannelLayout::Stereo)
{
    MixerConfig config;
    config.layout = layout;
    config.quality = ResamplerQuality::Hermite;
    config.max_voices = 8;
    config.max_buses = 4;
    config.ramp_frames = 64;
    config.lead_blocks = 0;
    config.limiter = true;
    return config;
}

Vector<f32> HdBlocks(Mixer& mixer, u32 blocks)
{
    Vector<f32> out;
    Vector<f32> block;
    block.Resize(static_cast<usize>(kBlockFrames) * mixer.Channels());
    for (u32 b = 0; b < blocks; ++b)
    {
        mixer.RenderBlock(Span<f32>(block.Data(), block.Size()));
        for (usize i = 0; i < block.Size(); ++i)
        {
            out.Push(block[i]);
        }
    }
    return out;
}

Vector<byte> HdSave(const Mixer& mixer)
{
    bytes::ByteWriter w;
    REQUIRE(mixer.SaveState(&w) == SnapshotStatus::Ok);
    Vector<byte> out;
    for (usize i = 0; i < w.Size(); ++i)
    {
        out.Push(w.Bytes()[i]);
    }
    return out;
}

bool HdSameBits(const Vector<f32>& a, const Vector<f32>& b)
{
    if (a.Size() != b.Size())
    {
        return false;
    }
    for (usize i = 0; i < a.Size(); ++i)
    {
        u32 x = 0;
        u32 y = 0;
        MemCopy(&x, &a[i], 4);
        MemCopy(&y, &b[i], 4);
        if (x != y)
        {
            return false;
        }
    }
    return true;
}

// The buses of the mutation scene: music and fx under the master, music sends to fx, fx is ducked by music.
struct HdBuses
{
    BusId music;
    BusId fx;
};

HdBuses HdTopology(Mixer& mixer)
{
    REQUIRE(mixer.Init(HdConfig()) == MixerStatus::Ok);
    HdBuses b;
    REQUIRE(mixer.CreateBus("music", BusId{0}, &b.music) == MixerStatus::Ok);
    REQUIRE(mixer.CreateBus("fx", BusId{0}, &b.fx) == MixerStatus::Ok);
    REQUIRE(mixer.SetBusSend(b.music, 0, b.fx, 0.5f, false) == MixerStatus::Ok);
    REQUIRE(mixer.SetDuck(b.fx, b.music, 0.4f, 0.05f, 2000.0f) == MixerStatus::Ok);
    REQUIRE(mixer.SetBusGain(b.music, 0.8f) == MixerStatus::Ok);
    return b;
}

void HdPut(Vector<byte>* bytes, usize at, u32 bits)
{
    for (u32 k = 0; k < 4; ++k)
    {
        (*bytes)[at + k] = static_cast<byte>((bits >> (8 * k)) & 0xFFu);
    }
}

ecs::WorldTransform HdAt(f32 x, f32 y, f32 z)
{
    return ecs::WorldTransform::FromMatrix(Mat4::FromBasis(Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1), Vec3(x, y, z)));
}

struct HdRig
{
    Mixer mixer;
    SpatialAudio audio;
    ecs::World world;
    Vector<f32> out;

    HdRig(u32 pool, u32 queue)
    {
        MixerConfig mc;
        mc.command_capacity = queue;
        mc.layout = ChannelLayout::Mono;
        mc.quality = ResamplerQuality::Linear;
        mc.max_voices = pool;
        mc.lead_blocks = 0;
        REQUIRE(mixer.Init(mc) == MixerStatus::Ok);
        SpatialAudioConfig config;
        audio.Init(&mixer, config);
        out.Resize(kBlockFrames);
    }
    void Block() { mixer.Render(out.Data(), kBlockFrames); }
};

}  // namespace

TEST_CASE(
    "Snapshot hardening: a non-finite or out-of-range value anywhere is refused or harmless, never NaN in the mix",
    "[audio][snapshot][fuzz]")
{
    Mixer source;
    const HdBuses buses = HdTopology(source);
    HdResolver resolver;
    resolver.clips.Push(HdClip("a", 3000, true));
    resolver.clips.Push(HdClip("b", 5000));
    for (u32 v = 0; v < 4; ++v)
    {
        VoiceDesc desc;
        desc.clip = resolver.clips[v & 1];
        desc.loop = true;
        desc.gain = 0.4f;
        desc.pitch = 0.75f + 0.2f * static_cast<f32>(v);
        desc.pan = -0.6f + 0.4f * static_cast<f32>(v);
        desc.bus = v < 2 ? buses.music : buses.fx;
        desc.directional = v == 3;
        VoiceId id;
        REQUIRE(source.Play(desc, &id) == MixerStatus::Ok);
        if (v == 1)
        {
            HdBlocks(source, 1);
            REQUIRE(source.Fade(id, 0.1f, 5000) == MixerStatus::Ok);  // a ramp in flight
        }
    }
    HdBlocks(source, 3);
    Vector<f32> device;
    device.Resize(300);
    source.Render(device.Data(), 100);  // the carried block is half used
    const Vector<byte> good = HdSave(source);

    Mixer target;
    HdTopology(target);
    HdBlocks(target, 1);
    REQUIRE(target.LoadState(ByteSpan(good.Data(), good.Size()), &resolver) == SnapshotStatus::Ok);

    // Every byte offset gets each hostile float: NaN, both infinities, and finite values far out of range. A load that
    // succeeds (the four bytes fell on a field where they are a valid value) must render a finite, bounded mix.
    const u32 patterns[] = {0x7FC00000u, 0x7F800000u, 0xFF800000u, 0x7149F2CAu /* 1e30 */, 0xF149F2CAu /* -1e30 */,
                            0x4CBEBC20u /* 1e8 */};
    u32 refused = 0;
    u32 loaded = 0;
    Vector<byte> bad;
    for (usize i = 0; i < good.Size(); ++i)
    {
        bad.Push(good[i]);
    }
    for (usize at = 0; at + 4 <= good.Size(); ++at)
    {
        for (u32 bits : patterns)
        {
            for (u32 k = 0; k < 4; ++k)
            {
                bad[at + k] = good[at + k];
            }
            if (at > 0)
            {
                bad[at - 1] = good[at - 1];
            }
            HdPut(&bad, at, bits);
            const SnapshotStatus status = target.LoadState(ByteSpan(bad.Data(), bad.Size()), &resolver);
            if (status != SnapshotStatus::Ok)
            {
                refused += 1;
                continue;
            }
            loaded += 1;
            const Vector<f32> mix = HdBlocks(target, 3);
            bool finite = true;
            for (usize i = 0; i < mix.Size(); ++i)
            {
                finite = finite && IsFinite(mix[i]) && mix[i] < 1.0e7f && mix[i] > -1.0e7f;
            }
            INFO("offset " << at << " pattern " << bits);
            REQUIRE(finite);
        }
        for (u32 k = 0; k < 4; ++k)
        {
            bad[at + k] = good[at + k];
        }
    }
    CHECK(refused > 0);
    CHECK(loaded > 0);
    // The unchanged snapshot still loads after all of that.
    CHECK(target.LoadState(ByteSpan(good.Data(), good.Size()), &resolver) == SnapshotStatus::Ok);
}

TEST_CASE("Snapshot hardening: a bus graph other than the mixer's own is refused, so no cycle can be loaded",
          "[audio][snapshot]")
{
    // A: music sends to fx. B: fx sends to music (valid there). A's snapshot in B would make music and fx feed each
    // other.
    Mixer a;
    REQUIRE(a.Init(HdConfig()) == MixerStatus::Ok);
    BusId a_music;
    BusId a_fx;
    REQUIRE(a.CreateBus("music", BusId{0}, &a_music) == MixerStatus::Ok);
    REQUIRE(a.CreateBus("fx", BusId{0}, &a_fx) == MixerStatus::Ok);
    REQUIRE(a.SetBusSend(a_music, 0, a_fx, 0.5f, false) == MixerStatus::Ok);
    HdBlocks(a, 1);
    const Vector<byte> snapshot = HdSave(a);

    Mixer b;
    REQUIRE(b.Init(HdConfig()) == MixerStatus::Ok);
    BusId b_music;
    BusId b_fx;
    REQUIRE(b.CreateBus("music", BusId{0}, &b_music) == MixerStatus::Ok);
    REQUIRE(b.CreateBus("fx", BusId{0}, &b_fx) == MixerStatus::Ok);
    REQUIRE(b.SetBusSend(b_fx, 0, b_music, 0.5f, false) == MixerStatus::Ok);
    HdBlocks(b, 1);
    HdResolver resolver;
    CHECK(b.LoadState(ByteSpan(snapshot.Data(), snapshot.Size()), &resolver) == SnapshotStatus::Mismatch);

    // A ducking source the mixer does not have is refused the same way.
    Mixer c;
    REQUIRE(c.Init(HdConfig()) == MixerStatus::Ok);
    BusId c_music;
    BusId c_fx;
    REQUIRE(c.CreateBus("music", BusId{0}, &c_music) == MixerStatus::Ok);
    REQUIRE(c.CreateBus("fx", BusId{0}, &c_fx) == MixerStatus::Ok);
    REQUIRE(c.SetBusSend(c_music, 0, c_fx, 0.5f, false) == MixerStatus::Ok);
    REQUIRE(c.SetDuck(c_fx, c_music, 0.5f, 0.1f, 100.0f) == MixerStatus::Ok);
    HdBlocks(c, 1);
    CHECK(c.LoadState(ByteSpan(snapshot.Data(), snapshot.Size()), &resolver) == SnapshotStatus::Mismatch);

    // The same graph loads.
    Mixer d;
    REQUIRE(d.Init(HdConfig()) == MixerStatus::Ok);
    BusId d_music;
    BusId d_fx;
    REQUIRE(d.CreateBus("music", BusId{0}, &d_music) == MixerStatus::Ok);
    REQUIRE(d.CreateBus("fx", BusId{0}, &d_fx) == MixerStatus::Ok);
    REQUIRE(d.SetBusSend(d_music, 0, d_fx, 0.5f, false) == MixerStatus::Ok);
    HdBlocks(d, 1);
    CHECK(d.LoadState(ByteSpan(snapshot.Data(), snapshot.Size()), &resolver) == SnapshotStatus::Ok);
}

TEST_CASE("Snapshot hardening: a fade longer than any ramp is saved and restored, the step exact while the pitch moves",
          "[audio][snapshot]")
{
    HdResolver resolver;
    resolver.clips.Push(HdClip("long", 40000, true));
    MixerConfig config = HdConfig(ChannelLayout::Mono);
    config.limiter = false;
    Mixer source;
    REQUIRE(source.Init(config) == MixerStatus::Ok);
    VoiceDesc desc;
    desc.clip = resolver.clips[0];
    desc.loop = true;
    VoiceId id;
    REQUIRE(source.Play(desc, &id) == MixerStatus::Ok);
    HdBlocks(source, 2);
    // A pitch change and, in the same block, a fade of four billion frames: the ramp carries the step from 1 to 4.
    REQUIRE(source.SetPitch(id, 4.0f) == MixerStatus::Ok);
    REQUIRE(source.Fade(id, 0.5f, 0xFFFFFFF0u) == MixerStatus::Ok);
    HdBlocks(source, 3);
    const Vector<byte> snapshot = HdSave(source);
    const Vector<f32> tail = HdBlocks(source, 8);

    Mixer restored;
    REQUIRE(restored.Init(config) == MixerStatus::Ok);
    HdBlocks(restored, 1);
    REQUIRE(restored.LoadState(ByteSpan(snapshot.Data(), snapshot.Size()), &resolver) == SnapshotStatus::Ok);
    CHECK(HdSameBits(tail, HdBlocks(restored, 8)));

    // The same ramp near its end: `delta * position` is far past 64 bits there. The ramp length (0xFFFFFFF0) is in the
    // snapshot followed by its position (three blocks); move the position to the last frames of the ramp.
    Vector<byte> late;
    for (usize i = 0; i < snapshot.Size(); ++i)
    {
        late.Push(snapshot[i]);
    }
    usize at = 0;
    for (usize i = 0; i + 8 <= late.Size(); ++i)
    {
        if (late[i] == byte(0xF0) && late[i + 1] == byte(0xFF) && late[i + 2] == byte(0xFF) &&
            late[i + 3] == byte(0xFF) && late[i + 4] == byte(3 * kBlockFrames & 0xFF) &&
            late[i + 5] == byte((3 * kBlockFrames) >> 8) && late[i + 6] == byte(0) && late[i + 7] == byte(0))
        {
            at = i + 4;
        }
    }
    REQUIRE(at != 0);
    HdPut(&late, at, 0xFFFFFFF0u - 2 * kBlockFrames);
    Mixer near_end;
    REQUIRE(near_end.Init(config) == MixerStatus::Ok);
    HdBlocks(near_end, 1);
    REQUIRE(near_end.LoadState(ByteSpan(late.Data(), late.Size()), &resolver) == SnapshotStatus::Ok);
    f64 before = 0.0;
    REQUIRE(near_end.Position(id, &before) == MixerStatus::Ok);
    HdBlocks(near_end, 1);
    f64 after = 0.0;
    REQUIRE(near_end.Position(id, &after) == MixerStatus::Ok);
    // Near the end of the ramp the step is the target's, pitch 4: a block moves the voice 4 * 256 frames (less a loop
    // length if it wrapped).
    const f64 loop_length = 40000.0 - 20.0;
    f64 moved = after - before;
    if (moved < 0.0)
    {
        moved += loop_length;
    }
    CHECK(moved == Approx(4.0 * kBlockFrames).epsilon(1e-6));
}

TEST_CASE("Mixer hardening: a paused virtual voice holds its position until it resumes", "[audio][mixer][voice]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(HdConfig(ChannelLayout::Mono)) == MixerStatus::Ok);
    VoiceDesc desc;
    desc.clip = HdClip("v", 48000);
    desc.is_virtual = true;
    VoiceId id;
    REQUIRE(mixer.Play(desc, &id) == MixerStatus::Ok);
    HdBlocks(mixer, 2);
    f64 at_pause = 0.0;
    REQUIRE(mixer.Position(id, &at_pause) == MixerStatus::Ok);
    CHECK(at_pause == 2.0 * kBlockFrames);
    REQUIRE(mixer.Pause(id) == MixerStatus::Ok);
    HdBlocks(mixer, 5);
    f64 held = 0.0;
    REQUIRE(mixer.Position(id, &held) == MixerStatus::Ok);
    CHECK(held == at_pause);
    // Made real while paused, it stays paused.
    REQUIRE(mixer.SetVirtual(id, false) == MixerStatus::Ok);
    HdBlocks(mixer, 2);
    REQUIRE(mixer.Position(id, &held) == MixerStatus::Ok);
    CHECK(held == at_pause);
    REQUIRE(mixer.SetVirtual(id, true) == MixerStatus::Ok);
    REQUIRE(mixer.Resume(id) == MixerStatus::Ok);
    HdBlocks(mixer, 1);
    f64 resumed = 0.0;
    REQUIRE(mixer.Position(id, &resumed) == MixerStatus::Ok);
    CHECK(resumed == at_pause + kBlockFrames);
}

TEST_CASE("Mixer hardening: a rate change bypasses the inserts, whose parameters were made for the old rate",
          "[audio][mixer][effect]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(HdConfig()) == MixerStatus::Ok);
    REQUIRE(mixer.SetInsert(BusId{0}, 0, MakeFilterEffect(2, 64)) == MixerStatus::Ok);
    HdBlocks(mixer, 1);
    const EffectParams params;
    REQUIRE(mixer.SetInsertParams(BusId{0}, 0, params) == MixerStatus::Ok);
    // The same rate and layout: nothing changes.
    REQUIRE(mixer.Reconfigure(48000, ChannelLayout::Stereo) == MixerStatus::Ok);
    CHECK(mixer.SetInsertParams(BusId{0}, 0, params) == MixerStatus::Ok);
    // Another rate: the slot is empty (an effect's cutoffs and delay lines are in frames of the old rate).
    REQUIRE(mixer.Reconfigure(44100, ChannelLayout::Stereo) == MixerStatus::Ok);
    CHECK(mixer.SetInsertParams(BusId{0}, 0, params) == MixerStatus::InvalidParam);
    // The effect can be inserted again for the new rate.
    CHECK(mixer.SetInsert(BusId{0}, 0, MakeFilterEffect(2, 64)) == MixerStatus::Ok);
}

TEST_CASE("Spatial hardening: a forgotten emitter's stop that a full queue refused is sent again", "[audio][spatial]")
{
    HdRig rig(8, 8);
    rig.world.Create(AudioListener(), HdAt(0, 0, 0));
    AudioSource s;
    s.clip = HdClip("loop", 48000);
    s.loop = true;
    const ecs::Entity e = rig.world.Create(s, HdAt(2, 0, 0));
    rig.audio.Update(rig.world, 0.016f);
    rig.Block();
    const VoiceId id = rig.audio.VoiceOf(e);
    REQUIRE(id.IsValid());
    // Fill the queue, then forget the emitter: its stop is refused.
    while (rig.mixer.SetPitch(id, 1.0f) == MixerStatus::Ok)
    {
    }
    rig.world.Destroy(e);
    rig.audio.Update(rig.world, 0.016f);
    CHECK_FALSE(rig.audio.IsKnown(e));
    rig.Block();                          // drains the queue
    rig.audio.Update(rig.world, 0.016f);  // the stop goes out now
    for (u32 i = 0; i < 6; ++i)
    {
        rig.Block();
    }
    CHECK_FALSE(rig.mixer.IsPlaying(id));
    CHECK(rig.mixer.VoiceCount() == 0);
}

TEST_CASE("Spatial hardening: a looping emitter whose voice was stolen starts again, a one-shot does not",
          "[audio][spatial]")
{
    HdRig rig(1, 64);
    rig.world.Create(AudioListener(), HdAt(0, 0, 0));
    AudioSource s;
    s.clip = HdClip("loop", 48000);
    s.loop = true;
    const ecs::Entity e = rig.world.Create(s, HdAt(2, 0, 0));
    rig.audio.Update(rig.world, 0.016f);
    rig.Block();
    REQUIRE(rig.audio.VoiceOf(e).IsValid());
    // A better voice takes the only slot.
    VoiceDesc better;
    better.clip = HdClip("better", 48000);
    better.loop = true;
    better.priority = 10;
    VoiceId taker;
    REQUIRE(rig.mixer.Play(better, &taker) == MixerStatus::Ok);
    rig.Block();
    rig.audio.Update(rig.world, 0.016f);
    CHECK_FALSE(rig.audio.IsFinished(e));
    // The taker ends; the loop is heard again.
    REQUIRE(rig.mixer.Stop(taker, 0) == MixerStatus::Ok);
    rig.Block();
    rig.audio.Update(rig.world, 0.016f);
    rig.audio.Update(rig.world, 0.016f);
    CHECK(rig.audio.VoiceOf(e).IsValid());
    rig.Block();
    CHECK(rig.mixer.IsPlaying(rig.audio.VoiceOf(e)));

    // A one-shot that played to its end stays finished.
    AudioSource shot;
    shot.clip = HdClip("shot", 300);
    shot.loop = false;
    rig.world.Destroy(e);
    rig.audio.Update(rig.world, 0.016f);
    for (u32 i = 0; i < 4; ++i)
    {
        rig.Block();
    }
    const ecs::Entity one = rig.world.Create(shot, HdAt(2, 0, 0));
    rig.audio.Update(rig.world, 0.016f);
    for (u32 i = 0; i < 4; ++i)
    {
        rig.Block();
    }
    rig.audio.Update(rig.world, 0.016f);
    CHECK(rig.audio.IsFinished(one));
    rig.audio.Update(rig.world, 0.016f);
    CHECK_FALSE(rig.audio.VoiceOf(one).IsValid());
}

TEST_CASE("Spatial hardening: an emitter that stops moving has no velocity, even when only its radius changes",
          "[audio][spatial][doppler]")
{
    HdRig rig(8, 256);
    rig.world.Create(AudioListener(), HdAt(0, 0, 0));
    AudioSource s;
    s.clip = HdClip("loop", 48000);
    s.loop = true;
    s.doppler = true;
    s.attenuation.min_distance = 1.0f;
    s.attenuation.max_distance = 100000.0f;  // far: the audible radius follows the gain
    const ecs::Entity e = rig.world.Create(s, HdAt(20, 0, 0));
    rig.audio.Update(rig.world, 0.1f);
    rig.world.Set(e, HdAt(17, 0, 0));  // 30 units per second towards the listener
    rig.audio.Update(rig.world, 0.1f);
    CHECK(rig.audio.PitchOf(e) > 1.05f);
    // It stops; its gain changes in the same update (a new audible radius).
    s.gain = 0.5f;
    rig.world.Set(e, s);
    rig.audio.Update(rig.world, 0.1f);
    CHECK(rig.audio.PitchOf(e) == Approx(1.0f));
}

TEST_CASE("Snapshot hardening: a mixer that never ran `Render` saves silence as its carried block, not old heap bytes",
          "[audio][snapshot]")
{
    // Leave NaN bytes in freed heap blocks the size of the carried block, so a buffer left uninitialised would hold
    // them.
    for (u32 round = 0; round < 4; ++round)
    {
        Vector<f32> junk;
        junk.Resize(static_cast<usize>(kBlockFrames) * 2);
        for (usize i = 0; i < junk.Size(); ++i)
        {
            junk[i] = Sqrt(-1.0f - static_cast<f32>(i));
        }
    }
    Mixer a;
    REQUIRE(a.Init(HdConfig()) == MixerStatus::Ok);
    HdBlocks(a, 2);
    const Vector<byte> snapshot = HdSave(a);
    Mixer b;
    REQUIRE(b.Init(HdConfig()) == MixerStatus::Ok);
    HdBlocks(b, 2);
    const Vector<byte> again = HdSave(b);
    REQUIRE(snapshot.Size() == again.Size());
    CHECK(MemCompare(snapshot.Data(), again.Data(), snapshot.Size()) == 0);
    HdResolver resolver;
    CHECK(b.LoadState(ByteSpan(snapshot.Data(), snapshot.Size()), &resolver) == SnapshotStatus::Ok);
}
