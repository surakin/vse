#pragma once

//! The platform's policy layers: what [[settings::Settings::Load]] adds when
//! [[settings::LoadOptions::use_policy]] is set. Called by `Load` only; a program sets the options instead. Windows:
//! the group-policy registry keys; macOS: managed preferences; Linux: none yet.
#include "settings/layers.h"
#include "settings/settings.h"

namespace vse::settings::detail
{

/// Adds the platform's policy layers to `stack`, lowest first, each with the role [[settings::LayerRole::Policy]].
///
/// - Param options: the load options (`app_name`, `policy_name`, `policy_path`).
/// - Param stack: the stack being built.
/// - Fails: never; a policy key that does not exist is an empty layer.
/// - Backend: Windows: two read-only registry stores, `HKCU` and then `HKLM` `Software\Policies\<name>`; macOS: one
/// forced-only user defaults store over the suite (managed preferences); Linux: none yet.
void AddPolicyLayers(const LoadOptions& options, LayerStack* stack);

}  // namespace vse::settings::detail
