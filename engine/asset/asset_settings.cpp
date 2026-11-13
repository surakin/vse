#include "asset/asset_settings.h"

#include "settings/desc.h"
#include "settings/setting.h"

namespace engine_placeholder::asset
{
namespace
{

void Declare(settings::Registry* registry, settings::SettingDesc d, const char* env, const char* flag,
             const char* label, const char* description)
{
    if (registry->Find(StringView(d.key)).IsValid())
    {
        return;
    }
    d.env = env;
    d.flag = flag;
    d.label = label;
    d.description = description;
    d.group = "assets";
    d.flags = settings::kAdvanced;
    registry->Register(d);
}

u64 Read(const settings::Registry& registry, const char* key)
{
    return static_cast<u64>(settings::Setting<i64>::Find(registry, StringView(key)).Get());
}

f64 ReadFloat(const settings::Registry& registry, const char* key)
{
    return settings::Setting<f64>::Find(registry, StringView(key)).Get();
}

}  // namespace

void RegisterAssetSettings(settings::Registry* registry)
{
    constexpr i64 kMiB = 1 << 20;
    Declare(registry, settings::MakeInt("assets.buffer_pool_bytes", 256 * kMiB, kMiB, i64(1) << 40),
            "ENGINE_ASSETS_BUFFER_POOL_BYTES", "assets-buffer-pool-bytes", "Load buffer pool size",
            "the most bytes of buffers (in use and idle) the pool resources are read into may hold");
    Declare(registry, settings::MakeInt("assets.memory_backend_bytes", 512 * kMiB, 0, i64(1) << 40),
            "ENGINE_ASSETS_MEMORY_BACKEND_BYTES", "assets-memory-backend-bytes", "Host-provided content budget",
            "the most bytes of resources the memory backend holds; an add past it is refused");
    Declare(registry, settings::MakeFloat("assets.anim_rotation_error_degrees", 0.05, 0.0001, 5.0),
            "ENGINE_ASSETS_ANIM_ROTATION_ERROR_DEGREES", "assets-anim-rotation-error-degrees",
            "Animation rotation error bound",
            "the largest rotation error, in degrees, a minted animation clip may have against its source keys");
    Declare(registry, settings::MakeFloat("assets.anim_translation_error", 1e-4, 1e-7, 0.1),
            "ENGINE_ASSETS_ANIM_TRANSLATION_ERROR", "assets-anim-translation-error",
            "Animation translation error bound",
            "the largest translation or scale error, relative to the channel's extent, a minted clip may have");
}

u64 BufferPoolBytesFromSettings(const settings::Registry& registry)
{
    return Read(registry, "assets.buffer_pool_bytes");
}

u64 MemoryBackendBytesFromSettings(const settings::Registry& registry)
{
    return Read(registry, "assets.memory_backend_bytes");
}

f64 AnimRotationErrorDegreesFromSettings(const settings::Registry& registry)
{
    return ReadFloat(registry, "assets.anim_rotation_error_degrees");
}

f64 AnimTranslationErrorFromSettings(const settings::Registry& registry)
{
    return ReadFloat(registry, "assets.anim_translation_error");
}

}  // namespace engine_placeholder::asset
