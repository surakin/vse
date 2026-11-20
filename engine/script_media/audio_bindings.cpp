#include "script_media/audio_bindings.h"

#include "audio/components.h"
#include "cstr/mem_ops.h"
#include "lang/scalar.h"
#include "script/merge_services.h"
#include "script/partition_context.h"
#include "script/script_world.h"
#include "storage/field.h"

namespace vse::script_media
{

using script::CommandKind;
using script::CommandKindTable;
using script::MergeContext;
using script::PartitionContext;
using script::ThreadRule;
using script::Value;
using script::ValueKind;

namespace
{

constexpr usize kMaxNameBytes = 255;
constexpr f32 kMaxGain = 16.0f;
constexpr f32 kMaxFadeSeconds = 600.0f;

// ------------------------------------------------------------------ the context

PartitionContext* Context()
{
    PartitionContext* context = script::CurrentPartitionContext();
    if (context == nullptr)
    {
        script::RaiseScriptError("no world is available to this script");
        return nullptr;
    }
    return context;
}

const AudioScriptServices* NeedServices(PartitionContext* context)
{
    const AudioScriptServices* services = static_cast<const AudioScriptServices*>(
        context->extensions[script::kServiceAudio]);
    if (services == nullptr || services->mixer == nullptr || services->voices == nullptr)
    {
        script::RaiseScriptError("this world has no audio");
        return nullptr;
    }
    return services;
}

void Raise(StringView a, StringView b = StringView(), StringView c = StringView(), StringView d = StringView())
{
    String<HeapAllocator> text(HeapAllocator::Default());
    text.Append(a);
    text.Append(b);
    text.Append(c);
    text.Append(d);
    script::RaiseScriptError(text.View());
}

bool Record(PartitionContext* context, u16 kind, const void* payload, u32 size)
{
    if (context->commands == nullptr)
    {
        script::RaiseScriptError("this script cannot change the world (no command buffer)");
        return false;
    }
    if (!context->commands->Record(kind, context->current_entity_key, ecs::Entity(), payload, size))
    {
        script::RaiseScriptError("the command buffer is full");
        return false;
    }
    return true;
}

// ------------------------------------------------------------------ handles

// A handle is a number a Lua state holds as a double, which keeps integers exactly up to 2^53, so it has 53 bits: the
// tick in the high 31 (wrapping after 2^31 - 1 ticks, over a year at 60 Hz, and never 0) and in the low 22 the
// partition (10 bits) and the partition's own count of voices started this tick (12 bits). All of them depend only on
// the script's own calls, never on the thread count.
constexpr u32 kHandleSlotBits = 22;
constexpr u32 kHandleCountBits = 12;
constexpr u64 kHandleTickRange = (u64{1} << 31) - 1;
static_assert(script::kMaxPartitions <= (u32{1} << (kHandleSlotBits - kHandleCountBits)),
              "a partition number fits its bits");

constexpr u64 HandleTickBits(u64 tick)
{
    return 1 + tick % kHandleTickRange;
}

constexpr u64 HandleTick(u64 handle)
{
    return handle >> kHandleSlotBits;
}

// Checks that a handle may be used now: a voice started in this very tick has no entry yet (the merge makes it), any
// other must be held and not stopped.
bool CheckHandle(PartitionContext* context, const AudioScriptServices* services, i64 raw)
{
    const u64 handle = static_cast<u64>(raw);
    if (raw <= 0 || HandleTick(handle) == 0 || handle >= (u64{1} << 53))
    {
        script::RaiseScriptError("not a voice handle (use the number `audio.play` returned)");
        return false;
    }
    if (HandleTick(handle) == HandleTickBits(context->tick))
    {
        return true;
    }
    const VoiceTable::Entry* entry = services->voices->Find(handle);
    if (entry == nullptr)
    {
        script::RaiseScriptError("the voice has ended (its handle is no longer valid)");
        return false;
    }
    if (entry->stopping)
    {
        script::RaiseScriptError("the voice was stopped (its handle is no longer valid)");
        return false;
    }
    return true;
}

// ------------------------------------------------------------------ options

bool ReadNumber(const Value& map, StringView key, f32 lo, f32 hi, f32* out)
{
    const Value* v = map.Find(key);
    if (v == nullptr)
    {
        return true;
    }
    if (!v->IsNumber())
    {
        Raise("the option `", key, "` must be a number");
        return false;
    }
    const f64 x = v->ToDouble();
    if (!IsFinite(static_cast<f32>(x)) || x < static_cast<f64>(lo) || x > static_cast<f64>(hi))
    {
        Raise("the option `", key, "` is out of range");
        return false;
    }
    *out = static_cast<f32>(x);
    return true;
}

bool ReadFlag(const Value& map, StringView key, bool* out)
{
    const Value* v = map.Find(key);
    if (v == nullptr)
    {
        return true;
    }
    if (v->Kind() != ValueKind::Bool)
    {
        Raise("the option `", key, "` must be true or false");
        return false;
    }
    *out = v->AsBool();
    return true;
}

bool KnownKey(StringView key)
{
    static const char* const kKeys[] = {"gain", "pitch", "pan", "bus", "loop", "priority", "fade_in"};
    for (const char* name : kKeys)
    {
        if (key == StringView(name))
        {
            return true;
        }
    }
    return false;
}

// Seconds to frames; false (and a script error) for a bad fade.
bool FadeFrames(const Value& seconds, const AudioScriptServices* services, u32* frames, bool* given)
{
    *given = false;
    if (seconds.IsNil())
    {
        return true;
    }
    if (!seconds.IsNumber())
    {
        script::RaiseScriptError("the fade must be a number of seconds (or nil)");
        return false;
    }
    const f64 s = seconds.ToDouble();
    if (!IsFinite(static_cast<f32>(s)) || s < 0.0 || s > static_cast<f64>(kMaxFadeSeconds))
    {
        script::RaiseScriptError("the fade must be from 0 to 600 seconds");
        return false;
    }
    *frames = static_cast<u32>(s * static_cast<f64>(services->sample_rate));
    *given = true;
    return true;
}

// ------------------------------------------------------------------ payloads

struct PlayHeader
{
    u64 handle;
    u64 owner;
    f32 gain;
    f32 pitch;
    f32 pan;
    u32 bus;
    u32 flags;  // 1 loop, 2 fade in
    i32 priority;
    u32 name_size;
};

struct HandleFade
{
    u64 handle;
    u32 frames;
    u32 use_default;
};

struct HandleFlag
{
    u64 handle;
    u32 flag;
    u32 pad;
};

struct HandleValue
{
    u64 handle;
    f32 value;
    u32 pad;
};

struct BusValue
{
    u32 bus;
    f32 value;
};

struct MusicHeader
{
    u32 frames;
    u32 crossfade;
    u32 name_size;
};

struct MusicStopPayload
{
    u32 frames;
    u32 pad;
};

// ------------------------------------------------------------------ the writes

i64 AudioPlay(StringView clip, Value options)
{
    PartitionContext* context = Context();
    const AudioScriptServices* services = context != nullptr ? NeedServices(context) : nullptr;
    if (services == nullptr)
    {
        return 0;
    }
    if (services->clips.resolve == nullptr)
    {
        script::RaiseScriptError("this world has no clips to play");
        return 0;
    }
    if (clip.Length() == 0 || clip.Length() > kMaxNameBytes)
    {
        script::RaiseScriptError("the clip name is empty or too long");
        return 0;
    }
    if (!options.IsNil() && options.Kind() != ValueKind::Map)
    {
        script::RaiseScriptError("the options are a table (or nil)");
        return 0;
    }
    if (services->clips.resolve(services->clips.user, clip).Get() == nullptr)
    {
        Raise("no clip is called `", clip, "`");
        return 0;
    }
    PlayHeader header;
    MemSet(&header, 0, sizeof(header));
    header.gain = 1.0f;
    header.pitch = 1.0f;
    header.pan = 0.0f;
    header.bus = 0;
    bool loop = false;
    bool fade_in = false;
    f32 priority = 0.0f;
    if (!options.IsNil())
    {
        for (usize i = 0; i < options.Size(); ++i)
        {
            if (!KnownKey(options.KeyAt(i)))
            {
                Raise("unknown option `", options.KeyAt(i), "` (gain, pitch, pan, bus, loop, priority, fade_in)");
                return 0;
            }
        }
        if (!ReadNumber(options, "gain", 0.0f, kMaxGain, &header.gain) ||
            !ReadNumber(options, "pitch", audio::kMinPitch, audio::kMaxPitch, &header.pitch) ||
            !ReadNumber(options, "pan", -1.0f, 1.0f, &header.pan) ||
            !ReadNumber(options, "priority", -1.0e6f, 1.0e6f, &priority) || !ReadFlag(options, "loop", &loop) ||
            !ReadFlag(options, "fade_in", &fade_in))
        {
            return 0;
        }
        const Value* bus = options.Find("bus");
        if (bus != nullptr)
        {
            if (bus->Kind() != ValueKind::Text)
            {
                script::RaiseScriptError("the option `bus` is a bus name");
                return 0;
            }
            const audio::BusId id = services->mixer->FindBus(bus->Text());
            if (!id.IsValid())
            {
                Raise("no bus is called `", bus->Text(), "`");
                return 0;
            }
            header.bus = id.index;
        }
    }
    header.priority = static_cast<i32>(priority);
    header.flags = (loop ? 1u : 0u) | (fade_in ? 2u : 0u);
    const ecs::Entity placeholder = context->commands != nullptr ? context->commands->Reserve() : ecs::Entity();
    if (!placeholder.IsValid())
    {
        script::RaiseScriptError("too many voices started in one tick by this partition");
        return 0;
    }
    const u32 partition = placeholder.index / script::kPlaceholdersPerPartition;
    const u32 count = placeholder.index % script::kPlaceholdersPerPartition;
    if (count >= (u32{1} << kHandleCountBits))
    {
        script::RaiseScriptError("too many voices started in one tick by this partition (4096)");
        return 0;
    }
    header.handle = (HandleTickBits(context->tick) << kHandleSlotBits) | (u64{partition} << kHandleCountBits) | count;
    header.owner = context->current_entity_key;
    header.name_size = static_cast<u32>(clip.Length());
    u8 bytes[sizeof(PlayHeader) + kMaxNameBytes];
    MemCopy(bytes, &header, sizeof(header));
    MemCopy(bytes + sizeof(header), clip.Data(), clip.Length());
    if (!Record(context, kCommandAudioPlay, bytes, static_cast<u32>(sizeof(header) + clip.Length())))
    {
        return 0;
    }
    return static_cast<i64>(header.handle);
}

void AudioStop(i64 voice, Value fade)
{
    PartitionContext* context = Context();
    const AudioScriptServices* services = context != nullptr ? NeedServices(context) : nullptr;
    if (services == nullptr || !CheckHandle(context, services, voice))
    {
        return;
    }
    HandleFade payload{static_cast<u64>(voice), 0, 1};
    bool given = false;
    if (!FadeFrames(fade, services, &payload.frames, &given))
    {
        return;
    }
    payload.use_default = given ? 0u : 1u;
    Record(context, kCommandAudioStop, &payload, sizeof(payload));
}

void RecordFlag(i64 voice, u32 flag)
{
    PartitionContext* context = Context();
    const AudioScriptServices* services = context != nullptr ? NeedServices(context) : nullptr;
    if (services == nullptr || !CheckHandle(context, services, voice))
    {
        return;
    }
    const HandleFlag payload{static_cast<u64>(voice), flag, 0};
    Record(context, kCommandAudioPause, &payload, sizeof(payload));
}

void AudioPause(i64 voice)
{
    RecordFlag(voice, 1);
}

void AudioResume(i64 voice)
{
    RecordFlag(voice, 0);
}

void RecordValue(u16 kind, i64 voice, f64 value, f32 lo, f32 hi, const char* what)
{
    PartitionContext* context = Context();
    const AudioScriptServices* services = context != nullptr ? NeedServices(context) : nullptr;
    if (services == nullptr || !CheckHandle(context, services, voice))
    {
        return;
    }
    if (!IsFinite(static_cast<f32>(value)) || value < static_cast<f64>(lo) || value > static_cast<f64>(hi))
    {
        Raise("the ", StringView(what), " is out of range");
        return;
    }
    const HandleValue payload{static_cast<u64>(voice), static_cast<f32>(value), 0};
    Record(context, kind, &payload, sizeof(payload));
}

void AudioSetGain(i64 voice, f64 gain)
{
    RecordValue(kCommandAudioSetGain, voice, gain, 0.0f, kMaxGain, "gain (0 to 16)");
}

void AudioSetPitch(i64 voice, f64 pitch)
{
    RecordValue(kCommandAudioSetPitch, voice, pitch, audio::kMinPitch, audio::kMaxPitch, "pitch (1/16 to 16)");
}

void AudioSetBusGain(StringView bus, f64 gain)
{
    PartitionContext* context = Context();
    const AudioScriptServices* services = context != nullptr ? NeedServices(context) : nullptr;
    if (services == nullptr)
    {
        return;
    }
    const audio::BusId id = services->mixer->FindBus(bus);
    if (!id.IsValid())
    {
        Raise("no bus is called `", bus, "`");
        return;
    }
    if (!IsFinite(static_cast<f32>(gain)) || gain < 0.0 || gain > static_cast<f64>(kMaxGain))
    {
        script::RaiseScriptError("the bus gain is out of range (0 to 16)");
        return;
    }
    const BusValue payload{id.index, static_cast<f32>(gain)};
    Record(context, kCommandAudioBusGain, &payload, sizeof(payload));
}

void RecordMusic(StringView stream, Value seconds, bool crossfade)
{
    PartitionContext* context = Context();
    const AudioScriptServices* services = context != nullptr ? NeedServices(context) : nullptr;
    if (services == nullptr)
    {
        return;
    }
    if (services->music == nullptr || services->streams.resolve == nullptr)
    {
        script::RaiseScriptError("this world has no music player");
        return;
    }
    if (stream.Length() == 0 || stream.Length() > kMaxNameBytes)
    {
        script::RaiseScriptError("the stream name is empty or too long");
        return;
    }
    if (services->streams.resolve(services->streams.user, stream).Get() == nullptr)
    {
        Raise("no music stream is called `", stream, "`");
        return;
    }
    MusicHeader header{0, crossfade ? 1u : 0u, static_cast<u32>(stream.Length())};
    bool given = false;
    if (!FadeFrames(seconds, services, &header.frames, &given))
    {
        return;
    }
    if (!given && crossfade)
    {
        header.frames = services->sample_rate;  // a one second crossfade unless the script says
    }
    u8 bytes[sizeof(MusicHeader) + kMaxNameBytes];
    MemCopy(bytes, &header, sizeof(header));
    MemCopy(bytes + sizeof(header), stream.Data(), stream.Length());
    Record(context, kCommandMusicPlay, bytes, static_cast<u32>(sizeof(header) + stream.Length()));
}

void MusicPlay(StringView stream, Value fade_seconds)
{
    RecordMusic(stream, fade_seconds, false);
}

void MusicCrossfade(StringView stream, Value seconds)
{
    RecordMusic(stream, seconds, true);
}

void MusicStop(Value fade_seconds)
{
    PartitionContext* context = Context();
    const AudioScriptServices* services = context != nullptr ? NeedServices(context) : nullptr;
    if (services == nullptr)
    {
        return;
    }
    if (services->music == nullptr)
    {
        script::RaiseScriptError("this world has no music player");
        return;
    }
    MusicStopPayload payload{services->sample_rate / 2, 0};  // half a second unless the script says
    bool given = false;
    if (!FadeFrames(fade_seconds, services, &payload.frames, &given))
    {
        return;
    }
    if (!given)
    {
        payload.frames = services->sample_rate / 2;
    }
    Record(context, kCommandMusicStop, &payload, sizeof(payload));
}

// ------------------------------------------------------------------ the reads

bool AudioIsPlaying(i64 voice)
{
    PartitionContext* context = Context();
    const AudioScriptServices* services = context != nullptr ? NeedServices(context) : nullptr;
    if (services == nullptr || voice <= 0)
    {
        return false;
    }
    const VoiceTable::Entry* entry = services->voices->Find(static_cast<u64>(voice));
    return entry != nullptr && !entry->stopping;
}

bool AudioExists(StringView clip)
{
    PartitionContext* context = Context();
    const AudioScriptServices* services = context != nullptr ? NeedServices(context) : nullptr;
    return services != nullptr && services->clips.resolve != nullptr &&
           services->clips.resolve(services->clips.user, clip).Get() != nullptr;
}

// ------------------------------------------------------------------ the merge

const AudioScriptServices* MergeAudio(MergeContext& context)
{
    const script::MergeServices* services = static_cast<const script::MergeServices*>(context.User());
    const AudioScriptServices* audio = services != nullptr ? static_cast<const AudioScriptServices*>(
                                                                 services->extensions[script::kServiceAudio])
                                                           : nullptr;
    return audio != nullptr && audio->mixer != nullptr && audio->voices != nullptr ? audio : nullptr;
}

template <class T> bool ReadPayload(Span<const u8> payload, T* out)
{
    if (payload.Size() != sizeof(T))
    {
        return false;
    }
    MemCopy(out, payload.Data(), sizeof(T));
    return true;
}

// The voice a handle names at the merge, or false (a play that failed, a voice that ended).
bool VoiceOf(const AudioScriptServices* audio, u64 handle, audio::VoiceId* voice)
{
    const VoiceTable::Entry* entry = audio->voices->Find(handle);
    if (entry == nullptr)
    {
        return false;
    }
    *voice = entry->voice;
    return true;
}

bool ApplyPlay(MergeContext& context, ecs::Entity, Span<const u8> payload)
{
    const AudioScriptServices* audio = MergeAudio(context);
    PlayHeader header;
    if (audio == nullptr || audio->clips.resolve == nullptr || payload.Size() < sizeof(header))
    {
        return false;
    }
    MemCopy(&header, payload.Data(), sizeof(header));
    if (payload.Size() != sizeof(header) + header.name_size)
    {
        return false;
    }
    const StringView name(reinterpret_cast<const char*>(payload.Data() + sizeof(header)), header.name_size);
    audio::VoiceDesc desc;
    desc.clip = audio->clips.resolve(audio->clips.user, name);
    if (desc.clip.Get() == nullptr)
    {
        return false;
    }
    desc.gain = header.gain;
    desc.pitch = header.pitch;
    desc.pan = header.pan;
    desc.priority = header.priority;
    desc.loop = (header.flags & 1u) != 0;
    desc.fade_in = (header.flags & 2u) != 0;
    desc.bus = audio::BusId{header.bus};
    audio::VoiceId voice;
    if (audio->mixer->Play(desc, &voice) != audio::MixerStatus::Ok)
    {
        return false;
    }
    audio->voices->Add(header.handle, voice, header.owner);
    return true;
}

bool ApplyStop(MergeContext& context, ecs::Entity, Span<const u8> payload)
{
    const AudioScriptServices* audio = MergeAudio(context);
    HandleFade p;
    audio::VoiceId voice;
    if (audio == nullptr || !ReadPayload(payload, &p) || !VoiceOf(audio, p.handle, &voice))
    {
        return false;
    }
    const audio::MixerStatus status = p.use_default != 0 ? audio->mixer->Stop(voice)
                                                         : audio->mixer->Stop(voice, p.frames);
    audio->voices->MarkStopping(p.handle);
    return status == audio::MixerStatus::Ok;
}

bool ApplyPause(MergeContext& context, ecs::Entity, Span<const u8> payload)
{
    const AudioScriptServices* audio = MergeAudio(context);
    HandleFlag p;
    audio::VoiceId voice;
    if (audio == nullptr || !ReadPayload(payload, &p) || !VoiceOf(audio, p.handle, &voice))
    {
        return false;
    }
    return (p.flag != 0 ? audio->mixer->Pause(voice) : audio->mixer->Resume(voice)) == audio::MixerStatus::Ok;
}

bool ApplySetGain(MergeContext& context, ecs::Entity, Span<const u8> payload)
{
    const AudioScriptServices* audio = MergeAudio(context);
    HandleValue p;
    audio::VoiceId voice;
    return audio != nullptr && ReadPayload(payload, &p) && VoiceOf(audio, p.handle, &voice) &&
           audio->mixer->SetGain(voice, p.value) == audio::MixerStatus::Ok;
}

bool ApplySetPitch(MergeContext& context, ecs::Entity, Span<const u8> payload)
{
    const AudioScriptServices* audio = MergeAudio(context);
    HandleValue p;
    audio::VoiceId voice;
    return audio != nullptr && ReadPayload(payload, &p) && VoiceOf(audio, p.handle, &voice) &&
           audio->mixer->SetPitch(voice, p.value) == audio::MixerStatus::Ok;
}

bool ApplyBusGain(MergeContext& context, ecs::Entity, Span<const u8> payload)
{
    const AudioScriptServices* audio = MergeAudio(context);
    BusValue p;
    return audio != nullptr && ReadPayload(payload, &p) &&
           audio->mixer->SetBusGain(audio::BusId{p.bus}, p.value) == audio::MixerStatus::Ok;
}

bool ApplyMusicPlay(MergeContext& context, ecs::Entity, Span<const u8> payload)
{
    const AudioScriptServices* audio = MergeAudio(context);
    MusicHeader header;
    if (audio == nullptr || audio->music == nullptr || audio->streams.resolve == nullptr ||
        payload.Size() < sizeof(header))
    {
        return false;
    }
    MemCopy(&header, payload.Data(), sizeof(header));
    if (payload.Size() != sizeof(header) + header.name_size)
    {
        return false;
    }
    const StringView name(reinterpret_cast<const char*>(payload.Data() + sizeof(header)), header.name_size);
    const audio::StreamBufferPtr stream = audio->streams.resolve(audio->streams.user, name);
    if (stream.Get() == nullptr)
    {
        return false;
    }
    const audio::MixerStatus status = header.crossfade != 0 ? audio->music->CrossFadeTo(stream, header.frames)
                                                            : audio->music->Play(stream, header.frames);
    return status == audio::MixerStatus::Ok;
}

bool ApplyMusicStop(MergeContext& context, ecs::Entity, Span<const u8> payload)
{
    const AudioScriptServices* audio = MergeAudio(context);
    MusicStopPayload p;
    if (audio == nullptr || audio->music == nullptr || !ReadPayload(payload, &p))
    {
        return false;
    }
    audio->music->Stop(p.frames);
    return true;
}

}  // namespace

bool RegisterAudioCommandKinds(CommandKindTable* kinds)
{
    bool ok = true;
    ok &= kinds->Register(kCommandAudioPlay, CommandKind{"audio.play", &ApplyPlay, false, false, true});
    ok &= kinds->Register(kCommandAudioStop, CommandKind{"audio.stop", &ApplyStop, false, false, true});
    ok &= kinds->Register(kCommandAudioPause, CommandKind{"audio.pause", &ApplyPause, false, false, true});
    ok &= kinds->Register(kCommandAudioSetGain, CommandKind{"audio.set_gain", &ApplySetGain, false, false, true});
    ok &= kinds->Register(kCommandAudioSetPitch, CommandKind{"audio.set_pitch", &ApplySetPitch, false, false, true});
    ok &= kinds->Register(kCommandAudioBusGain, CommandKind{"audio.set_bus_gain", &ApplyBusGain, false, false, true});
    ok &= kinds->Register(kCommandMusicPlay, CommandKind{"audio.music.play", &ApplyMusicPlay, false, false, true});
    ok &= kinds->Register(kCommandMusicStop, CommandKind{"audio.music.stop", &ApplyMusicStop, false, false, true});
    return ok;
}

bool RegisterAudioBindings(script::BindingRegistry* registry)
{
    bool ok = true;
    ok &= registry->Add<&AudioPlay>(
        "audio", "play", ThreadRule::WorldWrite, "clip, options",
        "Starts a clip by name and returns the voice's handle (a number). `options` is nil or {gain=0..16, "
        "pitch=1/16..16, pan=-1..1, bus=\"name\", loop=, priority=, fade_in=}. The sound starts when the commands of "
        "the tick are applied.");
    ok &= registry->Add<&AudioStop>("audio", "stop", ThreadRule::WorldWrite, "voice, fade_seconds",
                                    "Fades the voice out (nil: the mixer's short ramp) and frees it. The handle is "
                                    "then no longer valid.");
    ok &= registry->Add<&AudioPause>("audio", "pause", ThreadRule::WorldWrite, "voice",
                                     "Fades the voice to silence and holds its position.");
    ok &= registry->Add<&AudioResume>("audio", "resume", ThreadRule::WorldWrite, "voice", "Undoes `pause`.");
    ok &= registry->Add<&AudioSetGain>("audio", "set_gain", ThreadRule::WorldWrite, "voice, gain",
                                       "Changes the voice's gain (0 to 16) over a short ramp.");
    ok &= registry->Add<&AudioSetPitch>("audio", "set_pitch", ThreadRule::WorldWrite, "voice, pitch",
                                        "Changes the voice's pitch (1/16 to 16; 2 is an octave up).");
    ok &= registry->Add<&AudioSetBusGain>("audio", "set_bus_gain", ThreadRule::WorldWrite, "bus, gain",
                                          "Changes a bus's gain (0 to 16).");
    ok &= registry->Add<&MusicPlay>("audio", "play_music", ThreadRule::WorldWrite, "stream, fade_seconds",
                                    "Starts a music stream, fading in (nil: no fade).");
    ok &= registry->Add<&MusicCrossfade>("audio", "crossfade_music", ThreadRule::WorldWrite, "stream, seconds",
                                         "Replaces the playing track by a stream with a cross-fade (nil: one second).");
    ok &= registry->Add<&MusicStop>("audio", "stop_music", ThreadRule::WorldWrite, "fade_seconds",
                                    "Fades the music out (nil: half a second) and stops it.");
    ok &= registry->Add<&AudioIsPlaying>("audio", "is_playing", ThreadRule::WorldRead, "voice",
                                         "Returns whether the handle names a voice that is playing (a voice started "
                                         "in this very tick is not yet).");
    ok &= registry->Add<&AudioExists>("audio", "exists", ThreadRule::WorldRead, "clip",
                                      "Returns whether a clip of that name exists.");
    return ok;
}

namespace
{

const char* CheckListener(const void* component)
{
    const audio::AudioListener* listener = static_cast<const audio::AudioListener*>(component);
    return IsFinite(listener->gain) && listener->gain >= 0.0f && listener->gain <= 4.0f
               ? nullptr
               : "the listener's gain is from 0 to 4";
}

const char* ReasonOf(audio::VoiceEventType type)
{
    switch (type)
    {
    case audio::VoiceEventType::Finished: return "finished";
    case audio::VoiceEventType::Stopped: return "stopped";
    case audio::VoiceEventType::Stolen: return "stolen";
    default: return nullptr;
    }
}

}  // namespace

bool RegisterAudioComponents(storage::ComponentRegistry* registry)
{
    using audio::AudioListener;
    using storage::Field;
    return registry
        ->Register<AudioListener>(
            {.id = kAudioListenerId,
             .name = "audio_listener",
             .fields = {Field<&AudioListener::gain>("gain"), Field<&AudioListener::active>("active")}})
        .Ok();
}

void RegisterAudioValidators(script::ComponentCatalog* catalog)
{
    catalog->SetValidator(kAudioListenerId, &CheckListener);
}

AudioEventReport PumpAudioEvents(audio::Mixer* mixer, VoiceTable* voices, script::ScriptRuntime* runtime)
{
    AudioEventReport report;
    audio::VoiceEvent events[64];
    for (;;)
    {
        const u32 count = mixer->PollEvents(events, 64);
        for (u32 i = 0; i < count; ++i)
        {
            ++report.polled;
            const char* reason = ReasonOf(events[i].type);
            const u64 handle = reason != nullptr ? voices->HandleOf(events[i].id) : 0;
            if (handle == 0)
            {
                continue;
            }
            const VoiceTable::Entry* entry = voices->Find(handle);
            const u64 owner = entry->owner_key;
            if (runtime->HasHandler(owner, StringView(kEventVoiceEnd)))
            {
                Value payload = Value::MakeMap();
                payload.Set("voice", Value::FromInt(static_cast<i64>(handle)));
                payload.Set("reason", Value::FromText(StringView(reason)));
                if (runtime->Events().Emit(StringView(kEventVoiceEnd), Move(payload), owner))
                {
                    ++report.emitted;
                }
            }
            else
            {
                ++report.unhandled;
            }
            voices->Remove(handle);
        }
        if (count < 64)
        {
            break;
        }
    }
    return report;
}

}  // namespace vse::script_media
