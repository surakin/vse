// The mixer's snapshot (Phase 26 Day 437): `Mixer::SaveState` and `Mixer::LoadState`.
#include "audio/mixer.h"

#include "audio/state_io.h"
#include "bytes/byte_reader.h"

namespace engine_placeholder::audio
{

namespace
{

constexpr u32 kMagic = 0x504E5341;  // "ASNP"
constexpr u32 kVersion = 1;
constexpr u32 kNoIndex = 0xFFFFFFFFu;
constexpr u32 kMaxName = 256;

void WriteName(StateOut* out, StringView name)
{
    out->U32(static_cast<u32>(name.Length()));
    for (usize i = 0; i < name.Length(); ++i)
    {
        out->U32(static_cast<u8>(name[i]));
    }
}

// Reads a name into `buffer` (at least `kMaxName + 1` bytes); returns its length, or fails the reader.
u32 ReadName(StateIn* in, char* buffer)
{
    const u32 n = in->U32();
    if (!in->Ok() || n > kMaxName)
    {
        in->Fail();
        return 0;
    }
    for (u32 i = 0; i < n; ++i)
    {
        const u32 c = in->U32();
        if (!in->Ok() || c > 255)
        {
            in->Fail();
            return 0;
        }
        buffer[i] = static_cast<char>(c);
    }
    buffer[n] = '\0';
    return n;
}

void WriteMatrix(StateOut* out, const ChannelMatrix& m)
{
    out->U32(m.inputs);
    out->U32(m.outputs);
    for (u32 o = 0; o < kMaxChannels; ++o)
    {
        out->F32s(m.gain[o], kMaxChannels);
    }
}

void ReadMatrix(StateIn* in, ChannelMatrix* m)
{
    m->inputs = in->U32();
    m->outputs = in->U32();
    for (u32 o = 0; o < kMaxChannels; ++o)
    {
        in->F32s(m->gain[o], kMaxChannels);
    }
    if (m->inputs > kMaxChannels || m->outputs > kMaxChannels)
    {
        in->Fail();
    }
}

}  // namespace

SnapshotStatus Mixer::SaveState(bytes::ByteWriter* out) const
{
    if (commands_.Size() != 0)
    {
        return SnapshotStatus::Busy;
    }
    StateOut s(out);
    s.U32(kMagic);
    s.U32(kVersion);
    s.U32(config_.sample_rate);
    s.U32(channels_);
    s.U32(config_.max_voices);
    s.U32(config_.max_buses);
    s.U64(block_);
    s.U64(next_serial_);
    s.U64(control_stolen_);
    s.U64(control_refused_);
    s.U64(control_dropped_);
    s.U64(control_events_dropped_);
    s.U32(live_);
    s.U32(carry_next_);
    s.Floats(carry_);
    // The effects, in creation order; the buses name them by index.
    s.U32(effect_count_);
    for (u32 i = 0; i < effect_count_; ++i)
    {
        s.U32(effects_[i]->Channels());
        effects_[i]->SaveState(&s);
    }
    s.Bool(limiter_on_);
    s.F32(limiter_min_gain_);
    limiter_.SaveState(&s);
    for (u32 b = 0; b < kMaxBuses; ++b)
    {
        const Bus& bus = buses_[b];
        s.Bool(bus.active);
        s.Bool(bus.mute);
        s.Bool(bus.solo);
        s.Bool(bus.audible);
        s.Bool(bus.meter);
        s.U32(bus.parent);
        s.F32(bus.gain);
        s.F32(bus.applied);
        for (u32 k = 0; k < kMaxSends; ++k)
        {
            s.U32(bus.sends[k].target);
            s.F32(bus.sends[k].gain);
            s.Bool(bus.sends[k].pre_fader);
        }
        s.U32(bus.duck_source);
        s.F32(bus.duck_depth);
        s.F32(bus.duck_threshold);
        s.F32(bus.duck_release);
        s.F32(bus.duck_gain);
        s.F32(bus.last_peak);
        for (u32 k = 0; k < kMaxInserts; ++k)
        {
            u32 index = kNoIndex;
            for (u32 e = 0; e < effect_count_; ++e)
            {
                if (effects_[e].Get() == bus.inserts[k])
                {
                    index = e;
                }
            }
            s.U32(index);
        }
    }
    s.U32(static_cast<u32>(voices_.Size()));
    for (usize v = 0; v < voices_.Size(); ++v)
    {
        const Voice& voice = voices_[v];
        s.Bool(voice.active);
        s.U32(voice.generation);
        s.U64(voice.serial);
        if (!voice.active)
        {
            continue;
        }
        s.Bool(voice.stream != nullptr);
        WriteName(&s, voice.clip->Name());
        s.U32(static_cast<u32>(voice.clip->Frames()));
        s.U64(voice.position);
        s.U64(voice.step);
        s.U64(voice.step_from);
        s.U32(voice.delay);
        s.U32(voice.ramp_len);
        s.U32(voice.ramp_pos);
        s.U32(voice.bus);
        s.U32(voice.send_bus);
        s.F32(voice.send_gain);
        s.F32(voice.gain);
        s.F32(voice.pan);
        s.Bool(voice.directional);
        s.F32s(voice.dir, 3);
        s.F32(voice.spread);
        s.F32(voice.lfe_send);
        s.F32(voice.air_coefficient);
        s.F32s(voice.air_state, kMaxChannels);
        s.F32(voice.pitch);
        s.U32(static_cast<u32>(voice.priority));
        s.Bool(voice.loop);
        s.Bool(voice.is_virtual);
        s.Bool(voice.stopping);
        s.Bool(voice.pausing);
        s.Bool(voice.paused);
        s.Bool(voice.looped);
        WriteMatrix(&s, voice.route_play);
        WriteMatrix(&s, voice.route);
        WriteMatrix(&s, voice.route_from);
    }
    s.U32(static_cast<u32>(control_.Size()));
    for (usize i = 0; i < control_.Size(); ++i)
    {
        const ControlSlot& c = control_[i];
        s.U32(c.generation);
        s.Bool(c.active);
        s.Bool(c.stopping);
        s.U32(static_cast<u32>(c.priority));
        s.F32(c.gain);
        s.U64(c.serial);
    }
    return SnapshotStatus::Ok;
}

SnapshotStatus Mixer::LoadState(ByteSpan bytes, SnapshotResolver* resolver)
{
    if (commands_.Size() != 0)
    {
        return SnapshotStatus::Busy;
    }
    bytes::ByteReader reader(bytes);
    StateIn s(&reader);
    if (s.U32() != kMagic || !s.Ok())
    {
        return SnapshotStatus::Corrupt;
    }
    if (s.U32() != kVersion || !s.Ok())
    {
        return s.Ok() ? SnapshotStatus::Version : SnapshotStatus::Corrupt;
    }
    const u32 rate = s.U32();
    const u32 channels = s.U32();
    const u32 max_voices = s.U32();
    const u32 max_buses = s.U32();
    if (!s.Ok())
    {
        return SnapshotStatus::Corrupt;
    }
    if (rate != config_.sample_rate || channels != channels_ || max_voices != config_.max_voices ||
        max_buses != config_.max_buses)
    {
        return SnapshotStatus::Mismatch;
    }
    // Stage everything outside the mixer first; the effects and the limiter are the one thing loaded in place, and a
    // backup of them is put back when a later check fails.
    const u64 block = s.U64();
    const u64 next_serial = s.U64();
    const u64 stolen = s.U64();
    const u64 refused = s.U64();
    const u64 dropped = s.U64();
    const u64 events_dropped = s.U64();
    const u32 live = s.U32();
    const u32 carry_next = s.U32();
    Vector<f32> carry;
    if (!carry.TryResizeUninitialized(carry_.Size()))
    {
        return SnapshotStatus::Mismatch;
    }
    s.Floats(&carry);
    if (!s.Ok() || carry_next > kBlockFrames)
    {
        return SnapshotStatus::Corrupt;
    }
    bytes::ByteWriter backup;
    {
        StateOut b(&backup);
        for (u32 i = 0; i < effect_count_; ++i)
        {
            effects_[i]->SaveState(&b);
        }
        limiter_.SaveState(&b);
    }
    auto restore_backup = [&]()
    {
        bytes::ByteReader br(backup.Bytes());
        StateIn bi(&br);
        for (u32 i = 0; i < effect_count_; ++i)
        {
            effects_[i]->LoadState(&bi);
        }
        limiter_.LoadState(&bi);
    };
    auto fail = [&](SnapshotStatus status)
    {
        restore_backup();
        return status;
    };
    if (s.U32() != effect_count_ || !s.Ok())
    {
        return s.Ok() ? SnapshotStatus::Mismatch : SnapshotStatus::Corrupt;
    }
    for (u32 i = 0; i < effect_count_; ++i)
    {
        if (s.U32() != effects_[i]->Channels() || !s.Ok())
        {
            return fail(s.Ok() ? SnapshotStatus::Mismatch : SnapshotStatus::Corrupt);
        }
        effects_[i]->LoadState(&s);
        if (!s.Ok())
        {
            return fail(SnapshotStatus::Corrupt);
        }
    }
    const bool limiter_on = s.Bool();
    const f32 limiter_min = s.F32();
    limiter_.LoadState(&s);
    if (!s.Ok())
    {
        return fail(SnapshotStatus::Corrupt);
    }
    Bus buses[kMaxBuses];
    for (u32 b = 0; b < kMaxBuses; ++b)
    {
        Bus& bus = buses[b];
        bus.active = s.Bool();
        bus.mute = s.Bool();
        bus.solo = s.Bool();
        bus.audible = s.Bool();
        bus.meter = s.Bool();
        bus.parent = s.U32();
        bus.gain = s.F32();
        bus.applied = s.F32();
        for (u32 k = 0; k < kMaxSends; ++k)
        {
            bus.sends[k].target = s.U32();
            bus.sends[k].gain = s.F32();
            bus.sends[k].pre_fader = s.Bool();
        }
        bus.duck_source = s.U32();
        bus.duck_depth = s.F32();
        bus.duck_threshold = s.F32();
        bus.duck_release = s.F32();
        bus.duck_gain = s.F32();
        bus.last_peak = s.F32();
        for (u32 k = 0; k < kMaxInserts; ++k)
        {
            const u32 index = s.U32();
            if (!s.Ok() || (index != kNoIndex && index >= effect_count_))
            {
                return fail(SnapshotStatus::Corrupt);
            }
            bus.inserts[k] = index == kNoIndex ? nullptr : effects_[index].Get();
            if (s.Ok() && bus.inserts[k] != buses_[b].inserts[k])
            {
                return fail(SnapshotStatus::Mismatch);
            }
        }
        if (!s.Ok())
        {
            return fail(SnapshotStatus::Corrupt);
        }
        if (bus.active != buses_[b].active || bus.parent != buses_[b].parent)
        {
            return fail(SnapshotStatus::Mismatch);
        }
        if (bus.parent >= kMaxBuses || (bus.duck_source != kNoIndex && bus.duck_source >= kMaxBuses))
        {
            return fail(SnapshotStatus::Corrupt);
        }
        for (u32 k = 0; k < kMaxSends; ++k)
        {
            if (bus.sends[k].target != kNoIndex && bus.sends[k].target >= kMaxBuses)
            {
                return fail(SnapshotStatus::Corrupt);
            }
        }
    }
    const u32 voice_count = s.U32();
    if (!s.Ok() || voice_count != voices_.Size())
    {
        return fail(s.Ok() ? SnapshotStatus::Mismatch : SnapshotStatus::Corrupt);
    }
    Vector<Voice> voices;
    if (!voices.TryResize(voice_count))
    {
        return fail(SnapshotStatus::Mismatch);
    }
    u32 active_count = 0;
    for (u32 v = 0; v < voice_count; ++v)
    {
        Voice& voice = voices[v];
        voice.active = s.Bool();
        voice.generation = s.U32();
        voice.serial = s.U64();
        if (!s.Ok())
        {
            return fail(SnapshotStatus::Corrupt);
        }
        if (!voice.active)
        {
            continue;
        }
        active_count += 1;
        const bool streaming = s.Bool();
        char name_buffer[kMaxName + 1];
        const u32 name_length = ReadName(&s, name_buffer);
        if (!s.Ok())
        {
            return fail(SnapshotStatus::Corrupt);
        }
        const StringView name(name_buffer, name_length);
        const u32 frames = s.U32();
        voice.position = s.U64();
        voice.step = s.U64();
        voice.step_from = s.U64();
        voice.delay = s.U32();
        voice.ramp_len = s.U32();
        voice.ramp_pos = s.U32();
        voice.bus = s.U32();
        voice.send_bus = s.U32();
        voice.send_gain = s.F32();
        voice.gain = s.F32();
        voice.pan = s.F32();
        voice.directional = s.Bool();
        s.F32s(voice.dir, 3);
        voice.spread = s.F32();
        voice.lfe_send = s.F32();
        voice.air_coefficient = s.F32();
        s.F32s(voice.air_state, kMaxChannels);
        voice.pitch = s.F32();
        voice.priority = static_cast<i32>(s.U32());
        voice.loop = s.Bool();
        voice.is_virtual = s.Bool();
        voice.stopping = s.Bool();
        voice.pausing = s.Bool();
        voice.paused = s.Bool();
        voice.looped = s.Bool();
        ReadMatrix(&s, &voice.route_play);
        ReadMatrix(&s, &voice.route);
        ReadMatrix(&s, &voice.route_from);
        if (!s.Ok() || voice.bus >= kMaxBuses || !buses[voice.bus].active ||
            (voice.send_bus != kNoIndex && voice.send_bus >= kMaxBuses) || voice.ramp_pos > voice.ramp_len)
        {
            return fail(SnapshotStatus::Corrupt);
        }
        if (streaming)
        {
            // A stream voice plays the clip its stream reports (the same name and length as the saved one).
            voice.stream = resolver->FindStream(name);
            if (!voice.stream || !voice.stream->Clip() || voice.stream->Clip()->Frames() != frames)
            {
                return fail(SnapshotStatus::MissingClip);
            }
            voice.clip = voice.stream->Clip();
        }
        else
        {
            voice.clip = resolver->FindClip(name, frames);
            if (!voice.clip)
            {
                return fail(SnapshotStatus::MissingClip);
            }
        }
        if (!streaming && !voice.loop && (voice.position >> 32) >= frames)
        {
            return fail(SnapshotStatus::Corrupt);
        }
    }
    const u32 control_count = s.U32();
    if (!s.Ok() || control_count != control_.Size())
    {
        return fail(s.Ok() ? SnapshotStatus::Mismatch : SnapshotStatus::Corrupt);
    }
    Vector<ControlSlot> control;
    if (!control.TryResize(control_count))
    {
        return fail(SnapshotStatus::Mismatch);
    }
    for (u32 i = 0; i < control_count; ++i)
    {
        ControlSlot& c = control[i];
        c.generation = s.U32();
        c.active = s.Bool();
        c.stopping = s.Bool();
        c.priority = static_cast<i32>(s.U32());
        c.gain = s.F32();
        c.serial = s.U64();
    }
    if (!s.Ok() || !reader.AtEnd() || live != active_count)
    {
        return fail(SnapshotStatus::Corrupt);
    }
    // Commit: nothing below can fail.
    for (u32 v = 0; v < voice_count; ++v)
    {
        if (voices[v].stream)
        {
            // The ring starts again from where the voice was; the first blocks wait for a refill like after a seek.
            const u64 frames = voices[v].clip->Frames();
            const u64 at = voices[v].position >> 32;
            voices[v].stream->Flush(voices[v].loop && frames > 0 ? at % frames : at);
        }
    }
    voices_ = Move(voices);
    control_ = Move(control);
    for (u32 b = 0; b < kMaxBuses; ++b)
    {
        buses_[b] = buses[b];
    }
    RebuildBusOrder();
    carry_ = Move(carry);
    carry_next_ = carry_next;
    limiter_on_ = limiter_on;
    limiter_min_gain_ = limiter_min;
    block_ = block;
    live_ = live;
    next_serial_ = next_serial;
    control_stolen_ = stolen;
    control_refused_ = refused;
    control_dropped_ = dropped;
    control_events_dropped_ = events_dropped;
    next_block_.Store(block, MemoryOrder::Release);
    watermark_.Store(block, MemoryOrder::Release);
    pending_.Clear();
    return SnapshotStatus::Ok;
}

}  // namespace engine_placeholder::audio
