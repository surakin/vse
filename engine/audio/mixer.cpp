#include "audio/mixer.h"
#include "audio/panning.h"

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
    config.ramp_frames = settings.ramp_frames;
    return config;
}

MixerStatus Mixer::Init(const MixerConfig& config)
{
    if (config.sample_rate < kMinSampleRate || config.sample_rate > kMaxSampleRate || config.max_voices == 0 ||
        config.max_voices > kMaxVoices || ChannelCount(config.layout) == 0 || config.ramp_frames > 65536 ||
        config.lead_blocks > 1024 || config.command_capacity < 2 || config.event_capacity < 2)
    {
        return MixerStatus::InvalidConfig;
    }
    config_ = config;
    channels_ = ChannelCount(config.layout);
    voices_.Reset();
    bus_buffers_.Reset();
    control_.Reset();
    pending_.Reset();
    sinc_.Reset();
    carry_.Reset();
    if (!voices_.TryReserve(config.max_voices) || !control_.TryReserve(config.max_voices) ||
        !pending_.TryReserve(config.event_capacity))
    {
        return MixerStatus::OutOfMemory;
    }
    for (u32 i = 0; i < config.max_voices; ++i)
    {
        voices_.Push(Voice());
        control_.Push(ControlSlot());
    }
    if (!carry_.TryResizeUninitialized(static_cast<usize>(kBlockFrames) * channels_) ||
        !sinc_.TryResizeUninitialized(static_cast<usize>(kSincPhases + 1) * kSincTaps) ||
        !commands_.Init(config.command_capacity) ||
        !events_.Init(config.event_capacity > 2 * config.max_voices ? config.event_capacity : 2 * config.max_voices))
    {
        return MixerStatus::OutOfMemory;
    }
    BuildSincTable(sinc_.Data());
    // The buses: the master exists from the start; the buffers are allocated for all of them now.
    if (config.max_buses == 0 || config.max_buses > kMaxBuses ||
        !bus_buffers_.TryResizeUninitialized(static_cast<usize>(config.max_buses) * kBlockFrames * channels_))
    {
        return config.max_buses == 0 || config.max_buses > kMaxBuses ? MixerStatus::InvalidConfig
                                                                     : MixerStatus::OutOfMemory;
    }
    for (u32 b = 0; b < kMaxBuses; ++b)
    {
        buses_[b] = Bus();
        control_buses_[b] = ControlBus();
    }
    buses_[0].active = true;
    control_buses_[0].active = true;
    const char master_name[] = "master";
    for (u32 i = 0; i < sizeof(master_name); ++i)
    {
        control_buses_[0].name[i] = master_name[i];
    }
    bus_order_[0] = 0;
    bus_order_count_ = 1;
    limiter_on_ = false;
    if (!limiter_.Init(channels_, config.limiter_config))
    {
        return MixerStatus::InvalidConfig;
    }
    limiter_on_ = config.limiter;
    limiter_min_gain_ = 1.0f;
    live_ = 0;
    block_ = 0;
    next_serial_ = 1;
    audio_stats_ = MixerStats();
    control_stolen_ = 0;
    control_refused_ = 0;
    control_dropped_ = 0;
    control_events_dropped_ = 0;
    next_block_.Store(0, MemoryOrder::Release);
    watermark_.Store(0, MemoryOrder::Release);
    events_dropped_audio_.Store(0, MemoryOrder::Release);
    event_ring_capacity_ = events_.Capacity();
    carry_next_ = kBlockFrames;
    return MixerStatus::Ok;
}

MixerStatus Mixer::Reconfigure(u32 sample_rate, ChannelLayout layout)
{
    const u32 channels = ChannelCount(layout);
    if (sample_rate < kMinSampleRate || sample_rate > kMaxSampleRate || channels == 0)
    {
        return MixerStatus::InvalidConfig;
    }
    const bool channels_changed = channels != channels_;
    if (channels_changed)
    {
        Vector<f32> carry;
        Vector<f32> buses;
        Limiter limiter;
        if (!carry.TryResizeUninitialized(static_cast<usize>(kBlockFrames) * channels) ||
            !buses.TryResizeUninitialized(static_cast<usize>(config_.max_buses) * kBlockFrames * channels) ||
            !limiter.Init(channels, config_.limiter_config))
        {
            return MixerStatus::OutOfMemory;
        }
        carry_ = Move(carry);
        bus_buffers_ = Move(buses);
        limiter_ = Move(limiter);
        for (u32 b = 0; b < kMaxBuses; ++b)
        {
            for (u32 i = 0; i < kMaxInserts; ++i)
            {
                buses_[b].inserts[i] = nullptr;
                control_buses_[b].inserts[i] = nullptr;
            }
        }
        channels_ = channels;
    }
    config_.sample_rate = sample_rate;
    config_.layout = layout;
    carry_next_ = kBlockFrames;  // the half-rendered block belonged to the old format
    limiter_.Reset();
    for (u32 i = 0; i < voices_.Size(); ++i)
    {
        if (voices_[i].active)
        {
            Retarget(&voices_[i], 0);
        }
    }
    return MixerStatus::Ok;
}

MixerStats Mixer::Stats() const
{
    MixerStats stats = audio_stats_;
    stats.voices_stolen = control_stolen_;
    stats.voices_refused = control_refused_;
    stats.commands_dropped = control_dropped_;
    stats.events_dropped = control_events_dropped_ + events_dropped_audio_.Load(MemoryOrder::Acquire);
    stats.limiter_min_gain = limiter_min_gain_;
    return stats;
}

// ---- The control side ----

Mixer::ControlSlot* Mixer::FindControl(VoiceId id)
{
    if (id.index >= control_.Size())
    {
        return nullptr;
    }
    ControlSlot& slot = control_[id.index];
    if (!slot.active || slot.stopping || slot.generation != id.generation)
    {
        return nullptr;
    }
    return &slot;
}

void Mixer::AddUserEvent(const VoiceEvent& event)
{
    if (pending_.Size() >= config_.event_capacity)
    {
        control_events_dropped_ += 1;
        return;
    }
    pending_.Push(event);
}

// Moves the events the audio thread posted into the control side's list and frees the slots that finished.
void Mixer::DrainEvents()
{
    while (VoiceEvent* event = events_.Front())
    {
        const VoiceEvent value = *event;
        events_.Pop();
        if (value.type == VoiceEventType::Finished || value.type == VoiceEventType::Stopped)
        {
            if (value.id.index < control_.Size() && control_[value.id.index].generation == value.id.generation)
            {
                control_[value.id.index].active = false;
                control_[value.id.index].stopping = false;
            }
        }
        AddUserEvent(value);
    }
}

u64 Mixer::StampFor(u64 block) const
{
    if (block != kAtLead)
    {
        return block;
    }
    return next_block_.Load(MemoryOrder::Acquire) + config_.lead_blocks;
}

MixerStatus Mixer::Post(Command&& command)
{
    if (!commands_.Push(Move(command)))
    {
        control_dropped_ += 1;
        return MixerStatus::QueueFull;
    }
    return MixerStatus::Ok;
}

bool Mixer::IsPlaying(VoiceId id)
{
    DrainEvents();
    return FindControl(id) != nullptr;
}

u32 Mixer::PollEvents(VoiceEvent* out, u32 max)
{
    DrainEvents();
    u32 count = 0;
    while (count < max && count < pending_.Size())
    {
        out[count] = pending_[count];
        count += 1;
    }
    if (count > 0)
    {
        pending_.RemoveRange(0, count);
    }
    return count;
}

void Mixer::SetWatermark(u64 block)
{
    watermark_.Store(block + 1, MemoryOrder::Release);  // stored as the number of complete blocks
}

MixerStatus Mixer::Play(const VoiceDesc& desc, VoiceId* out, u64 block)
{
    if (!desc.clip)
    {
        return MixerStatus::NoClip;
    }
    if (!IsFinite(desc.gain) || desc.gain < 0.0f || desc.gain > kMaxVoiceGain || !IsFinite(desc.pan) ||
        desc.pan < -1.0f || desc.pan > 1.0f || !IsFinite(desc.pitch) || desc.pitch < kMinPitch ||
        desc.pitch > kMaxPitch || desc.start_frame >= desc.clip->Frames() || desc.delay_frames >= kBlockFrames ||
        (desc.stream && (desc.clip.Get() != desc.stream->Clip().Get() || desc.start_frame != 0)) ||
        !IsFinite(desc.send_gain) || desc.send_gain < 0.0f || desc.send_gain > kMaxVoiceGain ||
        !IsFinite(desc.direction_x) || !IsFinite(desc.direction_y) || !IsFinite(desc.direction_z) ||
        !IsFinite(desc.spread) || !IsFinite(desc.lfe_send) || desc.lfe_send < 0.0f || desc.lfe_send > 1.0f ||
        !(desc.air_coefficient > 0.0f) || desc.air_coefficient > 1.0f)
    {
        return MixerStatus::InvalidParam;
    }
    if (!ValidBus(desc.bus) || (desc.send_bus.IsValid() && !ValidBus(desc.send_bus)))
    {
        return MixerStatus::InvalidBus;
    }
    DrainEvents();
    const u64 stamp = StampFor(block);
    u32 slot = 0xFFFFFFFFu;
    for (u32 i = 0; i < control_.Size(); ++i)
    {
        if (!control_[i].active)
        {
            slot = i;
            break;
        }
    }
    bool steal = false;
    if (slot == 0xFFFFFFFFu)
    {
        // Full: the victim is the voice with the lowest (priority, gain, age); the new voice must not rank below it.
        u32 victim = 0;
        for (u32 i = 1; i < control_.Size(); ++i)
        {
            const ControlSlot& a = control_[i];
            const ControlSlot& b = control_[victim];
            const bool lower = a.stopping != b.stopping   ? a.stopping
                               : a.priority != b.priority ? a.priority < b.priority
                               : a.gain != b.gain         ? a.gain < b.gain
                                                          : a.serial < b.serial;
            if (lower)
            {
                victim = i;
            }
        }
        const ControlSlot& v = control_[victim];
        if (!v.stopping && (desc.priority < v.priority || (desc.priority == v.priority && desc.gain < v.gain)))
        {
            control_refused_ += 1;
            return MixerStatus::Full;
        }
        slot = victim;
        steal = true;
    }
    ControlSlot& control = control_[slot];
    if (steal)
    {
        // The victim stops at once, ahead of the new voice in the same slot.
        Command stop;
        stop.stamp = stamp;
        stop.type = CommandType::Stop;
        stop.id.index = slot;
        stop.id.generation = control.generation;
        stop.frames = 0;
        if (commands_.Size() + 2 > config_.command_capacity)
        {
            control_dropped_ += 1;
            return MixerStatus::QueueFull;
        }
        Post(Move(stop));
        VoiceEvent event;
        event.type = VoiceEventType::Stolen;
        event.id.index = slot;
        event.id.generation = control.generation;
        AddUserEvent(event);
        control_stolen_ += 1;
    }
    Command play;
    play.stamp = stamp;
    play.type = CommandType::Play;
    play.id.index = slot;
    play.id.generation = control.generation + 1;
    play.desc = desc;
    const MixerStatus status = Post(Move(play));
    if (status != MixerStatus::Ok)
    {
        return status;
    }
    control.generation += 1;
    control.active = true;
    control.stopping = false;
    control.priority = desc.priority;
    control.gain = desc.gain;
    control.serial = next_serial_++;
    out->index = slot;
    out->generation = control.generation;
    return MixerStatus::Ok;
}

// Posts a command for a live voice after checking the id against the control side.
MixerStatus Mixer::PostFor(VoiceId id, Command&& command)
{
    DrainEvents();
    if (FindControl(id) == nullptr)
    {
        return MixerStatus::InvalidVoice;
    }
    return Post(Move(command));
}

MixerStatus Mixer::Stop(VoiceId id, u32 fade_frames, u64 block)
{
    DrainEvents();
    ControlSlot* slot = FindControl(id);
    if (slot == nullptr)
    {
        return MixerStatus::InvalidVoice;
    }
    Command command;
    command.stamp = StampFor(block);
    command.type = CommandType::Stop;
    command.id = id;
    command.frames = fade_frames == kRampFrames ? config_.ramp_frames : fade_frames;
    const MixerStatus status = Post(Move(command));
    if (status == MixerStatus::Ok)
    {
        // The id is dead to the game from now on; the slot is reused after the Stopped event.
        slot->stopping = true;
    }
    return status;
}

MixerStatus Mixer::Pause(VoiceId id, u64 block)
{
    Command command;
    command.stamp = StampFor(block);
    command.type = CommandType::Pause;
    command.id = id;
    return PostFor(id, Move(command));
}

MixerStatus Mixer::Resume(VoiceId id, u64 block)
{
    Command command;
    command.stamp = StampFor(block);
    command.type = CommandType::Resume;
    command.id = id;
    return PostFor(id, Move(command));
}

MixerStatus Mixer::Seek(VoiceId id, u64 frame, u64 block)
{
    Command command;
    command.stamp = StampFor(block);
    command.type = CommandType::Seek;
    command.id = id;
    command.frames = frame;
    return PostFor(id, Move(command));
}

MixerStatus Mixer::SetGain(VoiceId id, f32 gain, u64 block)
{
    if (!IsFinite(gain) || gain < 0.0f || gain > kMaxVoiceGain)
    {
        return MixerStatus::InvalidParam;
    }
    Command command;
    command.stamp = StampFor(block);
    command.type = CommandType::SetGain;
    command.id = id;
    command.value = gain;
    const MixerStatus status = PostFor(id, Move(command));
    if (status == MixerStatus::Ok)
    {
        control_[id.index].gain = gain;
    }
    return status;
}

MixerStatus Mixer::SetPan(VoiceId id, f32 pan, u64 block)
{
    if (!IsFinite(pan) || pan < -1.0f || pan > 1.0f)
    {
        return MixerStatus::InvalidParam;
    }
    Command command;
    command.stamp = StampFor(block);
    command.type = CommandType::SetPan;
    command.id = id;
    command.value = pan;
    return PostFor(id, Move(command));
}

MixerStatus Mixer::SetPitch(VoiceId id, f32 pitch, u64 block)
{
    if (!IsFinite(pitch) || pitch < kMinPitch || pitch > kMaxPitch)
    {
        return MixerStatus::InvalidParam;
    }
    Command command;
    command.stamp = StampFor(block);
    command.type = CommandType::SetPitch;
    command.id = id;
    command.value = pitch;
    return PostFor(id, Move(command));
}

MixerStatus Mixer::SetLoop(VoiceId id, bool loop, u64 block)
{
    Command command;
    command.stamp = StampFor(block);
    command.type = CommandType::SetLoop;
    command.id = id;
    command.value = loop ? 1.0f : 0.0f;
    return PostFor(id, Move(command));
}

MixerStatus Mixer::Fade(VoiceId id, f32 gain, u32 frames, u64 block)
{
    if (!IsFinite(gain) || gain < 0.0f || gain > kMaxVoiceGain)
    {
        return MixerStatus::InvalidParam;
    }
    Command command;
    command.stamp = StampFor(block);
    command.type = CommandType::Fade;
    command.id = id;
    command.value = gain;
    command.frames = frames;
    const MixerStatus status = PostFor(id, Move(command));
    if (status == MixerStatus::Ok)
    {
        control_[id.index].gain = gain;
    }
    return status;
}

MixerStatus Mixer::SetDirection(VoiceId id, f32 x, f32 y, f32 z, f32 spread, u64 block)
{
    if (!IsFinite(x) || !IsFinite(y) || !IsFinite(z) || !IsFinite(spread))
    {
        return MixerStatus::InvalidParam;
    }
    Command command;
    command.stamp = StampFor(block);
    command.type = CommandType::SetDirection;
    command.id = id;
    command.value = x;
    command.value2 = y;
    command.value3 = z;
    command.value4 = spread;
    return PostFor(id, Move(command));
}

MixerStatus Mixer::SetAirAbsorption(VoiceId id, f32 coefficient, u64 block)
{
    if (!IsFinite(coefficient) || !(coefficient > 0.0f) || coefficient > 1.0f)
    {
        return MixerStatus::InvalidParam;
    }
    Command command;
    command.stamp = StampFor(block);
    command.type = CommandType::SetAirAbsorption;
    command.id = id;
    command.value = coefficient;
    return PostFor(id, Move(command));
}

MixerStatus Mixer::SetLfeSend(VoiceId id, f32 level, u64 block)
{
    if (!IsFinite(level) || level < 0.0f || level > 1.0f)
    {
        return MixerStatus::InvalidParam;
    }
    Command command;
    command.stamp = StampFor(block);
    command.type = CommandType::SetLfeSend;
    command.id = id;
    command.value = level;
    return PostFor(id, Move(command));
}

MixerStatus Mixer::SetVirtual(VoiceId id, bool is_virtual, u64 block)
{
    Command command;
    command.stamp = StampFor(block);
    command.type = CommandType::SetVirtual;
    command.id = id;
    command.value = is_virtual ? 1.0f : 0.0f;
    return PostFor(id, Move(command));
}

// ---- The audio side ----

void Mixer::PostEvent(VoiceEventType type, const Voice& voice, bool terminal)
{
    // A terminal event frees the control side's slot, so it must never be lost: the ring holds at least one per voice
    // and the other kinds stop being posted when only that reserve is left.
    if (!terminal && events_.Size() + config_.max_voices >= event_ring_capacity_)
    {
        events_dropped_audio_.FetchAdd(1, MemoryOrder::Relaxed);
        return;
    }
    VoiceEvent event;
    event.type = type;
    event.id.index = static_cast<u32>(&voice - voices_.Data());
    event.id.generation = voice.generation;
    event.block = block_;
    if (!events_.Push(Move(event)))
    {
        events_dropped_audio_.FetchAdd(1, MemoryOrder::Relaxed);
    }
}

void Mixer::Finish(Voice* voice, bool report, VoiceEventType type)
{
    if (report)
    {
        PostEvent(type, *voice, type == VoiceEventType::Finished || type == VoiceEventType::Stopped);
    }
    voice->active = false;
    voice->clip.Reset();
    voice->stream.Reset();
    live_ -= 1;
}

// The routing table and the step a voice's parameters ask for. A stopping or pausing voice is routed to silence.
void Mixer::ComputeTargets(Voice* voice, ChannelMatrix* route, u64* step) const
{
    const f64 ratio = static_cast<f64>(voice->pitch) * static_cast<f64>(voice->clip->Format().sample_rate) /
                      static_cast<f64>(config_.sample_rate);
    *step = static_cast<u64>(ratio * 4294967296.0);
    const ChannelLayout from = voice->clip->Format().layout;
    ChannelMatrix play = BuildChannelMatrix(from, config_.layout);
    const bool front_pair = config_.layout != ChannelLayout::Mono;
    if (from == ChannelLayout::Mono && front_pair)
    {
        // Mono over the front pair with the pan law (the table's mono rule copies to both with gain 1).
        for (u32 o = 0; o < play.outputs; ++o)
        {
            play.gain[o][0] = 0.0f;
        }
        if (voice->directional)
        {
            f32 gains[kMaxPanChannels];
            SpeakerGains(config_.layout, voice->dir[0], voice->dir[1], voice->dir[2], voice->spread, gains);
            for (u32 o = 0; o < play.outputs; ++o)
            {
                play.gain[o][0] = gains[o];
            }
            if (voice->lfe_send > 0.0f &&
                (config_.layout == ChannelLayout::Surround51 || config_.layout == ChannelLayout::Surround71))
            {
                play.gain[static_cast<u32>(Speaker::Lfe)][0] = voice->lfe_send;
            }
        }
        else
        {
            PanGains(voice->pan, &play.gain[0][0], &play.gain[1][0]);
        }
    }
    else if (from != ChannelLayout::Mono && front_pair)
    {
        f32 left = 1.0f;
        f32 right = 1.0f;
        BalanceGains(voice->pan, &left, &right);
        for (u32 i = 0; i < play.inputs; ++i)
        {
            play.gain[0][i] *= left;
            play.gain[1][i] *= right;
        }
    }
    for (u32 o = 0; o < play.outputs; ++o)
    {
        for (u32 i = 0; i < play.inputs; ++i)
        {
            play.gain[o][i] *= voice->gain;
        }
    }
    voice->route_play = play;
    *route = play;
    if (voice->stopping || voice->pausing || voice->paused)
    {
        for (u32 o = 0; o < route->outputs; ++o)
        {
            for (u32 i = 0; i < route->inputs; ++i)
            {
                route->gain[o][i] = 0.0f;
            }
        }
    }
}

// The route and step the voice has right now, in the middle of a ramp or not.
void Mixer::EffectiveNow(const Voice& voice, ChannelMatrix* route, u64* step) const
{
    if (voice.ramp_pos >= voice.ramp_len)
    {
        *route = voice.route;
        *step = voice.step;
        return;
    }
    const f32 t = static_cast<f32>(voice.ramp_pos) / static_cast<f32>(voice.ramp_len);
    *route = voice.route;
    for (u32 o = 0; o < route->outputs; ++o)
    {
        for (u32 i = 0; i < route->inputs; ++i)
        {
            route->gain[o][i] = voice.route_from.gain[o][i] +
                                (voice.route.gain[o][i] - voice.route_from.gain[o][i]) * t;
        }
    }
    const i64 delta = static_cast<i64>(voice.step) - static_cast<i64>(voice.step_from);
    *step = static_cast<u64>(static_cast<i64>(voice.step_from) +
                             delta * static_cast<i64>(voice.ramp_pos) / static_cast<i64>(voice.ramp_len));
}

// Recomputes the targets from the voice's parameters and ramps to them over `frames` (0: at once).
void Mixer::Retarget(Voice* voice, u32 frames)
{
    ChannelMatrix now;
    u64 now_step = 0;
    EffectiveNow(*voice, &now, &now_step);
    ChannelMatrix target;
    u64 target_step = 0;
    ComputeTargets(voice, &target, &target_step);
    voice->route = target;
    voice->step = target_step;
    if (frames == 0)
    {
        voice->route_from = target;
        voice->step_from = target_step;
        voice->ramp_len = 0;
        voice->ramp_pos = 0;
        return;
    }
    voice->route_from = now;
    voice->step_from = now_step;
    voice->ramp_len = frames;
    voice->ramp_pos = 0;
}

void Mixer::Apply(Command* command)
{
    switch (command->type)
    {
    case CommandType::CreateBus:
        if (command->a < kMaxBuses && command->b < kMaxBuses && buses_[command->b].active)
        {
            buses_[command->a] = Bus();
            buses_[command->a].active = true;
            buses_[command->a].parent = command->b;
            RebuildBusOrder();
        }
        return;
    case CommandType::SetBusGain:
        if (command->a < kMaxBuses && buses_[command->a].active)
        {
            buses_[command->a].gain = command->value;
        }
        return;
    case CommandType::SetBusMute:
        if (command->a < kMaxBuses && buses_[command->a].active)
        {
            buses_[command->a].mute = command->value != 0.0f;
        }
        return;
    case CommandType::SetBusSolo:
        if (command->a < kMaxBuses && buses_[command->a].active)
        {
            buses_[command->a].solo = command->value != 0.0f;
            RebuildBusOrder();
        }
        return;
    case CommandType::SetBusSend:
        if (command->a < kMaxBuses && command->b < kMaxSends && buses_[command->a].active)
        {
            Send& send = buses_[command->a].sends[command->b];
            send.target = command->frames < kMaxBuses && buses_[command->frames].active
                              ? static_cast<u32>(command->frames)
                              : 0xFFFFFFFFu;
            send.gain = command->value;
            send.pre_fader = command->value2 != 0.0f;
            RebuildBusOrder();
        }
        return;
    case CommandType::SetDuck:
        if (command->a < kMaxBuses && command->b < kMaxBuses && buses_[command->a].active && buses_[command->b].active)
        {
            Bus& bus = buses_[command->a];
            bus.duck_source = command->b;
            bus.duck_depth = command->value;
            bus.duck_threshold = command->value2;
            const u32 release_bits = static_cast<u32>(command->frames);
            MemCopy(&bus.duck_release, &release_bits, sizeof(f32));
            bus.duck_gain = 1.0f;
            RebuildBusOrder();
        }
        return;
    case CommandType::SetMeter:
        if (command->a < kMaxBuses && buses_[command->a].active)
        {
            buses_[command->a].meter = command->value != 0.0f;
        }
        return;
    case CommandType::SetInsert:
        // An effect made for another channel count (a device switch happened between the post and now) is refused:
        // its buffers would not match the bus buffer.
        if (command->a < kMaxBuses && command->b < kMaxInserts && buses_[command->a].active &&
            (command->effect == nullptr || command->effect->Channels() == channels_))
        {
            buses_[command->a].inserts[command->b] = command->effect;
        }
        return;
    case CommandType::SetEffectParams:
        if (command->effect != nullptr)
        {
            command->effect->SetParams(command->params);
        }
        return;
    case CommandType::SetLimiter:
        if (command->value != 0.0f && !limiter_on_)
        {
            limiter_.Reset();
        }
        limiter_on_ = command->value != 0.0f;
        return;
    default: break;
    }
    if (command->id.index >= voices_.Size())
    {
        return;
    }
    Voice& voice = voices_[command->id.index];
    if (command->type == CommandType::Play)
    {
        if (voice.active)
        {
            Finish(&voice, false, VoiceEventType::Stopped);
        }
        const VoiceDesc& desc = command->desc;
        voice = Voice();
        voice.clip = Move(command->desc.clip);
        voice.stream = Move(command->desc.stream);
        if (voice.stream)
        {
            voice.stream->SetLoop(command->desc.loop);
        }
        voice.position = desc.start_frame << 32;
        voice.delay = desc.delay_frames;
        voice.serial = 0;
        voice.generation = command->id.generation;
        voice.gain = desc.gain;
        voice.pan = desc.pan;
        voice.pitch = desc.pitch;
        voice.priority = desc.priority;
        voice.loop = desc.loop;
        voice.is_virtual = desc.is_virtual;
        voice.bus = desc.bus.index < kMaxBuses && buses_[desc.bus.index].active ? desc.bus.index : 0;
        voice.send_bus = desc.send_bus.IsValid() && desc.send_bus.index < kMaxBuses &&
                                 buses_[desc.send_bus.index].active
                             ? desc.send_bus.index
                             : 0xFFFFFFFFu;
        voice.send_gain = desc.send_gain;
        voice.directional = desc.directional;
        voice.dir[0] = desc.direction_x;
        voice.dir[1] = desc.direction_y;
        voice.dir[2] = desc.direction_z;
        voice.spread = desc.spread;
        voice.lfe_send = desc.lfe_send;
        voice.air_coefficient = desc.air_coefficient;
        voice.active = true;
        live_ += 1;
        Retarget(&voice, 0);
        if (desc.fade_in && config_.ramp_frames > 0)
        {
            for (u32 o = 0; o < voice.route_from.outputs; ++o)
            {
                for (u32 i = 0; i < voice.route_from.inputs; ++i)
                {
                    voice.route_from.gain[o][i] = 0.0f;
                }
            }
            voice.ramp_len = config_.ramp_frames;
            voice.ramp_pos = 0;
        }
        return;
    }
    if (!voice.active || voice.generation != command->id.generation)
    {
        return;  // a stale id: harmless
    }
    switch (command->type)
    {
    case CommandType::Stop:
        if (command->frames == 0 || voice.is_virtual || voice.paused)
        {
            Finish(&voice, true, VoiceEventType::Stopped);
        }
        else if (!voice.stopping)
        {
            voice.stopping = true;
            Retarget(&voice, static_cast<u32>(command->frames));
        }
        break;
    case CommandType::Pause:
        if (!voice.stopping && !voice.paused && !voice.pausing)
        {
            voice.pausing = true;
            Retarget(&voice, config_.ramp_frames);
            if (config_.ramp_frames == 0)
            {
                voice.pausing = false;
                voice.paused = true;
            }
        }
        break;
    case CommandType::Resume:
        if (voice.paused || voice.pausing)
        {
            voice.paused = false;
            voice.pausing = false;
            Retarget(&voice, config_.ramp_frames);
        }
        break;
    case CommandType::Seek:
    {
        if (voice.stream)
        {
            // The ring is dropped and refilled from the target; the voice is silent until the first frames arrive.
            voice.stream->Flush(command->frames);
            voice.position = command->frames << 32;
            break;
        }
        const u64 last = voice.clip->Frames() - 1;
        voice.position = (command->frames > last ? last : command->frames) << 32;
        break;
    }
    case CommandType::SetGain:
        voice.gain = command->value;
        Retarget(&voice, config_.ramp_frames);
        break;
    case CommandType::SetPan:
        voice.pan = command->value;
        Retarget(&voice, config_.ramp_frames);
        break;
    case CommandType::SetPitch:
        voice.pitch = command->value;
        Retarget(&voice, config_.ramp_frames);
        break;
    case CommandType::SetLoop:
        voice.loop = command->value != 0.0f;
        if (voice.stream)
        {
            voice.stream->SetLoop(voice.loop);
        }
        break;
    case CommandType::Fade:
        voice.gain = command->value;
        Retarget(&voice, static_cast<u32>(command->frames));
        break;
    case CommandType::SetDirection:
        voice.directional = true;
        voice.dir[0] = command->value;
        voice.dir[1] = command->value2;
        voice.dir[2] = command->value3;
        voice.spread = command->value4;
        {
            // A stereo clip balances by the lateral position.
            const f32 horizontal = Sqrt(voice.dir[0] * voice.dir[0] + voice.dir[2] * voice.dir[2]);
            voice.pan = horizontal > 1e-6f ? voice.dir[0] / horizontal : 0.0f;
        }
        Retarget(&voice, config_.ramp_frames);
        break;
    case CommandType::SetAirAbsorption: voice.air_coefficient = command->value; break;
    case CommandType::SetLfeSend:
        voice.lfe_send = command->value;
        Retarget(&voice, config_.ramp_frames);
        break;
    case CommandType::SetVirtual:
        if (command->value != 0.0f && !voice.is_virtual)
        {
            PostEvent(VoiceEventType::Virtualised, voice, false);
        }
        voice.is_virtual = command->value != 0.0f;
        break;
    case CommandType::SetVoiceBus:
        if (command->a < kMaxBuses && buses_[command->a].active)
        {
            voice.bus = command->a;
        }
        break;
    case CommandType::SetVoiceSend:
        if (command->a < kMaxBuses && buses_[command->a].active)
        {
            voice.send_bus = command->a;
            voice.send_gain = command->value;
        }
        break;
    default: break;
    }
}

void Mixer::ApplyCommands()
{
    while (Command* command = commands_.Front())
    {
        if (command->stamp > block_)
        {
            break;
        }
        if (command->stamp < block_)
        {
            audio_stats_.late_commands += 1;
        }
        Apply(command);
        audio_stats_.commands_applied += 1;
        commands_.Pop();
    }
}

MixerStatus Mixer::Position(VoiceId id, f64* frames) const
{
    if (id.index >= voices_.Size())
    {
        return MixerStatus::InvalidVoice;
    }
    const Voice& voice = voices_[id.index];
    if (!voice.active || voice.generation != id.generation)
    {
        return MixerStatus::InvalidVoice;
    }
    *frames = static_cast<f64>(voice.position) / 4294967296.0;
    return MixerStatus::Ok;
}

// Moves a virtual voice on by one block: the loop wrap and the end of the clip behave as for a mixed voice.
void Mixer::AdvanceVirtual(Voice* voice)
{
    if (voice->stopping)
    {
        Finish(voice, true, VoiceEventType::Stopped);
        return;
    }
    if (voice->stream)
    {
        // A virtual stream keeps consuming its ring, so it stays where it would be.
        f32 scratch[kBlockFrames * kMaxChannels];
        const u32 got = voice->stream->Pop(scratch, kBlockFrames);
        voice->position += static_cast<u64>(got) << 32;
        if (got < kBlockFrames && voice->stream->Ended())
        {
            audio_stats_.voices_finished += 1;
            Finish(voice, true, VoiceEventType::Finished);
        }
        return;
    }
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
                voice->looped = true;
            }
        }
        else if ((position >> 32) >= frames)
        {
            audio_stats_.voices_finished += 1;
            Finish(voice, true, VoiceEventType::Finished);
            return;
        }
    }
    voice->position = position;
}

void Mixer::MixVoice(Voice* voice, f32* out, f32* send_out)
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
    // A stream voice reads the block's frames from its ring (silence for what is missing), at a fixed pitch of 1.
    const bool streaming = voice->stream != nullptr;
    f32 stream_block[kBlockFrames * kMaxChannels];
    u32 stream_got = 0;
    // The first block of a delayed voice starts `skip` frames late: those frames add nothing and use nothing.
    const u32 skip = voice->delay;
    voice->delay = 0;
    if (streaming)
    {
        stream_got = voice->stream->Pop(stream_block, kBlockFrames - skip);
    }
    const u32 out_channels = channels_;
    u64 position = voice->position;
    ChannelMatrix ramped;
    for (u32 n = skip; n < kBlockFrames; ++n)
    {
        const i64 index = static_cast<i64>(position >> 32);
        if (streaming && n - skip >= stream_got && voice->stream->Ended())
        {
            audio_stats_.voices_finished += 1;
            Finish(voice, true, VoiceEventType::Finished);
            return;
        }
        if (!streaming && !voice->loop && index >= source.frames)
        {
            audio_stats_.voices_finished += 1;
            Finish(voice, true, VoiceEventType::Finished);
            return;
        }
        // A ramp moves by one step per frame and ends exactly on the target (the last frame uses the target itself).
        const ChannelMatrix* matrix = &voice->route;
        u64 step = voice->step;
        const bool ramping = voice->ramp_pos < voice->ramp_len;
        bool ramp_done = false;
        if (ramping)
        {
            voice->ramp_pos += 1;
            if (voice->ramp_pos == voice->ramp_len)
            {
                ramp_done = true;
            }
            else
            {
                const f32 t = static_cast<f32>(voice->ramp_pos) / static_cast<f32>(voice->ramp_len);
                ramped = voice->route;
                for (u32 o = 0; o < ramped.outputs; ++o)
                {
                    for (u32 i = 0; i < ramped.inputs; ++i)
                    {
                        ramped.gain[o][i] = voice->route_from.gain[o][i] +
                                            (voice->route.gain[o][i] - voice->route_from.gain[o][i]) * t;
                    }
                }
                matrix = &ramped;
                const i64 delta = static_cast<i64>(voice->step) - static_cast<i64>(voice->step_from);
                step = static_cast<u64>(static_cast<i64>(voice->step_from) +
                                        delta * static_cast<i64>(voice->ramp_pos) / static_cast<i64>(voice->ramp_len));
            }
        }
        source.periodic = index >= source.loop_start;
        const f32 frac = static_cast<f32>(static_cast<u32>(position & 0xFFFFFFFFull)) * (1.0f / 4294967296.0f);
        f32 value[kMaxChannels];
        for (u32 c = 0; c < in_channels; ++c)
        {
            if (streaming)
            {
                value[c] = n - skip < stream_got ? stream_block[static_cast<usize>(n - skip) * in_channels + c] : 0.0f;
                continue;
            }
            switch (config_.quality)
            {
            case ResamplerQuality::Linear: value[c] = InterpLinear(source, c, index, frac); break;
            case ResamplerQuality::Sinc: value[c] = InterpSinc(source, sinc_.Data(), c, index, frac); break;
            case ResamplerQuality::Hermite:
            default: value[c] = InterpHermite(source, c, index, frac); break;
            }
        }
        if (voice->air_coefficient < 1.0f)
        {
            for (u32 c = 0; c < in_channels; ++c)
            {
                f32 state = voice->air_state[c];
                state += voice->air_coefficient * (value[c] - state);
                state = (state < 1e-20f && state > -1e-20f) ? 0.0f : state;
                voice->air_state[c] = state;
                value[c] = state;
            }
        }
        f32* frame = out + static_cast<usize>(n) * out_channels;
        f32* send_frame = send_out != nullptr ? send_out + static_cast<usize>(n) * out_channels : nullptr;
        for (u32 o = 0; o < out_channels; ++o)
        {
            f32 sum = 0.0f;
            for (u32 c = 0; c < in_channels; ++c)
            {
                sum += value[c] * matrix->gain[o][c];
            }
            frame[o] += sum;
            if (send_frame != nullptr)
            {
                send_frame[o] += sum * voice->send_gain;
            }
        }
        position += streaming ? (n - skip < stream_got ? (u64{1} << 32) : 0) : step;
        if (voice->loop && !streaming)
        {
            while (position >= loop_end_fixed)
            {
                position -= loop_length;
                voice->looped = true;
            }
        }
        if (ramp_done)
        {
            voice->ramp_len = 0;
            voice->ramp_pos = 0;
            voice->route_from = voice->route;
            voice->step_from = voice->step;
            if (voice->stopping)
            {
                voice->position = position;
                Finish(voice, true, VoiceEventType::Stopped);
                return;
            }
            if (voice->pausing)
            {
                voice->pausing = false;
                voice->paused = true;
                break;
            }
        }
    }
    voice->position = position;
}

void Mixer::RenderBlock(Span<f32> out)
{
    const usize samples = static_cast<usize>(kBlockFrames) * channels_;
    ENGINE_ASSERT(out.Size() >= samples, "Mixer::RenderBlock: the output is smaller than a block");
    ApplyCommands();
    for (u32 b = 0; b < config_.max_buses; ++b)
    {
        if (buses_[b].active)
        {
            MemSet(BusBuffer(b), 0, samples * sizeof(f32));
        }
    }
    u32 mixed = 0;
    u32 virtual_count = 0;
    for (u32 i = 0; i < voices_.Size(); ++i)
    {
        Voice& voice = voices_[i];
        if (!voice.active || voice.paused)
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
            f32* send_out = voice.send_bus != 0xFFFFFFFFu && voice.send_gain > 0.0f ? BusBuffer(voice.send_bus)
                                                                                    : nullptr;
            MixVoice(&voice, BusBuffer(voice.bus), send_out);
        }
        if (voice.active && voice.looped)
        {
            voice.looped = false;
            PostEvent(VoiceEventType::Looped, voice, false);
        }
    }
    ProcessBuses(out.Data());
    block_ += 1;
    audio_stats_.blocks += 1;
    audio_stats_.voices_mixed = mixed;
    audio_stats_.voices_virtual = virtual_count;
    next_block_.Store(block_, MemoryOrder::Release);
}

bool Mixer::TryRenderBlock(Span<f32> out)
{
    if (config_.offline && block_ >= watermark_.Load(MemoryOrder::Acquire))
    {
        return false;
    }
    RenderBlock(out);
    return true;
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
        // The device boundary: the only place the mix is clipped to [-1, 1].
        const f32* source = carry_.Data() + static_cast<usize>(carry_next_) * channels_;
        const usize total = static_cast<usize>(count) * channels_;
        for (usize i = 0; i < total; ++i)
        {
            const f32 v = source[i];
            out[i] = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : (v == v ? v : 0.0f));  // a NaN becomes silence
        }
        out += total;
        carry_next_ += count;
        frames -= count;
    }
}

}  // namespace engine_placeholder::audio
