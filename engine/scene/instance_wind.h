#pragma once
//! A sample animation for instance hierarchies: [[scene::MakeWindSystem]] sways the nodes of every
//! [[scene::InstanceHierarchy]] that carry the [[scene::kTreeNodeWind]] flag, with an angle that is a function of the
//! simulation time and the node's id, so the same time gives the same pose on any machine and for any thread count. It
//! moves nodes with [[renderer::InstanceTree::SetLocal]], so the sync recomposes only the swaying subtrees.

#include "ecs/schedule.h"
#include "types.h"

namespace vse::scene
{

/// The `flags` bit of a tree node that the wind system sways.
inline constexpr u32 kTreeNodeWind = 1u;

/// The wind: a world resource the wind system reads.
///
/// - Thread-safety: thread-compatible plain data.
struct WindState
{
    f32 time = 0.0f;       ///< Simulation time in seconds; set by the host's tick.
    f32 amplitude = 0.1f;  ///< The largest swing in radians.
    f32 frequency = 0.5f;  ///< Swings a second.
};

/// The swing angle of node `id` at `time`, in radians: `amplitude * sin(2 pi * (frequency * time + phase(id)))`, where
/// the phase is a hash of the id in [0, 1).
///
/// - Fails: never.
f32 WindAngle(const WindState& wind, u32 id);

/// Builds the system that sets the local rotation of every wind node (a turn about the node's x axis by
/// [[scene::WindAngle]], the node's rest rotation being the identity; its offset and scale are kept).
///
/// - Param name: the system's name.
/// - Returns: the description to add to a schedule; it reads `InstanceHierarchy` and the `WindState` resource. It
///   writes the trees through their shared pointers, and declares `SceneLink` as written so it never runs beside the
///   sync system that recomposes and draws them; register it before that system.
/// - Fails: never.
ecs::SystemDesc MakeWindSystem(const char* name);

}  // namespace vse::scene
