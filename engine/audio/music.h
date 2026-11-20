// The music system (Phase 26 Day 436): what a game does with music, on the audio clock. A `MusicPlayer` drives mixer
// voices that read stream buffers: cross-fades with complementary equal-power gains, transitions at the bar of a tempo,
// layers that start on one frame and stay in sync, stingers on the next beat and playlists whose tracks join with no
// gap. Every time is a frame of the audio clock (`NextBlock * kBlockFrames`), so a replay reproduces every transition
// at the same sample.
//
// The mixer applies commands in order and a later-stamped command holds up the ones behind it, so the player keeps its
// future actions itself and posts each one only when its block is due: call `Update` at least once per block's worth of
// time.
#pragma once

#include "audio/mixer.h"
#include "audio/stream.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "types.h"

namespace vse::audio
{

/// Asks for the earliest frame a command can take effect.
inline constexpr u64 kMusicNow = 0xFFFFFFFFFFFFFFFFull;

/// A track's tempo, for the transitions that wait for a beat or a bar.
///
/// - Thread-safety: thread-compatible plain data.
struct Tempo
{
    /// Beats per minute, above 0.
    f64 beats_per_minute = 120.0;
    /// Beats in a bar, at least 1.
    u32 beats_per_bar = 4;
};

/// The order a playlist plays in: `0 .. count - 1`, or a shuffle of them that depends only on `seed`.
///
/// - Param count: the number of tracks.
/// - Param seed: the shuffle seed; the same seed gives the same order on every machine.
/// - Param shuffle: false for the natural order.
/// - Param out: receives `count` indices.
/// - Fails: never.
/// - Ownership: `out` is borrowed for the call.
void PlaylistOrder(u32 count, u64 seed, bool shuffle, Vector<u32>* out);

/// Plays music through a mixer bus.
///
/// - Thread-safety: thread-compatible; the control thread only.
/// - Ownership: borrows the mixer, which must outlive the player; shares the stream buffers.
class MusicPlayer
{
public:
    /// Makes a player.
    ///
    /// - Param mixer: the mixer; borrowed.
    /// - Param bus: the bus the music plays on.
    /// - Fails: never.
    /// - Ownership: borrows `mixer`.
    MusicPlayer(Mixer* mixer, BusId bus);

    /// The first frame a command posted now can take effect.
    ///
    /// - Returns: the frame of the audio clock.
    u64 EarliestFrame() const;

    /// Starts a track as the current one at `at`, fading in with a quarter-sine over `fade_in_frames` (0 starts at full
    /// level). Stops nothing: use `CrossFadeTo` to replace a playing track.
    ///
    /// - Param track: the stream, already refilled; shared.
    /// - Param fade_in_frames: the fade length.
    /// - Param at: the frame the first sample sounds, or `kMusicNow`.
    /// - Param tempo: the track's tempo.
    /// - Returns: `Ok`, or what the mixer refused.
    /// - Fails: reported by return value.
    MixerStatus Play(const StreamBufferPtr& track, u32 fade_in_frames, u64 at = kMusicNow,
                     const Tempo& tempo = Tempo());

    /// Replaces the current track with `track`: the old one fades out with the cosine, the new one in with the sine, so
    /// the powers add to one at every frame.
    ///
    /// - Param track: the stream, already refilled; shared.
    /// - Param frames: the cross-fade length.
    /// - Param at: the frame the new track's first sample sounds, or `kMusicNow`.
    /// - Param tempo: the new track's tempo.
    /// - Returns: `Ok`, or what the mixer refused.
    /// - Fails: reported by return value.
    MixerStatus CrossFadeTo(const StreamBufferPtr& track, u32 frames, u64 at = kMusicNow, const Tempo& tempo = Tempo());

    /// As `CrossFadeTo`, at the next bar boundary of the current track.
    ///
    /// - Param track: the stream, already refilled; shared.
    /// - Param frames: the cross-fade length.
    /// - Param tempo: the new track's tempo.
    /// - Returns: `Ok`, or what the mixer refused.
    /// - Fails: reported by return value.
    MixerStatus CrossFadeAtBar(const StreamBufferPtr& track, u32 frames, const Tempo& tempo = Tempo());

    /// Fades the current track out and stops it.
    ///
    /// - Param fade_out_frames: the fade length (0 stops at once).
    /// - Param at: the frame the fade starts, or `kMusicNow`.
    void Stop(u32 fade_out_frames, u64 at = kMusicNow);

    /// The first beat boundary of the current track at or after `EarliestFrame`.
    ///
    /// - Returns: the frame, or 0 when nothing plays.
    u64 NextBeatFrame() const;

    /// The first bar boundary of the current track at or after `EarliestFrame`.
    ///
    /// - Returns: the frame, or 0 when nothing plays.
    u64 NextBarFrame() const;

    /// Starts a layered track: every layer begins on the same frame and advances by the same frames, with its own gain.
    ///
    /// - Param layers: the streams, already refilled.
    /// - Param gains: one starting gain per layer.
    /// - Param at: the common start frame, or `kMusicNow`.
    /// - Returns: `Ok`, `InvalidParam` for a mismatch or more than eight layers, or what the mixer refused.
    /// - Fails: reported by return value.
    MixerStatus PlayLayers(Span<const StreamBufferPtr> layers, Span<const f32> gains, u64 at = kMusicNow);

    /// Moves one layer's gain to `gain` over `frames` (a linear ramp), starting at `at`.
    ///
    /// - Param layer: the index given to `PlayLayers`.
    /// - Param gain: the target.
    /// - Param frames: the ramp length.
    /// - Param at: the frame the ramp starts, or `kMusicNow`.
    void SetLayerGain(u32 layer, f32 gain, u32 frames, u64 at = kMusicNow);

    /// Moves every layer to `frame` on one block, so they stay in step.
    ///
    /// - Param frame: the position in frames of the layers.
    /// - Param at: the frame the seek takes effect, or `kMusicNow`.
    void SeekLayers(u64 frame, u64 at = kMusicNow);

    /// Plays a short clip over the music, at the next beat of the current track or at once.
    ///
    /// - Param clip: the clip.
    /// - Param gain: the gain.
    /// - Param on_beat: whether to wait for the next beat.
    /// - Param start_frame: receives the frame the stinger sounds at.
    /// - Returns: `Ok`, or what the mixer refused.
    /// - Fails: reported by return value.
    /// - Ownership: shares `clip`; `start_frame` is borrowed for the call.
    MixerStatus PlayStinger(const AudioClipConstPtr& clip, f32 gain, bool on_beat, u64* start_frame);

    /// Ducks `music_bus` while `dialogue_bus` is loud (the mixer's ducker).
    ///
    /// - Param dialogue_bus: the bus that ducks the music.
    /// - Param depth: how far the music falls, 0 to 1.
    /// - Param threshold: the dialogue level that starts it.
    /// - Param release_frames: how long the music takes to return.
    /// - Returns: `Ok`, or what the mixer refused.
    MixerStatus DuckForDialogue(BusId dialogue_bus, f32 depth, f32 threshold, f32 release_frames);

    /// Plays tracks one after another in the playlist order, each starting on the frame the last one ends (for tracks
    /// that state their length; the others start when the last has finished).
    ///
    /// - Param tracks: the streams, shared; each plays once.
    /// - Param seed: the shuffle seed.
    /// - Param shuffle: false for the given order.
    /// - Param at: the first track's start frame, or `kMusicNow`.
    /// - Returns: `Ok`, or what the mixer refused.
    MixerStatus PlayPlaylist(Span<const StreamBufferPtr> tracks, u64 seed, bool shuffle, u64 at = kMusicNow);

    /// Posts every action that is due; call at least once per block's worth of time.
    void Update();

    /// The frame the current track started at.
    ///
    /// - Returns: the frame, 0 when nothing plays.
    u64 CurrentStartFrame() const;

    /// The track index (into the playlist's order) now playing, or -1.
    ///
    /// - Returns: the index.
    i32 PlaylistPosition() const { return playlist_position_; }

private:
    struct Slot
    {
        VoiceId id = {0xFFFFFFFFu, 0};
        StreamBufferPtr stream;
        AudioClipConstPtr clip;
        u64 start_frame = 0;
        Tempo tempo;
        bool live = false;
        bool started = false;
    };

    enum class Kind : u8
    {
        Start,
        Fade,
        Stop,
        Seek
    };

    struct Action
    {
        Kind kind = Kind::Start;
        u64 block = 0;
        u32 slot = 0;
        f32 gain = 0.0f;
        u32 frames = 0;
        u64 frame = 0;
        bool loop = false;
    };

    u64 Resolve(u64 at) const;
    u32 NewSlot();
    void Queue(const Action& action);
    void ScheduleFade(u32 slot, u64 start_frame, u32 frames, bool rising);
    MixerStatus StartTrack(const StreamBufferPtr& track, u64 start, u32 fade_in, const Tempo& tempo, u32* slot);
    void Post(const Action& action);
    u64 NextBoundary(u32 divisor_beats) const;

    Mixer* mixer_;
    BusId bus_;
    Vector<Slot> slots_;
    Vector<Action> pending_;
    i32 current_ = -1;
    Vector<u32> layer_slots_;
    Vector<StreamBufferPtr> playlist_;
    Vector<u32> order_;
    u32 next_in_order_ = 0;
    i32 playlist_position_ = -1;
};

}  // namespace vse::audio
