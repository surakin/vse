#include "audio/mixer.h"

#include "cstr/mem_ops.h"
#include "lang/scalar.h"
#include "lang/utility.h"
#include "log/assert.h"

namespace engine_placeholder::audio
{

namespace
{

constexpr u32 kSincTaps = 16;
constexpr u32 kSincPhases = 32;
constexpr f64 kPi = 3.14159265358979323846;
constexpr f32 kMaxVoiceGain = 16.0f;

bool IsFinite(f32 v)
{
    return v == v && v - v == 0.0f;
}

i64 PosMod(i64 value, i64 modulus)
{
    i64 m = value % modulus;
    return m < 0 ? m + modulus : m;
}

// What the interpolators read: one clip, with the loop region and whether the current frame is inside it.
struct TapSource
{
    const f32* samples = nullptr;
    u32 channels = 0;
    i64 frames = 0;
    bool loop = false;
    bool periodic = false;
    i64 loop_start = 0;
    i64 loop_end = 0;
};

f32 FetchTap(const TapSource& s, u32 channel, i64 index)
{
    if (s.loop && (index >= s.loop_end || (index < s.loop_start && s.periodic)))
    {
        index = s.loop_start + PosMod(index - s.loop_start, s.loop_end - s.loop_start);
    }
    if (index < 0)
    {
        index = 0;  // before the start: hold the first sample
    }
    if (index >= s.frames)
    {
        return 0.0f;
    }
    return s.samples[static_cast<usize>(index) * s.channels + channel];
}

f32 InterpLinear(const TapSource& s, u32 channel, i64 index, f32 frac)
{
    const f32 a = FetchTap(s, channel, index);
    const f32 b = FetchTap(s, channel, index + 1);
    return a + (b - a) * frac;
}

f32 InterpHermite(const TapSource& s, u32 channel, i64 index, f32 frac)
{
    const f32 ym1 = FetchTap(s, channel, index - 1);
    const f32 y0 = FetchTap(s, channel, index);
    const f32 y1 = FetchTap(s, channel, index + 1);
    const f32 y2 = FetchTap(s, channel, index + 2);
    const f32 c1 = 0.5f * (y1 - ym1);
    const f32 c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    const f32 c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
    return ((c3 * frac + c2) * frac + c1) * frac + y0;
}

f32 InterpSinc(const TapSource& s, const f32* table, u32 channel, i64 index, f32 frac)
{
    const f32 position = frac * static_cast<f32>(kSincPhases);
    u32 phase = static_cast<u32>(position);
    if (phase >= kSincPhases)
    {
        phase = kSincPhases - 1;
    }
    const f32 blend = position - static_cast<f32>(phase);
    const f32* row0 = table + static_cast<usize>(phase) * kSincTaps;
    const f32* row1 = row0 + kSincTaps;
    f32 sum = 0.0f;
    for (u32 t = 0; t < kSincTaps; ++t)
    {
        const f32 w = row0[t] + (row1[t] - row0[t]) * blend;
        sum += w * FetchTap(s, channel, index + static_cast<i64>(t) - 7);
    }
    return sum;
}

// The sinc table: 33 rows (so the last phase can blend toward the next frame) of 16 taps, a Blackman-windowed sinc,
// each row normalised to a gain of 1 at DC. Row 0 is exactly a delta so that a whole-frame position reproduces the
// sample.
void BuildSincTable(f32* table)
{
    for (u32 p = 0; p <= kSincPhases; ++p)
    {
        f64 row[kSincTaps];
        f64 total = 0.0;
        const f64 frac = static_cast<f64>(p) / static_cast<f64>(kSincPhases);
        for (u32 t = 0; t < kSincTaps; ++t)
        {
            const f64 d = static_cast<f64>(static_cast<i32>(t) - 7) - frac;
            f64 sinc = 1.0;
            if (d != 0.0)
            {
                sinc = Sin(kPi * d) / (kPi * d);
            }
            const f64 x = d / 8.0;
            f64 window = 0.0;
            if (x > -1.0 && x < 1.0)
            {
                window = 0.42 + 0.5 * Cos(kPi * x) + 0.08 * Cos(2.0 * kPi * x);
            }
            row[t] = sinc * window;
            total += row[t];
        }
        for (u32 t = 0; t < kSincTaps; ++t)
        {
            table[static_cast<usize>(p) * kSincTaps + t] = static_cast<f32>(row[t] / total);
        }
        if (p == 0)
        {
            for (u32 t = 0; t < kSincTaps; ++t)
            {
                table[t] = t == 7 ? 1.0f : 0.0f;
            }
        }
    }
}

// The equal-power pan law of a mono clip: left = Sqrt((1 - p) / 2), right = Sqrt((1 + p) / 2); -3 dB at the centre.
void PanGains(f32 pan, f32* left, f32* right)
{
    *left = Sqrt((1.0f - pan) * 0.5f);
    *right = Sqrt((1.0f + pan) * 0.5f);
}

// The balance of a stereo clip: the side away from the pan is attenuated, the centre is unity.
void BalanceGains(f32 pan, f32* left, f32* right)
{
    *left = pan > 0.0f ? Sqrt(1.0f - pan) : 1.0f;
    *right = pan < 0.0f ? Sqrt(1.0f + pan) : 1.0f;
}

}  // namespace

MixerConfig MixerConfigFromSettings(const AudioSettings& settings, u32 sample_rate)
{
    MixerConfig config;
    config.sample_rate = sample_rate != 0 ? sample_rate : (settings.sample_rate != 0 ? settings.sample_rate : 48000);
    config.layout = LayoutForChannelCount(settings.channels);
    config.max_voices = settings.max_voices > kMaxVoices ? kMaxVoices : settings.max_voices;
    config.quality = settings.resampler;
    return config;
}

MixerStatus Mixer::Init(const MixerConfig& config)
{
    if (config.sample_rate < kMinSampleRate || config.sample_rate > kMaxSampleRate || config.max_voices == 0 ||
        config.max_voices > kMaxVoices || ChannelCount(config.layout) == 0)
    {
        return MixerStatus::InvalidConfig;
    }
    config_ = config;
    channels_ = ChannelCount(config.layout);
    voices_.Reset();
    sinc_.Reset();
    carry_.Reset();
    if (!voices_.TryReserve(config.max_voices))
    {
        return MixerStatus::OutOfMemory;
    }
    for (u32 i = 0; i < config.max_voices; ++i)
    {
        voices_.Push(Voice());
    }
    if (!carry_.TryResizeUninitialized(static_cast<usize>(kBlockFrames) * channels_) ||
        !sinc_.TryResizeUninitialized(static_cast<usize>(kSincPhases + 1) * kSincTaps))
    {
        return MixerStatus::OutOfMemory;
    }
    BuildSincTable(sinc_.Data());
    live_ = 0;
    next_serial_ = 1;
    stats_ = MixerStats();
    carry_next_ = kBlockFrames;
    return MixerStatus::Ok;
}

MixerStatus Mixer::Find(VoiceId id, Voice** voice)
{
    if (id.index >= voices_.Size())
    {
        return MixerStatus::InvalidVoice;
    }
    Voice& v = voices_[id.index];
    if (!v.active || v.generation != id.generation)
    {
        return MixerStatus::InvalidVoice;
    }
    *voice = &v;
    return MixerStatus::Ok;
}

MixerStatus Mixer::FindConst(VoiceId id, const Voice** voice) const
{
    if (id.index >= voices_.Size())
    {
        return MixerStatus::InvalidVoice;
    }
    const Voice& v = voices_[id.index];
    if (!v.active || v.generation != id.generation)
    {
        return MixerStatus::InvalidVoice;
    }
    *voice = &v;
    return MixerStatus::Ok;
}

void Mixer::Finish(Voice* voice)
{
    voice->active = false;
    voice->clip.Reset();
    voice->generation += 1;
    if (voice->generation == 0)
    {
        voice->generation = 1;
    }
    live_ -= 1;
}

// Computes what a voice needs from its parameters: the step and the routing table with the gain and pan in it.
void Mixer::Setup(Voice* voice) const
{
    const f64 ratio = static_cast<f64>(voice->pitch) * static_cast<f64>(voice->clip->Format().sample_rate) /
                      static_cast<f64>(config_.sample_rate);
    voice->step = static_cast<u64>(ratio * 4294967296.0);
    const ChannelLayout from = voice->clip->Format().layout;
    ChannelMatrix route = BuildChannelMatrix(from, config_.layout);
    const bool stereo_out = config_.layout != ChannelLayout::Mono;
    if (from == ChannelLayout::Mono && stereo_out)
    {
        // Mono over the front pair with the pan law (the table's mono rule copies to both with gain 1).
        for (u32 o = 0; o < route.outputs; ++o)
        {
            route.gain[o][0] = 0.0f;
        }
        PanGains(voice->pan, &route.gain[0][0], &route.gain[1][0]);
    }
    else if (from != ChannelLayout::Mono && stereo_out)
    {
        f32 left = 1.0f;
        f32 right = 1.0f;
        BalanceGains(voice->pan, &left, &right);
        for (u32 i = 0; i < route.inputs; ++i)
        {
            route.gain[0][i] *= left;
            route.gain[1][i] *= right;
        }
    }
    for (u32 o = 0; o < route.outputs; ++o)
    {
        for (u32 i = 0; i < route.inputs; ++i)
        {
            route.gain[o][i] *= voice->gain;
        }
    }
    voice->route = route;
}

MixerStatus Mixer::Play(const VoiceDesc& desc, VoiceId* out)
{
    if (!desc.clip)
    {
        return MixerStatus::NoClip;
    }
    if (!IsFinite(desc.gain) || desc.gain < 0.0f || desc.gain > kMaxVoiceGain || !IsFinite(desc.pan) ||
        desc.pan < -1.0f || desc.pan > 1.0f || !IsFinite(desc.pitch) || desc.pitch < kMinPitch ||
        desc.pitch > kMaxPitch || desc.start_frame >= desc.clip->Frames())
    {
        return MixerStatus::InvalidParam;
    }
    u32 slot = 0xFFFFFFFFu;
    for (u32 i = 0; i < voices_.Size(); ++i)
    {
        if (!voices_[i].active)
        {
            slot = i;
            break;
        }
    }
    if (slot == 0xFFFFFFFFu)
    {
        // Full: the victim is the voice with the lowest (priority, gain, age); the new voice must not rank below it.
        u32 victim = 0;
        for (u32 i = 1; i < voices_.Size(); ++i)
        {
            const Voice& a = voices_[i];
            const Voice& b = voices_[victim];
            const bool lower = a.priority != b.priority ? a.priority < b.priority
                               : a.gain != b.gain       ? a.gain < b.gain
                                                        : a.serial < b.serial;
            if (lower)
            {
                victim = i;
            }
        }
        const Voice& v = voices_[victim];
        if (desc.priority < v.priority || (desc.priority == v.priority && desc.gain < v.gain))
        {
            stats_.voices_refused += 1;
            return MixerStatus::Full;
        }
        Finish(&voices_[victim]);
        stats_.voices_stolen += 1;
        slot = victim;
    }
    Voice& voice = voices_[slot];
    voice.clip = desc.clip;
    voice.position = desc.start_frame << 32;
    voice.serial = next_serial_++;
    voice.gain = desc.gain;
    voice.pan = desc.pan;
    voice.pitch = desc.pitch;
    voice.priority = desc.priority;
    voice.loop = desc.loop;
    voice.is_virtual = desc.is_virtual;
    voice.active = true;
    Setup(&voice);
    live_ += 1;
    out->index = slot;
    out->generation = voice.generation;
    return MixerStatus::Ok;
}

MixerStatus Mixer::Stop(VoiceId id)
{
    Voice* voice = nullptr;
    const MixerStatus status = Find(id, &voice);
    if (status == MixerStatus::Ok)
    {
        Finish(voice);
    }
    return status;
}

bool Mixer::IsPlaying(VoiceId id) const
{
    const Voice* voice = nullptr;
    return FindConst(id, &voice) == MixerStatus::Ok;
}

MixerStatus Mixer::SetGain(VoiceId id, f32 gain)
{
    Voice* voice = nullptr;
    const MixerStatus status = Find(id, &voice);
    if (status != MixerStatus::Ok)
    {
        return status;
    }
    if (!IsFinite(gain) || gain < 0.0f || gain > kMaxVoiceGain)
    {
        return MixerStatus::InvalidParam;
    }
    voice->gain = gain;
    Setup(voice);
    return MixerStatus::Ok;
}

MixerStatus Mixer::SetPan(VoiceId id, f32 pan)
{
    Voice* voice = nullptr;
    const MixerStatus status = Find(id, &voice);
    if (status != MixerStatus::Ok)
    {
        return status;
    }
    if (!IsFinite(pan) || pan < -1.0f || pan > 1.0f)
    {
        return MixerStatus::InvalidParam;
    }
    voice->pan = pan;
    Setup(voice);
    return MixerStatus::Ok;
}

MixerStatus Mixer::SetPitch(VoiceId id, f32 pitch)
{
    Voice* voice = nullptr;
    const MixerStatus status = Find(id, &voice);
    if (status != MixerStatus::Ok)
    {
        return status;
    }
    if (!IsFinite(pitch) || pitch < kMinPitch || pitch > kMaxPitch)
    {
        return MixerStatus::InvalidParam;
    }
    voice->pitch = pitch;
    Setup(voice);
    return MixerStatus::Ok;
}

MixerStatus Mixer::SetVirtual(VoiceId id, bool is_virtual)
{
    Voice* voice = nullptr;
    const MixerStatus status = Find(id, &voice);
    if (status == MixerStatus::Ok)
    {
        voice->is_virtual = is_virtual;
    }
    return status;
}

MixerStatus Mixer::Position(VoiceId id, f64* frames) const
{
    const Voice* voice = nullptr;
    const MixerStatus status = FindConst(id, &voice);
    if (status == MixerStatus::Ok)
    {
        *frames = static_cast<f64>(voice->position) / 4294967296.0;
    }
    return status;
}

// Moves a virtual voice on by one block: the loop wrap and the end of the clip behave as for a mixed voice.
void Mixer::AdvanceVirtual(Voice* voice)
{
    const ClipView view = voice->clip->View();
    const u64 frames = view.Frames();
    const u64 loop_start = view.HasLoop() ? view.desc.loop_start : 0;
    const u64 loop_end = view.HasLoop() ? view.desc.loop_end : frames;
    u64 position = voice->position;
    for (u32 n = 0; n < kBlockFrames; ++n)
    {
        position += voice->step;
        if (voice->loop)
        {
            const u64 length = (loop_end - loop_start) << 32;
            while (position >= (loop_end << 32))
            {
                position -= length;
            }
        }
        else if ((position >> 32) >= frames)
        {
            stats_.voices_finished += 1;
            Finish(voice);
            return;
        }
    }
    voice->position = position;
}

void Mixer::MixVoice(Voice* voice, f32* out)
{
    const ClipView view = voice->clip->View();
    TapSource source;
    source.samples = view.samples.Data();
    source.channels = view.Channels();
    source.frames = static_cast<i64>(view.Frames());
    source.loop = voice->loop;
    source.loop_start = view.HasLoop() ? static_cast<i64>(view.desc.loop_start) : 0;
    source.loop_end = view.HasLoop() ? static_cast<i64>(view.desc.loop_end) : source.frames;
    const u64 loop_length = static_cast<u64>(source.loop_end - source.loop_start) << 32;
    const u64 loop_end_fixed = static_cast<u64>(source.loop_end) << 32;
    const u32 in_channels = source.channels;
    const u32 out_channels = channels_;
    u64 position = voice->position;
    for (u32 n = 0; n < kBlockFrames; ++n)
    {
        const i64 index = static_cast<i64>(position >> 32);
        if (!voice->loop && index >= source.frames)
        {
            stats_.voices_finished += 1;
            Finish(voice);
            return;
        }
        source.periodic = index >= source.loop_start;
        const f32 frac = static_cast<f32>(static_cast<u32>(position & 0xFFFFFFFFull)) * (1.0f / 4294967296.0f);
        f32 value[kMaxChannels];
        for (u32 c = 0; c < in_channels; ++c)
        {
            switch (config_.quality)
            {
            case ResamplerQuality::Linear: value[c] = InterpLinear(source, c, index, frac); break;
            case ResamplerQuality::Sinc: value[c] = InterpSinc(source, sinc_.Data(), c, index, frac); break;
            case ResamplerQuality::Hermite:
            default: value[c] = InterpHermite(source, c, index, frac); break;
            }
        }
        f32* frame = out + static_cast<usize>(n) * out_channels;
        for (u32 o = 0; o < out_channels; ++o)
        {
            f32 sum = 0.0f;
            for (u32 c = 0; c < in_channels; ++c)
            {
                sum += value[c] * voice->route.gain[o][c];
            }
            frame[o] += sum;
        }
        position += voice->step;
        if (voice->loop)
        {
            while (position >= loop_end_fixed)
            {
                position -= loop_length;
            }
        }
    }
    voice->position = position;
}

void Mixer::RenderBlock(Span<f32> out)
{
    const usize samples = static_cast<usize>(kBlockFrames) * channels_;
    ENGINE_ASSERT(out.Size() >= samples, "Mixer::RenderBlock: the output is smaller than a block");
    MemSet(out.Data(), 0, samples * sizeof(f32));
    u32 mixed = 0;
    u32 virtual_count = 0;
    for (u32 i = 0; i < voices_.Size(); ++i)
    {
        Voice& voice = voices_[i];
        if (!voice.active)
        {
            continue;
        }
        if (voice.is_virtual)
        {
            virtual_count += 1;
            AdvanceVirtual(&voice);
        }
        else
        {
            mixed += 1;
            MixVoice(&voice, out.Data());
        }
    }
    stats_.blocks += 1;
    stats_.voices_mixed = mixed;
    stats_.voices_virtual = virtual_count;
}

void Mixer::Render(f32* out, u32 frames)
{
    while (frames > 0)
    {
        if (carry_next_ >= kBlockFrames)
        {
            RenderBlock(Span<f32>(carry_.Data(), carry_.Size()));
            carry_next_ = 0;
        }
        u32 count = kBlockFrames - carry_next_;
        if (count > frames)
        {
            count = frames;
        }
        MemCopy(out, carry_.Data() + static_cast<usize>(carry_next_) * channels_,
                static_cast<usize>(count) * channels_ * sizeof(f32));
        out += static_cast<usize>(count) * channels_;
        carry_next_ += count;
        frames -= count;
    }
}

}  // namespace engine_placeholder::audio
