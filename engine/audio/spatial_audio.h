// Sound in the world (Phase 26 Day 428): the system that turns entities with `AudioSource` and the active
// `AudioListener` into mixer voices, choosing by loudness which are worth a real voice.
#pragma once

#include "audio/components.h"
#include "audio/mixer.h"
#include "audio/occlusion.h"
#include "containers/vector.h"
#include "ecs/world.h"
#include "mem/heap_allocator.h"
#include "containers/hash_map.h"
#include "spatial/spatial_set.h"
#include "types.h"

namespace engine_placeholder::audio
{

/// How the system chooses.
///
/// - Thread-safety: thread-compatible plain data.
struct SpatialAudioConfig
{
    /// The most real (mixed) voices the system keeps; the loudest emitters get them, the rest are virtual.
    u32 max_real_voices = 32;
    /// The gain below which an emitter is not worth a voice (it keeps its place as a virtual voice).
    f32 audibility_threshold = 0.001f;
    /// The frames over which a voice of a destroyed entity fades out.
    u32 stop_fade_frames = 512;
    /// The speed of sound in units per second.
    f32 speed_of_sound = 343.0f;
    /// The Doppler factor of the world; 0 turns the effect off.
    f32 doppler_factor = 1.0f;
    /// The air absorption factor of the world (it scales distances in the cutoff table); 0 turns it off.
    f32 air_absorption_factor = 1.0f;
    /// A jump of more than this many units in one update is a teleport, not a speed: it gives no velocity.
    f32 teleport_distance = 50.0f;
    /// How occlusion is tested and heard (used when a query is set).
    OcclusionConfig occlusion;
};

/// The listener as the last update read it.
///
/// - Thread-safety: thread-compatible plain data.
struct ListenerState
{
    /// Whether an active listener was found.
    bool valid = false;
    /// The world position.
    Vec3 position;
    /// The forward direction (unit).
    Vec3 forward = Vec3(0.0f, 0.0f, -1.0f);
    /// The right direction (unit).
    Vec3 right = Vec3(1.0f, 0.0f, 0.0f);
    /// The up direction (unit).
    Vec3 up = Vec3(0.0f, 1.0f, 0.0f);
    /// The velocity in units per second, from the change of position over the last update's `dt`.
    Vec3 velocity;
    /// The listener's gain.
    f32 gain = 1.0f;
};

/// What the last update did.
///
/// - Thread-safety: thread-compatible plain data.
struct SpatialAudioReport
{
    /// Emitters known to the system.
    u32 emitters = 0;
    /// Emitters the listener query found in range and loud enough.
    u32 audible = 0;
    /// Of those, the ones with a real voice.
    u32 real = 0;
    /// The occlusion tests made this update (the cost; at most `OcclusionConfig::max_tests_per_update`).
    u32 occlusion_tests = 0;
    /// The voices started this update.
    u32 started = 0;
    /// The voices stopped because their entity (or component) went.
    u32 stopped = 0;
    /// The emitters whose position or radius changed (their set entry moved).
    u32 moved = 0;
    /// The mixer commands posted (gain, pan, virtual, play, stop).
    u32 commands = 0;
};

/// Places the world's sounds: each update it reads every `AudioSource` with a `WorldTransform` and the active
/// `AudioListener`, keeps a [[spatial::SpatialSet]] of the emitters by their audible radius, asks it which are in
/// range, ranks those by loudness (gain over distance, then priority, then entity id) and gives the loudest
/// `max_real_voices` real voices and the rest virtual ones; gain and pan follow the listener. Only what changed posts a
/// mixer command.
///
/// - Thread-safety: thread-compatible; control thread only (it uses the mixer's control calls).
/// - Ownership: borrows the mixer, which must outlive it; shares each source's clip through the voice.
class SpatialAudio
{
public:
    /// Makes an idle system; `Init` gives it a mixer.
    ///
    /// - Fails: never.
    SpatialAudio();

    /// Starts the system on a mixer.
    ///
    /// - Param mixer: the mixer; borrowed.
    /// - Param config: the limits.
    /// - Fails: never.
    /// - Ownership: borrows the mixer, which must outlive the system.
    void Init(Mixer* mixer, const SpatialAudioConfig& config);

    /// One update: reads the world, posts the voice changes.
    ///
    /// - Param world: the world (read only; no structural change).
    /// - Param dt: the seconds since the last update, for the listener's velocity (0 keeps the old velocity).
    /// - Fails: a full mixer command queue drops commands (counted by the mixer); the next update repeats them.
    void Update(ecs::World& world, f32 dt);

    /// The voice of an emitter, if it has one.
    ///
    /// - Param entity: the entity.
    /// - Returns: the voice id (invalid when none).
    VoiceId VoiceOf(ecs::Entity entity) const;

    /// The listener the last update used.
    ///
    /// - Returns: the listener state.
    const ListenerState& Listener() const { return listener_; }

    /// Sets the occlusion query (null turns occlusion off).
    ///
    /// - Param query: the query; sources with `occlusion` set are tested through it.
    /// - Ownership: borrows the query, which must outlive its use.
    /// - Fails: never.
    void SetOcclusion(OcclusionQuery* query) { occlusion_ = query; }

    /// The smoothed occlusion of an emitter (0 open, 1 blocked).
    ///
    /// - Param entity: the entity.
    /// - Returns: the value, 0 when unknown.
    f32 OcclusionOf(ecs::Entity entity) const;

    /// What the last update did.
    ///
    /// - Returns: the report.
    const SpatialAudioReport& Report() const { return report_; }

    /// Whether `entity` was audible (in range and above the threshold) at the last update.
    ///
    /// - Param entity: the entity.
    /// - Returns: true if audible.
    bool IsAudible(ecs::Entity entity) const;

    /// The pitch last sent for an emitter's voice (its own pitch times the Doppler ratio).
    ///
    /// - Param entity: the entity.
    /// - Returns: the pitch, or 0 when it has no voice.
    f32 PitchOf(ecs::Entity entity) const;

    /// Whether `entity` has a real (mixed, not virtual) voice as of the last update.
    ///
    /// - Param entity: the entity.
    /// - Returns: true if it has a voice that is not virtual.
    bool IsReal(ecs::Entity entity) const;

private:
    struct Emitter
    {
        ecs::Entity entity;
        spatial::SetHandle handle = spatial::kInvalidSetHandle;
        VoiceId voice;
        const void* stream_sent = nullptr;  ///< The stream the voice was started on (a new one restarts the voice).
        Vec3 position;
        f32 radius = -1.0f;
        f32 gain_sent = -1.0f;
        Vec3 velocity;
        f32 pitch_sent = 0.0f;
        f32 air_sent = 1.0f;
        f32 occ_target = 0.0f;
        f32 occ_smooth = 0.0f;
        f32 occ_since = 1e9f;
        bool occ_known = false;
        Vec3 dir_sent = Vec3(0.0f, 0.0f, 0.0f);
        f32 spread_sent = -1.0f;
        bool virtual_sent = false;
        bool finished = false;
        bool flat = false;
        bool audible = false;
        u32 seen = 0;
    };

    struct Candidate
    {
        u32 emitter = 0;
        f32 loudness = 0.0f;
        Vec3 dir = Vec3(0.0f, 0.0f, 1.0f);
        f32 spread = 0.0f;
        f32 pitch = 1.0f;
        f32 air = 1.0f;
        f32 distance = 0.0f;
        bool occludable = false;
        i32 priority = 0;
        u64 key = 0;
    };

    Emitter* Find(ecs::Entity entity);
    void Drop(u32 index);
    void Drive(Emitter* e, const AudioSource& src, const Candidate& c, bool real);
    bool Post(MixerStatus status);

    Mixer* mixer_ = nullptr;
    SpatialAudioConfig config_;
    spatial::SpatialSet set_;
    HashMap<u64, u32, HeapAllocator> index_;
    Vector<Emitter> emitters_;
    Vector<Candidate> candidates_;
    Vector<u32> gone_;
    ListenerState listener_;
    SpatialAudioReport report_;
    OcclusionQuery* occlusion_ = nullptr;
    u64 round_robin_key_ = 0;
    u32 epoch_ = 0;
};

}  // namespace engine_placeholder::audio
