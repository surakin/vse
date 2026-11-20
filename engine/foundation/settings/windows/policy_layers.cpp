// Group policy as the settings' top layer on Windows (Phase 13 Day 157; docs/guide/settings.md, "Settings on Windows").
// An administrator enforces a value by writing it under Software\Policies\<app>: the user's policy key (HKCU) and then
// the machine's (HKLM), which wins, as group policy orders them. Both are read-only stores; a key that does not exist
// is an empty layer.
#include "settings/policy_layers.h"

#include "containers/unique_ptr.h"
#include "settings/windows/registry_store.h"

#include "cstr/format.h"

namespace vse::settings::detail
{

void AddPolicyLayers(const LoadOptions& options, LayerStack* stack)
{
    char path[512];
    if (options.policy_path != nullptr)
    {
        FormatTo(path, sizeof(path), "%s", options.policy_path);
    }
    else
    {
        FormatTo(path, sizeof(path), "Software\\Policies\\%s",
                 options.policy_name != nullptr ? options.policy_name : options.app_name);
    }
    RegistryStoreOptions user;
    user.hive = RegistryHive::CurrentUser;
    user.path = path;
    user.read_only = true;
    stack->AddLayer("user policy", LayerRole::Policy, MakeUnique<RegistryConfigStore>(user));
    if (options.policy_path != nullptr)
    {
        return;  // a test's own key lives under HKCU only (writing HKLM needs an administrator)
    }
    RegistryStoreOptions machine = user;
    machine.hive = RegistryHive::LocalMachine;
    stack->AddLayer("machine policy", LayerRole::Policy, MakeUnique<RegistryConfigStore>(machine));
}

}  // namespace vse::settings::detail
