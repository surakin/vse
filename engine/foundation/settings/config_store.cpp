#include "settings/config_store.h"

#include "log/assert.h"

namespace vse::settings
{

const char* StoreErrorName(StoreError error)
{
    switch (error)
    {
    case StoreError::None: return "none";
    case StoreError::NotFound: return "not_found";
    case StoreError::TypeMismatch: return "type_mismatch";
    case StoreError::ReadOnly: return "read_only";
    case StoreError::Damaged: return "damaged";
    case StoreError::Io: return "io";
    case StoreError::Unsupported: return "unsupported";
    case StoreError::TooLong: return "too_long";
    case StoreError::Invalid: return "invalid";
    case StoreError::PermissionDenied: return "permission_denied";
    case StoreError::Interrupted: return "interrupted";
    case StoreError::TooNew: return "too_new";
    }
    return "unknown";
}

StoreError ToStored(const SettingDesc& desc, const Value& value, bool typed, StoredValue* out)
{
    ENGINE_ASSERT(value.GetType() == desc.type, "settings::ToStored: the value's type differs from the declaration's");
    if (typed)
    {
        out->typed = true;
        out->value = value;
        return StoreError::None;
    }
    char text[kMaxValueTextBytes];
    const usize n = FormatValue(desc, value, text, sizeof(text));
    if (n == 0 || n > kMaxTextBytes)
    {
        return StoreError::TooLong;
    }
    // A text value is held quoted (FormatValue's form), so that it reads back as the same text through ParseValue.
    out->typed = false;
    out->value = Value::FromText(StringView(text, n));
    return StoreError::None;
}

ValueError FromStored(const SettingDesc& desc, const StoredValue& stored, Value* out)
{
    if (stored.typed)
    {
        return ValidateValue(desc, stored.value,
                             out);  // a typed store may hold anything: check it as the text path does
    }
    if (stored.value.GetType() != Type::Text)
    {
        return ValueError::BadNumber;
    }
    return ParseValue(desc, stored.value.AsText(), out);
}

}  // namespace vse::settings
