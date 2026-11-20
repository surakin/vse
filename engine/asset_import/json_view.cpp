#include "asset_import/json_view.h"
#include "containers/hash.h"

#include "algorithm.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "yyjson.h"

namespace vse::asset
{

namespace
{

const yyjson_val* Node(const void* node)
{
    return static_cast<const yyjson_val*>(node);
}

StringView KeyOf(const yyjson_val* key)
{
    return StringView(yyjson_get_str(const_cast<yyjson_val*>(key)), yyjson_get_len(const_cast<yyjson_val*>(key)));
}

// A repeated key in one object: a hash of every key, sorted, equal hashes compared for real. An object of up to 16
// members is compared pairwise.
bool HasRepeatedKey(const yyjson_val* object, StringView* repeated)
{
    const usize size = yyjson_obj_size(const_cast<yyjson_val*>(object));
    if (size < 2)
    {
        return false;
    }
    if (size <= 16)
    {
        // Pairwise: no allocation for the small objects that make up nearly every glTF.
        const yyjson_val* keys[16];
        usize count = 0;
        yyjson_obj_iter small;
        yyjson_obj_iter_init(const_cast<yyjson_val*>(object), &small);
        const yyjson_val* k = nullptr;
        while ((k = yyjson_obj_iter_next(&small)) != nullptr)
        {
            for (usize i = 0; i < count; ++i)
            {
                if (KeyOf(keys[i]) == KeyOf(k))
                {
                    *repeated = KeyOf(k);
                    return true;
                }
            }
            keys[count++] = k;
        }
        return false;
    }
    struct Entry
    {
        u64 hash;
        const yyjson_val* key;
    };
    Vector<Entry> entries;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(const_cast<yyjson_val*>(object), &iter);
    const yyjson_val* key = nullptr;
    while ((key = yyjson_obj_iter_next(&iter)) != nullptr)
    {
        const StringView text = KeyOf(key);
        entries.Push(Entry{Fnv1a64Builder(kFnv1a64LegacyBasis).AddText(text).Value(), key});
    }
    Sort(Span<Entry>(entries.Data(), entries.Size()), [](const Entry& a, const Entry& b) { return a.hash < b.hash; });
    for (usize i = 1; i < entries.Size(); ++i)
    {
        for (usize k = i; k > 0 && entries[k - 1].hash == entries[i].hash; --k)
        {
            if (KeyOf(entries[k - 1].key) == KeyOf(entries[i].key))
            {
                *repeated = KeyOf(entries[i].key);
                return true;
            }
        }
    }
    return false;
}

// Walks the tree (at most 128 deep: the reader refuses more) looking for an object that repeats a key.
bool FindRepeatedKey(const yyjson_val* value, StringView* repeated)
{
    if (yyjson_is_obj(const_cast<yyjson_val*>(value)))
    {
        if (HasRepeatedKey(value, repeated))
        {
            return true;
        }
        yyjson_obj_iter iter;
        yyjson_obj_iter_init(const_cast<yyjson_val*>(value), &iter);
        const yyjson_val* key = nullptr;
        while ((key = yyjson_obj_iter_next(&iter)) != nullptr)
        {
            if (FindRepeatedKey(yyjson_obj_iter_get_val(const_cast<yyjson_val*>(key)), repeated))
            {
                return true;
            }
        }
    }
    else if (yyjson_is_arr(const_cast<yyjson_val*>(value)))
    {
        yyjson_arr_iter iter;
        yyjson_arr_iter_init(const_cast<yyjson_val*>(value), &iter);
        const yyjson_val* element = nullptr;
        while ((element = yyjson_arr_iter_next(&iter)) != nullptr)
        {
            if (FindRepeatedKey(element, repeated))
            {
                return true;
            }
        }
    }
    return false;
}

void Say(GltfError* error, const JsonValue& value, const char* what)
{
    error->Set(value.Path(), StringView(what));
}

}  // namespace

void GltfError::Set(StringView where, StringView what)
{
    if (where.Length() != 0)
    {
        FormatTo(message.Data(), message.Size(), "%.*s: %.*s", static_cast<int>(where.Length()), where.Data(),
                 static_cast<int>(what.Length()), what.Data());
    }
    else
    {
        FormatTo(message.Data(), message.Size(), "%.*s", static_cast<int>(what.Length()), what.Data());
    }
}

void GltfError::SetNumber(StringView where, StringView what, u64 number)
{
    if (where.Length() != 0)
    {
        FormatTo(message.Data(), message.Size(), "%.*s: %.*s %llu", static_cast<int>(where.Length()), where.Data(),
                 static_cast<int>(what.Length()), what.Data(), static_cast<unsigned long long>(number));
    }
    else
    {
        FormatTo(message.Data(), message.Size(), "%.*s %llu", static_cast<int>(what.Length()), what.Data(),
                 static_cast<unsigned long long>(number));
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// JsonValue

bool JsonValue::IsObject() const
{
    return node_ != nullptr && yyjson_is_obj(const_cast<yyjson_val*>(Node(node_)));
}
bool JsonValue::IsArray() const
{
    return node_ != nullptr && yyjson_is_arr(const_cast<yyjson_val*>(Node(node_)));
}
bool JsonValue::IsString() const
{
    return node_ != nullptr && yyjson_is_str(const_cast<yyjson_val*>(Node(node_)));
}
bool JsonValue::IsNumber() const
{
    return node_ != nullptr && yyjson_is_num(const_cast<yyjson_val*>(Node(node_)));
}
bool JsonValue::IsBool() const
{
    return node_ != nullptr && yyjson_is_bool(const_cast<yyjson_val*>(Node(node_)));
}
bool JsonValue::IsNull() const
{
    return node_ != nullptr && yyjson_is_null(const_cast<yyjson_val*>(Node(node_)));
}

StringView JsonValue::Path() const
{
    return path_[0] == '\0' ? StringView("$") : StringView(path_.Data());
}

void JsonValue::Extend(const JsonValue& parent, StringView key, bool index_form, usize index)
{
    const StringView base = parent.path_[0] == '\0' ? StringView() : StringView(parent.path_.Data());
    if (index_form)
    {
        FormatTo(path_.Data(), path_.Size(), "%.*s[%llu]", static_cast<int>(base.Length()), base.Data(),
                 static_cast<unsigned long long>(index));
    }
    else if (base.Length() == 0)
    {
        FormatTo(path_.Data(), path_.Size(), "%.*s", static_cast<int>(key.Length()), key.Data());
    }
    else
    {
        FormatTo(path_.Data(), path_.Size(), "%.*s.%.*s", static_cast<int>(base.Length()), base.Data(),
                 static_cast<int>(key.Length()), key.Data());
    }
}

JsonValue JsonValue::Get(StringView key) const
{
    JsonValue result;
    if (!IsObject())
    {
        return result;
    }
    const yyjson_val* found = yyjson_obj_getn(const_cast<yyjson_val*>(Node(node_)), key.Data(), key.Length());
    if (found != nullptr)
    {
        result.node_ = found;
        result.Extend(*this, key, false, 0);
    }
    return result;
}

usize JsonValue::Size() const
{
    if (IsArray())
    {
        return yyjson_arr_size(const_cast<yyjson_val*>(Node(node_)));
    }
    if (IsObject())
    {
        return yyjson_obj_size(const_cast<yyjson_val*>(Node(node_)));
    }
    return 0;
}

JsonValue JsonValue::At(usize index) const
{
    JsonValue result;
    if (!IsArray())
    {
        return result;
    }
    yyjson_val* array = const_cast<yyjson_val*>(Node(node_));
    if (index >= yyjson_arr_size(array))
    {
        return result;
    }
    const yyjson_val* found = nullptr;
    if (cursor_node_ != nullptr && cursor_index_ <= index)
    {
        found = static_cast<const yyjson_val*>(cursor_node_);
        for (usize steps = index - cursor_index_; steps > 0; --steps)
        {
            found = unsafe_yyjson_get_next(found);
        }
    }
    else
    {
        found = yyjson_arr_get(array, index);
    }
    cursor_node_ = found;
    cursor_index_ = index;
    result.node_ = found;
    result.Extend(*this, StringView(), true, index);
    return result;
}

Status JsonValue::AsString(StringView* out, GltfError* error) const
{
    if (!IsString())
    {
        Say(error, *this, "must be a string");
        return Status::Make(ErrorCode::Invalid, "a value is not a string");
    }
    yyjson_val* node = const_cast<yyjson_val*>(Node(node_));
    const usize length = yyjson_get_len(node);
    const char* text = yyjson_get_str(node);
    for (usize i = 0; i < length; ++i)
    {
        if (text[i] == '\0')
        {
            Say(error, *this, "a string may not contain a NUL");
            return Status::Make(ErrorCode::Invalid, "a string holds a NUL");
        }
    }
    *out = StringView(text, length);
    return Status::Success();
}

Status JsonValue::AsBool(bool* out, GltfError* error) const
{
    if (!IsBool())
    {
        Say(error, *this, "must be true or false");
        return Status::Make(ErrorCode::Invalid, "a value is not a boolean");
    }
    *out = yyjson_get_bool(const_cast<yyjson_val*>(Node(node_)));
    return Status::Success();
}

Status JsonValue::AsDouble(f64* out, GltfError* error) const
{
    if (!IsNumber())
    {
        Say(error, *this, "must be a number");
        return Status::Make(ErrorCode::Invalid, "a value is not a number");
    }
    const f64 value = yyjson_get_num(const_cast<yyjson_val*>(Node(node_)));
    if (!(value - value == 0.0))  // not NaN or infinite
    {
        Say(error, *this, "must be a finite number");
        return Status::Make(ErrorCode::Invalid, "a number is not finite");
    }
    *out = value;
    return Status::Success();
}

Status JsonValue::AsUnsigned(u64* out, u64 max, GltfError* error) const
{
    if (!IsNumber())
    {
        Say(error, *this, "must be an integer");
        return Status::Make(ErrorCode::Invalid, "a value is not an integer");
    }
    yyjson_val* node = const_cast<yyjson_val*>(Node(node_));
    u64 value = 0;
    if (yyjson_is_uint(node))
    {
        value = yyjson_get_uint(node);
    }
    else if (yyjson_is_sint(node))
    {
        Say(error, *this, "must not be negative");
        return Status::Make(ErrorCode::Invalid, "an integer is negative");
    }
    else
    {
        // A real number is an integer when it has no fraction (some exporters write `3.0`), within what a double holds.
        const f64 real = yyjson_get_real(node);
        if (!(real >= 0.0 && real <= 9007199254740992.0) || real != static_cast<f64>(static_cast<u64>(real)))
        {
            Say(error, *this, "must be a non-negative integer");
            return Status::Make(ErrorCode::Invalid, "a number is not an integer");
        }
        value = static_cast<u64>(real);
    }
    if (value > max)
    {
        error->SetNumber(Path(), StringView("is above the limit"), max);
        return Status::Make(ErrorCode::Invalid, "an integer is above its limit");
    }
    *out = value;
    return Status::Success();
}

Status JsonValue::AsIndex(u32* out, u64 count, GltfError* error) const
{
    u64 value = 0;
    ENGINE_TRY(AsUnsigned(&value, 0xffffffffull, error));
    if (value >= count)
    {
        error->SetNumber(Path(), StringView("is not an index below"), count);
        return Status::Make(ErrorCode::Invalid, "an index is out of range");
    }
    *out = static_cast<u32>(value);
    return Status::Success();
}

Status JsonValue::OptionalString(StringView key, StringView* out, bool* present, GltfError* error) const
{
    const JsonValue member = Get(key);
    if (present != nullptr)
    {
        *present = member.Exists();
    }
    return member.Exists() ? member.AsString(out, error) : Status::Success();
}

Status JsonValue::OptionalUnsigned(StringView key, u64* out, u64 max, bool* present, GltfError* error) const
{
    const JsonValue member = Get(key);
    if (present != nullptr)
    {
        *present = member.Exists();
    }
    return member.Exists() ? member.AsUnsigned(out, max, error) : Status::Success();
}

Status JsonValue::OptionalBool(StringView key, bool* out, GltfError* error) const
{
    const JsonValue member = Get(key);
    return member.Exists() ? member.AsBool(out, error) : Status::Success();
}

namespace
{

Status Missing(const JsonValue& parent, StringView key, GltfError* error)
{
    char text[192];
    FormatTo(text, sizeof(text), "%.*s: the member \"%.*s\" is required", static_cast<int>(parent.Path().Length()),
             parent.Path().Data(), static_cast<int>(key.Length()), key.Data());
    error->Set(StringView(), StringView(text));
    return Status::Make(ErrorCode::Invalid, "a required member is missing");
}

}  // namespace

Status JsonValue::RequiredString(StringView key, StringView* out, GltfError* error) const
{
    const JsonValue member = Get(key);
    return member.Exists() ? member.AsString(out, error) : Missing(*this, key, error);
}

Status JsonValue::RequiredUnsigned(StringView key, u64* out, u64 max, GltfError* error) const
{
    const JsonValue member = Get(key);
    return member.Exists() ? member.AsUnsigned(out, max, error) : Missing(*this, key, error);
}

Status JsonValue::RequiredIndex(StringView key, u32* out, u64 count, GltfError* error) const
{
    const JsonValue member = Get(key);
    return member.Exists() ? member.AsIndex(out, count, error) : Missing(*this, key, error);
}

void JsonValue::Members(JsonMembers* iterator) const
{
    static_assert(sizeof(yyjson_obj_iter) <= sizeof(iterator->state_), "the iterator state must fit");
    iterator->parent_ = *this;
    yyjson_obj_iter* iter = reinterpret_cast<yyjson_obj_iter*>(iterator->state_);
    if (IsObject())
    {
        yyjson_obj_iter_init(const_cast<yyjson_val*>(Node(node_)), iter);
    }
    else
    {
        MemSet(iter, 0, sizeof(*iter));
    }
}

bool JsonMembers::Next(StringView* key, JsonValue* value)
{
    yyjson_obj_iter* iter = reinterpret_cast<yyjson_obj_iter*>(state_);
    if (iter->obj == nullptr)
    {
        return false;
    }
    const yyjson_val* k = yyjson_obj_iter_next(iter);
    if (k == nullptr)
    {
        return false;
    }
    *key = KeyOf(k);
    *value = JsonValue();
    value->node_ = yyjson_obj_iter_get_val(const_cast<yyjson_val*>(k));
    value->Extend(parent_, *key, false, 0);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// JsonDocument

JsonDocument::JsonDocument(JsonDocument&& other) noexcept
    : memory_(static_cast<Vector<byte>&&>(other.memory_)), root_(other.root_)
{
    other.root_ = nullptr;
}

JsonDocument& JsonDocument::operator=(JsonDocument&& other) noexcept
{
    memory_ = static_cast<Vector<byte>&&>(other.memory_);
    root_ = other.root_;
    other.root_ = nullptr;
    return *this;
}

Status JsonDocument::Parse(ByteSpan bytes, u64 max_bytes, GltfError* error)
{
    root_ = nullptr;
    if (bytes.Size() > max_bytes)
    {
        error->SetNumber(StringView(), StringView("the JSON is larger than the limit of bytes:"), max_bytes);
        return Status::Make(ErrorCode::TooLarge, "the JSON is too large");
    }
    if (bytes.Size() == 0)
    {
        error->Set(StringView(), StringView("the JSON is empty"));
        return Status::Make(ErrorCode::Invalid, "the JSON is empty");
    }
    // The pool: the most memory a parse of this many bytes can use, reserved before the input is looked at (a document
    // cannot make the reader ask for more).
    const usize pool_bytes = yyjson_read_max_memory_usage(bytes.Size(), YYJSON_READ_NOFLAG);
    if (!memory_.TryResizeUninitialized(pool_bytes + 16))
    {
        error->Set(StringView(), StringView("there is no memory for a document this size"));
        return Status::Make(ErrorCode::LimitExceeded, "out of memory");
    }
    usize aligned = reinterpret_cast<usize>(memory_.Data());
    aligned = (aligned + 15) & ~usize{15};
    yyjson_alc alc;
    if (!yyjson_alc_pool_init(&alc, reinterpret_cast<void*>(aligned), pool_bytes))
    {
        error->Set(StringView(), StringView("the memory pool cannot be set up"));
        return Status::Make(ErrorCode::Other, "the pool cannot be set up");
    }
    yyjson_read_err read_error;
    // The reader does not change the text without YYJSON_READ_INSITU; it takes a pointer that is not const.
    yyjson_doc* doc = yyjson_read_opts(const_cast<char*>(reinterpret_cast<const char*>(bytes.Data())), bytes.Size(),
                                       YYJSON_READ_NOFLAG, &alc, &read_error);
    if (doc == nullptr)
    {
        char text[160];
        FormatTo(text, sizeof(text), "the JSON is not valid at byte %llu: %s",
                 static_cast<unsigned long long>(read_error.pos), read_error.msg != nullptr ? read_error.msg : "error");
        error->Set(StringView(), StringView(text));
        return Status::Make(ErrorCode::Invalid, "the JSON is not valid");
    }
    const yyjson_val* root = yyjson_doc_get_root(doc);
    StringView repeated;
    if (FindRepeatedKey(root, &repeated))
    {
        char text[160];
        FormatTo(text, sizeof(text), "an object repeats the key \"%.*s\"", static_cast<int>(repeated.Length()),
                 repeated.Data());
        error->Set(StringView(), StringView(text));
        return Status::Make(ErrorCode::Invalid, "an object repeats a key");
    }
    root_ = root;
    return Status::Success();
}

JsonValue JsonDocument::Root() const
{
    JsonValue value;
    value.node_ = root_;
    return value;
}

}  // namespace vse::asset
