// Managed preferences as the settings' top layer on macOS (Phase 26 Day 309; docs/guide/settings.md, "Settings on
// macOS"). An administrator enforces a value with a configuration profile, which macOS installs as managed preferences
// for the application's defaults domain (/Library/Managed Preferences/[<user>/]<suite>.plist);
// CFPreferencesAppValueIsForced says which values are enforced. One read-only, forced-only store; a domain with nothing
// enforced is an empty layer.
#include "settings/policy_layers.h"

#include "containers/unique_ptr.h"
#include "settings/macos/user_defaults_store.h"

namespace engine_placeholder::settings::detail
{

void AddPolicyLayers(const LoadOptions& options, LayerStack* stack)
{
    UserDefaultsStoreOptions managed;
    managed.suite = options.policy_path != nullptr   ? options.policy_path
                    : options.policy_name != nullptr ? options.policy_name
                                                     : options.app_name;
    managed.forced_only = true;
    stack->AddLayer("managed preferences", LayerRole::Policy, MakeUnique<UserDefaultsConfigStore>(managed));
}

}  // namespace engine_placeholder::settings::detail
