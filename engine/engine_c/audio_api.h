#pragma once
//! The audio half of the C API (Phase 26 Day 438): the host-driven mixer of an engine and its table functions. Internal
//! to the module `engine_c`. The mixer has no device and no thread; the host renders it from its own audio callback.
#include "audio/mixer.h"
#include "audio/music.h"
#include "audio/stream.h"
#include "containers/shared_ptr.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "engine/engine_c.h"
#include "sync/atomic.h"
#include "sync/mutex.h"
#include "types.h"

namespace vse::engine_c
{

/// What `audio_open` makes: a mixer, the clip table and the lock of the control side.
///
/// - Thread-safety: the control functions take `mutex`; `Render` takes none and is for one thread at a time.
/// - Ownership: owned by the runtime of the engine.
class AudioHost
{
public:
    /// The control side's lock: every table function except `audio_render` holds it.
    Mutex mutex;
    /// The mixer; its control calls are made under `mutex`, `Render` by the audio thread without it.
    audio::Mixer mixer;
    /// A clip of the table.
    struct ClipSlot
    {
        audio::AudioClipConstPtr clip;
        u32 generation = 0;
    };
    /// The clips, by slot.
    Vector<ClipSlot> clips;
    /// The generation the next clip gets.
    u32 next_generation = 1;
    /// A stream of the table.
    struct StreamSlot
    {
        audio::StreamBufferPtr buffer;
        u32 generation = 0;
    };
    /// The streams, by slot.
    Vector<StreamSlot> streams;
    /// The generation the next stream gets.
    u32 next_stream_generation = 1;
    /// The pump that refills the streams on the engine's job threads (made by the first stream).
    UniquePtr<audio::StreamPump> pump;
    /// The music player (made by the first music call, on that call's bus).
    UniquePtr<audio::MusicPlayer> music;
    /// The bus the player plays on.
    u32 music_bus = 0;
    /// What each insert slot holds (an `ENGINE_AUDIO_EFFECT_*` kind) and the size of a reverb: the mixer keeps every
    /// effect it was given, so a call that changes only the numbers of the effect in a slot retunes it instead of
    /// using another of the mixer's limited effect slots.
    u8 insert_kind[audio::kMaxBuses][audio::kMaxInserts] = {};
    f32 insert_size[audio::kMaxBuses][audio::kMaxInserts] = {};
};

class Runtime;

/// The clip a clip handle of `audio_clip_create` names (for an audio source component).
///
/// - Param runtime: the engine's runtime.
/// - Param handle: the clip handle; 0 gives a null clip and `ENGINE_OK`.
/// - Param out: receives the clip.
/// - Returns: `ENGINE_OK`, `ENGINE_ERROR_NOT_READY` (audio is not open) or `ENGINE_ERROR_STALE_HANDLE`.
/// - Thread-safety: thread-safe.
engine_status AudioClipOfHandle(Runtime& runtime, u64 handle, audio::AudioClipConstPtr* out);

/// The handle of a clip of the table, 0 when it is not in it (or audio is closed).
///
/// - Thread-safety: thread-safe.
u64 AudioHandleOfClip(Runtime& runtime, const audio::AudioClipConstPtr& clip);

/// The stream buffer a stream handle of `audio_stream_open` and its siblings names (for an audio source component).
///
/// - Param handle: the stream handle; 0 gives a null buffer and `ENGINE_OK`.
/// - Returns: `ENGINE_OK`, `ENGINE_ERROR_NOT_READY` (audio is not open) or `ENGINE_ERROR_STALE_HANDLE`.
/// - Thread-safety: thread-safe.
engine_status AudioStreamOfHandle(Runtime& runtime, u64 handle, audio::StreamBufferPtr* out);

/// The handle of a stream of the table, 0 when it is not in it (or audio is closed).
///
/// - Thread-safety: thread-safe.
u64 AudioHandleOfStream(Runtime& runtime, const audio::StreamBufferPtr& stream);

engine_status AudioOpenFn(engine_handle engine, const engine_audio_info* info);
engine_status AudioCloseFn(engine_handle engine);
engine_status AudioClipCreateFn(engine_handle engine, const engine_audio_clip_info* info, const void* data, size_t size,
                                u64* out_clip);
engine_status AudioClipDestroyFn(engine_handle engine, u64 clip);
engine_status AudioPlayFn(engine_handle engine, const engine_audio_play* desc, u64* out_voice);
engine_status AudioVoiceSetFn(engine_handle engine, u64 voice, const engine_audio_voice_params* params);
engine_status AudioVoiceStopFn(engine_handle engine, u64 voice, u32 fade_frames);
engine_status AudioBusCreateFn(engine_handle engine, engine_string name, u32 parent, u32* out_bus);
engine_status AudioBusSetFn(engine_handle engine, u32 bus, const engine_audio_bus_params* params);
engine_status AudioRenderFn(engine_handle engine, u32 frames, float* out);
engine_status AudioEventsFn(engine_handle engine, engine_audio_event* out, size_t capacity, size_t* total);
engine_status AudioStateFn(engine_handle engine, engine_audio_state* out);
engine_status AudioStreamOpenClipFn(engine_handle engine, u64 clip, u64* out_stream);
engine_status AudioStreamOpenFn(engine_handle engine, const engine_audio_stream_source* source, u64* out_stream);
engine_status AudioStreamOpenPushFn(engine_handle engine, const engine_audio_push_info* info, u64* out_stream);
engine_status AudioStreamPushFn(engine_handle engine, u64 stream, const float* data, u32 frames, u32* accepted);
engine_status AudioStreamPushI16Fn(engine_handle engine, u64 stream, const i16* data, u32 frames, u32* accepted);
engine_status AudioStreamStatsFn(engine_handle engine, u64 stream, engine_audio_stream_stats* out);
engine_status AudioStreamCloseFn(engine_handle engine, u64 stream);
engine_status AudioStreamPlayFn(engine_handle engine, u64 stream, const engine_audio_play* desc, u64* out_voice);
engine_status AudioUpdateFn(engine_handle engine, u32 wait);
engine_status AudioMusicPlayFn(engine_handle engine, u64 stream, u32 bus, u32 fade_frames,
                               const engine_audio_tempo* tempo);
engine_status AudioMusicCrossfadeFn(engine_handle engine, u64 stream, u32 fade_frames, u32 at_bar,
                                    const engine_audio_tempo* tempo);
engine_status AudioMusicStopFn(engine_handle engine, u32 fade_frames);
engine_status AudioMusicLayersFn(engine_handle engine, const u64* streams, const float* gains, u32 count, u32 bus);
engine_status AudioMusicLayerGainFn(engine_handle engine, u32 layer, float gain, u32 frames);
engine_status AudioMusicStingerFn(engine_handle engine, u64 clip, float gain, u32 on_beat, u64* out_frame);
engine_status AudioBusEffectSetFn(engine_handle engine, u32 bus, u32 slot, const engine_audio_effect* effect);
engine_status AudioBusSendFn(engine_handle engine, u32 bus, u32 slot, u32 aux, float gain, u32 pre_fader);
engine_status AudioBusDuckFn(engine_handle engine, u32 target, u32 source, float depth, float threshold,
                             float release_frames);
engine_status AudioSnapshotFn(engine_handle engine, engine_buffer* out);
engine_status AudioRestoreFn(engine_handle engine, const void* data, size_t size);

}  // namespace vse::engine_c
