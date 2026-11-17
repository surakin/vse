// The physics settings (Phase 25 Day 390): the step rate, the substep cap, the gravity and interpolation, declared with
// `settings::Registry`.
#pragma once

#include "physics/world.h"
#include "settings/registry.h"

namespace engine_placeholder::physics
{

/// Declares the step settings, each with an environment variable and a flag.
///
/// They are `physics.step_hz`, `physics.max_substeps`, `physics.gravity_y`, `physics.contact_margin` and
/// `physics.interpolate`.
///
/// The gravity is a vector in `StepConfig`, but only its Y component is a setting (the usual case); a game that wants
/// sideways gravity sets `StepConfig::gravity` in code.
///
/// - Param registry: the registry to declare them in; not null.
/// - Ownership: borrowed for the call.
/// - Fails: never.
void RegisterPhysicsSettings(settings::Registry* registry);

/// Reads the step settings from a registry.
///
/// - Param registry: a registry in which `RegisterPhysicsSettings` was called; a setting that is missing keeps its
/// default.
/// - Returns: the config.
/// - Fails: never.
StepConfig StepConfigFromSettings(const settings::Registry& registry);

}  // namespace engine_placeholder::physics
