#include "asset_import/import_settings.h"

#include "settings/desc.h"
#include "settings/setting.h"

namespace vse::asset
{
namespace
{

void Declare(settings::Registry* registry, const char* key, i64 value, i64 minimum, i64 maximum, const char* env,
             const char* flag, const char* label, const char* description)
{
    if (registry->Find(StringView(key)).IsValid())
    {
        return;
    }
    settings::SettingDesc d = settings::MakeInt(key, value, minimum, maximum);
    d.env = env;
    d.flag = flag;
    d.label = label;
    d.description = description;
    d.group = "assets";
    d.flags = settings::kAdvanced;
    registry->Register(d);
}

void DeclareFloat(settings::Registry* registry, const char* key, f64 value, f64 minimum, f64 maximum, const char* env,
                  const char* flag, const char* label, const char* description)
{
    if (registry->Find(StringView(key)).IsValid())
    {
        return;
    }
    settings::SettingDesc d = settings::MakeFloat(key, value, minimum, maximum);
    d.env = env;
    d.flag = flag;
    d.label = label;
    d.description = description;
    d.group = "assets";
    d.flags = settings::kAdvanced;
    registry->Register(d);
}

u64 Read(const settings::Registry& registry, const char* key, u64 fallback)
{
    const settings::SettingId id = registry.Find(StringView(key));
    if (!id.IsValid())
    {
        return fallback;
    }
    return static_cast<u64>(settings::Setting<i64>::Find(registry, StringView(key)).Get());
}

}  // namespace

void RegisterImportSettings(settings::Registry* registry)
{
    const ImportLimits d;
    constexpr i64 kMax = i64(1) << 40;
    Declare(registry, "assets.import.max_file_bytes", static_cast<i64>(d.max_file_bytes), 1024, kMax,
            "ENGINE_ASSETS_IMPORT_MAX_FILE_BYTES", "assets-import-max-file-bytes", "Import: largest glTF file",
            "the most bytes of a .gltf or .glb file the importer reads");
    Declare(registry, "assets.import.max_json_bytes", static_cast<i64>(d.max_json_bytes), 1024, kMax,
            "ENGINE_ASSETS_IMPORT_MAX_JSON_BYTES", "assets-import-max-json-bytes", "Import: largest glTF JSON",
            "the most bytes of JSON text; the memory of a parse is a multiple of it");
    Declare(registry, "assets.import.max_buffer_bytes", static_cast<i64>(d.max_buffer_bytes), 1024, kMax,
            "ENGINE_ASSETS_IMPORT_MAX_BUFFER_BYTES", "assets-import-max-buffer-bytes", "Import: largest glTF buffers",
            "the most bytes of all buffers of a glTF together");
    Declare(registry, "assets.import.max_nodes", d.max_nodes, 1, 10000000, "ENGINE_ASSETS_IMPORT_MAX_NODES",
            "assets-import-max-nodes", "Import: most nodes", "the most glTF nodes");
    Declare(registry, "assets.import.max_accessors", d.max_accessors, 1, 10000000, "ENGINE_ASSETS_IMPORT_MAX_ACCESSORS",
            "assets-import-max-accessors", "Import: most accessors", "the most glTF accessors");
    Declare(registry, "assets.import.max_images", d.max_images, 0, 1000000, "ENGINE_ASSETS_IMPORT_MAX_IMAGES",
            "assets-import-max-images", "Import: most images", "the most glTF images");
    Declare(registry, "assets.import.max_pixels", static_cast<i64>(d.max_pixels), 1, i64(1) << 30,
            "ENGINE_ASSETS_IMPORT_MAX_PIXELS", "assets-import-max-pixels", "Import: most pixels per image",
            "the most pixels (width times height) of one decoded image");
    Declare(registry, "assets.import.max_output_bytes", static_cast<i64>(d.max_output_bytes), 1024, kMax,
            "ENGINE_ASSETS_IMPORT_MAX_OUTPUT_BYTES", "assets-import-max-output-bytes", "Import: most output bytes",
            "the most bytes of blobs one import may make");
    Declare(registry, "assets.import.max_collision_triangles", static_cast<i64>(d.max_collision_triangles), 0, kMax,
            "ENGINE_ASSETS_IMPORT_MAX_COLLISION_TRIANGLES", "assets-import-max-collision-triangles",
            "Import: most collision triangles", "the most triangles of every collider of one import together");
    const ImportOptions o;
    DeclareFloat(registry, "assets.import.light_intensity_scale", o.light_intensity_scale, 0.0, 1e9,
                 "ENGINE_ASSETS_IMPORT_LIGHT_INTENSITY_SCALE", "assets-import-light-intensity-scale",
                 "Import: point and spot light scale",
                 "multiplies the candela of KHR_lights_punctual point and spot lights");
    DeclareFloat(registry, "assets.import.sun_intensity_scale", o.sun_intensity_scale, 0.0, 1e9,
                 "ENGINE_ASSETS_IMPORT_SUN_INTENSITY_SCALE", "assets-import-sun-intensity-scale",
                 "Import: directional light scale", "multiplies the lux of KHR_lights_punctual directional lights");
    DeclareFloat(registry, "assets.import.light_cutoff", o.light_cutoff, 1e-9, 1.0, "ENGINE_ASSETS_IMPORT_LIGHT_CUTOFF",
                 "assets-import-light-cutoff", "Import: light cutoff",
                 "the light level at which a point or spot light without a range ends");
}

void ApplyImportLightSettings(const settings::Registry& registry, ImportOptions* options)
{
    const auto read = [&](const char* key, f32* field)
    {
        if (registry.Find(StringView(key)).IsValid())
        {
            *field = static_cast<f32>(settings::Setting<f64>::Find(registry, StringView(key)).Get());
        }
    };
    read("assets.import.light_intensity_scale", &options->light_intensity_scale);
    read("assets.import.sun_intensity_scale", &options->sun_intensity_scale);
    read("assets.import.light_cutoff", &options->light_cutoff);
}

ImportLimits ImportLimitsFromSettings(const settings::Registry& registry)
{
    ImportLimits limits;
    limits.max_file_bytes = Read(registry, "assets.import.max_file_bytes", limits.max_file_bytes);
    limits.max_json_bytes = Read(registry, "assets.import.max_json_bytes", limits.max_json_bytes);
    limits.max_buffer_bytes = Read(registry, "assets.import.max_buffer_bytes", limits.max_buffer_bytes);
    limits.max_nodes = static_cast<u32>(Read(registry, "assets.import.max_nodes", limits.max_nodes));
    limits.max_accessors = static_cast<u32>(Read(registry, "assets.import.max_accessors", limits.max_accessors));
    limits.max_images = static_cast<u32>(Read(registry, "assets.import.max_images", limits.max_images));
    limits.max_pixels = Read(registry, "assets.import.max_pixels", limits.max_pixels);
    limits.max_output_bytes = Read(registry, "assets.import.max_output_bytes", limits.max_output_bytes);
    limits.max_collision_triangles = Read(registry, "assets.import.max_collision_triangles",
                                          limits.max_collision_triangles);
    return limits;
}

}  // namespace vse::asset
