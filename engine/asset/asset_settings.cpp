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

void RegisterManagerSettings(settings::Registry* registry)
{
    Declare(registry, settings::MakeInt("assets.cache_bytes", 0, 0, i64(1) << 44), "ENGINE_ASSETS_CACHE_BYTES",
            "assets-cache-bytes", "Resident resource budget",
            "the most bytes of resources the manager keeps loaded; a load past it is refused (0 for no limit)");
    Declare(registry, settings::MakeBool("assets.allow_fallbacks", false), "ENGINE_ASSETS_ALLOW_FALLBACKS",
            "assets-allow-fallbacks", "Allow fallback resources (development)",
            "replace a missing or invalid resource of a type that has a fallback by it, logged and counted; off by "
            "default, because minted data must load");
    Declare(registry, settings::MakeBool("assets.verify_on_load", false), "ENGINE_ASSETS_VERIFY_ON_LOAD",
            "assets-verify-on-load", "Verify the content hash of every blob on load (development)",
            "hash every blob as it is loaded and refuse one that does not match its recorded hash");
    Declare(
        registry, settings::MakeInt("assets.max_inflight", 4, 1, 256), "ENGINE_ASSETS_MAX_INFLIGHT",
        "assets-max-inflight", "Asynchronous loads reading at once",
        "the most asynchronous loads whose blobs are being read on workers at one time; the others wait in a queue");
    Declare(registry, settings::MakeInt("assets.max_queued", 256, 0, 1000000), "ENGINE_ASSETS_MAX_QUEUED",
            "assets-max-queued", "Asynchronous loads waiting",
            "the most asynchronous loads waiting for a free reading slot; a request past it fails with LimitExceeded");
    Declare(registry, settings::MakeText("assets.mounts", ""), "ENGINE_ASSETS_MOUNTS", "assets-mounts",
            "Resource mounts",
            "PREFIX=PATH entries separated by ';': a directory of loose blobs or a pak file mounted under the prefix "
            "(/ when none is written); a later entry wins over an earlier one");
}

void RegisterServerSettings(settings::Registry* registry)
{
    Declare(registry, settings::MakeInt("assets.net.max_read_bytes", 256 * 1024, 4096, 1 << 22),
            "ENGINE_ASSETS_NET_MAX_READ_BYTES", "assets-net-max-read-bytes", "Resource server: largest read",
            "the most bytes one Read request of the resource server returns; a larger request is refused");
    Declare(registry, settings::MakeInt("assets.net.max_inflight", 16, 1, 4096), "ENGINE_ASSETS_NET_MAX_INFLIGHT",
            "assets-net-max-inflight", "Resource server: requests per connection per poll",
            "the requests the resource server answers for one connection in one poll; the others are answered Busy");
    Declare(registry, settings::MakeInt("assets.net.page_entries", 256, 1, 1024), "ENGINE_ASSETS_NET_PAGE_ENTRIES",
            "assets-net-page-entries", "Resource server: entries per page",
            "the entries of a table page or a list page of the resource server");
}

Status MountSpecsFromSettings(const settings::Registry& registry, Vector<MountSpec>* out)
{
    const settings::SettingId id = registry.Find(StringView("assets.mounts"));
    settings::TextSetting text(registry, id);
    char buffer[4096];
    const usize length = text.Get(buffer, sizeof(buffer) - 1);
    if (length >= sizeof(buffer) - 1)
    {
        return Status::Make(ErrorCode::LimitExceeded, "assets.mounts is longer than 4095 bytes");
    }
    out->Clear();
    const StringView all(buffer, length);
    usize pos = 0;
    while (pos <= all.Length())
    {
        usize end = pos;
        while (end < all.Length() && all[end] != ';')
        {
            ++end;
        }
        StringView entry = all.Substring(pos, end - pos);
        pos = end + 1;
        while (entry.Length() != 0 && (entry[0] == ' ' || entry[0] == '\t'))
        {
            entry = entry.Substring(1, entry.Length() - 1);
        }
        while (entry.Length() != 0 && (entry[entry.Length() - 1] == ' ' || entry[entry.Length() - 1] == '\t'))
        {
            entry = entry.Substring(0, entry.Length() - 1);
        }
        if (entry.Length() == 0)
        {
            continue;
        }
        MountSpec spec;
        const usize eq = entry.Find(StringView("="));
        if (eq == entry.Length())
        {
            spec.prefix.Append(StringView("/"));
            spec.path.Append(entry);
        }
        else
        {
            spec.prefix.Append(entry.Substring(0, eq));
            spec.path.Append(entry.Substring(eq + 1, entry.Length() - eq - 1));
        }
        if (spec.path.Length() == 0)
        {
            return Status::Make(ErrorCode::Invalid, "an assets.mounts entry has no path");
        }
        spec.priority = static_cast<i32>(out->Size());
        out->Push(static_cast<MountSpec&&>(spec));
    }
    return Status::Success();
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
