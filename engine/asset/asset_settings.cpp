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
}

u64 BufferPoolBytesFromSettings(const settings::Registry& registry)
{
    return Read(registry, "assets.buffer_pool_bytes");
}

u64 MemoryBackendBytesFromSettings(const settings::Registry& registry)
{
    return Read(registry, "assets.memory_backend_bytes");
}

}  // namespace engine_placeholder::asset
