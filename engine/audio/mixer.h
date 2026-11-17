// The mixer (Phase 26 Days 420-421): voices, the resampler, gain and pan, rendered in fixed blocks, and the lock-free
// control of the voices: the game thread posts commands stamped with the block they take effect at, the audio thread
// applies them before it mixes that block, and every change of gain, pan or pitch ramps so it never clicks. Buses and
// effects come on the next days.
#pragma once

#include "audio/audio_settings.h"
#include "audio/clip.h"
#include "audio/commands.h"
#include "audio/convert.h"
#include "audio/effect.h"
#include "audio/limiter.h"
#include "containers/vector.h"
#include "string_view.h"
#include "sync/atomic.h"
#include "types.h"

namespace engine_placeholder::audio
{

/// The frames of one mixer block: the unit the mixer renders, whatever size the device calls back with.
inline constexpr u32 kBlockFrames = 256;

/// The most voices a mixer tracks.
inline constexpr u32 kMaxVoices = 4096;

/// The most buses a mixer has, the master included.
inline constexpr u32 kMaxBuses = 32;
/// The inserts (effects) of a bus, processed in slot order.
inline constexpr u32 kMaxInserts = 4;
/// The effects a mixer can own over its life (an effect that is cleared or replaced is kept until the mixer goes).
inline constexpr u32 kMaxEffects = 64;
/// The sends one bus can have.
inline constexpr u32 kMaxSends = 4;
/// The longest bus name in bytes (without a terminator).
inline constexpr u32 kMaxBusNameBytes = 31;

/// The lowest and highest pitch (the rate ratio of a voice).
inline constexpr f32 kMinPitch = 1.0f / 16.0f;
/// See `kMinPitch`.
inline constexpr f32 kMaxPitch = 16.0f;

/// Why a mixer call did nothing.
///
/// - Thread-safety: thread-compatible plain data.
enum class MixerStatus : u8
{
    /// The call succeeded.
    Ok,
    /// The configuration is not valid.
    InvalidConfig,
    /// The id names no voice (never made, finished or stopped).
    InvalidVoice,
    /// A parameter is out of range or not finite.
    InvalidParam,
    /// The voice description has no clip.
    NoClip,
    /// The pool is full and every voice outranks the new one.
    Full,
    /// The command queue is full; nothing was posted and the call can be repeated after the next block.
    QueueFull,
    /// The bus does not exist.
    InvalidBus,
    /// The change would make the bus graph (parents, sends and ducking) a cycle.
    Cycle,
    /// The name is empty, too long or already taken, or there are no free buses or sends.
    NoBusSlot,
    /// The heap is exhausted.
    OutOfMemory,
    /// The mixer already owns `kMaxEffects` effects, or the slot or the effect is not valid.
    NoEffectSlot,
};

/// A bus: an index into the mixer's tree. The master bus always exists and is index 0.
///
/// - Thread-safety: thread-compatible plain data.
struct BusId
{
    /// The index; 0 is the master bus and `0xFFFFFFFF` is no bus.
    u32 index = 0;
    /// Whether this could name a bus.
    ///
    /// - Fails: never.
    bool IsValid() const { return index != 0xFFFFFFFFu; }
    /// Whether two ids are the same.
    bool operator==(const BusId& other) const { return index == other.index; }
};

/// The master bus.
inline constexpr BusId kMasterBus = {0};

/// A handle to a voice: a slot and a generation, so a stale id names nothing.
///
/// - Thread-safety: thread-compatible plain data.
struct VoiceId
{
    /// The slot; `0xFFFFFFFF` is no voice.
    u32 index = 0xFFFFFFFFu;
    /// The slot's generation when the voice began.
    u32 generation = 0;

    /// Whether this could name a voice.
    ///
    /// - Returns: true unless the index is the none value.
    /// - Fails: never.
    bool IsValid() const { return index != 0xFFFFFFFFu; }
    /// Whether two ids are the same.
    bool operator==(const VoiceId& other) const { return index == other.index && generation == other.generation; }
};

/// What a voice plays and how.
///
/// - Thread-safety: thread-compatible plain data.
struct VoiceDesc
{
    /// The clip; kept alive by the voice.
    AudioClipConstPtr clip;
    /// The linear gain, 0 to 16.
    f32 gain = 1.0f;
    /// The pan, -1 (left) to 1 (right); a mono clip pans with an equal-power law, a stereo clip balances.
    f32 pan = 0.0f;
    /// The rate ratio: 2 plays an octave up and twice as fast; `kMinPitch` to `kMaxPitch`.
    f32 pitch = 1.0f;
    /// Whether the voice loops (the clip's loop points, or the whole clip without them).
    bool loop = false;
    /// A virtual voice advances its position and mixes nothing.
    bool is_virtual = false;
    /// Higher survives when the pool is full.
    i32 priority = 0;
    /// Where the voice starts, in clip frames.
    u64 start_frame = 0;
    /// Fade in over `MixerConfig::ramp_frames` instead of starting at full gain.
    bool fade_in = false;
    /// The bus the voice mixes into (the master if the bus does not exist).
    BusId bus = {0};
    /// The auxiliary bus the voice also feeds (none if invalid).
    BusId send_bus = {0xFFFFFFFFu};
    /// The gain of that send, 0 to 16.
    f32 send_gain = 0.0f;
};

/// What a command does.
enum class CommandType : u8
{
    /// Start a voice in a reserved slot.
    Play,
    /// Fade a voice out over `frames` (0: at once) and free it.
    Stop,
    /// Fade a voice to silence over the ramp and hold its position.
    Pause,
    /// Undo `Pause`: ramp back up.
    Resume,
    /// Jump to a position (`frames`); not smoothed.
    Seek,
    /// Change the gain (`value`) over the ramp.
    SetGain,
    /// Change the pitch (`value`) over the ramp.
    SetPitch,
    /// Change the pan (`value`) over the ramp.
    SetPan,
    /// Turn looping on or off (`value` nonzero is on).
    SetLoop,
    /// Change the gain to `value` over `frames`.
    Fade,
    /// Make a voice virtual or real (`value` nonzero is virtual).
    SetVirtual,
    /// Create bus `a` under parent `b`.
    CreateBus,
    /// Change the gain of bus `a` to `value`.
    SetBusGain,
    /// Mute (`value` nonzero) or unmute bus `a`.
    SetBusMute,
    /// Solo or unsolo bus `a`.
    SetBusSolo,
    /// Set send `b` of bus `a`: target `frames`, gain `value`, pre-fader if `value2` is nonzero.
    SetBusSend,
    /// Duck bus `a` by bus `b`: depth `value`, threshold `value2`, release coefficient `frames` as a float's bits.
    SetDuck,
    /// Turn level events for bus `a` on or off.
    SetMeter,
    /// Move a voice to bus `a`.
    SetVoiceBus,
    /// Change a voice's send to bus `a` with gain `value`.
    SetVoiceSend,
    /// Turn the master limiter on or off.
    SetLimiter,
    /// Put `effect` (or none) into insert `b` of bus `a`.
    SetInsert,
    /// Give `effect` the parameter block `params`.
    SetEffectParams,
};

/// One control command; the game thread fills it and the audio thread applies it.
///
/// - Thread-safety: thread-compatible plain data (moved through the ring).
struct Command
{
    /// The block at which the command is applied (the first block rendered with the command in effect).
    u64 stamp = 0;
    /// What to do.
    CommandType type = CommandType::Stop;
    /// The voice (for `Play`: the slot and generation reserved for it).
    VoiceId id;
    /// The voice, for `Play`.
    VoiceDesc desc;
    /// The first argument of the command (gain, pitch, pan, flag).
    f32 value = 0.0f;
    /// A frame count or position.
    u64 frames = 0;
    /// Bus arguments.
    u32 a = 0;
    /// See `a`.
    u32 b = 0;
    /// A second float argument.
    f32 value2 = 0.0f;
    /// The effect of `SetInsert` and `SetEffectParams` (owned by the mixer).
    Effect* effect = nullptr;
    /// The parameters of `SetEffectParams`.
    EffectParams params;
};

/// What happened to a voice.
enum class VoiceEventType : u8
{
    /// The voice ran off the end of its clip.
    Finished,
    /// The voice wrapped its loop (at most one event per voice per block).
    Looped,
    /// The voice was made virtual.
    Virtualised,
    /// A newer, better voice took the slot; reported when the new voice is posted.
    Stolen,
    /// A `Stop` completed (the fade-out ended) and the voice was freed.
    Stopped,
    /// The level of a metered bus in the last block (`id.index` is the bus; `peak` and `rms` are linear).
    BusLevel,
};

/// A report from the audio thread (or the control side, for `Stolen`) to the game.
///
/// - Thread-safety: thread-compatible plain data.
struct VoiceEvent
{
    /// What happened.
    VoiceEventType type = VoiceEventType::Finished;
    /// The voice.
    VoiceId id;
    /// The block in which it happened (0 for `Stolen`).
    u64 block = 0;
    /// For `BusLevel`: the peak sample of the bus in the block.
    f32 peak = 0.0f;
    /// For `BusLevel`: the root mean square of the bus in the block.
    f32 rms = 0.0f;
};

/// How a mixer is built.
///
/// - Thread-safety: thread-compatible plain data.
struct MixerConfig
{
    /// The output rate in Hz.
    u32 sample_rate = 48000;
    /// The output layout.
    ChannelLayout layout = ChannelLayout::Stereo;
    /// The pool size, 1 to `kMaxVoices`.
    u32 max_voices = 256;
    /// The interpolation of a voice.
    ResamplerQuality quality = ResamplerQuality::Hermite;
    /// The frames over which a change of gain, pan or pitch ramps; 0 changes at once (for tests).
    u32 ramp_frames = 64;
    /// The blocks between the block being rendered and the one a command is stamped for (1 gives the audio thread a
    /// whole block of notice; 0 applies at the next block, for offline renders).
    u32 lead_blocks = 1;
    /// The slots of the command ring (rounded up to a power of two).
    u32 command_capacity = 1024;
    /// The slots of the event ring, and of the list `PollEvents` reads from.
    u32 event_capacity = 1024;
    /// An offline render: `TryRenderBlock` waits for the producer's watermark.
    bool offline = false;
    /// The buses the mixer can have, 1 (the master) to `kMaxBuses`.
    u32 max_buses = 16;
    /// Whether the master limiter starts on (it can be switched with `SetLimiter`); it delays the output by its
    /// look-ahead.
    bool limiter = false;
    /// The limiter's settings.
    LimiterConfig limiter_config;
};

/// Counters of what the mixer did.
///
/// - Thread-safety: thread-compatible plain data.
struct MixerStats
{
    /// The blocks rendered.
    u64 blocks = 0;
    /// The voices mixed in the last block.
    u32 voices_mixed = 0;
    /// The virtual voices advanced in the last block.
    u32 voices_virtual = 0;
    /// The voices stopped by a newer, louder voice when the pool was full.
    u64 voices_stolen = 0;
    /// The `Play` calls refused because the pool was full of better voices.
    u64 voices_refused = 0;
    /// The voices that ran off the end of their clip.
    u64 voices_finished = 0;
    /// The commands the audio thread applied.
    u64 commands_applied = 0;
    /// The commands applied after their stamp (they arrived late; applied at the block being rendered).
    u64 late_commands = 0;
    /// The commands refused because the ring was full.
    u64 commands_dropped = 0;
    /// The events lost because a ring or the event list was full.
    u64 events_dropped = 0;
    /// The lowest gain the master limiter applied in the last block (1 when it did nothing or is off).
    f32 limiter_min_gain = 1.0f;
};

/// The mixer: a voice pool, the command queue and the block renderer.
///
/// Positions are 32.32 fixed point in `u64`, so a long clip does not drift; the interpolation (`+ - * /` only) is
/// linear, 4-point Hermite or 16-tap windowed sinc. Voices are summed in slot order, never thread order. The output is
/// not clipped: the master limiter and the device boundary do that (Day 422). A clip's channels are routed to the
/// output layout: a mono clip pans over front left and right (equal power, centre gain 0.7071), a stereo clip on a
/// layout with front left and right balances (centre gain 1), any other pair goes through the conversion table of
/// `audio/convert.h`.
///
/// Two threads use a mixer. The **control thread** (the game) calls `Play`, `Stop`, the setters, `IsPlaying`,
/// `PollEvents` and `SetWatermark`; the **audio thread** (the device callback) calls `RenderBlock`, `TryRenderBlock`
/// and `Render`. The two sides share only the command ring, the event ring and two atomic counters, so no call locks,
/// and the audio side allocates nothing. Every control call returns at once: a `Play` reserves its slot and
/// generation on the control side (so the id can be used before the audio thread has seen the command) and posts the
/// command, which takes effect at its block stamp. Every setter ramps over `MixerConfig::ramp_frames`, ending exactly
/// on the target; a stop fades out before the voice is freed; the order of effect is the order posted.
///
/// A clip is released on the audio thread when its voice ends; keep another reference (the resource handle) while
/// voices play, so that the audio thread never frees a clip.
///
/// - Thread-safety: thread-compatible; one control thread and one audio thread use it as described above, and
/// `Position`, `VoiceCount` and `Stats` read audio-thread state, so they are for offline use or after the renderer has
/// stopped.
/// - Ownership: owns the voice pool, the rings and each voice's clip reference.
class Mixer
{
public:
    Mixer() = default;
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;

    /// Builds the voice pool, the rings and the tables; allocates here and never again.
    ///
    /// - Param config: the configuration.
    /// - Returns: `Ok`, `InvalidConfig` or `OutOfMemory`.
    /// - Fails: reported by return value.
    MixerStatus Init(const MixerConfig& config);

    /// Starts a voice (control thread).
    ///
    /// - Param desc: the voice; its clip must be set.
    /// - Param out: receives the id, usable at once.
    /// - Param block: the block it takes effect at; `kAtLead` stamps the next block plus the lead.
    /// - Returns: `Ok`; `NoClip`, `InvalidParam` (a gain, pan or pitch out of range, a start past the clip); `Full`
    /// when every voice of a full pool outranks this one (ranked by priority, then gain, then age: the lowest is
    /// stopped to make room); `QueueFull`.
    /// - Ownership: the voice keeps the clip alive until it ends or is stopped.
    /// - Fails: reported by return value; `out` is not written then.
    MixerStatus Play(const VoiceDesc& desc, VoiceId* out, u64 block = kAtLead);

    /// Fades a voice out and frees it (control thread).
    ///
    /// - Param id: the voice.
    /// - Param fade_frames: the fade; `kRampFrames` uses `MixerConfig::ramp_frames`, 0 stops at once.
    /// - Param block: as for `Play`.
    /// - Returns: `Ok`, `InvalidVoice` (never made, finished, or already stopping) or `QueueFull`.
    /// - Fails: reported by return value.
    MixerStatus Stop(VoiceId id, u32 fade_frames = kRampFrames, u64 block = kAtLead);

    /// Fades a voice to silence and holds its position (control thread).
    ///
    /// - Param id: the voice.
    /// - Param block: as for `Play`.
    /// - Returns: `Ok`, `InvalidVoice` or `QueueFull`.
    /// - Fails: reported by return value.
    MixerStatus Pause(VoiceId id, u64 block = kAtLead);

    /// Brings a paused voice back (control thread).
    ///
    /// - Param id: the voice.
    /// - Param block: as for `Play`.
    /// - Returns: `Ok`, `InvalidVoice` or `QueueFull`.
    /// - Fails: reported by return value.
    MixerStatus Resume(VoiceId id, u64 block = kAtLead);

    /// Moves a voice to a position (control thread); the jump is not smoothed.
    ///
    /// - Param id: the voice.
    /// - Param frame: the clip frame; clamped to the last frame by the audio thread.
    /// - Param block: as for `Play`.
    /// - Returns: `Ok`, `InvalidVoice` or `QueueFull`.
    /// - Fails: reported by return value.
    MixerStatus Seek(VoiceId id, u64 frame, u64 block = kAtLead);

    /// Changes a voice's gain over the ramp (control thread).
    ///
    /// - Param id: the voice.
    /// - Param gain: 0 to 16.
    /// - Param block: as for `Play`.
    /// - Returns: `Ok`, `InvalidVoice`, `InvalidParam` or `QueueFull`.
    /// - Fails: reported by return value.
    MixerStatus SetGain(VoiceId id, f32 gain, u64 block = kAtLead);

    /// Changes a voice's pan over the ramp (control thread).
    ///
    /// - Param id: the voice.
    /// - Param pan: -1 to 1.
    /// - Param block: as for `Play`.
    /// - Returns: `Ok`, `InvalidVoice`, `InvalidParam` or `QueueFull`.
    /// - Fails: reported by return value.
    MixerStatus SetPan(VoiceId id, f32 pan, u64 block = kAtLead);

    /// Changes a voice's pitch over the ramp (control thread); the position stays continuous.
    ///
    /// - Param id: the voice.
    /// - Param pitch: `kMinPitch` to `kMaxPitch`.
    /// - Param block: as for `Play`.
    /// - Returns: `Ok`, `InvalidVoice`, `InvalidParam` or `QueueFull`.
    /// - Fails: reported by return value.
    MixerStatus SetPitch(VoiceId id, f32 pitch, u64 block = kAtLead);

    /// Turns a voice's looping on or off (control thread).
    ///
    /// - Param id: the voice.
    /// - Param loop: whether it loops.
    /// - Param block: as for `Play`.
    /// - Returns: `Ok`, `InvalidVoice` or `QueueFull`.
    /// - Fails: reported by return value.
    MixerStatus SetLoop(VoiceId id, bool loop, u64 block = kAtLead);

    /// Changes a voice's gain to a target over a number of frames (control thread).
    ///
    /// - Param id: the voice.
    /// - Param gain: the target, 0 to 16.
    /// - Param frames: the length of the fade; 0 changes at once.
    /// - Param block: as for `Play`.
    /// - Returns: `Ok`, `InvalidVoice`, `InvalidParam` or `QueueFull`.
    /// - Fails: reported by return value.
    MixerStatus Fade(VoiceId id, f32 gain, u32 frames, u64 block = kAtLead);

    /// Makes a voice virtual or real again (control thread).
    ///
    /// - Param id: the voice.
    /// - Param is_virtual: whether it only advances.
    /// - Param block: as for `Play`.
    /// - Returns: `Ok`, `InvalidVoice` or `QueueFull`.
    /// - Fails: reported by return value.
    MixerStatus SetVirtual(VoiceId id, bool is_virtual, u64 block = kAtLead);

    /// Whether a voice is still playing, as far as the control side knows (control thread): false once it finished or
    /// a stop was posted.
    ///
    /// - Param id: the voice.
    /// - Returns: true if `id` names a live voice.
    /// - Fails: never.
    bool IsPlaying(VoiceId id);

    /// Takes the events that have arrived (control thread).
    ///
    /// - Param out: receives up to `max` events, oldest first.
    /// - Param max: the capacity of `out`.
    /// - Returns: the number written.
    /// - Ownership: `out` is borrowed for the call.
    /// - Fails: never.
    u32 PollEvents(VoiceEvent* out, u32 max);

    /// Tells an offline renderer that every command with a stamp up to `block` has been posted (control thread).
    ///
    /// - Param block: the highest block whose commands are complete.
    /// - Fails: never.
    void SetWatermark(u64 block);

    /// Creates a bus under a parent (control thread).
    ///
    /// - Param name: 1 to `kMaxBusNameBytes` bytes, unique.
    /// - Param parent: an existing bus; the bus is summed into it.
    /// - Param out: receives the id, usable at once.
    /// - Param block: as for `Play`.
    /// - Ownership: `name` is copied; `out` is borrowed for the call.
    /// - Returns: `Ok`; `InvalidBus` (no such parent); `NoBusSlot` (a bad or taken name, or all buses used);
    /// `QueueFull`.
    /// - Fails: reported by return value; `out` is not written then.
    MixerStatus CreateBus(StringView name, BusId parent, BusId* out, u64 block = kAtLead);

    /// Finds a bus by name (control thread).
    ///
    /// - Param name: the name; `master` is the master bus.
    /// - Returns: the id, or an invalid id if there is none.
    /// - Fails: never.
    BusId FindBus(StringView name) const;

    /// Changes a bus's gain (control thread); the change ramps over a block's first `ramp_frames`.
    ///
    /// - Param bus: the bus.
    /// - Param gain: 0 to 16.
    /// - Param block: as for `Play`.
    /// - Returns: `Ok`, `InvalidBus`, `InvalidParam` or `QueueFull`.
    /// - Fails: reported by return value.
    MixerStatus SetBusGain(BusId bus, f32 gain, u64 block = kAtLead);

    /// Mutes or unmutes a bus (control thread); a mute wins over a solo.
    ///
    /// - Param bus: the bus.
    /// - Param mute: whether to mute.
    /// - Param block: as for `Play`.
    /// - Returns: `Ok`, `InvalidBus` or `QueueFull`.
    /// - Fails: reported by return value.
    MixerStatus SetBusMute(BusId bus, bool mute, u64 block = kAtLead);

    /// Solos or unsolos a bus (control thread): while any bus is soloed, a bus is audible only if it is soloed, an
    /// ancestor of a soloed bus or a descendant of one; the master always is.
    ///
    /// - Param bus: the bus.
    /// - Param solo: whether to solo.
    /// - Param block: as for `Play`.
    /// - Returns: `Ok`, `InvalidBus` or `QueueFull`.
    /// - Fails: reported by return value.
    MixerStatus SetBusSolo(BusId bus, bool solo, u64 block = kAtLead);

    /// Sets one of a bus's sends to an auxiliary bus (control thread).
    ///
    /// - Param bus: the sending bus (not the master).
    /// - Param slot: 0 to `kMaxSends` - 1.
    /// - Param aux: the receiving bus.
    /// - Param gain: the send gain, 0 to 16; 0 turns the send off.
    /// - Param pre_fader: take the signal before the bus's gain, mute and solo.
    /// - Param block: as for `Play`.
    /// - Returns: `Ok`, `InvalidBus`, `InvalidParam`, `Cycle` (the graph would loop) or `QueueFull`.
    /// - Fails: reported by return value.
    MixerStatus SetBusSend(BusId bus, u32 slot, BusId aux, f32 gain, bool pre_fader, u64 block = kAtLead);

    /// Ducks a bus by another bus's level (control thread): while the source's peak is above the threshold the target's
    /// gain falls to `depth` (at once, ramped over the block) and it recovers exponentially after.
    ///
    /// - Param target: the bus that is ducked (not the master).
    /// - Param source: the bus whose level is watched.
    /// - Param depth: the ducked gain, 0 to 1 (`DbToGain(-12)` is about 0.25).
    /// - Param threshold: the source peak that starts a duck, above 0.
    /// - Param release_frames: the time constant of the recovery, in frames.
    /// - Param block: as for `Play`.
    /// - Returns: `Ok`, `InvalidBus`, `InvalidParam`, `Cycle` or `QueueFull`.
    /// - Fails: reported by return value.
    MixerStatus SetDuck(BusId target, BusId source, f32 depth, f32 threshold, f32 release_frames, u64 block = kAtLead);

    /// Turns the level events of a bus on or off (control thread): one `BusLevel` event per block while on.
    ///
    /// - Param bus: the bus.
    /// - Param on: whether to report.
    /// - Param block: as for `Play`.
    /// - Returns: `Ok`, `InvalidBus` or `QueueFull`.
    /// - Fails: reported by return value.
    MixerStatus SetMeter(BusId bus, bool on, u64 block = kAtLead);

    /// Moves a voice to another bus (control thread).
    ///
    /// - Param id: the voice.
    /// - Param bus: the bus.
    /// - Param block: as for `Play`.
    /// - Returns: `Ok`, `InvalidVoice`, `InvalidBus` or `QueueFull`.
    /// - Fails: reported by return value.
    MixerStatus SetVoiceBus(VoiceId id, BusId bus, u64 block = kAtLead);

    /// Changes the send of a voice (control thread).
    ///
    /// - Param id: the voice.
    /// - Param bus: the auxiliary bus.
    /// - Param gain: 0 to 16.
    /// - Param block: as for `Play`.
    /// - Returns: `Ok`, `InvalidVoice`, `InvalidBus`, `InvalidParam` or `QueueFull`.
    /// - Fails: reported by return value.
    MixerStatus SetVoiceSend(VoiceId id, BusId bus, f32 gain, u64 block = kAtLead);

    /// Puts an effect into an insert slot of a bus (control thread). Inserts run in slot order on the bus's sum, before
    /// its pre-fader sends and its fader; they keep running while the bus is silent, so a tail is not cut.
    ///
    /// - Param bus: the bus.
    /// - Param slot: 0 to `kMaxInserts` - 1; an effect already there is bypassed (and kept alive).
    /// - Param effect: an effect made for this mixer's channel count; the mixer takes it over.
    /// - Param block: as for `Play`.
    /// - Ownership: takes `effect`; on a failure it is destroyed with the call.
    /// - Returns: `Ok`, `InvalidBus`, `InvalidParam` (a bad slot or a channel mismatch), `NoEffectSlot` (the mixer owns
    ///   `kMaxEffects` already) or `QueueFull`.
    /// - Fails: reported by return value.
    MixerStatus SetInsert(BusId bus, u32 slot, UniquePtr<Effect> effect, u64 block = kAtLead);

    /// Bypasses an insert slot (control thread); the effect is kept alive until the mixer is destroyed.
    ///
    /// - Param bus: the bus.
    /// - Param slot: the slot.
    /// - Param block: as for `Play`.
    /// - Returns: `Ok`, `InvalidBus`, `InvalidParam` or `QueueFull`.
    /// - Fails: reported by return value.
    MixerStatus ClearInsert(BusId bus, u32 slot, u64 block = kAtLead);

    /// Changes the parameters of the effect in an insert slot (control thread); the effect smooths the change.
    ///
    /// - Param bus: the bus.
    /// - Param slot: the slot, which must hold an effect.
    /// - Param params: a block from the effect's `Make...Params`.
    /// - Param block: as for `Play`.
    /// - Returns: `Ok`, `InvalidBus`, `InvalidParam` (an empty slot) or `QueueFull`.
    /// - Fails: reported by return value.
    MixerStatus SetInsertParams(BusId bus, u32 slot, const EffectParams& params, u64 block = kAtLead);

    /// Turns the master limiter on or off (control thread).
    ///
    /// - Param on: whether it limits (it delays the output by its look-ahead while on).
    /// - Param block: as for `Play`.
    /// - Returns: `Ok` or `QueueFull`.
    /// - Fails: reported by return value.
    MixerStatus SetLimiter(bool on, u64 block = kAtLead);

    /// Reads a voice's position (offline use: the audio side must not be rendering).
    ///
    /// - Param id: the voice.
    /// - Param frames: receives the position in clip frames.
    /// - Returns: `Ok` or `InvalidVoice`.
    /// - Ownership: `frames` is borrowed for the call.
    /// - Fails: reported by return value.
    MixerStatus Position(VoiceId id, f64* frames) const;

    /// Renders one block: applies the commands stamped for it, then mixes `kBlockFrames` frames, interleaved,
    /// overwritten (audio thread).
    ///
    /// - Param out: `kBlockFrames * channels` samples.
    /// - Aborts: `out` is smaller than a block.
    /// - Fails: never; allocates nothing.
    void RenderBlock(Span<f32> out);

    /// Like `RenderBlock`, but an offline mixer first checks the producer's watermark (audio thread).
    ///
    /// - Param out: `kBlockFrames * channels` samples.
    /// - Returns: false (and writes nothing) if this block's commands are not all posted yet.
    /// - Aborts: `out` is smaller than a block.
    /// - Fails: reported by return value.
    bool TryRenderBlock(Span<f32> out);

    /// Renders any number of frames by pulling whole blocks, so the output does not depend on the size asked for
    /// (audio thread).
    ///
    /// - Param out: `frames * channels` samples.
    /// - Param frames: the number of frames.
    /// - Aborts: `out` is smaller than `frames` frames.
    /// - Ownership: `out` is borrowed for the call.
    /// - Fails: never; allocates nothing.
    void Render(f32* out, u32 frames);

    /// The block that the next render will mix (any thread).
    u64 NextBlock() const { return next_block_.Load(MemoryOrder::Acquire); }

    /// The channels of the output layout.
    u32 Channels() const { return channels_; }

    /// The configuration the mixer was built with.
    const MixerConfig& Config() const { return config_; }

    /// The counters (offline use).
    MixerStats Stats() const;

    /// The number of live voices on the audio side (offline use).
    u32 VoiceCount() const { return live_; }

    /// Stamps a command at the next block plus the lead.
    static constexpr u64 kAtLead = 0xFFFFFFFFFFFFFFFFull;
    /// Asks `Stop` to fade over `MixerConfig::ramp_frames`.
    static constexpr u32 kRampFrames = 0xFFFFFFFFu;

private:
    struct Voice
    {
        AudioClipConstPtr clip;
        u64 position = 0;
        u64 step = 0;
        u64 step_from = 0;
        u64 serial = 0;
        u32 generation = 0;
        u32 ramp_len = 0;
        u32 ramp_pos = 0;
        u32 bus = 0;
        u32 send_bus = 0xFFFFFFFFu;
        f32 send_gain = 0.0f;
        f32 gain = 1.0f;
        f32 pan = 0.0f;
        f32 pitch = 1.0f;
        i32 priority = 0;
        bool active = false;
        bool loop = false;
        bool is_virtual = false;
        bool stopping = false;
        bool pausing = false;
        bool paused = false;
        bool looped = false;
        ChannelMatrix route_play;
        ChannelMatrix route;
        ChannelMatrix route_from;
    };

    struct Send
    {
        u32 target = 0xFFFFFFFFu;
        f32 gain = 0.0f;
        bool pre_fader = false;
    };

    // A bus on the audio side.
    struct Bus
    {
        bool active = false;
        bool mute = false;
        bool solo = false;
        bool audible = true;
        bool meter = false;
        u32 parent = 0;
        f32 gain = 1.0f;
        f32 applied = 1.0f;  // the total gain at the end of the last block
        Send sends[kMaxSends];
        u32 duck_source = 0xFFFFFFFFu;
        f32 duck_depth = 1.0f;
        f32 duck_threshold = 1.0f;
        f32 duck_release = 0.0f;
        f32 duck_gain = 1.0f;
        f32 last_peak = 0.0f;
        Effect* inserts[kMaxInserts] = {};
    };

    // What the control side knows of a bus.
    struct ControlBus
    {
        bool active = false;
        u32 parent = 0;
        char name[kMaxBusNameBytes + 1] = {};
        u32 send_target[kMaxSends] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
        u32 duck_source = 0xFFFFFFFFu;
        Effect* inserts[kMaxInserts] = {};
    };

    // What the control side knows of a slot.
    struct ControlSlot
    {
        u32 generation = 0;
        bool active = false;
        bool stopping = false;
        i32 priority = 0;
        f32 gain = 0.0f;
        u64 serial = 0;
    };

    ControlSlot* FindControl(VoiceId id);
    void DrainEvents();
    void AddUserEvent(const VoiceEvent& event);
    u64 StampFor(u64 block) const;
    MixerStatus Post(Command&& command);
    MixerStatus PostFor(VoiceId id, Command&& command);
    void PostEvent(VoiceEventType type, const Voice& voice, bool terminal);
    void ApplyCommands();
    void Apply(Command* command);
    void Finish(Voice* voice, bool report, VoiceEventType type);
    void ComputeTargets(Voice* voice, ChannelMatrix* route, u64* step) const;
    void Retarget(Voice* voice, u32 frames);
    void EffectiveNow(const Voice& voice, ChannelMatrix* route, u64* step) const;
    void MixVoice(Voice* voice, f32* out, f32* send_out);
    bool BusReaches(u32 from, u32 to) const;
    bool ValidBus(BusId bus) const;
    MixerStatus PostBus(Command&& command);
    void RebuildBusOrder();
    void ProcessBuses(f32* out);
    f32* BusBuffer(u32 bus) { return bus_buffers_.Data() + static_cast<usize>(bus) * kBlockFrames * channels_; }
    void AdvanceVirtual(Voice* voice);

    MixerConfig config_;
    u32 channels_ = 0;
    // The audio side.
    u32 live_ = 0;
    u64 block_ = 0;
    MixerStats audio_stats_;
    Vector<Voice> voices_;
    Vector<f32> sinc_;
    Vector<f32> carry_;
    u32 carry_next_ = 0;
    Bus buses_[kMaxBuses];
    u32 bus_order_[kMaxBuses] = {};
    u32 bus_order_count_ = 0;
    Vector<f32> bus_buffers_;
    Limiter limiter_;
    bool limiter_on_ = false;
    f32 limiter_min_gain_ = 1.0f;
    // The control side.
    ControlBus control_buses_[kMaxBuses];
    UniquePtr<Effect> effects_[kMaxEffects];
    u32 effect_count_ = 0;
    Vector<ControlSlot> control_;
    Vector<VoiceEvent> pending_;
    u32 event_ring_capacity_ = 0;
    u64 next_serial_ = 1;
    u64 control_stolen_ = 0;
    u64 control_refused_ = 0;
    u64 control_dropped_ = 0;
    u64 control_events_dropped_ = 0;
    // Shared.
    SpscRing<Command> commands_;
    SpscRing<VoiceEvent> events_;
    Atomic<u64> next_block_{0};
    Atomic<u64> watermark_{0};
    Atomic<u64> events_dropped_audio_{0};
};

/// The mixer configuration the settings ask for.
///
/// - Param settings: the audio settings.
/// - Param sample_rate: the rate the device granted (the setting may be 0).
/// - Returns: a config; `max_voices` is capped at `kMaxVoices`.
/// - Fails: never.
MixerConfig MixerConfigFromSettings(const AudioSettings& settings, u32 sample_rate);

}  // namespace engine_placeholder::audio
