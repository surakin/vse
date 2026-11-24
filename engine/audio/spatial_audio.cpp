#include "audio/spatial_audio.h"

#include "algorithm.h"
#include "ecs/query.h"
#include "ecs/transform.h"
#include "lang/scalar.h"
#include "spatial/query.h"

namespace vse::audio
{

namespace
{

using ecs::Entity;
using ecs::Read;

Vec3 Column(const Mat4& m, int c)
{
    return Vec3(m.col[c].x, m.col[c].y, m.col[c].z);
}

Vec3 UnitOr(const Vec3& v, const Vec3& fallback)
{
    const f32 l2 = v.LengthSquared();
    return l2 > 1e-12f ? v * (1.0f / Sqrt(l2)) : fallback;
}

bool SameVec(const Vec3& a, const Vec3& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool SameVec(const Vec3d& a, const Vec3d& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool Finite(const Vec3d& v)
{
    return IsFinite(v.x) && IsFinite(v.y) && IsFinite(v.z);
}

}  // namespace

SpatialAudio::SpatialAudio() : index_(HeapAllocator::Default()) {}

void SpatialAudio::Init(Mixer* mixer, const SpatialAudioConfig& config)
{
    mixer_ = mixer;
    config_ = config;
}

SpatialAudio::Emitter* SpatialAudio::Find(Entity entity)
{
    const u32* slot = index_.Find(entity.ToU64());
    return slot != nullptr ? &emitters_[*slot] : nullptr;
}

VoiceId SpatialAudio::VoiceOf(Entity entity) const
{
    const u32* slot = index_.Find(entity.ToU64());
    return slot != nullptr ? emitters_[*slot].voice : VoiceId();
}

bool SpatialAudio::IsKnown(Entity entity) const
{
    return index_.Find(entity.ToU64()) != nullptr;
}

bool SpatialAudio::IsFinished(Entity entity) const
{
    const u32* slot = index_.Find(entity.ToU64());
    return slot != nullptr && emitters_[*slot].finished;
}

bool SpatialAudio::IsAudible(Entity entity) const
{
    const u32* slot = index_.Find(entity.ToU64());
    return slot != nullptr && emitters_[*slot].audible;
}

f32 SpatialAudio::OcclusionOf(Entity entity) const
{
    const u32* slot = index_.Find(entity.ToU64());
    return slot != nullptr ? emitters_[*slot].occ_smooth : 0.0f;
}

f32 SpatialAudio::PitchOf(Entity entity) const
{
    const u32* slot = index_.Find(entity.ToU64());
    return slot != nullptr && emitters_[*slot].voice.IsValid() ? emitters_[*slot].pitch_sent : 0.0f;
}

bool SpatialAudio::IsReal(Entity entity) const
{
    const u32* slot = index_.Find(entity.ToU64());
    return slot != nullptr && emitters_[*slot].voice.IsValid() && !emitters_[*slot].virtual_sent;
}

// Removes the emitter at `index` from the set, the map and the vector (swap with the last).
void SpatialAudio::Drop(u32 index)
{
    Emitter& e = emitters_[index];
    if (e.handle != spatial::kInvalidSetHandle)
    {
        set_.Remove(e.handle);
    }
    if (e.voice.IsValid())
    {
        Post(mixer_->Stop(e.voice, config_.stop_fade_frames));
        report_.stopped += 1;
    }
    index_.Remove(e.entity.ToU64());
    const u32 last = static_cast<u32>(emitters_.Size()) - 1;
    if (index != last)
    {
        emitters_[index] = emitters_[last];
        index_.Insert(emitters_[index].entity.ToU64(), index);
    }
    emitters_.Pop();
}

// Counts a command and tells whether the mixer took it: a refused one (a full queue) must be sent again next update, so
// the caller records what it sent only when this is true.
bool SpatialAudio::Post(MixerStatus status)
{
    report_.commands += 1;
    return status == MixerStatus::Ok;
}

// Starts or updates the voice of an emitter that has been ranked.
void SpatialAudio::Drive(Emitter* e, const AudioSource& src, const Candidate& c, bool real)
{
    if (e->voice.IsValid() && e->stream_sent != static_cast<const void*>(src.stream.Get()))
    {
        // Another stream was set: the voice reads the old one, so it stops and the new one starts below.
        Post(mixer_->Stop(e->voice, config_.stop_fade_frames));
        report_.stopped += 1;
        e->voice = VoiceId();
        e->finished = false;
    }
    if (!e->voice.IsValid())
    {
        if (e->finished || !src.play_on_start || (src.clip == nullptr && !src.stream) ||
            (!real && !src.loop && !src.stream))
        {
            return;
        }
        VoiceDesc desc;
        desc.clip = src.stream ? src.stream->Clip() : src.clip;
        desc.stream = src.stream;
        desc.gain = c.loudness;
        desc.directional = src.spatial;
        desc.direction_x = c.dir.x;
        desc.direction_y = c.dir.y;
        desc.direction_z = c.dir.z;
        desc.spread = c.spread;
        desc.pitch = Clamp(src.pitch * c.pitch, kMinPitch, kMaxPitch);
        desc.air_coefficient = c.air;
        desc.loop = src.loop;
        desc.is_virtual = !real;
        desc.priority = src.priority;
        desc.bus = src.bus;
        VoiceId id;
        if (mixer_->Play(desc, &id) == MixerStatus::Ok)
        {
            e->voice = id;
            e->stream_sent = src.stream.Get();
            e->gain_sent = c.loudness;
            e->dir_sent = c.dir;
            e->spread_sent = c.spread;
            e->pitch_sent = desc.pitch;
            e->air_sent = c.air;
            e->virtual_sent = !real;
            report_.started += 1;
            report_.commands += 1;
        }
        return;
    }
    if (e->virtual_sent == real && Post(mixer_->SetVirtual(e->voice, !real)))
    {
        e->virtual_sent = !real;
    }
    if (e->gain_sent != c.loudness && Post(mixer_->SetGain(e->voice, c.loudness)))
    {
        e->gain_sent = c.loudness;
    }
    const f32 pitch = Clamp(src.pitch * c.pitch, kMinPitch, kMaxPitch);
    if (e->pitch_sent != pitch && Post(mixer_->SetPitch(e->voice, pitch)))
    {
        e->pitch_sent = pitch;
    }
    if (e->air_sent != c.air && Post(mixer_->SetAirAbsorption(e->voice, c.air)))
    {
        e->air_sent = c.air;
    }
    if (src.spatial && (!SameVec(e->dir_sent, c.dir) || e->spread_sent != c.spread) &&
        Post(mixer_->SetDirection(e->voice, c.dir.x, c.dir.y, c.dir.z, c.spread)))
    {
        e->dir_sent = c.dir;
        e->spread_sent = c.spread;
    }
}

void SpatialAudio::Update(ecs::World& world, f32 dt)
{
    report_ = SpatialAudioReport();
    epoch_ += 1;

    // The listener: the active one with the lowest entity id.
    ListenerState previous = listener_;
    listener_.valid = false;
    Entity best;
    world.Query<Read<AudioListener>, Read<ecs::WorldTransform>>().ForEach(
        [&](Entity e, const AudioListener& l, const ecs::WorldTransform& t)
        {
            if (!l.active || (listener_.valid && !(e < best)))
            {
                return;
            }
            best = e;
            listener_.valid = true;
            listener_.position = t.position;
            listener_.right = UnitOr(Column(t.matrix, 0), Vec3(1.0f, 0.0f, 0.0f));
            listener_.up = UnitOr(Column(t.matrix, 1), Vec3(0.0f, 1.0f, 0.0f));
            listener_.forward = UnitOr(Column(t.matrix, 2) * -1.0f, Vec3(0.0f, 0.0f, -1.0f));
            listener_.gain = l.gain;
        });
    if (listener_.valid && !Finite(listener_.position))
    {
        listener_.valid = false;
    }
    listener_.velocity = Vec3(0.0f, 0.0f, 0.0f);
    if (listener_.valid && previous.valid && dt > 0.0f)
    {
        const Vec3 jump = (listener_.position - previous.position).Narrow();
        listener_.velocity = jump.Length() > config_.teleport_distance ? Vec3(0.0f, 0.0f, 0.0f) : jump * (1.0f / dt);
    }
    else if (listener_.valid && previous.valid)
    {
        listener_.velocity = previous.velocity;
    }

    // Emitters: keep the set true to the world; only a change touches it.
    world.Query<Read<AudioSource>, Read<ecs::WorldTransform>>().ForEach(
        [&](Entity e, const AudioSource& src, const ecs::WorldTransform& t)
        {
            Emitter* em = Find(e);
            if (em == nullptr)
            {
                Emitter fresh;
                fresh.entity = e;
                index_.Insert(e.ToU64(), static_cast<u32>(emitters_.Size()));
                emitters_.Push(fresh);
                em = &emitters_[emitters_.Size() - 1];
            }
            em->seen = epoch_;
            em->flat = !src.spatial;
            if (em->flat && em->handle != spatial::kInvalidSetHandle)
            {
                // A source that stopped being spatial leaves the set: it is a candidate by being flat, not twice.
                set_.Remove(em->handle);
                em->handle = spatial::kInvalidSetHandle;
                em->radius = -1.0f;
            }
            const Vec3d position = t.position;
            const f32 radius = src.spatial && IsValid(src.attenuation)
                                   ? AudibleRadius(src.attenuation,
                                                   config_.audibility_threshold / (src.gain > 1e-6f ? src.gain : 1e-6f))
                                   : 0.0f;
            if (em->radius == radius && SameVec(em->position, position))
            {
                em->velocity = Vec3(0.0f, 0.0f, 0.0f);
                return;
            }
            if (em->radius >= 0.0f && SameVec(em->position, position) == false && Finite(position))
            {
                const Vec3 jump = (position - em->position).Narrow();
                if (dt > 0.0f)
                {
                    em->velocity = jump.Length() > config_.teleport_distance ? Vec3(0.0f, 0.0f, 0.0f)
                                                                             : jump * (1.0f / dt);
                }
            }
            else if (em->radius < 0.0f)
            {
                em->velocity = Vec3(0.0f, 0.0f, 0.0f);
            }
            em->position = position;
            em->radius = radius;
            if (src.spatial && Finite(position))
            {
                const Vec3d r(radius, radius, radius);
                Aabbd box = Aabbd::FromMinMax(position - r, position + r);
                if (em->handle == spatial::kInvalidSetHandle)
                {
                    em->handle = set_.AddDynamic(box, e.ToU64(), 1);
                }
                else
                {
                    set_.Move(em->handle, box);
                }
                report_.moved += 1;
            }
        });

    // Entities that lost the source or the transform: stop their voices with a fade and forget them.
    gone_.Clear();
    for (u32 i = 0; i < emitters_.Size(); ++i)
    {
        if (emitters_[i].seen != epoch_)
        {
            gone_.Push(i);
        }
    }
    for (u32 k = static_cast<u32>(gone_.Size()); k-- > 0;)
    {
        Drop(gone_[k]);  // from the highest index down: a swap never moves a pending one
    }
    for (u32 i = 0; i < emitters_.Size(); ++i)
    {
        emitters_[i].audible = false;
    }
    report_.emitters = static_cast<u32>(emitters_.Size());

    // The ones in range of the listener, by the set.
    candidates_.Clear();
    if (listener_.valid)
    {
        Sphered ears;
        ears.center = listener_.position;
        ears.radius = 0.0f;
        spatial::QueryFilter filter;
        spatial::Overlap(set_, ears, filter,
                         [&](const spatial::Item& item)
                         {
                             Candidate c;
                             c.key = item.key;
                             candidates_.Push(c);
                             return true;
                         });
    }
    // Flat sources are always heard (their position is not used).
    for (u32 i = 0; i < emitters_.Size(); ++i)
    {
        if (emitters_[i].flat)
        {
            Candidate c;
            c.key = emitters_[i].entity.ToU64();
            candidates_.Push(c);
        }
    }
    // The entity order makes the work independent of how the set returned them.
    Sort(Span<Candidate>(candidates_.Data(), candidates_.Size()),
         [](const Candidate& a, const Candidate& b) { return a.key < b.key; });

    u32 kept = 0;
    for (u32 i = 0; i < candidates_.Size(); ++i)
    {
        Candidate c = candidates_[i];
        const Entity entity = Entity::FromU64(c.key);
        const u32* slot = index_.Find(c.key);
        const AudioSource* src = world.Get<AudioSource>(entity);
        const ecs::WorldTransform* t = world.Get<ecs::WorldTransform>(entity);
        if (slot == nullptr || src == nullptr || t == nullptr)
        {
            continue;
        }
        c.emitter = *slot;
        c.priority = src->priority;
        c.pitch = 1.0f;
        c.air = 1.0f;
        f32 loudness = src->gain * (listener_.valid ? listener_.gain : 1.0f);
        Vec3 dir(0.0f, 0.0f, 1.0f);
        f32 spread = 0.0f;
        if (!src->spatial)
        {
            c.loudness = loudness;
        }
        else
        {
            const Vec3 to_emitter = (t->position - listener_.position).Narrow();
            const f32 d2 = to_emitter.LengthSquared();
            const f32 d = Sqrt(d2);
            loudness *= DistanceGain(src->attenuation, d);
            if (d > 1e-4f)
            {
                const Vec3 world_dir = to_emitter * (1.0f / d);
                dir = Vec3(world_dir.Dot(listener_.right), world_dir.Dot(listener_.up),
                           world_dir.Dot(listener_.forward));
                if (src->cone.inner_angle < 6.2831853f)
                {
                    const Vec3 axis = UnitOr(Column(t->matrix, 2) * -1.0f, Vec3(0.0f, 0.0f, -1.0f));
                    loudness *= ConeGain(src->cone, -world_dir.Dot(axis));
                }
            }
            // Close to the listener the direction is unstable: the source spreads over every speaker instead of
            // jumping.
            const f32 near = src->attenuation.min_distance;
            spread = near > 0.0f && d < near ? 1.0f - d / near : 0.0f;
            if (d > 1e-4f && src->doppler)
            {
                const AudioVelocity* explicit_velocity = world.Get<AudioVelocity>(entity);
                const Vec3 ev = explicit_velocity != nullptr ? explicit_velocity->velocity
                                                             : emitters_[c.emitter].velocity;
                c.pitch = DopplerRatio(config_.speed_of_sound, config_.doppler_factor, to_emitter * (1.0f / d),
                                       listener_.velocity, ev);
            }
            c.distance = d;
            c.occludable = src->occlusion;
            c.loudness = loudness;
        }
        c.dir = dir;
        c.spread = spread;
        // The mixer refuses a gain above 16 (a source gain of 16 times a listener gain of 4 would never start).
        c.loudness = c.loudness > 16.0f ? 16.0f : c.loudness;
        if (!(c.loudness > config_.audibility_threshold))
        {
            continue;
        }
        candidates_[kept] = c;
        kept += 1;
    }
    candidates_.Resize(kept);

    // Occlusion: a bounded number of tests per update, round-robin in entity order from where the last update stopped,
    // each emitter at most `tests_per_second`; the result is smoothed every update so a door fades.
    const OcclusionConfig& oc = config_.occlusion;
    const f32 interval = oc.tests_per_second > 0.0f ? 1.0f / oc.tests_per_second : 1e9f;
    const f32 smooth_k = dt > 0.0f && oc.smooth_seconds > 0.0f
                             ? static_cast<f32>(1.0 - Exp(-static_cast<f64>(dt) / static_cast<f64>(oc.smooth_seconds)))
                             : 1.0f;
    if (occlusion_ != nullptr && listener_.valid && kept > 0)
    {
        u32 start = 0;
        while (start < kept && candidates_[start].key < round_robin_key_)
        {
            start += 1;
        }
        u32 budget = oc.max_tests_per_update;
        for (u32 n = 0; n < kept; ++n)
        {
            Emitter& em = emitters_[candidates_[(start + n) % kept].emitter];
            em.occ_since += dt > 0.0f ? dt : 0.0f;
        }
        for (u32 n = 0; n < kept && budget > 0; ++n)
        {
            const Candidate& c = candidates_[(start + n) % kept];
            Emitter& em = emitters_[c.emitter];
            if (!c.occludable || em.occ_since < interval)
            {
                continue;
            }
            em.occ_target = occlusion_->Occlusion(em.position, listener_.position, nullptr);
            em.occ_since = 0.0f;
            if (!em.occ_known)
            {
                em.occ_smooth = em.occ_target;
                em.occ_known = true;
            }
            budget -= 1;
            report_.occlusion_tests += 1;
            round_robin_key_ = c.key + 1;
        }
    }
    u32 loud = 0;
    for (u32 i = 0; i < kept; ++i)
    {
        Candidate c = candidates_[i];
        Emitter& em = emitters_[c.emitter];
        f32 cutoff = AirCutoffHz(c.distance, config_.air_absorption_factor);
        if (c.occludable && occlusion_ != nullptr)
        {
            em.occ_smooth += (em.occ_target - em.occ_smooth) * smooth_k;
            c.loudness *= 1.0f - em.occ_smooth * (1.0f - oc.occluded_gain);
            if (oc.occluded_cutoff_hz < cutoff)
            {
                cutoff += (oc.occluded_cutoff_hz - cutoff) * em.occ_smooth;
            }
        }
        c.air = AirCoefficient(cutoff, mixer_->Config().sample_rate);
        if (!(c.loudness > config_.audibility_threshold))
        {
            continue;
        }
        candidates_[loud] = c;
        loud += 1;
    }
    kept = loud;
    candidates_.Resize(kept);
    Sort(Span<Candidate>(candidates_.Data(), candidates_.Size()),
         [](const Candidate& a, const Candidate& b)
         {
             if (a.loudness != b.loudness)
             {
                 return a.loudness > b.loudness;
             }
             if (a.priority != b.priority)
             {
                 return a.priority > b.priority;
             }
             return a.key < b.key;
         });
    report_.audible = kept;
    for (u32 i = 0; i < kept; ++i)
    {
        const Candidate& c = candidates_[i];
        Emitter* em = &emitters_[c.emitter];
        const AudioSource* src = world.Get<AudioSource>(em->entity);
        const bool real = i < config_.max_real_voices;
        em->audible = true;
        if (real)
        {
            report_.real += 1;
        }
        Drive(em, *src, c, real);
    }

    // Everything else that has a voice goes (or stays) virtual; a one-shot that ended is forgotten.
    for (u32 i = 0; i < emitters_.Size(); ++i)
    {
        Emitter& em = emitters_[i];
        if (!em.voice.IsValid())
        {
            continue;
        }
        if (!mixer_->IsPlaying(em.voice))
        {
            em.voice = VoiceId();
            em.finished = true;
            continue;
        }
        if (!em.audible && !em.virtual_sent)
        {
            if (Post(mixer_->SetVirtual(em.voice, true)))
            {
                em.virtual_sent = true;
            }
        }
    }
}

}  // namespace vse::audio
