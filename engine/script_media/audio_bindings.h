// The audio bindings (Phase 27 Day 468): scripts play and control sounds and music. A separate target above `script`
// and `audio` (layer 14), like `script_physics`; the host registers the bindings, the command kinds and the services
// below with a [[script::ScriptRuntime]] (docs/guide/scripting.md, "Audio and input").
//
// Everything a script says goes through the partition's command buffer and reaches the mixer at the merge, in the
// merge's fixed order, so the mixer receives the same commands in the same order whatever the thread count: with an
// offline mixer the rendered blocks hash the same. A voice handle is the 64-bit number `audio.play` returns before the
// voice exists (see [[script_media::VoiceTable]]); using the handle of a voice that was stopped or has ended is a
// script error, and `on_voice_end` tells the starting entity when a voice ends.
#pragma once

#include "script/bindings.h"
#include "script/component_catalog.h"
#include "script/merge.h"
#include "script/script_runtime.h"
#include "script/service_slots.h"
#include "script_media/audio_services.h"
#include "storage/component_registry.h"
#include "types.h"

namespace engine_placeholder::script_media
{

/// The command kinds the audio bindings record (ids the other kinds do not use).
///
/// - Thread-safety: thread-compatible plain data.
enum AudioCommandKind : u16
{
    kCommandAudioPlay = 120,      ///< `audio.play`.
    kCommandAudioStop = 121,      ///< `audio.stop`.
    kCommandAudioPause = 122,     ///< `audio.pause` and `audio.resume`.
    kCommandAudioSetGain = 123,   ///< `audio.set_gain`.
    kCommandAudioSetPitch = 124,  ///< `audio.set_pitch`.
    kCommandAudioBusGain = 125,   ///< `audio.set_bus_gain`.
    kCommandMusicPlay = 126,      ///< `audio.music.play` and `crossfade`.
    kCommandMusicStop = 127,      ///< `audio.music.stop`.
};

/// The stored id of the listener component.
///
/// - Thread-safety: thread-compatible plain data.
enum AudioComponentId : u32
{
    kAudioListenerId = 110,  ///< `audio::AudioListener`, name `audio_listener`.
};

/// Registers the `audio_listener` component (`gain`, `active`) in the storage registry.
///
/// `AudioSource` is not registered: it holds shared clip pointers, which a stored component cannot (the host puts clips
/// on entities).
///
/// - Param registry: the registry to add to; not null.
/// - Returns: whether it was added.
/// - Ownership: borrowed for the call.
/// - Fails: returns false when the id or the name is taken.
bool RegisterAudioComponents(storage::ComponentRegistry* registry);

/// Makes `component.set/add` check `audio_listener` (gain from 0 to 4) at the call.
///
/// - Param catalog: the catalog scripts use; not null; call before it is frozen.
/// - Ownership: borrowed for the call.
/// - Fails: never.
void RegisterAudioValidators(script::ComponentCatalog* catalog);

/// Registers the commands that apply the audio writes at the merge (kinds 120 to 127).
///
/// - Param kinds: the runtime's table ([[script::ScriptRuntime::Kinds]]); not null.
/// - Returns: false when an id is taken.
/// - Ownership: borrowed for the call.
/// - Fails: returns false, as described.
bool RegisterAudioCommandKinds(script::CommandKindTable* kinds);

/// Adds the `audio.*` functions.
///
/// The writes are `world-write`, recorded and applied at the merge: `play(clip, options)` returning
/// a handle (`options`: `gain`, `pitch`, `pan`, `bus`, `loop`, `priority`, `fade_in`), `stop(voice, fade_seconds)`,
/// `pause(voice)`, `resume(voice)`, `set_gain(voice, gain)`, `set_pitch(voice, pitch)`, `set_bus_gain(bus, gain)`,
/// `play_music(stream, fade_seconds)`, `crossfade_music(stream, seconds)`, `stop_music(fade_seconds)`; and the reads
/// `is_playing(voice)` and `exists(clip)` (`world-read`). Every argument is given (pass `nil` for no options).
///
/// - Param registry: the registry to add to; not null.
/// - Returns: true when all were added.
/// - Ownership: borrowed for the call.
/// - Fails: returns false when a name is taken.
bool RegisterAudioBindings(script::BindingRegistry* registry);

/// What an event pump did.
///
/// - Thread-safety: thread-compatible plain data.
struct AudioEventReport
{
    /// Mixer events read.
    u32 polled = 0;
    /// `on_voice_end` events put on the bus.
    u32 emitted = 0;
    /// Voices of scripts that ended with no listening handler (nothing was built).
    u32 unhandled = 0;
};

/// The name of the event a voice's end sends to the entity that started it.
inline constexpr const char* kEventVoiceEnd = "on_voice_end";

/// Tells scripts which of their voices ended, from the mixer's events.
///
/// The payload of `on_voice_end` is `voice`
/// (the handle) and `reason` (`"finished"`, `"stopped"` or `"stolen"`). The handle is then forgotten. Call it at the
/// sync point, on the thread that merges, between script ticks. With an offline mixer the events of the blocks rendered
/// so far arrive, in block order; with a device the timing follows the audio thread, so a game that needs repeatable
/// event timing drives an offline mixer.
///
/// - Param mixer: the mixer; its events are consumed.
/// - Param voices: the handle table.
/// - Param runtime: the runtime whose bus receives the events; not null.
/// - Returns: the counts.
/// - Thread-safety: thread-compatible; not while a script tick runs.
/// - Ownership: borrows everything for the call.
/// - Fails: never.
AudioEventReport PumpAudioEvents(audio::Mixer* mixer, VoiceTable* voices, script::ScriptRuntime* runtime);

}  // namespace engine_placeholder::script_media
