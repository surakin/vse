// The physics step as an ECS system (Phase 25 Day 378): the fixed-step update and the write-back of the interpolated
// transforms.
#pragma once

#include "ecs/schedule.h"
#include "ecs/world.h"
#include "physics/sync.h"
#include "physics/world.h"
#include "types.h"

namespace engine_placeholder::physics
{

/// Writes the transform of every dynamic body that has an entity into the entity's `LocalTransform`.
///
/// The value is the blend of the
/// body's last two states by `alpha` (the position and rotation only; the scale is left alone). Kinematic and static
/// bodies are driven by the program, not by the simulation, so they are not written.
///
/// - Param ecs: the ECS world.
/// - Param physics: the physics world.
/// - Param alpha: from 0 to 1, usually `PhysicsWorld::Alpha()`.
/// - Returns: the number of transforms written.
/// - Aborts: called while the ECS world is being iterated.
/// - Fails: an entity that is gone or has no `LocalTransform` is skipped.
u32 WriteBackTransforms(ecs::World& ecs, const PhysicsWorld& physics, f32 alpha);

/// Makes the system that steps the simulation.
///
/// It calls `PhysicsWorld::Update` on the resource `PhysicsWorld` with the
/// frame's time (`SystemContext::Dt`), and writes the interpolated transforms back to `LocalTransform` of the entities
/// with a `BodyLink`. The declared reads and writes let the scheduler order it against the systems that move kinematic
/// bodies or read the transforms. `SyncBodies` is not part of it: it changes the ECS world's structure, so the host
/// calls it between runs of the schedule.
///
/// - Returns: the system, to add to an `ecs::Schedule`.
/// - Fails: never.
ecs::SystemDesc MakePhysicsStepSystem();

}  // namespace engine_placeholder::physics
