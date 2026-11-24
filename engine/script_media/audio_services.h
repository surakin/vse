// What the audio bindings need from the host (Phase 27 Day 468): the mixer, the lookup of clips and music streams by
// name, and the table that turns the handles scripts hold into mixer voices. A host fills an [[AudioScriptServices]]
// and puts its address in `MergeServices::extensions[script::kServiceAudio]`.
#pragma once

#include "audio/clip.h"
#include "audio/mixer.h"
#include "audio/music.h"
#include "audio/spatial_audio.h"
#include "audio/stream.h"
#include "containers/hash_map.h"
#include "containers/vector.h"
#include "ecs/entity.h"
#include "mem/heap_allocator.h"
#include "string_view.h"
#include "types.h"

namespace vse::script_media
{

/// Finds a clip by the name a script uses (`audio.play("explosion")`).
///
/// - Thread-safety: thread-safe; `resolve` is called from every partition at once during the script phase and again at
/// the merge, so it must only read.
/// - Ownership: `user` is borrowed and must outlive the runtime.
struct ClipSource
{
    /// Returns the clip, or a null pointer for an unknown name.
    audio::AudioClipConstPtr (*resolve)(void* user, StringView name) = nullptr;
    /// Passed to `resolve`.
    void* user = nullptr;
};

/// Finds a music stream by name; the stream is already filled (or refilled by the host's streaming job).
///
/// - Thread-safety: thread-safe; `resolve` must only read, as for [[script_media::ClipSource]].
/// - Ownership: `user` is borrowed and must outlive the runtime.
struct StreamSource
{
    /// Returns the stream, or a null pointer for an unknown name.
    audio::StreamBufferPtr (*resolve)(void* user, StringView name) = nullptr;
    /// Passed to `resolve`.
    void* user = nullptr;
};

/// The handles of the voices scripts started.
///
/// A script gets its handle when it calls `audio.play`, before the voice exists: the handle is made of the tick and the
/// partition's reserved placeholder number, so it depends only on the script's own calls (never on the thread count),
/// and the merge binds it to the mixer's [[audio::VoiceId]]. An entry goes when `PumpAudioEvents` reads the voice's end
/// from the mixer, so a host calls that once a frame (a host that never does keeps one entry per voice ever started,
/// and the handles of ended voices stay valid).
///
/// - Thread-safety: thread-compatible; the merge and the event pump change it (one thread at a time, never during the
/// script phase), the script phase only reads.
/// - Ownership: owns the entries.
class VoiceTable
{
public:
    /// What the table keeps for a handle.
    ///
    /// - Thread-safety: thread-compatible plain data.
    struct Entry
    {
        /// The mixer's voice.
        audio::VoiceId voice;
        /// The stable id of the entity whose script started the voice: the receiver of `on_voice_end`.
        u64 owner_key = 0;
        /// Whether a stop was asked: the handle is then an error value for scripts.
        bool stopping = false;
        /// For a placed sound (`audio.play` with `position` or `entity`): the entity whose `AudioSource` plays it; the
        /// invalid entity for a flat voice. `voice` is invalid for a placed sound: the spatial system starts the voice.
        ecs::Entity emitter;
        /// Whether the emitter is an entity made for this sound (`position`): it is destroyed when the sound ends.
        bool transient = false;
    };

    /// Makes an empty table.
    VoiceTable() : by_handle_(HeapAllocator::Default()), by_voice_(HeapAllocator::Default()) {}

    VoiceTable(const VoiceTable&) = delete;
    VoiceTable& operator=(const VoiceTable&) = delete;

    /// Binds a handle to a voice.
    ///
    /// - Param handle: the script's handle.
    /// - Param voice: the mixer's voice.
    /// - Param owner_key: the starting entity's stable id.
    /// - Fails: never; a handle that is already bound is replaced.
    void Add(u64 handle, audio::VoiceId voice, u64 owner_key);

    /// Binds a handle to a placed sound's emitter entity.
    ///
    /// - Param handle: the script's handle.
    /// - Param emitter: the entity that carries the `AudioSource`.
    /// - Param owner_key: the starting entity's stable id.
    /// - Param transient: whether the entity was made for the sound and goes with it.
    /// - Fails: never; a handle that is already bound is replaced.
    void AddPlaced(u64 handle, ecs::Entity emitter, u64 owner_key, bool transient);

    /// The handles of the placed sounds, in ascending order (so that what is done for each is the same on every run).
    ///
    /// - Param out: receives the handles; cleared first.
    /// - Ownership: `out` is borrowed for the call.
    /// - Fails: never.
    void PlacedHandles(Vector<u64>* out) const;

    /// The entry of a handle.
    ///
    /// - Returns: the entry, or null for a handle the table does not hold.
    /// - Ownership: borrowed; valid until the table changes.
    /// - Fails: returns null, as described.
    const Entry* Find(u64 handle) const { return by_handle_.Find(handle); }

    /// Marks a handle as stopping (scripts then get an error for it).
    ///
    /// - Returns: false for no such handle.
    /// - Fails: returns false, as described.
    bool MarkStopping(u64 handle);

    /// The handle of a mixer voice.
    ///
    /// - Returns: the handle, or 0 when the voice is none of the table's.
    /// - Fails: returns 0, as described.
    u64 HandleOf(audio::VoiceId voice) const;

    /// Forgets a handle.
    ///
    /// - Returns: whether it was held.
    /// - Fails: returns false, as described.
    bool Remove(u64 handle);

    /// How many handles are held.
    ///
    /// - Fails: never.
    usize Size() const { return by_handle_.Size(); }

private:
    static u64 Pack(audio::VoiceId voice) { return (u64{voice.generation} << 32) | voice.index; }

    HashMap<u64, Entry, HeapAllocator> by_handle_;
    HashMap<u64, u64, HeapAllocator> by_voice_;
};

/// The services of the audio bindings.
///
/// - Thread-safety: thread-compatible plain data; scripts read it during the script phase, the merge uses the mixer.
/// - Ownership: borrows everything; it must outlive the runtime.
struct AudioScriptServices
{
    /// The mixer scripts' commands go to; not null.
    audio::Mixer* mixer = nullptr;
    /// The spatial system that plays `AudioSource` entities; null means `audio.play` refuses `position` and `entity`.
    /// The host calls `PumpPlacedSounds` once a frame after `SpatialAudio::Update`.
    audio::SpatialAudio* spatial = nullptr;
    /// Clips by name; `resolve` null means `audio.play` is refused.
    ClipSource clips;
    /// Music streams by name; `resolve` null means `audio.music.*` is refused.
    StreamSource streams;
    /// The player behind `audio.music.*`; null when the host has no music.
    audio::MusicPlayer* music = nullptr;
    /// The handles; not null.
    VoiceTable* voices = nullptr;
    /// The mixer's output rate in Hz, to turn the seconds scripts give (fades) into frames.
    u32 sample_rate = 48000;
};

}  // namespace vse::script_media
