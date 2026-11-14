#include "level_source.h"

#include "algorithm.h"
#include "animation/components.h"
#include "asset/clip.h"
#include "bytes/byte_reader.h"
#include "bytes/byte_writer.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/number.h"
#include "cstr/varargs.h"
#include "ecs/component_set.h"
#include "lang/limits.h"
#include "lang/scalar.h"
#include "lang/status_try.h"
#include "storage/builtin_components.h"
#include "storage/component_record.h"
#include "storage/field_walk.h"
#include "storage/memory_store.h"
#include "storage/world_storage.h"
#include "string/utf8.h"

namespace engine_placeholder::asset
{
namespace
{

// ---------------------------------------------------------------------------------------------------------------------
// Messages

struct Msg
{
    Array<char, 256> text = {};
};

void SetMsg(Msg* msg, const char* text)
{
    FormatTo(msg->text.Data(), msg->text.Size(), "%s", text);
}

#define LV_MSG(msg, ...) FormatTo((msg)->text.Data(), (msg)->text.Size(), __VA_ARGS__)

Status Fail(LevelSourceError* error, u32 line, const char* message)
{
    error->line = line;
    FormatTo(error->message.Data(), error->message.Size(), "%s", message);
    return Status::Make(ErrorCode::Invalid, "the level source is invalid");
}

Status FailMsg(LevelSourceError* error, u32 line, const Msg& msg)
{
    return Fail(error, line, msg.text.Data());
}

// ---------------------------------------------------------------------------------------------------------------------
// Tokens and numbers

bool IsBlank(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

// Splits `line` at blanks into `out`; a token that starts with a double quote runs to the closing quote (backslash
// escapes the next character); a `#` outside a quote starts a comment. Returns false for a quote that is not closed.
bool Split(StringView line, Vector<StringView>* out)
{
    out->Clear();
    usize i = 0;
    while (i < line.Length())
    {
        while (i < line.Length() && IsBlank(line[i]))
        {
            ++i;
        }
        if (i >= line.Length() || line[i] == '#')
        {
            break;
        }
        const usize start = i;
        if (line[i] == '"')
        {
            ++i;
            bool closed = false;
            while (i < line.Length())
            {
                if (line[i] == '\\')
                {
                    i += 2;
                    continue;
                }
                if (line[i] == '"')
                {
                    ++i;
                    closed = true;
                    break;
                }
                ++i;
            }
            if (!closed)
            {
                return false;
            }
        }
        else
        {
            while (i < line.Length() && !IsBlank(line[i]) && line[i] != '#' && line[i] != '=')
            {
                ++i;
            }
            if (i == start)  // a lone '=' is a token of its own
            {
                ++i;
            }
        }
        out->Push(line.Substring(start, i - start));
    }
    return true;
}

bool ParseU64(StringView token, u64* out)
{
    if (token.Length() == 0 || token.Length() > 20)
    {
        return false;
    }
    u64 value = 0;
    for (usize i = 0; i < token.Length(); ++i)
    {
        if (token[i] < '0' || token[i] > '9')
        {
            return false;
        }
        const u64 digit = static_cast<u64>(token[i] - '0');
        if (value > (Limits<u64>::Max() - digit) / 10)
        {
            return false;
        }
        value = value * 10 + digit;
    }
    *out = value;
    return true;
}

bool ParseI64(StringView token, i64* out)
{
    const bool negative = token.Length() != 0 && token[0] == '-';
    u64 magnitude = 0;
    if (!ParseU64(negative ? token.Substring(1, token.Length() - 1) : token, &magnitude))
    {
        return false;
    }
    if (negative ? magnitude > (u64{1} << 63) : magnitude > static_cast<u64>(Limits<i64>::Max()))
    {
        return false;
    }
    *out = negative ? static_cast<i64>(0 - magnitude) : static_cast<i64>(magnitude);
    return true;
}

bool ParseF64(StringView token, f64* out)
{
    const ParseDoubleResult r = ParseDoubleExact(token.Data(), token.Data() + token.Length());
    if (!r.ok || r.end != token.Data() + token.Length() || !IsFinite(r.value))
    {
        return false;
    }
    *out = r.value;
    return true;
}

bool ParseF32(StringView token, f32* out)
{
    f64 value = 0;
    if (!ParseF64(token, &value) || Abs(value) > 3.0e38)
    {
        return false;
    }
    *out = static_cast<f32>(value);
    return true;
}

bool ParseHex(StringView text, usize digits, u64* out)
{
    if (text.Length() != digits)
    {
        return false;
    }
    u64 value = 0;
    for (usize i = 0; i < text.Length(); ++i)
    {
        const char c = text[i];
        u64 d = 0;
        if (c >= '0' && c <= '9')
        {
            d = static_cast<u64>(c - '0');
        }
        else if (c >= 'a' && c <= 'f')
        {
            d = static_cast<u64>(c - 'a' + 10);
        }
        else if (c >= 'A' && c <= 'F')
        {
            d = static_cast<u64>(c - 'A' + 10);
        }
        else
        {
            return false;
        }
        value = (value << 4) | d;
    }
    *out = value;
    return true;
}

// `@` and 32 hex digits.
bool ParseKeyLiteral(StringView token, storage::EntityKey* out)
{
    if (token.Length() != 33 || token[0] != '@')
    {
        return false;
    }
    storage::EntityKey key;
    if (!ParseHex(token.Substring(1, 16), 16, &key.high) || !ParseHex(token.Substring(17, 16), 16, &key.low))
    {
        return false;
    }
    *out = key;
    return true;
}

// Reads `count` bytes (at most 8) as a little-endian number.
bytes::ByteError ReadLE(bytes::ByteReader& in, u32 count, u64* out)
{
    ByteSpan raw;
    const bytes::ByteError e = in.ReadBytes(count, &raw);
    if (e != bytes::ByteError::Ok)
    {
        return e;
    }
    u64 value = 0;
    for (u32 i = 0; i < count; ++i)
    {
        value |= static_cast<u64>(raw[i]) << (8 * i);
    }
    *out = value;
    return bytes::ByteError::Ok;
}

void AppendPrinted(String<HeapAllocator>* out, const char* format, ...) ENGINE_PRINTF_FORMAT(2, 3);

void AppendPrinted(String<HeapAllocator>* out, const char* format, ...)
{
    char buffer[96];
    VarArgs args;
    ENGINE_VA_START(args, format);
    VFormatTo(buffer, sizeof(buffer), format, args);
    ENGINE_VA_END(args);
    out->Append(StringView(buffer, CStrLength(buffer)));
}

// ---------------------------------------------------------------------------------------------------------------------
// One field: text to its stored bytes

struct Cursor
{
    const Vector<StringView>* tokens = nullptr;
    usize position = 0;

    bool Next(StringView* out)
    {
        if (position >= tokens->Size())
        {
            return false;
        }
        *out = (*tokens)[position++];
        return true;
    }
    bool Done() const { return position >= tokens->Size(); }
};

struct IntSpec
{
    u32 bytes = 0;
    bool is_signed = false;
};

IntSpec SpecOf(storage::FieldKind kind)
{
    switch (kind)
    {
    case storage::FieldKind::U8: return {1, false};
    case storage::FieldKind::U16: return {2, false};
    case storage::FieldKind::U32: return {4, false};
    case storage::FieldKind::U64: return {8, false};
    case storage::FieldKind::I8: return {1, true};
    case storage::FieldKind::I16: return {2, true};
    case storage::FieldKind::I32: return {4, true};
    case storage::FieldKind::I64: return {8, true};
    default: break;
    }
    return {};
}

void WriteLittle(bytes::ByteWriter& out, u64 value, u32 width)
{
    for (u32 i = 0; i < width; ++i)
    {
        out.WriteU8(static_cast<u8>(value >> (8 * i)));
    }
}

bool ParseQuoted(StringView token, const storage::FieldOptions& options, String<HeapAllocator>* out, Msg* msg)
{
    if (token.Length() < 2 || token[0] != '"' || token[token.Length() - 1] != '"')
    {
        SetMsg(msg, "a string is written in double quotes");
        return false;
    }
    String<HeapAllocator> text(HeapAllocator::Default());
    for (usize i = 1; i + 1 < token.Length(); ++i)
    {
        char c = token[i];
        if (c == '\\')
        {
            ++i;
            if (i + 1 >= token.Length())
            {
                SetMsg(msg, "a string ends in a backslash");
                return false;
            }
            const char e = token[i];
            if (e == 'n')
            {
                c = '\n';
            }
            else if (e == 't')
            {
                c = '\t';
            }
            else if (e == 'r')
            {
                c = '\r';
            }
            else if (e == '"' || e == '\\')
            {
                c = e;
            }
            else if (e == 'x')
            {
                u64 value = 0;
                if (i + 2 >= token.Length() || !ParseHex(token.Substring(i + 1, 2), 2, &value))
                {
                    SetMsg(msg, "\\x needs two hexadecimal digits");
                    return false;
                }
                i += 2;
                c = static_cast<char>(value);
            }
            else
            {
                SetMsg(msg, "a string has an escape that is not \\\" \\\\ \\n \\t \\r or \\xHH");
                return false;
            }
        }
        const char one[1] = {c};
        text.AppendRawBytes(one, 1);
    }
    if (!IsValidUtf8(text.View()))
    {
        SetMsg(msg, "a string is not valid UTF-8");
        return false;
    }
    if (text.Length() > options.max_bytes)
    {
        LV_MSG(msg, "a string is longer than the field's limit of %llu bytes",
               static_cast<unsigned long long>(options.max_bytes));
        return false;
    }
    *out = static_cast<String<HeapAllocator>&&>(text);
    return true;
}

// What the minter knows while it turns text into bytes.
struct MintContext;
using ReferenceFn = bool (*)(MintContext&, StringView token, const storage::FieldDesc& field, u64* id, Msg* msg);

struct MintContext
{
    const storage::CodecLimits* limits = nullptr;
    ReferenceFn resolve_reference = nullptr;
    void* user = nullptr;
};

bool ParseElement(storage::FieldKind kind, usize memory_size, const storage::FieldDesc& field, Cursor& cursor,
                  bytes::ByteWriter& out, MintContext& ctx, Msg* msg);

bool NeedNumbers(Cursor& cursor, u32 count, f32* values, Msg* msg)
{
    for (u32 i = 0; i < count; ++i)
    {
        StringView token;
        if (!cursor.Next(&token))
        {
            LV_MSG(msg, "%u numbers are expected", static_cast<unsigned>(count));
            return false;
        }
        if (!ParseF32(token, &values[i]))
        {
            LV_MSG(msg, "'%.*s' is not a finite number", static_cast<int>(token.Length()), token.Data());
            return false;
        }
    }
    return true;
}

bool ParseElement(storage::FieldKind kind, usize memory_size, const storage::FieldDesc& field, Cursor& cursor,
                  bytes::ByteWriter& out, MintContext& ctx, Msg* msg)
{
    using storage::FieldKind;
    StringView token;
    switch (kind)
    {
    case FieldKind::U8:
    case FieldKind::U16:
    case FieldKind::U32:
    case FieldKind::U64:
    case FieldKind::I8:
    case FieldKind::I16:
    case FieldKind::I32:
    case FieldKind::I64:
    {
        if (!cursor.Next(&token))
        {
            SetMsg(msg, "a number is expected");
            return false;
        }
        const IntSpec spec = SpecOf(kind);
        if (spec.is_signed)
        {
            i64 value = 0;
            if (!ParseI64(token, &value))
            {
                LV_MSG(msg, "'%.*s' is not an integer", static_cast<int>(token.Length()), token.Data());
                return false;
            }
            const i64 limit = spec.bytes == 8 ? 0 : (i64{1} << (8 * spec.bytes - 1));
            if (spec.bytes != 8 && (value < -limit || value >= limit))
            {
                LV_MSG(msg, "%lld does not fit %u bits", static_cast<long long>(value),
                       static_cast<unsigned>(spec.bytes * 8));
                return false;
            }
            WriteLittle(out, static_cast<u64>(value), spec.bytes);
        }
        else
        {
            u64 value = 0;
            if (!ParseU64(token, &value))
            {
                LV_MSG(msg, "'%.*s' is not an unsigned integer", static_cast<int>(token.Length()), token.Data());
                return false;
            }
            if (spec.bytes != 8 && value >= (u64{1} << (8 * spec.bytes)))
            {
                LV_MSG(msg, "%llu does not fit %u bits", static_cast<unsigned long long>(value),
                       static_cast<unsigned>(spec.bytes * 8));
                return false;
            }
            WriteLittle(out, value, spec.bytes);
        }
        return true;
    }
    case FieldKind::F32:
    {
        f32 value = 0;
        if (!NeedNumbers(cursor, 1, &value, msg))
        {
            return false;
        }
        out.WriteF32(value);
        return true;
    }
    case FieldKind::F64:
    {
        f64 value = 0;
        if (!cursor.Next(&token) || !ParseF64(token, &value))
        {
            SetMsg(msg, "a finite number is expected");
            return false;
        }
        out.WriteF64(value);
        return true;
    }
    case FieldKind::Bool:
    {
        if (!cursor.Next(&token) || (token != StringView("true") && token != StringView("false")))
        {
            SetMsg(msg, "true or false is expected");
            return false;
        }
        out.WriteBool(token == StringView("true"));
        return true;
    }
    case FieldKind::Vec2:
    case FieldKind::Vec3:
    case FieldKind::Vec4:
    case FieldKind::Quat:
    case FieldKind::Aabb:
    case FieldKind::Mat4:
    {
        const u32 count = kind == FieldKind::Vec2   ? 2
                          : kind == FieldKind::Vec3 ? 3
                          : kind == FieldKind::Aabb ? 6
                          : kind == FieldKind::Mat4 ? 16
                                                    : 4;
        f32 values[16] = {};
        if (!NeedNumbers(cursor, count, values, msg))
        {
            return false;
        }
        for (u32 i = 0; i < count; ++i)
        {
            out.WriteF32(values[i]);
        }
        return true;
    }
    case FieldKind::Enum:
    {
        i64 value = 0;
        if (!cursor.Next(&token) || !ParseI64(token, &value))
        {
            SetMsg(msg, "an integer is expected");
            return false;
        }
        if (!field.options.has_range || value < field.options.range_min || value > field.options.range_max)
        {
            LV_MSG(msg, "%lld is outside the field's range %lld to %lld", static_cast<long long>(value),
                   static_cast<long long>(field.options.range_min), static_cast<long long>(field.options.range_max));
            return false;
        }
        WriteLittle(out, static_cast<u64>(value), static_cast<u32>(memory_size));
        return true;
    }
    case FieldKind::String:
    {
        if (!cursor.Next(&token))
        {
            SetMsg(msg, "a quoted string is expected");
            return false;
        }
        String<HeapAllocator> text(HeapAllocator::Default());
        storage::FieldOptions options = field.options;
        if (options.max_bytes > ctx.limits->max_string_bytes)
        {
            options.max_bytes = ctx.limits->max_string_bytes;
        }
        if (!ParseQuoted(token, options, &text, msg))
        {
            return false;
        }
        out.WriteVarint(text.Length());
        out.WriteBytes(ByteSpan(reinterpret_cast<const byte*>(text.View().Data()), text.Length()));
        return true;
    }
    case FieldKind::ResourceRef:
    {
        u64 id = 0;
        if (!cursor.Next(&token) || !ctx.resolve_reference(ctx, token, field, &id, msg))
        {
            if (msg->text[0] == '\0')
            {
                SetMsg(msg, "a path, @HEX16 or null is expected");
            }
            return false;
        }
        out.WriteU64(id);
        return true;
    }
    case FieldKind::EntityRef:
    case FieldKind::Array: break;
    }
    SetMsg(msg, "this kind of field has no text form here");
    return false;
}

// One whole field from the tokens after '='. Entity references are not handled here (they wait for every entity to be
// known); the caller passes the token on.
bool ParseField(const storage::FieldDesc& field, const Vector<StringView>& tokens, bytes::ByteWriter& out,
                MintContext& ctx, Msg* msg)
{
    msg->text[0] = '\0';
    Cursor cursor;
    cursor.tokens = &tokens;
    if (field.kind == storage::FieldKind::Array)
    {
        if (field.element_kind == storage::FieldKind::Array || field.element_kind == storage::FieldKind::String ||
            field.element_kind == storage::FieldKind::EntityRef ||
            field.element_kind == storage::FieldKind::ResourceRef)
        {
            SetMsg(msg, "an array of this element kind has no text form");
            return false;
        }
        out.WriteVarint(field.element_count);
        for (usize i = 0; i < field.element_count; ++i)
        {
            if (!ParseElement(field.element_kind, field.element_size, field, cursor, out, ctx, msg))
            {
                return false;
            }
        }
    }
    else if (!ParseElement(field.kind, field.memory_size, field, cursor, out, ctx, msg))
    {
        return false;
    }
    if (!cursor.Done())
    {
        SetMsg(msg, "there are more values than the field holds");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// The source model

struct LvField
{
    bool set = false;
    bool deferred = false;  // an entity reference, written at the end
    Vector<byte> bytes;
    StringView token;
    u32 line = 0;
};

struct LvComponent
{
    const storage::RegisteredComponent* component = nullptr;
    u32 registry_index = 0;
    u32 line = 0;
    Vector<LvField> fields;
};

struct LvEntity
{
    StringView name;
    bool named = false;
    storage::EntityKey key;
    u32 line = 0;
    Vector<LvComponent> components;
    bool has_parent = false;
    StringView parent;
    u32 parent_line = 0;
};

struct DefaultFields
{
    u32 component_id = 0;
    bool available = false;
    Vector<Vector<byte>> fields;
};

struct Resolved
{
    ResourceId id;
    ResourceType type;
    u32 type_version = 0;
    ByteSpan blob;
};

class Minter
{
public:
    Minter(const LevelReferenceResolver& resolver, const TypeRegistry& types,
           const storage::ComponentRegistry& registry, LevelSourceError* error)
        : resolver_(resolver), types_(types), registry_(registry), error_(error)
    {
        ctx_.limits = &limits_;
        ctx_.resolve_reference = &Minter::ResolveReferenceThunk;
        ctx_.user = this;
    }

    Status Run(StringView text, Vector<byte>* out, u32 target);

private:
    static bool ResolveReferenceThunk(MintContext& ctx, StringView token, const storage::FieldDesc& field, u64* id,
                                      Msg* msg)
    {
        return static_cast<Minter*>(ctx.user)->ResolveReference(token, field, id, msg);
    }

    bool ResolveReference(StringView token, const storage::FieldDesc& field, u64* id, Msg* msg);
    bool Lookup(StringView token, ByteSpan* blob, ResourceId* id, Msg* msg);
    bool Note(ResourceId id, ByteSpan blob, u32 role, ResourceType* type, Msg* msg);
    bool CheckCycle(ByteSpan blob, ResourceId included, usize depth, Vector<ResourceId>* visited, Msg* msg);
    Status ParseLines(StringView text);
    Status Line(const Vector<StringView>& tokens, u32 line);
    Status AddComponent(LvEntity* entity, StringView name, u32 line, LvComponent** out);
    Status SetField(LvEntity* entity, LvComponent* component, StringView field_name, const Vector<StringView>& tokens,
                    u32 line);
    Status Sugar(const Vector<StringView>& tokens, u32 line);
    const DefaultFields* DefaultsOf(const storage::RegisteredComponent& component);
    Status Finish(Vector<byte>* out, u32 target);
    Status Records(LvEntity& entity, usize entity_index, const Vector<storage::EntityKey>& keys,
                   storage::EntityRecord* record);
    Status CheckClips(const Vector<storage::EntityRecord>& records);
    const LvEntity* FindByName(StringView name) const;
    void NamesOf(const storage::RegisteredComponent& component, Msg* msg) const;
    void ComponentList(Msg* msg) const;
    const Resolved* FindResolved(ResourceId id) const;

    const LevelReferenceResolver& resolver_;
    const TypeRegistry& types_;
    const storage::ComponentRegistry& registry_;
    LevelSourceError* error_;
    storage::CodecLimits limits_;
    MintContext ctx_;
    ResourceId id_;
    bool have_id_ = false;
    bool entities_started_ = false;
    Vector<LvEntity> entities_;
    Vector<LevelResource> resources_;
    Vector<Resolved> resolved_;
    Vector<DefaultFields> defaults_;
    ecs::World scratch_;
    Msg msg_;
    u32 current_line_ = 0;
};

void Minter::ComponentList(Msg* msg) const
{
    String<HeapAllocator> names(HeapAllocator::Default());
    for (u32 i = 0; i < registry_.Count(); ++i)
    {
        const storage::RegisteredComponent& c = registry_.At(i);
        if (!c.IsStored() || c.Id() == storage::kEntityKeyId || c.Id() == storage::kParentLinkId ||
            c.Id() == kEntityNameId)
        {
            continue;
        }
        if (names.Length() != 0)
        {
            names.Append(StringView(", "));
        }
        names.Append(StringView(c.Name()));
    }
    LV_MSG(msg, "no such component; the registry has: %.*s", static_cast<int>(names.Length()), names.View().Data());
}

void Minter::NamesOf(const storage::RegisteredComponent& component, Msg* msg) const
{
    String<HeapAllocator> names(HeapAllocator::Default());
    const Span<const storage::FieldDesc> fields = component.Fields();
    for (usize i = 0; i < fields.Size(); ++i)
    {
        if (names.Length() != 0)
        {
            names.Append(StringView(", "));
        }
        names.Append(StringView(fields[i].name));
    }
    LV_MSG(msg, "%s has no such field; its fields are: %.*s", component.Name(), static_cast<int>(names.Length()),
           names.View().Data());
}

const Resolved* Minter::FindResolved(ResourceId id) const
{
    for (usize i = 0; i < resolved_.Size(); ++i)
    {
        if (resolved_[i].id == id)
        {
            return &resolved_[i];
        }
    }
    return nullptr;
}

// Finds the resource a token names: a path, or `@HEX16`.
bool Minter::Lookup(StringView token, ByteSpan* blob, ResourceId* id, Msg* msg)
{
    if (token.Length() != 0 && token[0] == '@')
    {
        if (!ParseResourceId(token.Substring(1, token.Length() - 1), id).Ok())
        {
            SetMsg(msg, "an @ id is 16 hexadecimal digits");
            return false;
        }
        const Status found = resolver_.ResolveId(*id, blob);
        if (found.Failed())
        {
            LV_MSG(msg, "the project has no resource with the id %.*s", static_cast<int>(token.Length()), token.Data());
            return false;
        }
        return true;
    }
    const Status found = resolver_.ResolvePath(token, id, blob);
    if (found.Failed())
    {
        LV_MSG(msg, "'%.*s' is not a resource of the project%s%s", static_cast<int>(token.Length()), token.Data(),
               found.Is(ErrorCode::NotFound) ? "" : ": ", found.Is(ErrorCode::NotFound) ? "" : found.Message());
        return false;
    }
    return true;
}

// Records a resource the level names: opens its blob for the type and version, and merges the role.
bool Minter::Note(ResourceId id, ByteSpan blob, u32 role, ResourceType* type, Msg* msg)
{
    const Resolved* known = FindResolved(id);
    Resolved entry;
    if (known == nullptr)
    {
        BlobReader reader;
        const Status opened = reader.Open(blob, types_);
        if (opened.Failed())
        {
            LV_MSG(msg, "the resource's blob does not open: %s", opened.Message());
            return false;
        }
        entry.id = id;
        entry.type = reader.Type();
        entry.type_version = reader.TypeVersion();
        entry.blob = blob;
        resolved_.Push(entry);
        known = &resolved_[resolved_.Size() - 1];
    }
    *type = known->type;
    for (usize i = 0; i < resources_.Size(); ++i)
    {
        if (resources_[i].id == id)
        {
            resources_[i].roles |= role;
            return true;
        }
    }
    LevelResource resource;
    resource.id = id;
    resource.type = known->type;
    resource.type_version = known->type_version;
    resource.roles = role;
    resources_.Push(resource);
    return true;
}

bool Minter::ResolveReference(StringView token, const storage::FieldDesc& field, u64* id, Msg* msg)
{
    if (token == StringView("null"))
    {
        if (!field.options.nullable)
        {
            SetMsg(msg, "the field may not be null");
            return false;
        }
        *id = 0;
        return true;
    }
    ByteSpan blob;
    ResourceId resource;
    if (!Lookup(token, &blob, &resource, msg))
    {
        return false;
    }
    if (resource == id_)
    {
        SetMsg(msg, "a level cannot name itself");
        return false;
    }
    ResourceType type;
    if (!Note(resource, blob, kLevelRoleReference, &type, msg))
    {
        return false;
    }
    if (field.options.ref_type != 0 && type.Tag() != field.options.ref_type)
    {
        TypeInfo have;
        TypeInfo want;
        const bool have_known = types_.Find(type, &have);
        const bool want_known = types_.Find(ResourceType::FromTag(field.options.ref_type), &want);
        LV_MSG(msg, "'%.*s' is a %s but the field takes a %s", static_cast<int>(token.Length()), token.Data(),
               have_known ? have.name : "resource", want_known ? want.name : "resource of another type");
        return false;
    }
    *id = resource.Value();
    return true;
}

// Walks the includes of an included level: none of them may be this level.
bool Minter::CheckCycle(ByteSpan blob, ResourceId included, usize depth, Vector<ResourceId>* visited, Msg* msg)
{
    if (depth > 64)
    {
        SetMsg(msg, "the include chain is longer than 64 levels");
        return false;
    }
    BlobReader reader;
    const Status opened = reader.Open(blob, types_);
    if (opened.Failed())
    {
        LV_MSG(msg, "an included level does not open: %s", opened.Message());
        return false;
    }
    if (reader.Type() != types::kLevel)
    {
        SetMsg(msg, "an include names a resource that is not a level");
        return false;
    }
    for (usize i = 0; i < visited->Size(); ++i)
    {
        if ((*visited)[i] == included)
        {
            return true;
        }
    }
    visited->Push(included);
    for (usize i = 0; i < reader.DependencyCount(); ++i)
    {
        const Dependency d = reader.DependencyAt(i);
        if ((d.role & kLevelRoleInclude) == 0)
        {
            continue;
        }
        if (d.id == id_)
        {
            SetMsg(msg, "the includes form a cycle: an included level includes this one");
            return false;
        }
        ByteSpan next;
        if (resolver_.ResolveId(d.id, &next).Failed())
        {
            SetMsg(msg, "an included level includes a level that the project does not have");
            return false;
        }
        if (!CheckCycle(next, d.id, depth + 1, visited, msg))
        {
            return false;
        }
    }
    return true;
}

const LvEntity* Minter::FindByName(StringView name) const
{
    for (usize i = 0; i < entities_.Size(); ++i)
    {
        if (entities_[i].named && entities_[i].name == name)
        {
            return &entities_[i];
        }
    }
    return nullptr;
}

const DefaultFields* Minter::DefaultsOf(const storage::RegisteredComponent& component)
{
    for (usize i = 0; i < defaults_.Size(); ++i)
    {
        if (defaults_[i].component_id == component.Id())
        {
            return &defaults_[i];
        }
    }
    DefaultFields entry;
    entry.component_id = component.Id();
    ecs::ComponentSet set;
    set.Add(component.EcsId());
    const ecs::Entity e = scratch_.CreateFromSet(set);
    if (e.IsValid())
    {
        entry.available = true;
        const Span<const storage::FieldDesc> fields = component.Fields();
        for (usize i = 0; i < fields.Size(); ++i)
        {
            bytes::ByteWriter w;
            if (fields[i].encode(fields[i], scratch_.CellOf(e, component.EcsId()), w, limits_).Failed())
            {
                entry.available = false;
                break;
            }
            Vector<byte> copy;
            for (usize b = 0; b < w.Size(); ++b)
            {
                copy.Push(w.Bytes()[b]);
            }
            entry.fields.Push(static_cast<Vector<byte>&&>(copy));
        }
        static_cast<void>(scratch_.Destroy(e));
    }
    defaults_.Push(static_cast<DefaultFields&&>(entry));
    return &defaults_[defaults_.Size() - 1];
}

Status Minter::AddComponent(LvEntity* entity, StringView name, u32 line, LvComponent** out)
{
    const storage::RegisteredComponent* c = registry_.FindByName(name);
    if (c == nullptr || !c->IsStored() || c->Id() == storage::kEntityKeyId || c->Id() == storage::kParentLinkId ||
        c->Id() == kEntityNameId)
    {
        Msg msg;
        ComponentList(&msg);
        if (c != nullptr && c->Id() == kEntityNameId)
        {
            SetMsg(&msg, "entity_name is set by the entity line, not as a component");
        }
        else if (c != nullptr && c->Id() == storage::kParentLinkId)
        {
            SetMsg(&msg, "a parent is set by a parent line, not as a component");
        }
        return FailMsg(error_, line, msg);
    }
    for (usize i = 0; i < entity->components.Size(); ++i)
    {
        if (entity->components[i].component == c)
        {
            return Fail(error_, line, "the entity has this component already");
        }
    }
    LvComponent component;
    component.component = c;
    component.line = line;
    for (u32 i = 0; i < registry_.Count(); ++i)
    {
        if (&registry_.At(i) == c)
        {
            component.registry_index = i;
        }
    }
    component.fields.Resize(c->Fields().Size());
    entity->components.Push(static_cast<LvComponent&&>(component));
    *out = &entity->components[entity->components.Size() - 1];
    return Status::Success();
}

Status Minter::SetField(LvEntity* entity, LvComponent* component, StringView field_name,
                        const Vector<StringView>& tokens, u32 line)
{
    static_cast<void>(entity);
    const Span<const storage::FieldDesc> fields = component->component->Fields();
    usize index = fields.Size();
    for (usize i = 0; i < fields.Size(); ++i)
    {
        if (StringView(fields[i].name) == field_name)
        {
            index = i;
        }
    }
    if (index == fields.Size())
    {
        Msg msg;
        NamesOf(*component->component, &msg);
        return FailMsg(error_, line, msg);
    }
    LvField& slot = component->fields[index];
    if (slot.set)
    {
        return Fail(error_, line, "the field is set twice");
    }
    slot.set = true;
    slot.line = line;
    const storage::FieldDesc& field = fields[index];
    if (field.kind == storage::FieldKind::EntityRef)
    {
        if (tokens.Size() != 1)
        {
            return Fail(error_, line, "an entity reference is one entity name, @HEX32 or null");
        }
        slot.deferred = true;
        slot.token = tokens[0];
        return Status::Success();
    }
    bytes::ByteWriter w;
    Msg msg;
    if (!ParseField(field, tokens, w, ctx_, &msg))
    {
        Msg full;
        LV_MSG(&full, "%s.%s: %s", component->component->Name(), field.name, msg.text.Data());
        return FailMsg(error_, line, full);
    }
    for (usize b = 0; b < w.Size(); ++b)
    {
        slot.bytes.Push(w.Bytes()[b]);
    }
    return Status::Success();
}

// `skeleton PATH` and `animation PATH [loop|once] [speed X]`.
Status Minter::Sugar(const Vector<StringView>& tokens, u32 line)
{
    if (entities_.Size() == 0)
    {
        return Fail(error_, line, "this line belongs to an entity; none has started");
    }
    LvEntity* entity = &entities_[entities_.Size() - 1];
    const bool skeleton = tokens[0] == StringView("skeleton");
    LvComponent* component = nullptr;
    ENGINE_TRY(AddComponent(entity, StringView(skeleton ? "skeleton_instance" : "animation_player"), line, &component));
    Vector<StringView> one;
    if (tokens.Size() < 2)
    {
        return Fail(error_, line, "a path is expected");
    }
    one.Push(tokens[1]);
    ENGINE_TRY(SetField(entity, component, StringView(skeleton ? "skeleton" : "clip"), one, line));
    if (skeleton)
    {
        return tokens.Size() == 2 ? Status::Success() : Fail(error_, line, "skeleton takes one path");
    }
    Vector<StringView> value;
    for (usize i = 2; i < tokens.Size(); ++i)
    {
        value.Clear();
        if (tokens[i] == StringView("loop") || tokens[i] == StringView("once"))
        {
            value.Push(tokens[i] == StringView("loop") ? StringView("true") : StringView("false"));
            ENGINE_TRY(SetField(entity, component, StringView("looping"), value, line));
        }
        else if (tokens[i] == StringView("speed") && i + 1 < tokens.Size())
        {
            value.Push(tokens[++i]);
            ENGINE_TRY(SetField(entity, component, StringView("speed"), value, line));
        }
        else
        {
            return Fail(error_, line, "animation takes loop, once and speed X after the clip's path");
        }
    }
    return Status::Success();
}

Status Minter::Line(const Vector<StringView>& tokens, u32 line)
{
    const StringView head = tokens[0];
    const bool field_line = tokens.Size() >= 3 && tokens[1] == StringView("=");  // `FIELD = VALUE`, whatever FIELD is
    if (head == StringView("id") && !field_line)
    {
        if (have_id_ || entities_started_ || tokens.Size() != 2 || line == 0)
        {
            return Fail(error_, line, "the id line comes first, once: id <16 hexadecimal digits>");
        }
        if (!ParseResourceId(tokens[1], &id_).Ok())
        {
            return Fail(error_, line, "the id is 16 hexadecimal digits and not zero");
        }
        have_id_ = true;
        return Status::Success();
    }
    if (!have_id_)
    {
        return Fail(error_, line, "the id line must come first (asset_mint adopt writes it)");
    }
    if (head == StringView("include") && !field_line)
    {
        if (entities_started_ || tokens.Size() != 2)
        {
            return Fail(error_, line, "include PATH comes before the first entity");
        }
        ByteSpan blob;
        ResourceId included;
        Msg msg;
        if (!Lookup(tokens[1], &blob, &included, &msg))
        {
            return FailMsg(error_, line, msg);
        }
        if (included == id_)
        {
            return Fail(error_, line, "a level cannot include itself");
        }
        ResourceType type;
        if (!Note(included, blob, kLevelRoleInclude, &type, &msg))
        {
            return FailMsg(error_, line, msg);
        }
        Vector<ResourceId> visited;
        if (!CheckCycle(blob, included, 0, &visited, &msg))
        {
            return FailMsg(error_, line, msg);
        }
        return Status::Success();
    }
    if (head == StringView("entity") && !field_line)
    {
        entities_started_ = true;
        LvEntity entity;
        entity.line = line;
        if (tokens.Size() == 3 && tokens[1] == StringView("key"))
        {
            if (!ParseKeyLiteral(tokens[2], &entity.key) || entity.key.IsNull())
            {
                return Fail(error_, line, "a key is @ and 32 hexadecimal digits, not zero");
            }
        }
        else if (tokens.Size() == 2 || (tokens.Size() == 4 && tokens[2] == StringView("key")))
        {
            if (!LevelNameValid(tokens[1]))
            {
                return Fail(error_, line,
                            "an entity name is letters, digits and _ . : / -, up to 128 characters, and not 'key'");
            }
            if (FindByName(tokens[1]) != nullptr)
            {
                return Fail(error_, line, "two entities have this name");
            }
            entity.name = tokens[1];
            entity.named = true;
            if (tokens.Size() == 4)
            {
                if (!ParseKeyLiteral(tokens[3], &entity.key) || entity.key.IsNull())
                {
                    return Fail(error_, line, "a key is @ and 32 hexadecimal digits, not zero");
                }
            }
            else
            {
                entity.key = LevelEntityKey(id_, entity.name);
            }
        }
        else
        {
            return Fail(error_, line, "entity NAME [key @HEX32] or entity key @HEX32");
        }
        if (entities_.Size() >= kMaxLevelEntities)
        {
            return Fail(error_, line, "the level has more entities than the limit");
        }
        entities_.Push(static_cast<LvEntity&&>(entity));
        return Status::Success();
    }
    const bool shorthand = head == StringView("parent") || head == StringView("skeleton") ||
                           head == StringView("animation") || head == StringView("component");
    if (!shorthand && !field_line)
    {
        Msg msg;
        LV_MSG(&msg,
               "unknown line '%.*s'; the lines are id, include, entity, parent, skeleton, animation, component and "
               "FIELD = VALUE",
               static_cast<int>(head.Length()), head.Data());
        return FailMsg(error_, line, msg);
    }
    if (!entities_started_)
    {
        return Fail(error_, line, "an entity line comes first (entity NAME)");
    }
    LvEntity* entity = &entities_[entities_.Size() - 1];
    if (head == StringView("parent") && !field_line)
    {
        if (tokens.Size() != 2 || entity->has_parent)
        {
            return Fail(error_, line, "parent NAME, once per entity");
        }
        entity->has_parent = true;
        entity->parent = tokens[1];
        entity->parent_line = line;
        return Status::Success();
    }
    if ((head == StringView("skeleton") || head == StringView("animation")) && !field_line)
    {
        return Sugar(tokens, line);
    }
    if (head == StringView("component") && !field_line)
    {
        if (tokens.Size() != 2)
        {
            return Fail(error_, line, "component TYPE");
        }
        LvComponent* component = nullptr;
        ENGINE_TRY(AddComponent(entity, tokens[1], line, &component));
        return Status::Success();
    }
    // FIELD = VALUE...
    if (tokens.Size() >= 3 && tokens[1] == StringView("="))
    {
        if (entity->components.Size() == 0)
        {
            return Fail(error_, line, "a field line follows a component line");
        }
        // The field belongs to the last component that was started by a component line (not by shorthand): the last
        // one.
        LvComponent* component = &entity->components[entity->components.Size() - 1];
        Vector<StringView> values;
        for (usize i = 2; i < tokens.Size(); ++i)
        {
            values.Push(tokens[i]);
        }
        return SetField(entity, component, head, values, line);
    }
    Msg msg;
    LV_MSG(
        &msg,
        "unknown line '%.*s'; the lines are id, include, entity, parent, skeleton, animation, component and FIELD = VALUE",
        static_cast<int>(head.Length()), head.Data());
    return FailMsg(error_, line, msg);
}

Status Minter::ParseLines(StringView text)
{
    usize pos = 0;
    u32 line_number = 0;
    Vector<StringView> tokens;
    while (pos < text.Length())
    {
        usize end = pos;
        while (end < text.Length() && text[end] != '\n')
        {
            ++end;
        }
        ++line_number;
        const StringView line = text.Substring(pos, end - pos);
        pos = end + 1;
        if (!Split(line, &tokens))
        {
            return Fail(error_, line_number, "a string is not closed");
        }
        if (tokens.Size() == 0)
        {
            continue;
        }
        current_line_ = line_number;
        ENGINE_TRY(Line(tokens, line_number));
    }
    return Status::Success();
}

Status Minter::Records(LvEntity& entity, usize entity_index, const Vector<storage::EntityKey>& keys,
                       storage::EntityRecord* record)
{
    static_cast<void>(entity_index);
    struct Piece
    {
        u32 order = 0;
        u32 id = 0;
        u32 version = 0;
        Vector<byte> bytes;
    };
    Vector<Piece> pieces;
    if (entity.named)
    {
        Piece p;
        p.order = 0;
        p.id = kEntityNameId;
        p.version = registry_.FindById(kEntityNameId)->Version();
        bytes::ByteWriter w;
        w.WriteVarint(entity.name.Length());
        w.WriteBytes(ByteSpan(reinterpret_cast<const byte*>(entity.name.Data()), entity.name.Length()));
        for (usize b = 0; b < w.Size(); ++b)
        {
            p.bytes.Push(w.Bytes()[b]);
        }
        // Placed by its registry position like every other component.
        for (u32 i = 0; i < registry_.Count(); ++i)
        {
            if (registry_.At(i).Id() == kEntityNameId)
            {
                p.order = i;
            }
        }
        pieces.Push(static_cast<Piece&&>(p));
    }
    for (usize c = 0; c < entity.components.Size(); ++c)
    {
        LvComponent& lc = entity.components[c];
        const DefaultFields* defaults = DefaultsOf(*lc.component);
        Piece p;
        p.order = lc.registry_index;
        p.id = lc.component->Id();
        p.version = lc.component->Version();
        const Span<const storage::FieldDesc> fields = lc.component->Fields();
        for (usize f = 0; f < fields.Size(); ++f)
        {
            LvField& field = lc.fields[f];
            if (field.deferred)
            {
                storage::EntityKey key;
                if (field.token == StringView("null"))
                {
                }
                else if (!ParseKeyLiteral(field.token, &key))
                {
                    const LvEntity* target = FindByName(field.token);
                    if (target == nullptr)
                    {
                        Msg msg;
                        LV_MSG(&msg, "%s.%s: no entity is called '%.*s'", lc.component->Name(), fields[f].name,
                               static_cast<int>(field.token.Length()), field.token.Data());
                        return FailMsg(error_, field.line, msg);
                    }
                    key = target->key;
                }
                p.bytes.Reserve(p.bytes.Size() + 16);
                for (u32 i = 0; i < 8; ++i)
                {
                    p.bytes.Push(static_cast<byte>(key.high >> (8 * i)));
                }
                for (u32 i = 0; i < 8; ++i)
                {
                    p.bytes.Push(static_cast<byte>(key.low >> (8 * i)));
                }
            }
            else if (field.set)
            {
                for (usize b = 0; b < field.bytes.Size(); ++b)
                {
                    p.bytes.Push(field.bytes[b]);
                }
            }
            else if (defaults->available)
            {
                const Vector<byte>& d = defaults->fields[f];
                for (usize b = 0; b < d.Size(); ++b)
                {
                    p.bytes.Push(d[b]);
                }
            }
            else
            {
                Msg msg;
                LV_MSG(&msg, "%s.%s is missing and the component has no default", lc.component->Name(), fields[f].name);
                return FailMsg(error_, lc.line, msg);
            }
        }
        Vector<storage::ReferenceValue> refs;
        if (storage::ReadReferences(*lc.component, ByteSpan(p.bytes.Data(), p.bytes.Size()), limits_, &refs).Ok())
        {
            for (usize r = 0; r < refs.Size(); ++r)
            {
                if (refs[r].id == 0 && !refs[r].field->options.nullable)
                {
                    Msg msg;
                    LV_MSG(&msg, "%s.%s is null and the field may not be (set it to a resource)", lc.component->Name(),
                           refs[r].field->name);
                    return FailMsg(error_, lc.line, msg);
                }
            }
        }
        pieces.Push(static_cast<Piece&&>(p));
    }
    Sort(Span<Piece>(pieces.Data(), pieces.Size()), [](const Piece& a, const Piece& b) { return a.order < b.order; });
    if (entity.has_parent)
    {
        Piece p;
        p.order = Limits<u32>::Max();
        p.id = storage::kParentLinkId;
        p.version = registry_.FindById(storage::kParentLinkId)->Version();
        storage::EntityKey key;
        if (!ParseKeyLiteral(entity.parent, &key))
        {
            const LvEntity* target = FindByName(entity.parent);
            if (target == nullptr)
            {
                Msg msg;
                LV_MSG(&msg, "no entity is called '%.*s'", static_cast<int>(entity.parent.Length()),
                       entity.parent.Data());
                return FailMsg(error_, entity.parent_line, msg);
            }
            if (target == &entity)
            {
                return Fail(error_, entity.parent_line, "an entity cannot be its own parent");
            }
            key = target->key;
        }
        for (u32 i = 0; i < 8; ++i)
        {
            p.bytes.Push(static_cast<byte>(key.high >> (8 * i)));
        }
        for (u32 i = 0; i < 8; ++i)
        {
            p.bytes.Push(static_cast<byte>(key.low >> (8 * i)));
        }
        pieces.Push(static_cast<Piece&&>(p));
    }
    static_cast<void>(keys);
    bytes::ByteWriter container;
    container.WriteU32(static_cast<u32>(pieces.Size()));
    for (usize i = 0; i < pieces.Size(); ++i)
    {
        storage::ComponentRecord rec;
        rec.id = pieces[i].id;
        rec.version = pieces[i].version;
        rec.bytes = ByteSpan(pieces[i].bytes.Data(), pieces[i].bytes.Size());
        const storage::Status written = storage::WriteComponentRecord(rec, limits_, container);
        if (written.Failed())
        {
            return Fail(error_, entity.line, written.Message());
        }
    }
    *record = storage::EntityRecord::Make(entity.key, container.Bytes());
    return Status::Success();
}

// An entity with a skeleton and a clip: the clip must have been made for that skeleton.
Status Minter::CheckClips(const Vector<storage::EntityRecord>& records)
{
    for (usize e = 0; e < records.Size(); ++e)
    {
        bytes::ByteReader in(ByteSpan(records[e].blob.Data(), records[e].blob.Size()));
        u32 count = 0;
        static_cast<void>(in.ReadU32(&count));
        u64 skeleton = 0;
        u64 clip = 0;
        for (u32 i = 0; i < count; ++i)
        {
            storage::ComponentRecord rec;
            if (storage::ReadComponentRecord(in, limits_, &rec).Failed())
            {
                break;
            }
            const storage::RegisteredComponent* c = registry_.FindById(rec.id);
            if (c == nullptr || (rec.id != animation::kSkeletonInstanceId && rec.id != animation::kAnimationPlayerId))
            {
                continue;
            }
            Vector<storage::ReferenceValue> refs;
            if (storage::ReadReferences(*c, rec.bytes, limits_, &refs).Failed() || refs.Size() == 0)
            {
                continue;
            }
            (rec.id == animation::kSkeletonInstanceId ? skeleton : clip) = refs[0].id;
        }
        if (skeleton == 0 || clip == 0)
        {
            continue;
        }
        const Resolved* clip_blob = FindResolved(ResourceId::FromValue(clip));
        if (clip_blob == nullptr)
        {
            continue;
        }
        ClipView view;
        if (view.Open(clip_blob->blob, types_).Failed())
        {
            continue;  // the blob opened as a resource; a clip view that does not is the clip's own defect
        }
        if (view.Skeleton() != ResourceId::FromValue(skeleton))
        {
            u32 line = 0;
            for (usize i = 0; i < entities_.Size(); ++i)
            {
                if (entities_[i].key == records[e].key)
                {
                    line = entities_[i].line;
                    for (usize c = 0; c < entities_[i].components.Size(); ++c)
                    {
                        if (entities_[i].components[c].component->Id() == animation::kAnimationPlayerId)
                        {
                            line = entities_[i].components[c].line;
                        }
                    }
                }
            }
            return Fail(error_, line, "the clip was made for another skeleton than the entity's");
        }
    }
    return Status::Success();
}

Status Minter::Finish(Vector<byte>* out, u32 target)
{
    if (!have_id_)
    {
        return Fail(error_, 0, "the level source has no id line (asset_mint adopt writes it)");
    }
    if (registry_.FindById(kEntityNameId) == nullptr || registry_.FindById(storage::kParentLinkId) == nullptr)
    {
        return Fail(error_, 0, "the registry lacks entity_name or parent_link: register the level components");
    }
    // Keys: sorted, none twice.
    struct KeyIndexEntry
    {
        storage::EntityKey key;
        usize entity = 0;
    };
    Vector<KeyIndexEntry> order;
    for (usize i = 0; i < entities_.Size(); ++i)
    {
        order.Push(KeyIndexEntry{entities_[i].key, i});
    }
    Sort(Span<KeyIndexEntry>(order.Data(), order.Size()),
         [](const KeyIndexEntry& a, const KeyIndexEntry& b) { return a.key < b.key; });
    for (usize i = 1; i < order.Size(); ++i)
    {
        if (order[i - 1].key == order[i].key)
        {
            return Fail(error_, entities_[order[i].entity].line, "two entities have the same key");
        }
    }
    LevelData data;
    Vector<storage::EntityKey> keys;
    for (usize i = 0; i < order.Size(); ++i)
    {
        storage::EntityRecord record;
        ENGINE_TRY(Records(entities_[order[i].entity], order[i].entity, keys, &record));
        data.entities.Push(static_cast<storage::EntityRecord&&>(record));
    }
    ENGINE_TRY(CheckClips(data.entities));
    Sort(Span<LevelResource>(resources_.Data(), resources_.Size()),
         [](const LevelResource& a, const LevelResource& b) { return a.id < b.id; });
    for (usize i = 0; i < resources_.Size(); ++i)
    {
        data.resources.Push(resources_[i]);
    }
    const Status built = BuildLevelBlob(types_, registry_, id_, data, out, target, limits_);
    if (built.Failed())
    {
        return Fail(error_, 0, built.Message());
    }
    // The loader reads what was written: parse it, and load it into a world of its own.
    ecs::World world;
    storage::KeyIndex index;
    const Status loaded = LoadLevelBlob(ByteSpan(out->Data(), out->Size()), types_, registry_, world, index, nullptr,
                                        limits_);
    if (loaded.Failed())
    {
        out->Clear();
        return Fail(error_, 0, loaded.Message());
    }
    return Status::Success();
}

Status Minter::Run(StringView text, Vector<byte>* out, u32 target)
{
    ENGINE_TRY(ParseLines(text));
    return Finish(out, target);
}

}  // namespace

Status MintLevel(StringView text, const LevelReferenceResolver& resolver, const TypeRegistry& types,
                 const storage::ComponentRegistry& registry, Vector<byte>* out, LevelSourceError* error, u32 target)
{
    Minter minter(resolver, types, registry, error);
    return minter.Run(text, out, target);
}

namespace
{

// ---------------------------------------------------------------------------------------------------------------------
// Records to text

struct FmtEntity
{
    const storage::EntityRecord* record = nullptr;
    bool named = false;
    String<HeapAllocator> name = String<HeapAllocator>(HeapAllocator::Default());
    Vector<storage::ComponentRecord> records;
};

struct FmtContext
{
    const LevelReferenceResolver* resolver = nullptr;
    const storage::ComponentRegistry* registry = nullptr;
    const Vector<FmtEntity>* entities = nullptr;  // in key order
    const storage::CodecLimits* limits = nullptr;
};

void AppendKey(String<HeapAllocator>* out, const storage::EntityKey& key)
{
    AppendPrinted(out, "@%016llx%016llx", static_cast<unsigned long long>(key.high),
                  static_cast<unsigned long long>(key.low));
}

void AppendEntityText(const FmtContext& ctx, const storage::EntityKey& key, String<HeapAllocator>* out)
{
    if (key.IsNull())
    {
        out->Append(StringView("null"));
        return;
    }
    const Vector<FmtEntity>& list = *ctx.entities;
    usize low = 0;
    usize high = list.Size();
    while (low < high)
    {
        const usize mid = low + (high - low) / 2;
        if (list[mid].record->key == key)
        {
            if (list[mid].named)
            {
                out->Append(list[mid].name.View());
                return;
            }
            break;
        }
        if (list[mid].record->key < key)
        {
            low = mid + 1;
        }
        else
        {
            high = mid;
        }
    }
    AppendKey(out, key);
}

bool PathTokenSafe(StringView path)
{
    if (path.Length() == 0)
    {
        return false;
    }
    for (usize i = 0; i < path.Length(); ++i)
    {
        const char c = path[i];
        if (c <= ' ' || c == '#' || c == '=' || c == '"' || c == '@' || c == 0x7f)
        {
            return false;
        }
    }
    return path != StringView("null");
}

bool FormatElement(storage::FieldKind kind, usize memory_size, const storage::FieldDesc& field, bytes::ByteReader& in,
                   const FmtContext& ctx, String<HeapAllocator>* out, Msg* msg)
{
    using storage::FieldKind;
    switch (kind)
    {
    case FieldKind::U8:
    case FieldKind::U16:
    case FieldKind::U32:
    case FieldKind::U64:
    case FieldKind::I8:
    case FieldKind::I16:
    case FieldKind::I32:
    case FieldKind::I64:
    {
        const IntSpec spec = SpecOf(kind);
        u64 raw = 0;
        if (ReadLE(in, spec.bytes, &raw) != bytes::ByteError::Ok)
        {
            SetMsg(msg, "the record is truncated");
            return false;
        }
        if (spec.is_signed)
        {
            const u32 shift = 64 - 8 * spec.bytes;
            const i64 value = static_cast<i64>(raw << shift) >> shift;
            AppendPrinted(out, "%lld", static_cast<long long>(value));
        }
        else
        {
            AppendPrinted(out, "%llu", static_cast<unsigned long long>(raw));
        }
        return true;
    }
    case FieldKind::F32:
    case FieldKind::Vec2:
    case FieldKind::Vec3:
    case FieldKind::Vec4:
    case FieldKind::Quat:
    case FieldKind::Aabb:
    case FieldKind::Mat4:
    {
        const u32 count = kind == FieldKind::F32    ? 1
                          : kind == FieldKind::Vec2 ? 2
                          : kind == FieldKind::Vec3 ? 3
                          : kind == FieldKind::Aabb ? 6
                          : kind == FieldKind::Mat4 ? 16
                                                    : 4;
        for (u32 i = 0; i < count; ++i)
        {
            f32 value = 0;
            if (in.ReadF32(&value) != bytes::ByteError::Ok)
            {
                SetMsg(msg, "the record is truncated");
                return false;
            }
            if (!IsFinite(value))
            {
                SetMsg(msg, "a float is not finite, which the text form cannot hold");
                return false;
            }
            if (i != 0)
            {
                out->Append(' ');
            }
            AppendPrinted(out, "%.9g", static_cast<double>(value));
        }
        return true;
    }
    case FieldKind::F64:
    {
        f64 value = 0;
        if (in.ReadF64(&value) != bytes::ByteError::Ok || !IsFinite(value))
        {
            SetMsg(msg, "a float is truncated or not finite");
            return false;
        }
        AppendPrinted(out, "%.17g", static_cast<double>(value));
        return true;
    }
    case FieldKind::Bool:
    {
        bool value = false;
        if (in.ReadBool(&value) != bytes::ByteError::Ok)
        {
            SetMsg(msg, "a bool is not 0 or 1");
            return false;
        }
        out->Append(StringView(value ? "true" : "false"));
        return true;
    }
    case FieldKind::Enum:
    {
        u64 raw = 0;
        if (ReadLE(in, static_cast<u32>(memory_size), &raw) != bytes::ByteError::Ok)
        {
            SetMsg(msg, "the record is truncated");
            return false;
        }
        if (field.options.range_min < 0)
        {
            const u32 shift = 64 - 8 * static_cast<u32>(memory_size);
            AppendPrinted(out, "%lld", static_cast<long long>(static_cast<i64>(raw << shift) >> shift));
        }
        else
        {
            AppendPrinted(out, "%llu", static_cast<unsigned long long>(raw));
        }
        return true;
    }
    case FieldKind::String:
    {
        u64 length = 0;
        ByteSpan raw;
        if (in.ReadLength(ctx.limits->max_string_bytes, &length) != bytes::ByteError::Ok ||
            in.ReadBytes(static_cast<usize>(length), &raw) != bytes::ByteError::Ok)
        {
            SetMsg(msg, "a string is truncated or too long");
            return false;
        }
        if (!IsValidUtf8(StringView(reinterpret_cast<const char*>(raw.Data()), raw.Size())))
        {
            SetMsg(msg, "a string is not valid UTF-8");
            return false;
        }
        out->Append('"');
        for (usize i = 0; i < raw.Size(); ++i)
        {
            const u8 c = static_cast<u8>(raw[i]);
            if (c == '"' || c == '\\')
            {
                out->Append('\\');
                out->Append(static_cast<char>(c));
            }
            else if (c == '\n')
            {
                out->Append(StringView("\\n"));
            }
            else if (c == '\t')
            {
                out->Append(StringView("\\t"));
            }
            else if (c == '\r')
            {
                out->Append(StringView("\\r"));
            }
            else if (c < 0x20 || c == 0x7f)
            {
                AppendPrinted(out, "\\x%02x", static_cast<unsigned>(c));
            }
            else
            {
                const char one[1] = {static_cast<char>(c)};
                out->AppendRawBytes(one, 1);
            }
        }
        out->Append('"');
        return true;
    }
    case FieldKind::EntityRef:
    {
        storage::EntityKey key;
        if (in.ReadU64(&key.high) != bytes::ByteError::Ok || in.ReadU64(&key.low) != bytes::ByteError::Ok)
        {
            SetMsg(msg, "the record is truncated");
            return false;
        }
        AppendEntityText(ctx, key, out);
        return true;
    }
    case FieldKind::ResourceRef:
    {
        u64 id = 0;
        if (in.ReadU64(&id) != bytes::ByteError::Ok)
        {
            SetMsg(msg, "the record is truncated");
            return false;
        }
        if (id == 0)
        {
            out->Append(StringView("null"));
            return true;
        }
        String<HeapAllocator> path(HeapAllocator::Default());
        if (ctx.resolver->PathOf(ResourceId::FromValue(id), &path).Ok() && PathTokenSafe(path.View()))
        {
            out->Append(path.View());
        }
        else
        {
            AppendPrinted(out, "@%016llx", static_cast<unsigned long long>(id));
        }
        return true;
    }
    case FieldKind::Array: break;
    }
    SetMsg(msg, "this kind of field has no text form here");
    return false;
}

bool FormatField(const storage::FieldDesc& field, bytes::ByteReader& in, const FmtContext& ctx,
                 String<HeapAllocator>* out, Msg* msg)
{
    if (field.kind != storage::FieldKind::Array)
    {
        return FormatElement(field.kind, field.memory_size, field, in, ctx, out, msg);
    }
    if (field.element_kind == storage::FieldKind::Array || field.element_kind == storage::FieldKind::String ||
        field.element_kind == storage::FieldKind::EntityRef || field.element_kind == storage::FieldKind::ResourceRef)
    {
        SetMsg(msg, "an array of this element kind has no text form");
        return false;
    }
    u64 count = 0;
    if (in.ReadLength(ctx.limits->max_array_elements, &count) != bytes::ByteError::Ok || count != field.element_count)
    {
        SetMsg(msg, "an array has another element count than its type");
        return false;
    }
    for (usize i = 0; i < field.element_count; ++i)
    {
        if (i != 0)
        {
            out->Append(' ');
        }
        if (!FormatElement(field.element_kind, field.element_size, field, in, ctx, out, msg))
        {
            return false;
        }
    }
    return true;
}

}  // namespace

Status FormatLevelRecords(ResourceId id, Span<const storage::EntityRecord> entities, Span<const ResourceId> includes,
                          const LevelReferenceResolver& resolver, const storage::ComponentRegistry& registry,
                          String<HeapAllocator>* out, LevelSourceError* error)
{
    const storage::CodecLimits limits;
    Vector<FmtEntity> list;
    for (usize i = 0; i < entities.Size(); ++i)
    {
        if (i != 0 && !(entities[i - 1].key < entities[i].key))
        {
            return Fail(error, 0, "the entities are not in strictly increasing key order");
        }
        FmtEntity entity;
        entity.record = &entities[i];
        bytes::ByteReader in(ByteSpan(entities[i].blob.Data(), entities[i].blob.Size()));
        u32 count = 0;
        if (in.ReadU32(&count) != bytes::ByteError::Ok || count > limits.max_components_per_entity)
        {
            return Fail(error, 0, "an entity container is damaged");
        }
        for (u32 r = 0; r < count; ++r)
        {
            storage::ComponentRecord rec;
            if (storage::ReadComponentRecord(in, limits, &rec).Failed())
            {
                return Fail(error, 0, "an entity container is damaged");
            }
            entity.records.Push(rec);
            if (rec.id == kEntityNameId)
            {
                bytes::ByteReader name(rec.bytes);
                u64 length = 0;
                ByteSpan raw;
                if (name.ReadLength(kMaxLevelNameBytes, &length) != bytes::ByteError::Ok ||
                    name.ReadBytes(static_cast<usize>(length), &raw) != bytes::ByteError::Ok)
                {
                    return Fail(error, 0, "an entity name is damaged");
                }
                const StringView text(reinterpret_cast<const char*>(raw.Data()), raw.Size());
                if (!LevelNameValid(text))
                {
                    Msg msg;
                    LV_MSG(
                        &msg,
                        "the entity name '%.*s' cannot be written in the text form (letters, digits and _ . : / - only)",
                        static_cast<int>(text.Length()), text.Data());
                    return FailMsg(error, 0, msg);
                }
                entity.named = true;
                entity.name.Append(text);
            }
        }
        list.Push(static_cast<FmtEntity&&>(entity));
    }
    // Ordered by name; unnamed last, by key.
    Vector<usize> order;
    for (usize i = 0; i < list.Size(); ++i)
    {
        order.Push(i);
    }
    Sort(Span<usize>(order.Data(), order.Size()),
         [&list](usize a, usize b)
         {
             if (list[a].named != list[b].named)
             {
                 return list[a].named;
             }
             if (list[a].named && list[a].name.View() != list[b].name.View())
             {
                 return list[a].name.View() < list[b].name.View();
             }
             return list[a].record->key < list[b].record->key;
         });

    for (usize o = 1; o < order.Size(); ++o)
    {
        if (list[order[o]].named && list[order[o - 1]].named &&
            list[order[o]].name.View() == list[order[o - 1]].name.View())
        {
            Msg msg;
            LV_MSG(&msg, "two entities are called '%s', which the text form cannot tell apart",
                   list[order[o]].name.View().Data());
            return FailMsg(error, 0, msg);
        }
    }
    FmtContext ctx;
    ctx.resolver = &resolver;
    ctx.registry = &registry;
    ctx.entities = &list;
    ctx.limits = &limits;
    String<HeapAllocator> text(HeapAllocator::Default());
    char hex[17];
    FormatResourceId(id, hex);
    AppendPrinted(&text, "id %s\n", hex);
    for (usize i = 0; i < includes.Size(); ++i)
    {
        String<HeapAllocator> path(HeapAllocator::Default());
        if (resolver.PathOf(includes[i], &path).Ok() && PathTokenSafe(path.View()))
        {
            AppendPrinted(&text, "include %.*s\n", static_cast<int>(path.Length()), path.View().Data());
        }
        else
        {
            FormatResourceId(includes[i], hex);
            AppendPrinted(&text, "include @%s\n", hex);
        }
    }
    for (usize o = 0; o < order.Size(); ++o)
    {
        const FmtEntity& entity = list[order[o]];
        text.Append('\n');
        if (entity.named)
        {
            AppendPrinted(&text, "entity %.*s", static_cast<int>(entity.name.Length()), entity.name.View().Data());
            if (entity.record->key != LevelEntityKey(id, entity.name.View()))
            {
                text.Append(StringView(" key "));
                AppendKey(&text, entity.record->key);
            }
        }
        else
        {
            text.Append(StringView("entity key "));
            AppendKey(&text, entity.record->key);
        }
        text.Append('\n');
        for (usize r = 0; r < entity.records.Size(); ++r)
        {
            const storage::ComponentRecord& rec = entity.records[r];
            if (rec.id == kEntityNameId)
            {
                continue;
            }
            const storage::RegisteredComponent* component = registry.FindById(rec.id);
            if (component == nullptr)
            {
                Msg msg;
                LV_MSG(&msg, "an entity holds a record of the component %u, which the registry does not know",
                       static_cast<unsigned>(rec.id));
                return FailMsg(error, 0, msg);
            }
            if (rec.id == storage::kParentLinkId)
            {
                bytes::ByteReader in(rec.bytes);
                storage::EntityKey parent;
                if (in.ReadU64(&parent.high) != bytes::ByteError::Ok || in.ReadU64(&parent.low) != bytes::ByteError::Ok)
                {
                    return Fail(error, 0, "a parent link is damaged");
                }
                if (!parent.IsNull())
                {
                    text.Append(StringView("  parent "));
                    AppendEntityText(ctx, parent, &text);
                    text.Append('\n');
                }
                continue;
            }
            if (!component->IsStored() || rec.id == storage::kEntityKeyId)
            {
                continue;  // derived state is not authored
            }
            AppendPrinted(&text, "  component %s\n", component->Name());
            bytes::ByteReader in(rec.bytes);
            const Span<const storage::FieldDesc> fields = component->Fields();
            for (usize f = 0; f < fields.Size(); ++f)
            {
                String<HeapAllocator> value(HeapAllocator::Default());
                Msg msg;
                if (!FormatField(fields[f], in, ctx, &value, &msg))
                {
                    Msg full;
                    LV_MSG(&full, "%s.%s of the entity '%s': %s", component->Name(), fields[f].name,
                           entity.named ? entity.name.View().Data() : "(unnamed)", msg.text.Data());
                    return FailMsg(error, 0, full);
                }
                AppendPrinted(&text, "    %s = ", fields[f].name);
                text.Append(value.View());
                text.Append('\n');
            }
        }
    }
    *out = static_cast<String<HeapAllocator>&&>(text);
    return Status::Success();
}

Status FormatLevelBlob(ByteSpan blob, const TypeRegistry& types, const storage::ComponentRegistry& registry,
                       const LevelReferenceResolver& resolver, String<HeapAllocator>* out, LevelSourceError* error)
{
    LevelData data;
    ResourceId id;
    const Status parsed = ParseLevelBlob(blob, types, registry, &data, &id);
    if (parsed.Failed())
    {
        return Fail(error, 0, parsed.Message());
    }
    Vector<ResourceId> includes;
    for (usize i = 0; i < data.resources.Size(); ++i)
    {
        if ((data.resources[i].roles & kLevelRoleInclude) != 0)
        {
            includes.Push(data.resources[i].id);
        }
    }
    return FormatLevelRecords(id, Span<const storage::EntityRecord>(data.entities.Data(), data.entities.Size()),
                              Span<const ResourceId>(includes.Data(), includes.Size()), resolver, registry, out, error);
}

namespace
{

class Collect final : public storage::RecordVisitor
{
public:
    bool Visit(const storage::EntityRecord& record) override
    {
        records.Push(record.Clone());
        return true;
    }
    Vector<storage::EntityRecord> records;
};

}  // namespace

Status FormatWorld(ResourceId id, ecs::World& world, storage::KeyIndex& index, Span<const ResourceId> includes,
                   const LevelReferenceResolver& resolver, const storage::ComponentRegistry& registry,
                   String<HeapAllocator>* out, LevelSourceError* error)
{
    storage::MemoryStore store;
    storage::WorldStorage storage(registry, store);
    storage::SaveOptions options;
    const storage::Status saved = storage.Save(world, index, options, nullptr);
    if (saved.Failed())
    {
        return Fail(error, 0, saved.Message());
    }
    Collect collect;
    const storage::Status scanned = store.Scan(storage::KeyRange(), collect);
    if (scanned.Failed())
    {
        return Fail(error, 0, scanned.Message());
    }
    return FormatLevelRecords(id, Span<const storage::EntityRecord>(collect.records.Data(), collect.records.Size()),
                              includes, resolver, registry, out, error);
}

}  // namespace engine_placeholder::asset
