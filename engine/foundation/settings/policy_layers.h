#pragma once

//! The platform's policy layers (Phase 13 Day 157): what [[settings::Settings::Load]] adds when [[settings::LoadOptions::use_policy]] is set.
//! Called by `Load` only; a program sets the options instead. Windows: the group-policy registry keys; Linux: none yet.
#include "settings/layers.h"
#include "settings/settings.h"

namespace engine_placeholder::settings::detail {

/// Adds the platform's policy layers to `stack`, lowest first, each with the role [[settings::LayerRole::Policy]].
///
/// - Param options: the load options (`app_name`, `policy_name`, `policy_path`).
/// - Param stack: the stack being built.
/// - Fails: never; a policy key that does not exist is an empty layer.
/// - Backend: Windows: two read-only registry stores, `HKCU` and then `HKLM` `Software\Policies\<name>`; Linux and macOS: none yet.
void AddPolicyLayers(const LoadOptions& options, LayerStack* stack);

} // namespace engine_placeholder::settings::detail
