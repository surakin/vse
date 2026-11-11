#include "settings/value.h"

#include "log/assert.h"
#include "cstr/mem_ops.h"

namespace engine_placeholder::settings
{

const char* TypeName(Type type)
{
    switch (type)
    {
    case Type::Bool: return "bool";
    case Type::Int: return "int";
    case Type::Float: return "float";
    case Type::Enum: return "enum";
    case Type::Text: return "text";
    }
    return "unknown";
}

Value Value::FromBool(bool b)
{
    Value v;
    v.type_ = Type::Bool;
    v.int_ = b ? 1 : 0;
    return v;
}

Value Value::FromInt(i64 i)
{
    Value v;
    v.type_ = Type::Int;
    v.int_ = i;
    return v;
}

Value Value::FromFloat(f64 f)
{
    ENGINE_ASSERT(f == f && f - f == 0.0, "settings::Value::FromFloat: NaN and infinity are not values");
    Value v;
    v.type_ = Type::Float;
    v.float_ = f;
    return v;
}

Value Value::FromEnum(u32 index)
{
    Value v;
    v.type_ = Type::Enum;
    v.int_ = static_cast<i64>(index);
    return v;
}

Value Value::FromText(StringView text)
{
    ENGINE_ASSERT(text.Length() <= kMaxTextBytes, "settings::Value::FromText: text is too long");
    Value v;
    v.type_ = Type::Text;
    v.text_length_ = static_cast<u16>(text.Length());
    for (usize i = 0; i < text.Length(); ++i)
    {
        v.text_[i] = text[i];
    }
    v.text_[text.Length()] = '\0';
    return v;
}

bool Value::AsBool() const
{
    ENGINE_ASSERT(type_ == Type::Bool, "settings::Value::AsBool: the value is not a bool");
    return int_ != 0;
}

i64 Value::AsInt() const
{
    ENGINE_ASSERT(type_ == Type::Int, "settings::Value::AsInt: the value is not an int");
    return int_;
}

f64 Value::AsFloat() const
{
    ENGINE_ASSERT(type_ == Type::Float, "settings::Value::AsFloat: the value is not a float");
    return float_;
}

u32 Value::AsEnum() const
{
    ENGINE_ASSERT(type_ == Type::Enum, "settings::Value::AsEnum: the value is not an enum");
    return static_cast<u32>(int_);
}

StringView Value::AsText() const
{
    ENGINE_ASSERT(type_ == Type::Text, "settings::Value::AsText: the value is not a text");
    return StringView(text_, text_length_);
}

bool Value::operator==(const Value& other) const
{
    if (type_ != other.type_)
    {
        return false;
    }
    switch (type_)
    {
    case Type::Bool:
    case Type::Int:
    case Type::Enum: return int_ == other.int_;
    case Type::Float: return MemCompare(&float_, &other.float_, sizeof(float_)) == 0;  // bitwise: -0.0 differs from 0.0
    case Type::Text: return StringView(text_, text_length_) == StringView(other.text_, other.text_length_);
    }
    return false;
}

}  // namespace engine_placeholder::settings
