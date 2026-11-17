// The physics implementation of the audio occlusion query (Phase 26 Day 431): the one place the audio side names
// physics.
#pragma once

#include "audio/occlusion.h"
#include "containers/vector.h"
#include "physics/query.h"
#include "physics/world.h"
#include "types.h"

namespace engine_placeholder::audio_physics
{

/// What the occlusion ray counts and how much each material absorbs.
///
/// - Thread-safety: thread-compatible plain data.
struct PhysicsOcclusionConfig
{
    /// The collision layers that block sound, one bit per layer (0 to 31).
    u32 layer_mask = 0xFFFFFFFFu;
    /// Whether trigger colliders block sound.
    bool include_triggers = false;
    /// The optical depth per metre of material, per collider layer: the occlusion of a wall of thickness `t` is
    /// `1 - exp(-absorption * t)`.
    f32 absorption_per_metre[32] = {};
    /// The thickness assumed for a collider the path enters but whose far side the reverse ray did not find (a path
    /// that starts inside it or grazes it), in metres.
    f32 default_thickness = 0.1f;

    /// Makes the configuration with every layer absorbing 3 per metre (a 0.1 m panel blocks 26 percent, a metre of wall
    /// 95 percent).
    PhysicsOcclusionConfig()
    {
        for (u32 i = 0; i < 32; ++i)
        {
            absorption_per_metre[i] = 3.0f;
        }
    }
};

/// Casts a ray from the emitter to the listener through the physics world and turns what it crosses into an occlusion
/// value. The thickness of each collider is measured with two rays (one from each end: the entry of the second is the
/// exit of the first), summed with the layer's absorption; no diffraction around the obstacle is modelled (the guide
/// says so). The queries scan the world's colliders, so a thousand emitters are fine and a hundred thousand are not.
///
/// - Thread-safety: thread-compatible; call it between physics steps, from the control thread.
/// - Ownership: borrows the world, which must outlive the query.
class PhysicsOcclusion final : public audio::OcclusionQuery
{
public:
    /// Makes the query over a world.
    ///
    /// - Param world: the world to read; borrowed.
    /// - Param config: the layers and absorptions.
    /// - Fails: never.
    /// - Ownership: borrows `world`.
    explicit PhysicsOcclusion(const physics::PhysicsWorld* world,
                              const PhysicsOcclusionConfig& config = PhysicsOcclusionConfig())
        : world_(world), config_(config)
    {
    }

    /// Tests the path.
    ///
    /// - Param from: the emitter's position.
    /// - Param to: the listener's position.
    /// - Param crossed: receives the colliders crossed (may be null).
    /// - Returns: 0 for an open path to 1 for a blocked one.
    /// - Fails: returns 0 for a zero-length path or a null world.
    /// - Ownership: `crossed` is borrowed for the call.
    f32 Occlusion(const Vec3& from, const Vec3& to, u32* crossed) override;

private:
    const physics::PhysicsWorld* world_;
    PhysicsOcclusionConfig config_;
    Vector<physics::PhysicsHit> forward_;
    Vector<physics::PhysicsHit> backward_;
};

}  // namespace engine_placeholder::audio_physics
