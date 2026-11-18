#include "script/script_settings.h"

#include "settings/setting.h"

namespace engine_placeholder::script
{

void RegisterScriptSettings(settings::Registry* registry)
{
    settings::SettingDesc desc = settings::MakeText("script.backend", "luajit");
    desc.env = "ENGINE_SCRIPT_BACKEND";
    desc.flag = "script-backend";
    desc.label = "Script language";
    desc.description = "the scripting language backend: luajit (the default), or another one the program registers";
    desc.group = "script";
    desc.flags = settings::kRestartRequired;
    registry->Register(desc);
}

String<HeapAllocator> ScriptBackendName(const settings::Registry& registry)
{
    const settings::SettingId id = registry.Find(StringView("script.backend"));
    if (id.IsValid())
    {
        char text[64];
        const usize length = settings::TextSetting(registry, id).Get(text, sizeof(text));
        if (length > 0 && length < sizeof(text))
        {
            return String<HeapAllocator>(HeapAllocator::Default(), StringView(text, length));
        }
    }
    return String<HeapAllocator>(HeapAllocator::Default(), "luajit");
}

}  // namespace engine_placeholder::script
