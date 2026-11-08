#include "renderer/shared_settings.h"

#include <cstdio>
#include <cstring>

#include "log/assert.h"
#include "renderer/renderer.h"

namespace engine_placeholder::renderer {

namespace sset = ::engine_placeholder::settings;

namespace {

constexpr usize kKeyBytes = 48;

struct KeyTable {
    char keys[kMaxSettings][kKeyBytes] = {};
    u32 count = 0;
    KeyTable() {
        const SettingsRegistry& r = SettingsRegistry::Builtin();
        count = r.Count();
        for (u32 i = 0; i < count; ++i) std::snprintf(keys[i], kKeyBytes, "renderer.%s", r.Find(SettingId{static_cast<u16>(i + 1)})->name);
    }
};

// "renderer.<name>" for each built-in setting, in static storage (the declarations borrow the strings). Built once, thread-safely.
const char* SharedKey(u32 index) {
    static const KeyTable table;
    return table.keys[index];
}

// Copies a shared text into a renderer text buffer (the shared declaration limits it to the buffer's size, so nothing is cut).
void CopyText(StringView t, char* out, usize capacity) {
    const usize n = t.Length() < capacity - 1 ? t.Length() : capacity - 1;
    std::memcpy(out, t.Data(), n);
    out[n] = '\0';
}

sset::Type MapType(SettingType t) {
    switch (t) {
    case SettingType::Bool: return sset::Type::Bool;
    case SettingType::Enum: return sset::Type::Enum;
    case SettingType::Int: return sset::Type::Int;
    case SettingType::Float: return sset::Type::Float;
    case SettingType::Text: return sset::Type::Text;
    }
    return sset::Type::Bool;
}

}  // namespace

void RegisterSharedSettings(sset::Registry* registry) {
    const SettingsRegistry& builtin = SettingsRegistry::Builtin();
    for (u32 i = 0; i < builtin.Count(); ++i) {
        const SettingDesc& d = *builtin.Find(SettingId{static_cast<u16>(i + 1)});
        sset::SettingDesc s;
        s.key = SharedKey(i);
        s.type = MapType(d.type);
        s.apply_class = static_cast<u8>(d.cost);
        if (d.restart_required) s.flags |= sset::kRestartRequired;
        s.capability = d.capability;
        s.policy = d.range_policy == RangePolicy::Reject ? sset::RangePolicy::Reject : sset::RangePolicy::Clamp;
        switch (d.type) {
        case SettingType::Bool: s.default_value = sset::Value::FromBool(d.default_int != 0); break;
        case SettingType::Enum:
            s.default_value = sset::Value::FromEnum(static_cast<u32>(d.default_int));
            s.enum_names = d.enum_names;
            s.enum_count = d.enum_count;
            break;
        case SettingType::Int:
            s.default_value = sset::Value::FromInt(d.default_int);
            s.min_int = d.min_int;
            s.max_int = d.max_int;
            break;
        case SettingType::Float:
            s.default_value = sset::Value::FromFloat(static_cast<f64>(d.default_float));
            s.min_float = static_cast<f64>(d.min_float);
            s.max_float = static_cast<f64>(d.max_float);
            break;
        case SettingType::Text:
            s.max_text = kSettingTextBytes - 1;
            s.default_value = sset::Value::FromText(d.default_text == nullptr ? StringView() : StringView(d.default_text)); break;
        }
        // What a user sees in --list-settings and --help, and the environment variables and flags that were already there.
        static const struct { const char* name; const char* description; const char* env; const char* flag; } kInfo[] = {
            {"backend", "the graphics backend: auto (the preferred one this program has and this machine runs), null (no GPU), vulkan or dx12", nullptr, "rhi"},
            {"adapter", "the GPU: an id from --list-gpus, an index, or part of the name (empty: the best one)", "ENGINE_RHI_GPU", "gpu"},
            {"validation", "the validation layer: auto, off or on", nullptr, nullptr},
            {"sync_validation", "also run synchronization validation", "ENGINE_RHI_VALIDATION_SYNC", nullptr},
            {"frames_in_flight", "frames the CPU may record ahead of the GPU (1 to 3)", nullptr, nullptr},
            {"present_mode", "how frames reach the display: fifo (vsync), mailbox or immediate", nullptr, nullptr},
            {"width", "the window width in pixels (0: the default size)", nullptr, nullptr},
            {"height", "the window height in pixels (0: the default size)", nullptr, nullptr},
            {"fullscreen", "fullscreen (needs the fullscreen capability)", nullptr, nullptr},
            {"swapchain_format", "the swapchain format: auto, srgb8, rgba16f or rgb10a2", nullptr, nullptr},
            {"hdr", "HDR output: off, scrgb or hdr10 (needs the hdr capability)", nullptr, nullptr},
            {"shader_quality", "shader quality: low, medium or high", nullptr, nullptr},
            {"gbuffer_debug_layer", "show a G-buffer layer instead of the lit picture", nullptr, nullptr},
            {"render_scale", "the internal resolution as a fraction of the window", nullptr, nullptr},
        };
        for (const auto& info : kInfo) {
            if (std::strcmp(info.name, d.name) == 0) {
                s.description = info.description;
                s.env = info.env;
                s.flag = info.flag;
                s.group = "renderer";
            }
        }
        sset::RegisterError error = sset::RegisterError::None;
        const sset::SettingId id = registry->Register(s, &error);
        ENGINE_ASSERT(id.IsValid(), "renderer::RegisterSharedSettings: a rendering setting could not be declared (duplicate key or bad declaration)");
    }
    static_assert(kRendererSchemaVersion == 2, "update the literal below with the constant: the settings_fixtures check reads the literal");
    registry->SetSchemaVersion("renderer", 2);
}

void AddRendererMigrations(sset::MigrationSet* migrations) {
    const SettingsRegistry& builtin = SettingsRegistry::Builtin();
    for (u32 i = 0; i < builtin.Count(); ++i) {
        sset::MigrationStep step;
        step.section = "renderer";
        step.from_version = 1;
        step.op = sset::MigrationOp::Rename;
        step.key = builtin.Find(SettingId{static_cast<u16>(i + 1)})->name; // the flat key of schema 1
        step.new_key = SharedKey(i);
        migrations->Add(step);
    }
}

RenderSettings RenderSettingsFromSnapshot(const sset::Registry& registry, const sset::SettingsSnapshot& snapshot) {
    RenderSettings out;
    const SettingsRegistry& builtin = SettingsRegistry::Builtin();
    for (u32 i = 0; i < builtin.Count(); ++i) {
        const SettingId id{static_cast<u16>(i + 1)};
        const sset::SettingId shared = registry.Find(SharedKey(i));
        if (!shared.IsValid()) continue;
        const sset::Value& v = snapshot.Get(shared);
        switch (builtin.Find(id)->type) {
        case SettingType::Bool: out.SetBool(id, v.AsBool()); break;
        case SettingType::Enum: out.SetEnum(id, v.AsEnum()); break;
        case SettingType::Int: out.SetInt(id, v.AsInt()); break;
        case SettingType::Float: out.SetFloat(id, static_cast<f32>(v.AsFloat())); break;
        case SettingType::Text: {
            char text[kSettingTextBytes];
            CopyText(v.AsText(), text, sizeof(text));
            out.SetText(id, text);
            break;
        }
        }
    }
    return out;
}

void RenderSettingsToChanges(const RenderSettings& settings, const sset::Registry& registry, Vector<sset::ChangeRequest>* out) {
    const SettingsRegistry& builtin = SettingsRegistry::Builtin();
    for (u32 i = 0; i < builtin.Count(); ++i) {
        const SettingId id{static_cast<u16>(i + 1)};
        const sset::SettingId shared = registry.Find(SharedKey(i));
        if (!shared.IsValid()) continue;
        sset::ChangeRequest r;
        r.id = shared;
        switch (builtin.Find(id)->type) {
        case SettingType::Bool: r.value = sset::Value::FromBool(settings.GetBool(id)); break;
        case SettingType::Enum: r.value = sset::Value::FromEnum(settings.GetEnum(id)); break;
        case SettingType::Int: r.value = sset::Value::FromInt(settings.GetInt(id)); break;
        case SettingType::Float: r.value = sset::Value::FromFloat(static_cast<f64>(settings.GetFloat(id))); break;
        case SettingType::Text: r.value = sset::Value::FromText(StringView(settings.GetText(id))); break;
        }
        out->Push(r);
    }
}

// Sets the renderer's value for built-in setting `id` from a shared value.
void StoreValue(RenderSettings* out, SettingId id, SettingType type, const sset::Value& v) {
    switch (type) {
    case SettingType::Bool: out->SetBool(id, v.AsBool()); break;
    case SettingType::Enum: out->SetEnum(id, v.AsEnum()); break;
    case SettingType::Int: out->SetInt(id, v.AsInt()); break;
    case SettingType::Float: out->SetFloat(id, static_cast<f32>(v.AsFloat())); break;
    case SettingType::Text: {
        char text[kSettingTextBytes];
        CopyText(v.AsText(), text, sizeof(text));
        out->SetText(id, text);
        break;
    }
    }
}

bool ApplySharedChange(void* renderer_ptr, const sset::ChangeEvent& event) {
    Renderer* renderer = static_cast<Renderer*>(renderer_ptr);
    const SettingsRegistry& builtin = SettingsRegistry::Builtin();
    // Start from what the settings system holds (it includes changes accepted earlier that the renderer has only queued), then this batch.
    RenderSettings requested = renderer->Settings();
    for (u32 i = 0; i < builtin.Count(); ++i) {
        const SettingId id{static_cast<u16>(i + 1)};
        const sset::SettingId shared = event.registry->Find(SharedKey(i));
        if (!shared.IsValid()) continue;
        StoreValue(&requested, id, builtin.Find(id)->type, event.registry->Current(shared));
    }
    bool any = false;
    for (usize c = 0; c < event.count; ++c) {
        for (u32 i = 0; i < builtin.Count(); ++i) {
            if (event.registry->KeyOf(event.changes[c].id).View() != StringView(SharedKey(i))) continue;
            const SettingId id{static_cast<u16>(i + 1)};
            StoreValue(&requested, id, builtin.Find(id)->type, event.changes[c].new_value);
            any = true;
            break;
        }
    }
    if (!any) return true;
    const ApplyResult result = renderer->ApplySettings(requested);
    return result.ok || result.queued;
}

bool SyncSharedFromRenderer(Renderer* renderer, sset::Settings* settings) {
    const RenderSettings shared = RenderSettingsFromSnapshot(settings->GetRegistry(), *settings->Snapshot());
    // A setting that takes effect at start keeps what the settings system holds: that is the value for the next start, not a change that failed.
    RenderSettings actual = renderer->Settings();
    const SettingsRegistry& builtin = SettingsRegistry::Builtin();
    for (u32 i = 0; i < builtin.Count(); ++i) {
        const SettingId id{static_cast<u16>(i + 1)};
        if (!builtin.Find(id)->restart_required) continue;
        char text[kSettingTextBytes];
        shared.ValueToString(id, text, sizeof(text));
        actual.SetFromString(id, text);
    }
    if (shared == actual) return false;
    Vector<sset::ChangeRequest> changes;
    RenderSettingsToChanges(actual, settings->GetRegistry(), &changes);
    settings->Apply(changes.Data(), changes.Size(), nullptr, nullptr);
    return true;
}

bool RendererCapability(void* renderer, const char* capability) { return static_cast<Renderer*>(renderer)->GetCapabilities().Has(capability); }

} // namespace engine_placeholder::renderer
