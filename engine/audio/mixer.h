// The mixer (Phase 26 Day 420): voices, the resampler, gain and pan, rendered in fixed blocks. Buses, the command queue
// and effects come on the next days; here a voice is played, changed and stopped by direct calls from the control
// thread, never while a block renders.
#pragma once

#include "audio/audio_settings.h"
#include "audio/clip.h"
#include "audio/convert.h"
#include "containers/vector.h"
#include "types.h"

namespace engine_placeholder::audio
{

/// The frames of one mixer block: the unit the mixer renders, whatever size the device calls back with.
inline constexpr u32 kBlockFrames = 256;

/// The most voices a mixer tracks.
inline constexpr u32 kMaxVoices = 4096;

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
    /// The heap is exhausted.
    OutOfMemory,
};

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
};

/// The mixer: a voice pool and the block renderer.
///
/// Positions are 32.32 fixed point in `u64`, so a long clip does not drift; the interpolation (`+ - * /` only) is
/// linear, 4-point Hermite or 16-tap windowed sinc. Voices are summed in slot order, never thread order. The output is
/// not clipped: the master limiter and the device boundary do that (Day 422). A clip's channels are routed to the
/// output layout: a mono clip pans over front left and right (equal power, centre gain 0.7071), a stereo clip on a
/// layout with front left and right balances (centre gain 1), any other pair goes through the conversion table of
/// `audio/convert.h`.
///
/// - Thread-safety: thread-compatible; control calls and `RenderBlock` must not overlap until the command queue
/// (Day 421) arrives.
/// - Ownership: owns the voice pool and each voice holds its clip.
class Mixer
{
public:
    Mixer() = default;
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;

    /// Builds the voice pool and the tables; allocates here and never again.
    ///
    /// - Param config: the configuration.
    /// - Returns: `Ok`, `InvalidConfig` or `OutOfMemory`.
    /// - Fails: reported by return value.
    MixerStatus Init(const MixerConfig& config);

    /// Starts a voice.
    ///
    /// - Param desc: the voice; its clip must be set.
    /// - Param out: receives the id.
    /// - Returns: `Ok`; `NoClip`, `InvalidParam` (a gain, pan or pitch out of range, a start past the clip); `Full`
    /// when every voice of a full pool outranks this one (ranked by priority, then gain, then age: the lowest is
    /// stopped to make room).
    /// - Ownership: the voice keeps the clip alive until it ends or is stopped.
    /// - Fails: reported by return value; `out` is not written then.
    MixerStatus Play(const VoiceDesc& desc, VoiceId* out);

    /// Stops a voice and releases its clip.
    ///
    /// - Param id: the voice.
    /// - Returns: `Ok` or `InvalidVoice`.
    /// - Fails: reported by return value.
    MixerStatus Stop(VoiceId id);

    /// Whether a voice is still playing.
    ///
    /// - Param id: the voice.
    /// - Returns: true if `id` names a live voice.
    /// - Fails: never.
    bool IsPlaying(VoiceId id) const;

    /// Changes a voice's gain.
    ///
    /// - Param id: the voice.
    /// - Param gain: 0 to 16.
    /// - Returns: `Ok`, `InvalidVoice` or `InvalidParam`.
    /// - Fails: reported by return value.
    MixerStatus SetGain(VoiceId id, f32 gain);

    /// Changes a voice's pan.
    ///
    /// - Param id: the voice.
    /// - Param pan: -1 to 1.
    /// - Returns: `Ok`, `InvalidVoice` or `InvalidParam`.
    /// - Fails: reported by return value.
    MixerStatus SetPan(VoiceId id, f32 pan);

    /// Changes a voice's pitch; the position stays continuous, so there is no click.
    ///
    /// - Param id: the voice.
    /// - Param pitch: `kMinPitch` to `kMaxPitch`.
    /// - Returns: `Ok`, `InvalidVoice` or `InvalidParam`.
    /// - Fails: reported by return value.
    MixerStatus SetPitch(VoiceId id, f32 pitch);

    /// Makes a voice virtual or real again.
    ///
    /// - Param id: the voice.
    /// - Param is_virtual: whether it only advances.
    /// - Returns: `Ok` or `InvalidVoice`.
    /// - Fails: reported by return value.
    MixerStatus SetVirtual(VoiceId id, bool is_virtual);

    /// Reads a voice's position.
    ///
    /// - Param id: the voice.
    /// - Param frames: receives the position in clip frames.
    /// - Returns: `Ok` or `InvalidVoice`.
    /// - Ownership: `frames` is borrowed for the call.
    /// - Fails: reported by return value.
    MixerStatus Position(VoiceId id, f64* frames) const;

    /// Renders one block: `kBlockFrames` frames, interleaved, overwritten.
    ///
    /// - Param out: `kBlockFrames * channels` samples.
    /// - Aborts: `out` is smaller than a block.
    /// - Fails: never; allocates nothing.
    void RenderBlock(Span<f32> out);

    /// Renders any number of frames by pulling whole blocks, so the output does not depend on the size asked for.
    ///
    /// - Param out: `frames * channels` samples.
    /// - Param frames: the number of frames.
    /// - Aborts: `out` is smaller than `frames` frames.
    /// - Ownership: `out` is borrowed for the call.
    /// - Fails: never; allocates nothing.
    void Render(f32* out, u32 frames);

    /// The channels of the output layout.
    u32 Channels() const { return channels_; }

    /// The configuration the mixer was built with.
    const MixerConfig& Config() const { return config_; }

    /// The counters.
    const MixerStats& Stats() const { return stats_; }

    /// The number of live voices.
    u32 VoiceCount() const { return live_; }

private:
    struct Voice
    {
        AudioClipConstPtr clip;
        u64 position = 0;
        u64 step = 0;
        u64 serial = 0;
        u32 generation = 1;
        f32 gain = 1.0f;
        f32 pan = 0.0f;
        f32 pitch = 1.0f;
        i32 priority = 0;
        bool active = false;
        bool loop = false;
        bool is_virtual = false;
        ChannelMatrix route;
    };

    MixerStatus Find(VoiceId id, Voice** voice);
    MixerStatus FindConst(VoiceId id, const Voice** voice) const;
    void Finish(Voice* voice);
    void Setup(Voice* voice) const;
    void MixVoice(Voice* voice, f32* out);
    void AdvanceVirtual(Voice* voice);

    MixerConfig config_;
    u32 channels_ = 0;
    u32 live_ = 0;
    u64 next_serial_ = 1;
    MixerStats stats_;
    Vector<Voice> voices_;
    Vector<f32> sinc_;
    Vector<f32> carry_;
    u32 carry_next_ = 0;
};

/// The mixer configuration the settings ask for.
///
/// - Param settings: the audio settings.
/// - Param sample_rate: the rate the device granted (the setting may be 0).
/// - Returns: a config; `max_voices` is capped at `kMaxVoices`.
/// - Fails: never.
MixerConfig MixerConfigFromSettings(const AudioSettings& settings, u32 sample_rate);

}  // namespace engine_placeholder::audio
