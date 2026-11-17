#include "audio/music.h"

#include "lang/scalar.h"
#include "lang/utility.h"

namespace engine_placeholder::audio
{

namespace
{

// A cross-fade is a run of linear ramps of one block each along the quarter sine and cosine: the piecewise line is
// within (pi / 2 * 256 / fade_frames)^2 / 8 of the curve (9e-4 for a tenth of a second, 6e-6 for two seconds).
constexpr u32 kSegmentFrames = kBlockFrames;
constexpr u64 kPlaylistLookahead = 8 * kBlockFrames;
constexpr f64 kHalfPi = 1.57079632679489661923;

u64 NextRandom(u64* state)
{
    u64 x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

}  // namespace

void PlaylistOrder(u32 count, u64 seed, bool shuffle, Vector<u32>* out)
{
    out->Clear();
    for (u32 i = 0; i < count; ++i)
    {
        out->Push(i);
    }
    if (!shuffle)
    {
        return;
    }
    u64 state = seed == 0 ? 0x9E3779B97F4A7C15ull : seed;
    for (u32 i = count; i > 1; --i)
    {
        const u32 j = static_cast<u32>(NextRandom(&state) % i);
        const u32 held = (*out)[i - 1];
        (*out)[i - 1] = (*out)[j];
        (*out)[j] = held;
    }
}

MusicPlayer::MusicPlayer(Mixer* mixer, BusId bus) : mixer_(mixer), bus_(bus) {}

u64 MusicPlayer::EarliestFrame() const
{
    return (mixer_->NextBlock() + mixer_->Config().lead_blocks) * kBlockFrames;
}

u64 MusicPlayer::Resolve(u64 at) const
{
    const u64 earliest = EarliestFrame();
    return at == kMusicNow || at < earliest ? earliest : at;
}

u32 MusicPlayer::NewSlot()
{
    for (u32 i = 0; i < slots_.Size(); ++i)
    {
        Slot& s = slots_[i];
        if (!s.live && (!s.started || !mixer_->IsPlaying(s.id)))
        {
            bool queued = false;
            for (usize a = 0; a < pending_.Size(); ++a)
            {
                queued = queued || pending_[a].slot == i;
            }
            if (!queued)
            {
                s = Slot();
                return i;
            }
        }
    }
    for (u32 i = 0; i < slots_.Size(); ++i)
    {
        Slot& s = slots_[i];
        if (s.started && !mixer_->IsPlaying(s.id))
        {
            bool queued = false;
            for (usize a = 0; a < pending_.Size(); ++a)
            {
                queued = queued || pending_[a].slot == i;
            }
            if (!queued)
            {
                s = Slot();
                return i;
            }
        }
    }
    slots_.Push(Slot());
    return static_cast<u32>(slots_.Size() - 1);
}

void MusicPlayer::Queue(const Action& action)
{
    usize at = pending_.Size();
    while (at > 0 && pending_[at - 1].block > action.block)
    {
        at -= 1;
    }
    pending_.Push(action);
    for (usize i = pending_.Size() - 1; i > at; --i)
    {
        const Action held = pending_[i];
        pending_[i] = pending_[i - 1];
        pending_[i - 1] = held;
    }
}

void MusicPlayer::ScheduleFade(u32 slot, u64 start_frame, u32 frames, bool rising)
{
    const u64 block0 = start_frame / kBlockFrames;
    const u32 segments = (frames + kSegmentFrames - 1) / kSegmentFrames;
    u32 last_length = kSegmentFrames;
    for (u32 j = 0; j < segments; ++j)
    {
        const u32 done = (j + 1) * kSegmentFrames;
        const u32 length = done <= frames ? kSegmentFrames : frames - j * kSegmentFrames;
        last_length = length;
        const f64 t = static_cast<f64>(j * kSegmentFrames + length) / static_cast<f64>(frames);
        Action fade;
        fade.kind = Kind::Fade;
        fade.block = block0 + static_cast<u64>(j) * (kSegmentFrames / kBlockFrames);
        fade.slot = slot;
        fade.gain = static_cast<f32>(rising ? Sin(kHalfPi * t) : Cos(kHalfPi * t));
        fade.frames = length;
        Queue(fade);
    }
    if (!rising)
    {
        Action stop;
        stop.kind = Kind::Stop;
        stop.block = block0 + static_cast<u64>(segments - 1) * (kSegmentFrames / kBlockFrames) +
                     (last_length + kBlockFrames - 1) / kBlockFrames;
        stop.slot = slot;
        Queue(stop);
    }
}

MixerStatus MusicPlayer::StartTrack(const StreamBufferPtr& track, u64 start, u32 fade_in, const Tempo& tempo, u32* slot)
{
    if (!track || !(tempo.beats_per_minute > 0.0) || tempo.beats_per_bar == 0)
    {
        return MixerStatus::InvalidParam;
    }
    const u32 index = NewSlot();
    Slot& s = slots_[index];
    s.stream = track;
    s.start_frame = start;
    s.tempo = tempo;
    s.live = true;
    Action begin;
    begin.kind = Kind::Start;
    begin.block = start / kBlockFrames;
    begin.slot = index;
    begin.frame = start;
    begin.gain = fade_in > 0 ? 0.0f : 1.0f;
    Queue(begin);
    if (fade_in > 0)
    {
        ScheduleFade(index, start, fade_in, true);
    }
    *slot = index;
    return MixerStatus::Ok;
}

MixerStatus MusicPlayer::Play(const StreamBufferPtr& track, u32 fade_in_frames, u64 at, const Tempo& tempo)
{
    u32 slot = 0;
    const MixerStatus status = StartTrack(track, Resolve(at), fade_in_frames, tempo, &slot);
    if (status == MixerStatus::Ok)
    {
        current_ = static_cast<i32>(slot);
        Update();
    }
    return status;
}

MixerStatus MusicPlayer::CrossFadeTo(const StreamBufferPtr& track, u32 frames, u64 at, const Tempo& tempo)
{
    const u64 start = Resolve(at);
    const i32 old = current_;
    u32 slot = 0;
    const MixerStatus status = StartTrack(track, start, frames, tempo, &slot);
    if (status != MixerStatus::Ok)
    {
        return status;
    }
    if (old >= 0 && slots_[static_cast<u32>(old)].live)
    {
        if (frames > 0)
        {
            ScheduleFade(static_cast<u32>(old), start, frames, false);
        }
        else
        {
            Action stop;
            stop.kind = Kind::Stop;
            stop.block = start / kBlockFrames;
            stop.slot = static_cast<u32>(old);
            Queue(stop);
        }
        slots_[static_cast<u32>(old)].live = false;
    }
    current_ = static_cast<i32>(slot);
    Update();
    return MixerStatus::Ok;
}

MixerStatus MusicPlayer::CrossFadeAtBar(const StreamBufferPtr& track, u32 frames, const Tempo& tempo)
{
    const u64 bar = NextBarFrame();
    return CrossFadeTo(track, frames, bar == 0 ? kMusicNow : bar, tempo);
}

void MusicPlayer::Stop(u32 fade_out_frames, u64 at)
{
    if (current_ < 0)
    {
        return;
    }
    const u64 start = Resolve(at);
    const u32 slot = static_cast<u32>(current_);
    if (fade_out_frames > 0)
    {
        ScheduleFade(slot, start, fade_out_frames, false);
    }
    else
    {
        Action stop;
        stop.kind = Kind::Stop;
        stop.block = start / kBlockFrames;
        stop.slot = slot;
        Queue(stop);
    }
    slots_[slot].live = false;
    current_ = -1;
    playlist_.Clear();
    Update();
}

u64 MusicPlayer::NextBoundary(u32 beats) const
{
    if (current_ < 0)
    {
        return 0;
    }
    const Slot& s = slots_[static_cast<u32>(current_)];
    const f64 beat = static_cast<f64>(mixer_->Config().sample_rate) * 60.0 / s.tempo.beats_per_minute;
    const f64 unit = beat * static_cast<f64>(beats == 0 ? s.tempo.beats_per_bar : beats);
    const u64 earliest = EarliestFrame();
    if (earliest <= s.start_frame)
    {
        return s.start_frame;
    }
    u64 k = static_cast<u64>(static_cast<f64>(earliest - s.start_frame) / unit);
    while (s.start_frame + static_cast<u64>(static_cast<f64>(k) * unit) < earliest)
    {
        k += 1;
    }
    return s.start_frame + static_cast<u64>(static_cast<f64>(k) * unit);
}

u64 MusicPlayer::NextBeatFrame() const
{
    return NextBoundary(1);
}

u64 MusicPlayer::NextBarFrame() const
{
    return NextBoundary(0);
}

u64 MusicPlayer::CurrentStartFrame() const
{
    return current_ < 0 ? 0 : slots_[static_cast<u32>(current_)].start_frame;
}

MixerStatus MusicPlayer::PlayLayers(Span<const StreamBufferPtr> layers, Span<const f32> gains, u64 at)
{
    if (layers.Size() == 0 || layers.Size() > 8 || gains.Size() != layers.Size())
    {
        return MixerStatus::InvalidParam;
    }
    const u64 start = Resolve(at);
    layer_slots_.Clear();
    for (usize i = 0; i < layers.Size(); ++i)
    {
        u32 slot = 0;
        const MixerStatus status = StartTrack(layers[i], start, 0, Tempo(), &slot);
        if (status != MixerStatus::Ok)
        {
            return status;
        }
        // Each layer opens at its own gain and loops on its own length.
        for (usize a = 0; a < pending_.Size(); ++a)
        {
            if (pending_[a].kind == Kind::Start && pending_[a].slot == slot)
            {
                pending_[a].gain = gains[i];
                pending_[a].loop = true;
            }
        }
        layer_slots_.Push(slot);
    }
    current_ = static_cast<i32>(layer_slots_[0]);
    Update();
    return MixerStatus::Ok;
}

void MusicPlayer::SetLayerGain(u32 layer, f32 gain, u32 frames, u64 at)
{
    if (layer >= layer_slots_.Size())
    {
        return;
    }
    Action fade;
    fade.kind = Kind::Fade;
    fade.block = Resolve(at) / kBlockFrames;
    fade.slot = layer_slots_[layer];
    fade.gain = gain;
    fade.frames = frames;
    Queue(fade);
    Update();
}

void MusicPlayer::SeekLayers(u64 frame, u64 at)
{
    const u64 block = Resolve(at) / kBlockFrames;
    for (usize i = 0; i < layer_slots_.Size(); ++i)
    {
        Action seek;
        seek.kind = Kind::Seek;
        seek.block = block;
        seek.slot = layer_slots_[i];
        seek.frame = frame;
        Queue(seek);
    }
    Update();
}

MixerStatus MusicPlayer::PlayStinger(const AudioClipConstPtr& clip, f32 gain, bool on_beat, u64* start_frame)
{
    if (!clip)
    {
        return MixerStatus::NoClip;
    }
    u64 frame = EarliestFrame();
    if (on_beat)
    {
        const u64 beat = NextBeatFrame();
        frame = beat == 0 ? frame : beat;
    }
    const u32 index = NewSlot();
    Slot& s = slots_[index];
    s.clip = clip;
    s.start_frame = frame;
    s.live = true;
    Action begin;
    begin.kind = Kind::Start;
    begin.block = frame / kBlockFrames;
    begin.slot = index;
    begin.frame = frame;
    begin.gain = gain;
    Queue(begin);
    if (start_frame != nullptr)
    {
        *start_frame = frame;
    }
    Update();
    return MixerStatus::Ok;
}

MixerStatus MusicPlayer::DuckForDialogue(BusId dialogue_bus, f32 depth, f32 threshold, f32 release_frames)
{
    return mixer_->SetDuck(bus_, dialogue_bus, depth, threshold, release_frames);
}

MixerStatus MusicPlayer::PlayPlaylist(Span<const StreamBufferPtr> tracks, u64 seed, bool shuffle, u64 at)
{
    if (tracks.Size() == 0)
    {
        return MixerStatus::InvalidParam;
    }
    playlist_.Clear();
    for (usize i = 0; i < tracks.Size(); ++i)
    {
        playlist_.Push(tracks[i]);
    }
    PlaylistOrder(static_cast<u32>(tracks.Size()), seed, shuffle, &order_);
    next_in_order_ = 1;
    playlist_position_ = 0;
    u32 slot = 0;
    const MixerStatus status = StartTrack(playlist_[order_[0]], Resolve(at), 0, Tempo(), &slot);
    if (status == MixerStatus::Ok)
    {
        current_ = static_cast<i32>(slot);
        Update();
    }
    return status;
}

void MusicPlayer::Post(const Action& action)
{
    const u64 due = mixer_->NextBlock() + mixer_->Config().lead_blocks;
    const u64 block = action.block > due ? action.block : due;
    Slot& s = slots_[action.slot];
    switch (action.kind)
    {
    case Kind::Start:
    {
        VoiceDesc desc;
        desc.stream = s.stream;
        desc.clip = s.stream ? s.stream->Clip() : s.clip;
        desc.gain = action.gain;
        desc.loop = action.loop;
        desc.bus = bus_;
        // On time, the voice starts on its exact frame inside the block; late, at once.
        desc.delay_frames = action.block == block ? static_cast<u32>(action.frame % kBlockFrames) : 0;
        if (mixer_->Play(desc, &s.id, block) == MixerStatus::Ok)
        {
            s.started = true;
        }
        else
        {
            s.live = false;
        }
        break;
    }
    case Kind::Fade:
        if (s.started)
        {
            mixer_->Fade(s.id, action.gain, action.frames, block);
        }
        break;
    case Kind::Stop:
        if (s.started)
        {
            mixer_->Stop(s.id, 0, block);
        }
        s.live = false;
        break;
    case Kind::Seek:
        if (s.started)
        {
            mixer_->Seek(s.id, action.frame, block);
        }
        break;
    }
}

void MusicPlayer::Update()
{
    const u64 due = mixer_->NextBlock() + mixer_->Config().lead_blocks;
    while (pending_.Size() > 0 && pending_[0].block <= due)
    {
        const Action action = pending_[0];
        for (usize i = 1; i < pending_.Size(); ++i)
        {
            pending_[i - 1] = pending_[i];
        }
        pending_.Resize(pending_.Size() - 1);
        Post(action);
    }
    // A playlist's next track starts on the frame this one ends on, primed ahead.
    if (playlist_.Size() > 0 && next_in_order_ < order_.Size() && current_ >= 0)
    {
        const Slot& cur = slots_[static_cast<u32>(current_)];
        const u64 length = cur.stream ? cur.stream->Length() : 0;
        const StreamBufferPtr& next = playlist_[order_[next_in_order_]];
        bool start_now = false;
        u64 at = 0;
        if (length > 0)
        {
            at = cur.start_frame + length;
            start_now = at <= EarliestFrame() + kPlaylistLookahead;
        }
        else if (cur.started && !mixer_->IsPlaying(cur.id))
        {
            at = EarliestFrame();
            start_now = true;
        }
        if (start_now)
        {
            next->Refill();
            u32 slot = 0;
            if (StartTrack(next, Resolve(at), 0, Tempo(), &slot) == MixerStatus::Ok)
            {
                slots_[static_cast<u32>(current_)].live = false;
                current_ = static_cast<i32>(slot);
                playlist_position_ = static_cast<i32>(next_in_order_);
                next_in_order_ += 1;
            }
        }
    }
}

}  // namespace engine_placeholder::audio
