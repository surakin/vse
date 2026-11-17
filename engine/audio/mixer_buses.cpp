// The bus tree of the mixer (Phase 26 Day 422): the control calls that make and change buses, and the audio-side
// processing that sums them child before parent, in a fixed order.
#include "audio/mixer.h"

#include "cstr/mem_ops.h"
#include "lang/scalar.h"
#include "lang/utility.h"

namespace engine_placeholder::audio
{

namespace
{

bool BusFinite(f32 v)
{
    return v == v && v - v == 0.0f;
}

}  // namespace

// ---- The control side ----

bool Mixer::ValidBus(BusId bus) const
{
    return bus.index < kMaxBuses && control_buses_[bus.index].active;
}

// Whether `to` can be reached from `from` along the edges of the graph: a bus to its parent, a sending bus to the bus
// it sends to, a ducking source to the bus it ducks.
bool Mixer::BusReaches(u32 from, u32 to) const
{
    if (from == to)
    {
        return true;
    }
    bool seen[kMaxBuses] = {};
    u32 stack[kMaxBuses];
    u32 top = 0;
    stack[top++] = from;
    seen[from] = true;
    while (top > 0)
    {
        const u32 bus = stack[--top];
        u32 next[kMaxSends + 2];
        u32 count = 0;
        if (bus != 0)
        {
            next[count++] = control_buses_[bus].parent;
        }
        for (u32 i = 0; i < kMaxSends; ++i)
        {
            if (control_buses_[bus].send_target[i] != 0xFFFFFFFFu)
            {
                next[count++] = control_buses_[bus].send_target[i];
            }
        }
        for (u32 t = 0; t < kMaxBuses; ++t)
        {
            if (control_buses_[t].active && control_buses_[t].duck_source == bus)
            {
                if (t == to)
                {
                    return true;
                }
                if (!seen[t])
                {
                    seen[t] = true;
                    stack[top++] = t;
                }
            }
        }
        for (u32 i = 0; i < count; ++i)
        {
            if (next[i] == to)
            {
                return true;
            }
            if (!seen[next[i]])
            {
                seen[next[i]] = true;
                stack[top++] = next[i];
            }
        }
    }
    return false;
}

MixerStatus Mixer::PostBus(Command&& command)
{
    DrainEvents();
    return Post(Move(command));
}

BusId Mixer::FindBus(StringView name) const
{
    BusId none;
    none.index = 0xFFFFFFFFu;
    for (u32 b = 0; b < config_.max_buses && b < kMaxBuses; ++b)
    {
        if (control_buses_[b].active && StringView(control_buses_[b].name) == name)
        {
            BusId id;
            id.index = b;
            return id;
        }
    }
    return none;
}

MixerStatus Mixer::CreateBus(StringView name, BusId parent, BusId* out, u64 block)
{
    if (!ValidBus(parent))
    {
        return MixerStatus::InvalidBus;
    }
    if (name.Empty() || name.Length() > kMaxBusNameBytes || FindBus(name).IsValid())
    {
        return MixerStatus::NoBusSlot;
    }
    u32 index = 0xFFFFFFFFu;
    for (u32 b = 1; b < config_.max_buses; ++b)
    {
        if (!control_buses_[b].active)
        {
            index = b;
            break;
        }
    }
    if (index == 0xFFFFFFFFu)
    {
        return MixerStatus::NoBusSlot;
    }
    Command command;
    command.stamp = StampFor(block);
    command.type = CommandType::CreateBus;
    command.a = index;
    command.b = parent.index;
    const MixerStatus status = PostBus(Move(command));
    if (status != MixerStatus::Ok)
    {
        return status;
    }
    ControlBus& bus = control_buses_[index];
    bus = ControlBus();
    bus.active = true;
    bus.parent = parent.index;
    for (u32 i = 0; i < name.Length(); ++i)
    {
        bus.name[i] = name[i];
    }
    out->index = index;
    return MixerStatus::Ok;
}

MixerStatus Mixer::SetBusGain(BusId bus, f32 gain, u64 block)
{
    if (!ValidBus(bus))
    {
        return MixerStatus::InvalidBus;
    }
    if (!BusFinite(gain) || gain < 0.0f || gain > 16.0f)
    {
        return MixerStatus::InvalidParam;
    }
    Command command;
    command.stamp = StampFor(block);
    command.type = CommandType::SetBusGain;
    command.a = bus.index;
    command.value = gain;
    return PostBus(Move(command));
}

MixerStatus Mixer::SetBusMute(BusId bus, bool mute, u64 block)
{
    if (!ValidBus(bus))
    {
        return MixerStatus::InvalidBus;
    }
    Command command;
    command.stamp = StampFor(block);
    command.type = CommandType::SetBusMute;
    command.a = bus.index;
    command.value = mute ? 1.0f : 0.0f;
    return PostBus(Move(command));
}

MixerStatus Mixer::SetBusSolo(BusId bus, bool solo, u64 block)
{
    if (!ValidBus(bus))
    {
        return MixerStatus::InvalidBus;
    }
    Command command;
    command.stamp = StampFor(block);
    command.type = CommandType::SetBusSolo;
    command.a = bus.index;
    command.value = solo ? 1.0f : 0.0f;
    return PostBus(Move(command));
}

MixerStatus Mixer::SetBusSend(BusId bus, u32 slot, BusId aux, f32 gain, bool pre_fader, u64 block)
{
    if (!ValidBus(bus) || !ValidBus(aux) || bus.index == 0)
    {
        return MixerStatus::InvalidBus;
    }
    if (slot >= kMaxSends || !BusFinite(gain) || gain < 0.0f || gain > 16.0f)
    {
        return MixerStatus::InvalidParam;
    }
    // A cycle: the aux bus already reaches the sender (its parent chain, other sends, ducking).
    const u32 old_target = control_buses_[bus.index].send_target[slot];
    control_buses_[bus.index].send_target[slot] = 0xFFFFFFFFu;
    const bool cycle = gain > 0.0f && BusReaches(aux.index, bus.index);
    control_buses_[bus.index].send_target[slot] = old_target;
    if (cycle)
    {
        return MixerStatus::Cycle;
    }
    Command command;
    command.stamp = StampFor(block);
    command.type = CommandType::SetBusSend;
    command.a = bus.index;
    command.b = slot;
    command.frames = aux.index;
    command.value = gain;
    command.value2 = pre_fader ? 1.0f : 0.0f;
    const MixerStatus status = PostBus(Move(command));
    if (status == MixerStatus::Ok)
    {
        control_buses_[bus.index].send_target[slot] = gain > 0.0f ? aux.index : 0xFFFFFFFFu;
    }
    return status;
}

MixerStatus Mixer::SetDuck(BusId target, BusId source, f32 depth, f32 threshold, f32 release_frames, u64 block)
{
    if (!ValidBus(target) || !ValidBus(source) || target.index == 0)
    {
        return MixerStatus::InvalidBus;
    }
    if (!BusFinite(depth) || depth < 0.0f || depth > 1.0f || !BusFinite(threshold) || threshold <= 0.0f ||
        !BusFinite(release_frames) || release_frames < 0.0f)
    {
        return MixerStatus::InvalidParam;
    }
    if (target.index == source.index)
    {
        return MixerStatus::Cycle;
    }
    const u32 old_source = control_buses_[target.index].duck_source;
    control_buses_[target.index].duck_source = 0xFFFFFFFFu;
    const bool cycle = BusReaches(target.index, source.index);
    control_buses_[target.index].duck_source = old_source;
    if (cycle)
    {
        return MixerStatus::Cycle;
    }
    // The release as the fraction of the gap closed per block: 1 - exp(-block / release), computed here with libm.
    const f32 coefficient = release_frames > 0.0f ? static_cast<f32>(1.0 - Exp(-static_cast<f64>(kBlockFrames) /
                                                                               static_cast<f64>(release_frames)))
                                                  : 1.0f;
    Command command;
    command.stamp = StampFor(block);
    command.type = CommandType::SetDuck;
    command.a = target.index;
    command.b = source.index;
    command.value = depth;
    command.value2 = threshold;
    u32 bits = 0;
    MemCopy(&bits, &coefficient, sizeof(bits));
    command.frames = bits;
    const MixerStatus status = PostBus(Move(command));
    if (status == MixerStatus::Ok)
    {
        control_buses_[target.index].duck_source = source.index;
    }
    return status;
}

MixerStatus Mixer::SetMeter(BusId bus, bool on, u64 block)
{
    if (!ValidBus(bus))
    {
        return MixerStatus::InvalidBus;
    }
    Command command;
    command.stamp = StampFor(block);
    command.type = CommandType::SetMeter;
    command.a = bus.index;
    command.value = on ? 1.0f : 0.0f;
    return PostBus(Move(command));
}

MixerStatus Mixer::SetVoiceBus(VoiceId id, BusId bus, u64 block)
{
    if (!ValidBus(bus))
    {
        return MixerStatus::InvalidBus;
    }
    Command command;
    command.stamp = StampFor(block);
    command.type = CommandType::SetVoiceBus;
    command.id = id;
    command.a = bus.index;
    return PostFor(id, Move(command));
}

MixerStatus Mixer::SetVoiceSend(VoiceId id, BusId bus, f32 gain, u64 block)
{
    if (!ValidBus(bus))
    {
        return MixerStatus::InvalidBus;
    }
    if (!BusFinite(gain) || gain < 0.0f || gain > kMaxPitch)
    {
        return MixerStatus::InvalidParam;
    }
    Command command;
    command.stamp = StampFor(block);
    command.type = CommandType::SetVoiceSend;
    command.id = id;
    command.a = bus.index;
    command.value = gain;
    return PostFor(id, Move(command));
}

MixerStatus Mixer::SetLimiter(bool on, u64 block)
{
    Command command;
    command.stamp = StampFor(block);
    command.type = CommandType::SetLimiter;
    command.value = on ? 1.0f : 0.0f;
    return PostBus(Move(command));
}

// ---- The audio side ----

// Orders the buses so that every bus comes after the buses that feed it (its children, the buses that send to it and
// the bus that ducks it), ties by index, and works out which buses a solo leaves audible.
void Mixer::RebuildBusOrder()
{
    u32 pending[kMaxBuses];
    bool done[kMaxBuses] = {};
    u32 active = 0;
    for (u32 b = 0; b < kMaxBuses; ++b)
    {
        pending[b] = 0;
        if (buses_[b].active)
        {
            active += 1;
        }
    }
    // The number of feeders each bus waits for.
    for (u32 b = 0; b < kMaxBuses; ++b)
    {
        if (!buses_[b].active)
        {
            continue;
        }
        if (b != 0)
        {
            pending[buses_[b].parent] += 1;
        }
        for (u32 i = 0; i < kMaxSends; ++i)
        {
            if (buses_[b].sends[i].target != 0xFFFFFFFFu && buses_[b].sends[i].gain > 0.0f)
            {
                pending[buses_[b].sends[i].target] += 1;
            }
        }
        if (buses_[b].duck_source != 0xFFFFFFFFu)
        {
            pending[b] += 1;
        }
    }
    u32 count = 0;
    while (count < active)
    {
        u32 chosen = 0xFFFFFFFFu;
        for (u32 b = 0; b < kMaxBuses; ++b)
        {
            if (buses_[b].active && !done[b] && pending[b] == 0)
            {
                chosen = b;
                break;
            }
        }
        if (chosen == 0xFFFFFFFFu)
        {
            break;  // a cycle the control side should have refused: leave the rest out of the order
        }
        done[chosen] = true;
        bus_order_[count++] = chosen;
        if (chosen != 0)
        {
            pending[buses_[chosen].parent] -= 1;
        }
        for (u32 i = 0; i < kMaxSends; ++i)
        {
            if (buses_[chosen].sends[i].target != 0xFFFFFFFFu && buses_[chosen].sends[i].gain > 0.0f)
            {
                pending[buses_[chosen].sends[i].target] -= 1;
            }
        }
        for (u32 t = 0; t < kMaxBuses; ++t)
        {
            if (buses_[t].active && buses_[t].duck_source == chosen)
            {
                pending[t] -= 1;
            }
        }
    }
    bus_order_count_ = count;
    // Solo: while any bus is soloed, only a soloed bus, its ancestors and its descendants are audible.
    bool any_solo = false;
    for (u32 b = 0; b < kMaxBuses; ++b)
    {
        any_solo = any_solo || (buses_[b].active && buses_[b].solo);
    }
    for (u32 b = 0; b < kMaxBuses; ++b)
    {
        buses_[b].audible = !any_solo;
    }
    if (any_solo)
    {
        for (u32 s = 0; s < kMaxBuses; ++s)
        {
            if (!buses_[s].active || !buses_[s].solo)
            {
                continue;
            }
            for (u32 up = s;; up = buses_[up].parent)  // the soloed bus and its ancestors
            {
                buses_[up].audible = true;
                if (up == 0)
                {
                    break;
                }
            }
            for (u32 b = 1; b < kMaxBuses; ++b)  // its descendants
            {
                if (!buses_[b].active)
                {
                    continue;
                }
                for (u32 up = b;; up = buses_[up].parent)
                {
                    if (up == s)
                    {
                        buses_[b].audible = true;
                        break;
                    }
                    if (up == 0)
                    {
                        break;
                    }
                }
            }
        }
    }
    buses_[0].audible = true;
}

// Sums the buses in order: each bus applies its gain (ramped over the block's first frames), feeds its sends and its
// parent; the master's result is the block, which the limiter then holds under its threshold when it is on.
void Mixer::ProcessBuses(f32* out)
{
    const u32 channels = channels_;
    const usize samples = static_cast<usize>(kBlockFrames) * channels;
    for (u32 n = 0; n < bus_order_count_; ++n)
    {
        const u32 index = bus_order_[n];
        Bus& bus = buses_[index];
        f32* buffer = BusBuffer(index);
        // Pre-fader sends.
        for (u32 i = 0; i < kMaxSends; ++i)
        {
            const Send& send = bus.sends[i];
            if (send.target != 0xFFFFFFFFu && send.gain > 0.0f && send.pre_fader)
            {
                f32* target = BusBuffer(send.target);
                for (usize k = 0; k < samples; ++k)
                {
                    target[k] += buffer[k] * send.gain;
                }
            }
        }
        // The ducker: the source's peak decides this block's gain; it falls at once and recovers exponentially.
        if (bus.duck_source != 0xFFFFFFFFu)
        {
            const f32 wanted = buses_[bus.duck_source].last_peak > bus.duck_threshold ? bus.duck_depth : 1.0f;
            bus.duck_gain = wanted < bus.duck_gain ? wanted
                                                   : bus.duck_gain + (wanted - bus.duck_gain) * bus.duck_release;
        }
        const f32 total = (bus.mute || !bus.audible) ? 0.0f : bus.gain * bus.duck_gain;
        if (total != 1.0f || bus.applied != 1.0f)
        {
            const u32 ramp = config_.ramp_frames < kBlockFrames ? config_.ramp_frames : kBlockFrames;
            for (u32 f = 0; f < kBlockFrames; ++f)
            {
                const f32 g = f + 1 < ramp ? bus.applied + (total - bus.applied) *
                                                               (static_cast<f32>(f + 1) / static_cast<f32>(ramp))
                                           : total;
                f32* frame = buffer + static_cast<usize>(f) * channels;
                for (u32 c = 0; c < channels; ++c)
                {
                    frame[c] *= g;
                }
            }
        }
        bus.applied = total;
        // The level of the bus after its gain: for the ducker and the meter.
        f32 peak = 0.0f;
        f64 energy = 0.0;
        for (usize k = 0; k < samples; ++k)
        {
            const f32 v = buffer[k] < 0.0f ? -buffer[k] : buffer[k];
            if (v > peak)
            {
                peak = v;
            }
            energy += static_cast<f64>(buffer[k]) * static_cast<f64>(buffer[k]);
        }
        bus.last_peak = peak;
        if (bus.meter)
        {
            VoiceEvent event;
            event.type = VoiceEventType::BusLevel;
            event.id.index = index;
            event.block = block_;
            event.peak = peak;
            event.rms = Sqrt(static_cast<f32>(energy / static_cast<f64>(samples)));
            if (events_.Size() + config_.max_voices < event_ring_capacity_)
            {
                events_.Push(Move(event));
            }
            else
            {
                events_dropped_audio_.FetchAdd(1, MemoryOrder::Relaxed);
            }
        }
        // Post-fader sends, then the parent.
        for (u32 i = 0; i < kMaxSends; ++i)
        {
            const Send& send = bus.sends[i];
            if (send.target != 0xFFFFFFFFu && send.gain > 0.0f && !send.pre_fader)
            {
                f32* target = BusBuffer(send.target);
                for (usize k = 0; k < samples; ++k)
                {
                    target[k] += buffer[k] * send.gain;
                }
            }
        }
        if (index != 0)
        {
            f32* parent = BusBuffer(bus.parent);
            for (usize k = 0; k < samples; ++k)
            {
                parent[k] += buffer[k];
            }
        }
    }
    MemCopy(out, BusBuffer(0), samples * sizeof(f32));
    if (limiter_on_)
    {
        limiter_.Process(out, kBlockFrames);
        limiter_min_gain_ = limiter_.MinGain();
    }
    else
    {
        limiter_min_gain_ = 1.0f;
    }
}

}  // namespace engine_placeholder::audio
