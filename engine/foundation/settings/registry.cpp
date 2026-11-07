#include "settings/registry.h"

#include <cstring>

#include "log/assert.h"
#include "settings/setting.h"

namespace engine_placeholder::settings {

namespace detail {

u64 Encode(const Value& value) {
    u64 bits = 0;
    switch (value.GetType()) {
    case Type::Bool: bits = value.AsBool() ? 1 : 0; break;
    case Type::Int: {
        const i64 v = value.AsInt();
        std::memcpy(&bits, &v, sizeof(bits));
        break;
    }
    case Type::Float: {
        const f64 v = value.AsFloat();
        std::memcpy(&bits, &v, sizeof(bits));
        break;
    }
    case Type::Enum: bits = value.AsEnum(); break;
    case Type::Text: bits = 0; break;
    }
    return bits;
}

} // namespace detail

const char* RegisterErrorName(RegisterError error) {
    switch (error) {
    case RegisterError::None: return "none";
    case RegisterError::BadKey: return "bad_key";
    case RegisterError::Duplicate: return "duplicate";
    case RegisterError::BadDefault: return "bad_default";
    case RegisterError::BadRange: return "bad_range";
    case RegisterError::BadEnum: return "bad_enum";
    case RegisterError::BadDeprecated: return "bad_deprecated";
    case RegisterError::Full: return "full";
    }
    return "unknown";
}

struct Registry::Slot {
    SettingDesc desc;
    Key key;
    Cell cell;
    Value text; // the current value of a text setting (guarded by the registry's text mutex)
};

Registry::Registry() = default;
Registry::~Registry() = default;

Registry& Registry::Global() {
    static Registry instance;
    return instance;
}

namespace {

bool FinitePair(f64 a, f64 b) { return a == a && b == b && a - a == 0.0 && b - b == 0.0; }

bool GoodEnumName(const char* name) {
    if (name == nullptr || name[0] == '\0' || std::strlen(name) > kMaxEnumNameBytes) return false;
    for (const char* p = name; *p != '\0'; ++p) { // printable ASCII, no blank, and none of the characters the file format uses
        const u8 c = static_cast<u8>(*p);
        if (c <= 0x20 || c >= 0x7f || c == '=' || c == '#' || c == ';' || c == '"' || c == '[' || c == ']' || c == '\\') return false;
    }
    return true;
}

} // namespace

usize Registry::LowerBound(StringView key) const {
    usize lo = 0, hi = sorted_.Size();
    while (lo < hi) {
        const usize mid = lo + (hi - lo) / 2;
        if (slots_[sorted_[mid] - 1u]->key.View().Compare(key) < 0) lo = mid + 1; else hi = mid;
    }
    return lo;
}

SettingId Registry::Register(const SettingDesc& desc, RegisterError* error) {
    ENGINE_ASSERT(!frozen_.load(std::memory_order_relaxed), "settings::Registry::Register: the registry is frozen");
    auto fail = [&](RegisterError e) {
        if (error != nullptr) *error = e;
        return SettingId{};
    };
    if (error != nullptr) *error = RegisterError::None;
    if (slots_.Size() >= kMaxRegisteredSettings) return fail(RegisterError::Full);
    Key key;
    if (desc.key == nullptr || Key::Parse(StringView(desc.key), &key) != KeyError::None) return fail(RegisterError::BadKey);
    // `version` and `<section>.schema` hold the file's version stamps; a setting cannot take them.
    if (key.View() == "version" || key.View().EndsWith(".schema")) return fail(RegisterError::BadKey);
    // `__` is how the environment variable name writes a dot, so a key containing it would share a variable with another key.
    if (key.View().Contains("__")) return fail(RegisterError::BadKey);
    if (Find(key.View()).IsValid()) return fail(RegisterError::Duplicate);
    if (desc.deprecated_count > kMaxDeprecatedNames || (desc.deprecated_count != 0 && desc.deprecated_keys == nullptr)) return fail(RegisterError::BadDeprecated);
    for (u32 i = 0; i < desc.deprecated_count; ++i) {
        if (desc.deprecated_keys[i] == nullptr) return fail(RegisterError::BadDeprecated);
        Key old;
        if (Key::Parse(StringView(desc.deprecated_keys[i]), &old) != KeyError::None) return fail(RegisterError::BadKey);
        if (old.View() == "version" || old.View().EndsWith(".schema") || old.View().Contains("__")) return fail(RegisterError::BadKey);
        if (Find(old.View()).IsValid() || old == key) return fail(RegisterError::Duplicate);
        for (u32 j = 0; j < i; ++j) {
            if (StringView(desc.deprecated_keys[j]) == old.View()) return fail(RegisterError::Duplicate);
        }
    }
    const Value& def = desc.default_value;
    if (def.GetType() != desc.type) return fail(RegisterError::BadDefault);
    switch (desc.type) {
    case Type::Bool: break;
    case Type::Int:
        if (desc.min_int > desc.max_int) return fail(RegisterError::BadRange);
        if (def.AsInt() < desc.min_int || def.AsInt() > desc.max_int) return fail(RegisterError::BadDefault);
        break;
    case Type::Float:
        if (!FinitePair(desc.min_float, desc.max_float) || desc.min_float > desc.max_float) return fail(RegisterError::BadRange);
        if (def.AsFloat() < desc.min_float || def.AsFloat() > desc.max_float) return fail(RegisterError::BadDefault);
        break;
    case Type::Enum:
        if (desc.enum_names == nullptr || desc.enum_count == 0 || desc.enum_count > kMaxEnumNames) return fail(RegisterError::BadEnum);
        for (u32 i = 0; i < desc.enum_count; ++i) {
            if (!GoodEnumName(desc.enum_names[i])) return fail(RegisterError::BadEnum);
            for (u32 j = 0; j < i; ++j) {
                if (std::strcmp(desc.enum_names[i], desc.enum_names[j]) == 0) return fail(RegisterError::BadEnum);
            }
        }
        if (def.AsEnum() >= desc.enum_count) return fail(RegisterError::BadDefault);
        break;
    case Type::Text:
        if (desc.max_text > kMaxTextBytes || (desc.max_text != 0 && def.AsText().Length() > desc.max_text)) return fail(RegisterError::BadDefault);
        break;
    }
    UniquePtr<Slot> slot = MakeUnique<Slot>();
    slot->desc = desc;
    slot->key = key;
    slot->text = def;
    slot->cell.bits.store(detail::Encode(def), std::memory_order_relaxed);
    slots_.Push(static_cast<UniquePtr<Slot>&&>(slot));
    const SettingId id{static_cast<u16>(slots_.Size())};
    sorted_.Insert(LowerBound(key.View()), id.value);
    if (desc.deprecated_count != 0) aliases_sorted_.Push(id.value);
    return id;
}

void Registry::SetSchemaVersion(StringView section, u32 version) {
    ENGINE_ASSERT(!frozen_.load(std::memory_order_relaxed), "settings::Registry::SetSchemaVersion: the registry is frozen");
    Key key;
    ENGINE_ASSERT(version != 0 && Key::Parse(section, &key) == KeyError::None, "settings::Registry::SetSchemaVersion: a valid section and a version of at least 1 are needed");
    for (usize i = 0; i < schemas_.Size(); ++i) {
        if (schemas_[i].section == key) {
            schemas_[i].version = version;
            return;
        }
    }
    Schema s;
    s.section = key;
    s.version = version;
    schemas_.Push(s);
}

u32 Registry::SchemaVersion(StringView section) const {
    for (usize i = 0; i < schemas_.Size(); ++i) {
        if (schemas_[i].section.View() == section) return schemas_[i].version;
    }
    return 0;
}

StringView Registry::SchemaAt(u32 index, u32* version) const {
    ENGINE_ASSERT(index < schemas_.Size(), "settings::Registry::SchemaAt: index out of range");
    *version = schemas_[index].version;
    return schemas_[index].section.View();
}

void Registry::Freeze() { frozen_.store(true, std::memory_order_relaxed); }

SettingId Registry::Find(StringView key) const {
    const usize at = LowerBound(key);
    if (at < sorted_.Size() && slots_[sorted_[at] - 1u]->key.View() == key) return SettingId{sorted_[at]};
    for (usize i = 0; i < aliases_sorted_.Size(); ++i) {
        const Slot& s = *slots_[aliases_sorted_[i] - 1u];
        for (u32 k = 0; k < s.desc.deprecated_count; ++k) {
            if (StringView(s.desc.deprecated_keys[k]) == key) return SettingId{aliases_sorted_[i]};
        }
    }
    return SettingId{};
}

bool Registry::IsDeprecatedKey(StringView key) const {
    const SettingId id = Find(key);
    return id.IsValid() && slots_[id.value - 1u]->key.View() != key;
}

const SettingDesc* Registry::Desc(SettingId id) const {
    return id.value >= 1 && id.value <= slots_.Size() ? &slots_[id.value - 1u]->desc : nullptr;
}

const Key& Registry::KeyOf(SettingId id) const {
    static const Key kNone;
    return id.value >= 1 && id.value <= slots_.Size() ? slots_[id.value - 1u]->key : kNone;
}

SettingId Registry::SortedAt(u32 index) const {
    ENGINE_ASSERT(index < sorted_.Size(), "settings::Registry::SortedAt: index out of range");
    return SettingId{sorted_[index]};
}

const Registry::Cell* Registry::CellOf(SettingId id) const {
    return id.value >= 1 && id.value <= slots_.Size() ? &slots_[id.value - 1u]->cell : nullptr;
}

Value Registry::Current(SettingId id) const {
    ENGINE_ASSERT(id.value >= 1 && id.value <= slots_.Size(), "settings::Registry::Current: unknown id");
    const Slot& s = *slots_[id.value - 1u];
    if (s.desc.type == Type::Text) {
        std::lock_guard<std::mutex> lock(text_mutex_);
        return s.text;
    }
    const u64 bits = s.cell.bits.load(std::memory_order_relaxed);
    switch (s.desc.type) {
    case Type::Bool: return Value::FromBool(bits != 0);
    case Type::Int: return Value::FromInt(detail::Decode<i64>(bits));
    case Type::Float: return Value::FromFloat(detail::Decode<f64>(bits));
    case Type::Enum: return Value::FromEnum(static_cast<u32>(bits));
    case Type::Text: break;
    }
    return Value();
}

void Registry::Publish(SettingId id, const Value& value) {
    ENGINE_ASSERT(id.value >= 1 && id.value <= slots_.Size(), "settings::Registry::Publish: unknown id");
    Slot& s = *slots_[id.value - 1u];
    ENGINE_ASSERT(value.GetType() == s.desc.type, "settings::Registry::Publish: the value has another type than the setting");
    ENGINE_ASSERT(s.desc.type != Type::Enum || value.AsEnum() < s.desc.enum_count, "settings::Registry::Publish: the enum index is outside the names");
    if (s.desc.type == Type::Text) {
        std::lock_guard<std::mutex> lock(text_mutex_);
        s.text = value;
        return;
    }
    s.cell.bits.store(detail::Encode(value), std::memory_order_relaxed);
}

usize Registry::CurrentText(SettingId id, char* out, usize capacity) const {
    ENGINE_ASSERT(id.value >= 1 && id.value <= slots_.Size() && slots_[id.value - 1u]->desc.type == Type::Text,
                  "settings::Registry::CurrentText: not a registered text setting");
    std::lock_guard<std::mutex> lock(text_mutex_);
    const StringView t = slots_[id.value - 1u]->text.AsText();
    if (t.Length() + 1 <= capacity) {
        std::memcpy(out, t.Data(), t.Length());
        out[t.Length()] = '\0';
    }
    return t.Length();
}

} // namespace engine_placeholder::settings
