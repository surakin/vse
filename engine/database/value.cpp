#include "database/value.h"

#include "cstr/mem_ops.h"
#include "log/assert.h"

namespace engine_placeholder::database {

const char* ValueTypeName(ValueType type) {
    switch (type) {
    case ValueType::Null: return "null";
    case ValueType::Integer: return "integer";
    case ValueType::Real: return "real";
    case ValueType::Text: return "text";
    case ValueType::Blob: return "blob";
    }
    return "null";
}

i64 ValueView::AsInteger() const {
    ENGINE_ASSERT(type_ == ValueType::Integer, "ValueView::AsInteger: the value is not an integer");
    return integer_;
}

f64 ValueView::AsReal() const {
    ENGINE_ASSERT(type_ == ValueType::Real, "ValueView::AsReal: the value is not a real");
    return real_;
}

StringView ValueView::AsText() const {
    ENGINE_ASSERT(type_ == ValueType::Text, "ValueView::AsText: the value is not text");
    return text_;
}

ByteSpan ValueView::AsBlob() const {
    ENGINE_ASSERT(type_ == ValueType::Blob, "ValueView::AsBlob: the value is not a blob");
    return bytes_;
}

namespace {
bool SameBytes(const void* a, const void* b, usize count) { return count == 0 || MemCompare(a, b, count) == 0; }
} // namespace

bool ValueView::operator==(const ValueView& other) const {
    if (type_ != other.type_) {
        return false;
    }
    switch (type_) {
    case ValueType::Null: return true;
    case ValueType::Integer: return integer_ == other.integer_;
    case ValueType::Real: return SameBytes(&real_, &other.real_, sizeof(f64));
    case ValueType::Text: return text_.Length() == other.text_.Length() && SameBytes(text_.Data(), other.text_.Data(), text_.Length());
    case ValueType::Blob: return bytes_.Size() == other.bytes_.Size() && SameBytes(bytes_.Data(), other.bytes_.Data(), bytes_.Size());
    }
    return false;
}

Value Value::Integer(i64 value) {
    Value v;
    v.type_ = ValueType::Integer;
    v.integer_ = value;
    return v;
}

Value Value::Real(f64 value) {
    Value v;
    v.type_ = ValueType::Real;
    v.real_ = value;
    return v;
}

Value Value::Text(StringView text) {
    Value v;
    v.type_ = ValueType::Text;
    v.bytes_.Resize(text.Length());
    if (text.Length() != 0) {
        MemCopy(v.bytes_.Data(), text.Data(), text.Length());
    }
    return v;
}

Value Value::Blob(ByteSpan bytes) {
    Value v;
    v.type_ = ValueType::Blob;
    v.bytes_.Resize(bytes.Size());
    if (bytes.Size() != 0) {
        MemCopy(v.bytes_.Data(), bytes.Data(), bytes.Size());
    }
    return v;
}

Value Value::From(const ValueView& view) {
    switch (view.Type()) {
    case ValueType::Null: return Value();
    case ValueType::Integer: return Integer(view.AsInteger());
    case ValueType::Real: return Real(view.AsReal());
    case ValueType::Text: return Text(view.AsText());
    case ValueType::Blob: return Blob(view.AsBlob());
    }
    return Value();
}

ValueView Value::View() const {
    switch (type_) {
    case ValueType::Null: return ValueView::Null();
    case ValueType::Integer: return ValueView::Integer(integer_);
    case ValueType::Real: return ValueView::Real(real_);
    case ValueType::Text: return ValueView::Text(StringView(reinterpret_cast<const char*>(bytes_.Data()), bytes_.Size()));
    case ValueType::Blob: return ValueView::Blob(ByteSpan(bytes_.Data(), bytes_.Size()));
    }
    return ValueView::Null();
}

} // namespace engine_placeholder::database
