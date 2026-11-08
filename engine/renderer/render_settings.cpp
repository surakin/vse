#include "renderer/render_settings.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "log/assert.h"
#include "renderer/renderer_log.h"
#include "rhi/rhi.h"

namespace engine_placeholder::renderer {

namespace {

bool ValidName(const char* name) {
    if (name == nullptr || name[0] == '\0') {
        return false;
    }
    usize length = 0;
    for (; name[length] != '\0'; ++length) {
        const char c = name[length];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') || length >= 31) {
            return false;
        }
    }
    return true;
}

bool ValidText(const char* text) {
    if (text[0] == ' ' || (text[0] != '\0' && text[std::strlen(text) - 1] == ' ')) {
        return false; // the file format trims blanks, so they would not survive a round trip
    }
    usize length = 0;
    for (; text[length] != '\0'; ++length) {
        const byte c = static_cast<byte>(text[length]);
        if (c < 32 || c > 126 || length + 1 >= kSettingTextBytes) {
            return false;
        }
    }
    return true;
}

bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

// An enum name must survive the text format: 1 to 31 printable characters, no blank, '=' or '#'.
bool ValidEnumName(const char* name) {
    if (name == nullptr || name[0] == '\0') {
        return false;
    }
    usize length = 0;
    for (; name[length] != '\0'; ++length) {
        const byte c = static_cast<byte>(name[length]);
        if (c <= 32 || c > 126 || c == '=' || c == '#' || length >= 31) {
            return false;
        }
    }
    return true;
}

// Whether setting `id` has the same value in both.
bool SameValue(const RenderSettings& a, const RenderSettings& b, SettingId id, const SettingDesc& d) {
    switch (d.type) {
        case SettingType::Bool: return a.GetBool(id) == b.GetBool(id);
        case SettingType::Enum: return a.GetEnum(id) == b.GetEnum(id);
        case SettingType::Int: return a.GetInt(id) == b.GetInt(id);
        case SettingType::Float: return a.GetFloat(id) == b.GetFloat(id);
        case SettingType::Text: return std::strcmp(a.GetText(id), b.GetText(id)) == 0;
    }
    return false;
}

// In rhi::Backend's order, so a value converts by a cast (renderer.cpp).
const char* const kBackendNames[] = {"null", "vulkan", "dx12", "auto"};
static_assert(static_cast<u32>(rhi::Backend::Null) == 0 && static_cast<u32>(rhi::Backend::Vulkan) == 1 &&
                  static_cast<u32>(rhi::Backend::Dx12) == 2 && static_cast<u32>(rhi::Backend::Auto) == 3,
              "kBackendNames follows rhi::Backend");
const char* const kValidationNames[] = {"auto", "off", "on"};
const char* const kPresentNames[] = {"fifo", "mailbox", "immediate"};
const char* const kFormatNames[] = {"auto", "srgb8", "rgba16f", "rgb10a2"};
const char* const kHdrNames[] = {"off", "scrgb", "hdr10"};
const char* const kQualityNames[] = {"low", "medium", "high"};
const char* const kLayerNames[] = {"none", "albedo", "normal", "material", "depth", "worldpos"};

bool AllEnumNamesValid(const SettingDesc& desc) {
    for (u32 i = 0; i < desc.enum_count; ++i) {
        if (!ValidEnumName(desc.enum_names[i])) {
            return false;
        }
    }
    return true;
}

SettingDesc MakeBool(const char* name, CostClass cost, bool value) {
    SettingDesc d;
    d.name = name;
    d.type = SettingType::Bool;
    d.cost = cost;
    d.default_int = value ? 1 : 0;
    return d;
}

template <usize N>
SettingDesc MakeEnum(const char* name, CostClass cost, const char* const (&names)[N], u32 default_index) {
    SettingDesc d;
    d.name = name;
    d.type = SettingType::Enum;
    d.cost = cost;
    d.enum_names = names;
    d.enum_count = static_cast<u32>(N);
    d.default_int = default_index;
    return d;
}

SettingDesc MakeInt(const char* name, CostClass cost, i64 default_value, i64 min, i64 max) {
    SettingDesc d;
    d.name = name;
    d.type = SettingType::Int;
    d.cost = cost;
    d.default_int = default_value;
    d.min_int = min;
    d.max_int = max;
    return d;
}

} // namespace

const char* CostClassName(CostClass cost) {
    switch (cost) {
        case CostClass::None: return "none";
        case CostClass::Dynamic: return "dynamic";
        case CostClass::Swapchain: return "swapchain";
        case CostClass::PipelineRebuild: return "pipeline-rebuild";
        case CostClass::DeviceReset: return "device-reset";
    }
    return "unknown";
}

SettingId SettingsRegistry::Register(const SettingDesc& desc) {
    const char* problem = nullptr;
    if (count_ >= kMaxSettings) {
        problem = "the registry is full";
    } else if (!ValidName(desc.name)) {
        problem = "the name must be 1 to 31 characters of a-z, 0-9 and _";
    } else if (FindByName(desc.name).IsValid()) {
        problem = "the name is already registered";
    } else {
        switch (desc.type) {
            case SettingType::Bool:
                if (desc.default_int != 0 && desc.default_int != 1) problem = "a bool default is 0 or 1";
                break;
            case SettingType::Enum:
                if (desc.enum_names == nullptr || desc.enum_count == 0 || desc.enum_count > 16) {
                    problem = "an enum needs 1 to 16 names";
                } else if (!AllEnumNamesValid(desc)) {
                    problem = "an enum name must be 1 to 31 printable characters without blanks, '=' or '#'";
                } else if (desc.default_int < 0 || desc.default_int >= static_cast<i64>(desc.enum_count)) {
                    problem = "the enum default is outside its names";
                }
                break;
            case SettingType::Int:
                if (desc.min_int > desc.max_int || desc.default_int < desc.min_int || desc.default_int > desc.max_int) {
                    problem = "the int range is empty or excludes the default";
                }
                break;
            case SettingType::Float:
                if (!std::isfinite(desc.min_float) || !std::isfinite(desc.max_float) || !std::isfinite(desc.default_float) ||
                    desc.min_float > desc.max_float || desc.default_float < desc.min_float || desc.default_float > desc.max_float) {
                    problem = "the float range is not finite, is empty or excludes the default";
                }
                break;
            case SettingType::Text:
                if (desc.default_text != nullptr && !ValidText(desc.default_text)) problem = "the text default is too long or not printable ASCII";
                break;
        }
    }
    if (problem != nullptr) {
        RenderLogV(LogVerbosity::Error, "settings: cannot register '%s': %s", desc.name != nullptr ? desc.name : "(null)", problem);
        return SettingId{};
    }
    descs_[count_] = desc;
    ++count_;
    return SettingId{static_cast<u16>(count_)};
}

const SettingDesc* SettingsRegistry::Find(SettingId id) const {
    if (id.value == 0 || id.value > count_) {
        return nullptr;
    }
    return &descs_[id.value - 1];
}

SettingId SettingsRegistry::FindByName(const char* name) const {
    if (name == nullptr) {
        return SettingId{};
    }
    for (u32 i = 0; i < count_; ++i) {
        if (std::strcmp(descs_[i].name, name) == 0) {
            return SettingId{static_cast<u16>(i + 1)};
        }
    }
    return SettingId{};
}

SettingsRegistry& SettingsRegistry::Builtin() {
    static SettingsRegistry registry = [] {
        SettingsRegistry r;
        RegisterBuiltinSettings(&r);
        return r;
    }();
    return registry;
}

void RegisterBuiltinSettings(SettingsRegistry* registry) {
    ENGINE_ASSERT(registry != nullptr && registry->Count() == 0, "built-in settings go into an empty registry");
    auto add = [registry](SettingDesc d, SettingId expected) {
        const SettingId id = registry->Register(d);
        ENGINE_ASSERT(id == expected, "a built-in setting got an unexpected id");
    };
    add(MakeEnum("backend", CostClass::DeviceReset, kBackendNames, 3), settings::kBackend);
    {
        SettingDesc d;
        d.name = "adapter";
        d.type = SettingType::Text;
        d.cost = CostClass::DeviceReset;
        add(d, settings::kAdapter);
    }
    add(MakeEnum("validation", CostClass::DeviceReset, kValidationNames, 0), settings::kValidation);
    add(MakeBool("sync_validation", CostClass::DeviceReset, false), settings::kSyncValidation);
    add(MakeInt("frames_in_flight", CostClass::DeviceReset, 2, 1, 3), settings::kFramesInFlight);
    add(MakeEnum("present_mode", CostClass::Swapchain, kPresentNames, 0), settings::kPresentMode);
    add(MakeInt("width", CostClass::Swapchain, 0, 0, 16384), settings::kWidth);
    add(MakeInt("height", CostClass::Swapchain, 0, 0, 16384), settings::kHeight);
    {
        SettingDesc d = MakeBool("fullscreen", CostClass::Swapchain, false);
        d.capability = "fullscreen";
        add(d, settings::kFullscreen);
    }
    add(MakeEnum("swapchain_format", CostClass::Swapchain, kFormatNames, 0), settings::kSwapchainFormat);
    {
        SettingDesc d = MakeEnum("hdr", CostClass::Swapchain, kHdrNames, 0);
        d.capability = "hdr";
        add(d, settings::kHdr);
    }
    {
        SettingDesc d = MakeEnum("shader_quality", CostClass::PipelineRebuild, kQualityNames, 2);
        d.mechanism_only = true;
        add(d, settings::kShaderQuality);
    }
    add(MakeEnum("gbuffer_debug_layer", CostClass::Dynamic, kLayerNames, 0), settings::kGBufferDebugLayer);
    {
        SettingDesc d;
        d.name = "render_scale";
        d.type = SettingType::Float;
        d.cost = CostClass::Dynamic;
        d.mechanism_only = true;
        d.default_float = 1.0f;
        d.min_float = 0.25f;
        d.max_float = 2.0f;
        add(d, settings::kRenderScale);
    }
}

RenderSettings::RenderSettings(const SettingsRegistry& registry) : registry_(&registry) {}

RenderSettings::Value RenderSettings::Get(SettingId id, const SettingDesc* desc) const {
    if (id.value <= count_) {
        return values_[id.value - 1];
    }
    Value v;
    switch (desc->type) {
        case SettingType::Float: v.real = desc->default_float; break;
        case SettingType::Text:
            if (desc->default_text != nullptr) std::strncpy(v.text, desc->default_text, kSettingTextBytes - 1);
            break;
        default: v.integer = desc->default_int; break;
    }
    return v;
}

void RenderSettings::Store(SettingId id, const Value& value) {
    while (count_ < id.value) { // fill the gap with defaults so reads below count_ are all real values
        const SettingId gap{static_cast<u16>(count_ + 1)};
        values_[count_] = Get(gap, registry_->Find(gap));
        ++count_;
    }
    values_[id.value - 1] = value;
}

namespace {
const SettingDesc* TypedDesc(const SettingsRegistry& registry, SettingId id, SettingType type) {
    const SettingDesc* d = registry.Find(id);
    return d != nullptr && d->type == type ? d : nullptr;
}
} // namespace

bool RenderSettings::GetBool(SettingId id) const {
    const SettingDesc* d = TypedDesc(*registry_, id, SettingType::Bool);
    return d != nullptr && Get(id, d).integer != 0;
}
i64 RenderSettings::GetInt(SettingId id) const {
    const SettingDesc* d = TypedDesc(*registry_, id, SettingType::Int);
    return d != nullptr ? Get(id, d).integer : 0;
}
u32 RenderSettings::GetEnum(SettingId id) const {
    const SettingDesc* d = TypedDesc(*registry_, id, SettingType::Enum);
    return d != nullptr ? static_cast<u32>(Get(id, d).integer) : 0;
}
f32 RenderSettings::GetFloat(SettingId id) const {
    const SettingDesc* d = TypedDesc(*registry_, id, SettingType::Float);
    return d != nullptr ? Get(id, d).real : 0.0f;
}
const char* RenderSettings::GetText(SettingId id) const {
    const SettingDesc* d = TypedDesc(*registry_, id, SettingType::Text);
    if (d == nullptr) {
        return "";
    }
    if (id.value <= count_) {
        return values_[id.value - 1].text;
    }
    return d->default_text != nullptr ? d->default_text : "";
}

SetStatus RenderSettings::SetBool(SettingId id, bool value) {
    if (registry_->Find(id) == nullptr) return SetStatus::UnknownId;
    if (TypedDesc(*registry_, id, SettingType::Bool) == nullptr) return SetStatus::WrongType;
    Value v;
    v.integer = value ? 1 : 0;
    Store(id, v);
    return SetStatus::Ok;
}

SetStatus RenderSettings::SetInt(SettingId id, i64 value) {
    if (registry_->Find(id) == nullptr) return SetStatus::UnknownId;
    const SettingDesc* d = TypedDesc(*registry_, id, SettingType::Int);
    if (d == nullptr) return SetStatus::WrongType;
    SetStatus status = SetStatus::Ok;
    if (value < d->min_int || value > d->max_int) {
        if (d->range_policy == RangePolicy::Reject) return SetStatus::OutOfRange;
        value = value < d->min_int ? d->min_int : d->max_int;
        status = SetStatus::Clamped;
    }
    Value v;
    v.integer = value;
    Store(id, v);
    return status;
}

SetStatus RenderSettings::SetEnum(SettingId id, u32 index) {
    if (registry_->Find(id) == nullptr) return SetStatus::UnknownId;
    const SettingDesc* d = TypedDesc(*registry_, id, SettingType::Enum);
    if (d == nullptr) return SetStatus::WrongType;
    if (index >= d->enum_count) return SetStatus::OutOfRange;
    Value v;
    v.integer = index;
    Store(id, v);
    return SetStatus::Ok;
}

SetStatus RenderSettings::SetFloat(SettingId id, f32 value) {
    if (registry_->Find(id) == nullptr) return SetStatus::UnknownId;
    const SettingDesc* d = TypedDesc(*registry_, id, SettingType::Float);
    if (d == nullptr) return SetStatus::WrongType;
    if (!std::isfinite(value)) return SetStatus::OutOfRange;
    SetStatus status = SetStatus::Ok;
    if (value < d->min_float || value > d->max_float) {
        if (d->range_policy == RangePolicy::Reject) return SetStatus::OutOfRange;
        value = value < d->min_float ? d->min_float : d->max_float;
        status = SetStatus::Clamped;
    }
    Value v;
    v.real = value;
    Store(id, v);
    return status;
}

SetStatus RenderSettings::SetText(SettingId id, const char* value) {
    if (registry_->Find(id) == nullptr) return SetStatus::UnknownId;
    if (TypedDesc(*registry_, id, SettingType::Text) == nullptr) return SetStatus::WrongType;
    if (value == nullptr) value = "";
    if (!ValidText(value)) return SetStatus::BadText;
    Value v;
    std::strncpy(v.text, value, kSettingTextBytes - 1);
    Store(id, v);
    return SetStatus::Ok;
}

SetStatus RenderSettings::SetFromString(SettingId id, const char* text) {
    const SettingDesc* d = registry_->Find(id);
    if (d == nullptr) return SetStatus::UnknownId;
    if (text == nullptr) text = "";
    switch (d->type) {
        case SettingType::Bool:
            if (std::strcmp(text, "true") == 0) return SetBool(id, true);
            if (std::strcmp(text, "false") == 0) return SetBool(id, false);
            return SetStatus::OutOfRange;
        case SettingType::Enum:
            for (u32 i = 0; i < d->enum_count; ++i) {
                if (std::strcmp(d->enum_names[i], text) == 0) return SetEnum(id, i);
            }
            return SetStatus::OutOfRange;
        case SettingType::Int: {
            errno = 0;
            char* end = nullptr;
            const long long parsed = std::strtoll(text, &end, 10);
            if (text[0] == '\0' || *end != '\0' || errno == ERANGE) return SetStatus::OutOfRange;
            return SetInt(id, parsed);
        }
        case SettingType::Float: {
            errno = 0;
            char* end = nullptr;
            const float parsed = std::strtof(text, &end);
            if (text[0] == '\0' || *end != '\0') return SetStatus::OutOfRange;
            return SetFloat(id, parsed);
        }
        case SettingType::Text: return SetText(id, text);
    }
    return SetStatus::WrongType;
}

usize RenderSettings::ValueToString(SettingId id, char* out, usize capacity) const {
    const SettingDesc* d = registry_->Find(id);
    if (d == nullptr) {
        if (capacity > 0) out[0] = '\0';
        return 0;
    }
    const Value v = Get(id, d);
    int n = 0;
    switch (d->type) {
        case SettingType::Bool: n = std::snprintf(out, capacity, "%s", v.integer != 0 ? "true" : "false"); break;
        case SettingType::Enum: n = std::snprintf(out, capacity, "%s", d->enum_names[v.integer]); break;
        case SettingType::Int: n = std::snprintf(out, capacity, "%lld", static_cast<long long>(v.integer)); break;
        case SettingType::Float: n = std::snprintf(out, capacity, "%.9g", static_cast<double>(v.real)); break;
        case SettingType::Text: n = std::snprintf(out, capacity, "%s", v.text); break;
    }
    return n < 0 ? 0 : static_cast<usize>(n);
}

bool RenderSettings::operator==(const RenderSettings& other) const {
    ENGINE_ASSERT(registry_ == other.registry_, "settings that follow different registries cannot be compared");
    for (u32 i = 1; i <= registry_->Count(); ++i) {
        const SettingId id{static_cast<u16>(i)};
        if (!SameValue(*this, other, id, *registry_->Find(id))) {
            return false;
        }
    }
    return true;
}

bool Capabilities::Add(const char* name) {
    if (Has(name)) {
        return true;
    }
    if (count_ >= kMaxCapabilities) {
        return false;
    }
    names_[count_++] = name;
    return true;
}

void Capabilities::SetAdapterCount(u32 count) {
    adapter_count_ = count;
}

bool Capabilities::Has(const char* name) const {
    if (name == nullptr) {
        return true;
    }
    if (std::strcmp(name, "multi_adapter") == 0) {
        return adapter_count_ > 1; // derived, so a later smaller count removes it
    }
    for (u32 i = 0; i < count_; ++i) {
        if (std::strcmp(names_[i], name) == 0) {
            return true;
        }
    }
    return false;
}

SettingsChange DiffSettings(const RenderSettings& from, const RenderSettings& to, const Capabilities& capabilities) {
    ENGINE_ASSERT(&from.Registry() == &to.Registry(), "settings that follow different registries cannot be diffed");
    SettingsChange change;
    const SettingsRegistry& registry = from.Registry();
    const RenderSettings defaults(registry);
    for (u32 i = 1; i <= registry.Count(); ++i) {
        const SettingId id{static_cast<u16>(i)};
        const SettingDesc* d = registry.Find(id);
        if (SameValue(from, to, id, *d)) {
            continue;
        }
        // A capability is needed to turn something on, not to put a setting back to its default.
        if (!SameValue(to, defaults, id, *d) && !capabilities.Has(d->capability)) {
            change.unsupported[change.unsupported_count++] = UnsupportedSetting{id, d->capability};
            continue;
        }
        change.changed[change.changed_count++] = id;
        if (d->cost > change.cost) {
            change.cost = d->cost;
        }
    }
    return change;
}

RenderSettings ResolveSettings(const RenderSettings& current, const RenderSettings& requested, const Capabilities& capabilities) {
    const SettingsChange change = DiffSettings(current, requested, capabilities);
    RenderSettings result = requested;
    for (u32 i = 0; i < change.unsupported_count; ++i) {
        char text[kSettingTextBytes];
        current.ValueToString(change.unsupported[i].id, text, sizeof(text));
        result.SetFromString(change.unsupported[i].id, text);
    }
    return result;
}

usize SerializeSettings(const RenderSettings& settings, char* out, usize capacity) {
    usize total = 0;
    auto append = [&](const char* fmt, const char* a, const char* b) {
        const usize room = out != nullptr && total < capacity ? capacity - total : 0;
        const int n = std::snprintf(out != nullptr ? out + total : nullptr, room, fmt, a, b);
        if (n > 0) total += static_cast<usize>(n);
    };
    append("# engine render settings%s%s\n", "", "");
    char version[16];
    std::snprintf(version, sizeof(version), "%u", kSettingsFormatVersion);
    append("version = %s%s\n", version, "");
    const SettingsRegistry& registry = settings.Registry();
    for (u32 i = 1; i <= registry.Count(); ++i) {
        const SettingId id{static_cast<u16>(i)};
        char value[kSettingTextBytes];
        settings.ValueToString(id, value, sizeof(value));
        append("%s = %s\n", registry.Find(id)->name, value);
    }
    if (out != nullptr && capacity > 0 && total >= capacity) {
        out[capacity - 1] = '\0';
    }
    return total;
}

namespace {

void AddProblem(SettingsProblem* list, u32* count, u32 line, const char* key, const char* reason) {
    if (*count < kMaxParseProblems) {
        list[*count].line = line;
        std::snprintf(list[*count].message, sizeof(list[*count].message), "%s: %s", key, reason);
    }
    ++*count;
}

// Splits `text` into lines; calls `fn(line_number, begin, end)` with trailing blanks and the newline removed.
template <typename Fn>
void ForEachLine(const char* text, usize length, Fn fn) {
    u32 number = 0;
    usize pos = 0;
    while (pos < length) {
        usize end = pos;
        while (end < length && text[end] != '\n') ++end;
        ++number;
        usize b = pos;
        usize e = end;
        while (b < e && IsSpace(text[b])) ++b;
        while (e > b && IsSpace(text[e - 1])) --e;
        fn(number, b, e);
        pos = end + 1;
    }
}

} // namespace

bool ParseSettings(const char* text, usize length, RenderSettings* settings, SettingsParseReport* report, SettingsKeyMigrationFn migrate) {
    ENGINE_ASSERT(settings != nullptr, "ParseSettings needs settings to fill");
    SettingsParseReport local;
    SettingsParseReport& r = report != nullptr ? *report : local;
    r = SettingsParseReport{};
    if (text == nullptr) {
        length = 0;
    }
    const SettingsRegistry& registry = settings->Registry();
    bool seen[kMaxSettings + 1] = {};
    RenderSettings work = *settings;
    bool version_seen = false;

    // First pass: the version, so the second pass knows which keys to migrate.
    ForEachLine(text, length, [&](u32 line, usize b, usize e) {
        if (b == e || text[b] == '#') return;
        char buf[128];
        const usize n = e - b < sizeof(buf) - 1 ? e - b : sizeof(buf) - 1;
        std::memcpy(buf, text + b, n);
        buf[n] = '\0';
        char* eq = std::strchr(buf, '=');
        if (eq == nullptr) return;
        char* key_end = eq;
        while (key_end > buf && IsSpace(key_end[-1])) --key_end;
        *key_end = '\0';
        if (std::strcmp(buf, "version") != 0) return;
        char* value = eq + 1;
        while (IsSpace(*value)) ++value;
        char* end = nullptr;
        const unsigned long v = std::strtoul(value, &end, 10);
        if (version_seen) {
            AddProblem(r.errors, &r.error_count, line, "version", "declared twice");
        } else if (value[0] == '\0' || *end != '\0') {
            AddProblem(r.errors, &r.error_count, line, "version", "not a number");
        } else if (v > kSettingsFormatVersion) {
            AddProblem(r.errors, &r.error_count, line, "version", "newer than this program reads");
        } else {
            r.version = static_cast<u32>(v);
        }
        version_seen = true;
    });

    ForEachLine(text, length, [&](u32 line, usize b, usize e) {
        if (b == e || text[b] == '#') return;
        char buf[256];
        if (e - b >= sizeof(buf)) {
            AddProblem(r.errors, &r.error_count, line, "line", "too long");
            return;
        }
        std::memcpy(buf, text + b, e - b);
        buf[e - b] = '\0';
        char* eq = std::strchr(buf, '=');
        if (eq == nullptr) {
            AddProblem(r.errors, &r.error_count, line, "line", "expected key = value");
            return;
        }
        char* key_end = eq;
        while (key_end > buf && IsSpace(key_end[-1])) --key_end;
        *key_end = '\0';
        char* value = eq + 1;
        while (IsSpace(*value)) ++value;
        if (std::strcmp(buf, "version") == 0) return; // handled in the first pass
        const char* key = buf;
        if (migrate != nullptr) {
            const char* renamed = migrate(r.version, key);
            if (renamed != nullptr) key = renamed;
        }
        const SettingId id = registry.FindByName(key);
        if (!id.IsValid()) {
            AddProblem(r.warnings, &r.warning_count, line, key, "unknown key, ignored");
            return;
        }
        if (seen[id.value]) {
            AddProblem(r.errors, &r.error_count, line, key, "repeated key");
            return;
        }
        seen[id.value] = true;
        const SetStatus status = work.SetFromString(id, value);
        if (status == SetStatus::OutOfRange || status == SetStatus::BadText || status == SetStatus::WrongType) {
            AddProblem(r.errors, &r.error_count, line, key, "bad value");
        } else if (status == SetStatus::Clamped) {
            AddProblem(r.warnings, &r.warning_count, line, key, "value clamped into its range");
        }
    });

    if (r.error_count != 0) {
        return false;
    }
    work.SetGeneration(settings->Generation());
    *settings = work;
    return true;
}

} // namespace engine_placeholder::renderer
