#include "asset_import/gltf_container.h"

#include "cstr/format.h"
#include "cstr/mem_ops.h"

namespace engine_placeholder::asset
{

namespace
{

constexpr u32 kGlbMagic = 0x46546C67;   // "glTF"
constexpr u32 kChunkJson = 0x4E4F534A;  // "JSON"
constexpr u32 kChunkBin = 0x004E4942;   // "BIN\0"
constexpr usize kMaxChunks = 16;
constexpr usize kMaxUriBytes = 512;

const char* const kSupported[] = {"KHR_lights_punctual",
                                  "KHR_materials_emissive_strength",
                                  "KHR_materials_pbrSpecularGlossiness",
                                  "KHR_materials_unlit",
                                  "OMI_physics_body",
                                  "OMI_physics_shape",
                                  "KHR_texture_transform"};

bool AddOverflows(u64 a, u64 b, u64* out)
{
    *out = a + b;
    return *out < a;
}

bool MulOverflows(u64 a, u64 b, u64* out)
{
    if (a != 0 && b > ~u64{0} / a)
    {
        return true;
    }
    *out = a * b;
    return false;
}

u32 LoadU32(const byte* p)
{
    return static_cast<u32>(p[0]) | static_cast<u32>(p[1]) << 8 | static_cast<u32>(p[2]) << 16 |
           static_cast<u32>(p[3]) << 24;
}

u32 ComponentBytes(u32 component_type)
{
    switch (component_type)
    {
    case 5120:
    case 5121: return 1;
    case 5122:
    case 5123: return 2;
    case 5125:
    case 5126: return 4;
    default: return 0;
    }
}

Status Fail(GltfError* error, StringView where, const char* what, ErrorCode code = ErrorCode::Invalid)
{
    error->Set(where, StringView(what));
    return Status::Make(code, "the glTF breaks a rule: see the error text");
}

// The bytes of one accessor element and its components.
bool TypeInfo(StringView name, GltfAccessorType* type, u32* components, u32* columns, u32* column_components)
{
    struct Row
    {
        const char* name;
        GltfAccessorType type;
        u32 components;
        u32 columns;
        u32 column_components;
    };
    static const Row kRows[] = {
        {"SCALAR", GltfAccessorType::Scalar, 1, 1, 1}, {"VEC2", GltfAccessorType::Vec2, 2, 1, 2},
        {"VEC3", GltfAccessorType::Vec3, 3, 1, 3},     {"VEC4", GltfAccessorType::Vec4, 4, 1, 4},
        {"MAT2", GltfAccessorType::Mat2, 4, 2, 2},     {"MAT3", GltfAccessorType::Mat3, 9, 3, 3},
        {"MAT4", GltfAccessorType::Mat4, 16, 4, 4}};
    for (const Row& r : kRows)
    {
        if (name == StringView(r.name))
        {
            *type = r.type;
            *components = r.components;
            *columns = r.columns;
            *column_components = r.column_components;
            return true;
        }
    }
    return false;
}

Status ArrayCount(const JsonValue& root, StringView name, u64 limit, usize* count, GltfError* error)
{
    *count = 0;
    const JsonValue array = root.Get(name);
    if (!array.Exists())
    {
        return Status::Success();
    }
    if (!array.IsArray())
    {
        return Fail(error, array.Path(), "must be an array");
    }
    if (array.Size() > limit)
    {
        error->SetNumber(array.Path(), StringView("has more entries than the limit of"), limit);
        return Status::Make(ErrorCode::TooLarge, "an array is above its limit");
    }
    *count = array.Size();
    return Status::Success();
}

// ---------------------------------------------------------------------------------------------------------------------
// The file: .glb or .gltf

struct Chunks
{
    ByteSpan json;
    ByteSpan bin;
    bool has_bin = false;
};

Status SplitGlb(ByteSpan file, Chunks* out, GltfError* error)
{
    if (file.Size() < 12)
    {
        return Fail(error, StringView(), "the .glb is shorter than its 12-byte header");
    }
    if (LoadU32(file.Data() + 4) != 2)
    {
        return Fail(error, StringView(), "the .glb is not container version 2");
    }
    if (LoadU32(file.Data() + 8) != file.Size())
    {
        return Fail(error, StringView(), "the length in the .glb header is not the length of the file");
    }
    usize at = 12;
    usize chunks = 0;
    bool have_json = false;
    while (at < file.Size())
    {
        if (++chunks > kMaxChunks)
        {
            return Fail(error, StringView(), "the .glb has too many chunks");
        }
        if (file.Size() - at < 8)
        {
            return Fail(error, StringView(), "a .glb chunk header is cut off");
        }
        const u32 length = LoadU32(file.Data() + at);
        const u32 type = LoadU32(file.Data() + at + 4);
        at += 8;
        if (length % 4 != 0)
        {
            return Fail(error, StringView(), "a .glb chunk length is not a multiple of four");
        }
        if (length > file.Size() - at)
        {
            return Fail(error, StringView(), "a .glb chunk runs past the end of the file");
        }
        const ByteSpan body(file.Data() + at, length);
        at += length;
        if (!have_json)
        {
            if (type != kChunkJson)
            {
                return Fail(error, StringView(), "the first .glb chunk is not the JSON chunk");
            }
            out->json = body;
            have_json = true;
        }
        else if (type == kChunkBin && !out->has_bin && chunks == 2)
        {
            out->bin = body;
            out->has_bin = true;
        }
        // Any other chunk is ignored (the container allows extensions), counted against kMaxChunks.
    }
    if (!have_json)
    {
        return Fail(error, StringView(), "the .glb has no JSON chunk");
    }
    return Status::Success();
}

// ---------------------------------------------------------------------------------------------------------------------
// Buffers

int Base64Digit(char c)
{
    if (c >= 'A' && c <= 'Z')
    {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z')
    {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9')
    {
        return c - '0' + 52;
    }
    return c == '+' ? 62 : (c == '/' ? 63 : -1);
}

bool Hex(char c, u32* out)
{
    if (c >= '0' && c <= '9')
    {
        *out = static_cast<u32>(c - '0');
        return true;
    }
    if (c >= 'a' && c <= 'f')
    {
        *out = static_cast<u32>(c - 'a' + 10);
        return true;
    }
    if (c >= 'A' && c <= 'F')
    {
        *out = static_cast<u32>(c - 'A' + 10);
        return true;
    }
    return false;
}

Status ReadBuffers(const JsonValue& root, bool is_glb, const Chunks& chunks, GltfSource& source,
                   const ImportLimits& limits, Vector<Vector<byte>>* out, GltfError* error)
{
    const JsonValue buffers = root.Get("buffers");
    u64 total = 0;
    for (usize i = 0; i < buffers.Size(); ++i)
    {
        const JsonValue buffer = buffers.At(i);
        if (!buffer.IsObject())
        {
            return Fail(error, buffer.Path(), "must be an object");
        }
        u64 length = 0;
        ENGINE_TRY(buffer.RequiredUnsigned(StringView("byteLength"), &length, limits.max_buffer_bytes, error));
        if (length == 0)
        {
            return Fail(error, buffer.Path(), "has a byteLength of zero");
        }
        if (AddOverflows(total, length, &total) || total > limits.max_buffer_bytes)
        {
            error->SetNumber(buffer.Path(), StringView("the buffers together are above the limit of bytes:"),
                             limits.max_buffer_bytes);
            return Status::Make(ErrorCode::TooLarge, "the buffers are above the limit");
        }
        Vector<byte> bytes;
        StringView uri;
        bool has_uri = false;
        ENGINE_TRY(buffer.OptionalString(StringView("uri"), &uri, &has_uri, error));
        if (!has_uri)
        {
            if (i != 0 || !is_glb || !chunks.has_bin)
            {
                return Fail(error, buffer.Path(),
                            "has no uri, and only buffer 0 of a .glb with a BIN chunk may have none");
            }
            if (chunks.bin.Size() < length)
            {
                return Fail(error, buffer.Path(), "is longer than the BIN chunk");
            }
            if (!bytes.TryResizeUninitialized(static_cast<usize>(length)))
            {
                return Fail(error, buffer.Path(), "cannot be had: out of memory", ErrorCode::LimitExceeded);
            }
            MemCopy(bytes.Data(), chunks.bin.Data(), static_cast<usize>(length));
        }
        else if (uri.StartsWith(StringView("data:")))
        {
            // data:<media type>;base64,<characters>
            const usize comma = uri.IndexOf(',');
            if (comma == uri.Length())
            {
                return Fail(error, buffer.Path(), "has a data: URI with no comma");
            }
            const StringView header = uri.Substring(5, comma - 5);
            const bool octets = header == StringView("application/octet-stream;base64") ||
                                header == StringView("application/gltf-buffer;base64");
            if (!octets)
            {
                return Fail(
                    error, buffer.Path(),
                    "has a data: URI that is not application/octet-stream or application/gltf-buffer in base64");
            }
            // The text may decode to a few bytes more than the byteLength (padding), never to a different size class.
            const Status decoded = DecodeBase64(uri.Substring(comma + 1), length + 3, &bytes, error);
            if (decoded.Failed())
            {
                GltfError inner = *error;
                error->Set(buffer.Path(), StringView(inner.message.Data()));
                return decoded;
            }
            if (bytes.Size() < length)
            {
                return Fail(error, buffer.Path(), "has a data: URI shorter than its byteLength");
            }
            bytes.Resize(static_cast<usize>(length));
        }
        else
        {
            String<HeapAllocator> name(HeapAllocator::Default());
            Status cleaned = CleanRelativeUri(uri, &name, error);
            if (cleaned.Failed())
            {
                GltfError inner = *error;
                error->Set(buffer.Path(), StringView(inner.message.Data()));
                return cleaned;
            }
            const Status read = source.Read(name.View(), length, &bytes);
            if (read.Failed())
            {
                char text[192];
                FormatTo(text, sizeof(text), "the file \"%.*s\" cannot be read: %s", static_cast<int>(name.Length()),
                         name.CStr(), read.Message());
                error->Set(buffer.Path(), StringView(text));
                return read;
            }
            if (bytes.Size() < length)
            {
                return Fail(error, buffer.Path(), "names a file shorter than its byteLength");
            }
            if (bytes.Size() > length)
            {
                bytes.Resize(static_cast<usize>(length));  // a source that returned more than it was asked for
            }
        }
        out->Push(static_cast<Vector<byte>&&>(bytes));
    }
    return Status::Success();
}

// ---------------------------------------------------------------------------------------------------------------------
// Views and accessors

Status ReadViews(const JsonValue& root, const Vector<Vector<byte>>& buffers, const ImportLimits& limits,
                 Vector<GltfBufferView>* out, GltfError* error)
{
    const JsonValue views = root.Get("bufferViews");
    for (usize i = 0; i < views.Size(); ++i)
    {
        const JsonValue view = views.At(i);
        if (!view.IsObject())
        {
            return Fail(error, view.Path(), "must be an object");
        }
        GltfBufferView v;
        ENGINE_TRY(view.RequiredIndex(StringView("buffer"), &v.buffer, buffers.Size(), error));
        u64 length = 0;
        u64 offset = 0;
        u64 stride = 0;
        ENGINE_TRY(view.RequiredUnsigned(StringView("byteLength"), &length, limits.max_buffer_bytes, error));
        ENGINE_TRY(view.OptionalUnsigned(StringView("byteOffset"), &offset, limits.max_buffer_bytes, nullptr, error));
        ENGINE_TRY(view.OptionalUnsigned(StringView("byteStride"), &stride, 252, nullptr, error));
        u64 end = 0;
        if (length == 0 || AddOverflows(offset, length, &end) || end > buffers[v.buffer].Size())
        {
            return Fail(error, view.Path(), "does not lie inside its buffer");
        }
        if (stride != 0 && (stride < 4 || stride % 4 != 0))
        {
            return Fail(error, view.Path(), "has a byteStride that is not a multiple of four from 4 to 252");
        }
        v.offset = offset;
        v.length = length;
        v.stride = static_cast<u32>(stride);
        out->Push(v);
    }
    return Status::Success();
}

// The first byte past the last element of `count` elements of `element` bytes, `stride` apart, from `offset`.
bool ExtentOf(u64 offset, u64 count, u32 element, u32 stride, u64* end)
{
    u64 span = 0;
    u64 last = 0;
    if (MulOverflows(count - 1, stride, &span) || AddOverflows(offset, span, &last) || AddOverflows(last, element, end))
    {
        return false;
    }
    return true;
}

Status ReadAccessors(const JsonValue& root, const Vector<GltfBufferView>& views, const ImportLimits& limits,
                     Vector<GltfAccessor>* out, GltfError* error)
{
    const JsonValue accessors = root.Get("accessors");
    for (usize i = 0; i < accessors.Size(); ++i)
    {
        const JsonValue accessor = accessors.At(i);
        if (!accessor.IsObject())
        {
            return Fail(error, accessor.Path(), "must be an object");
        }
        GltfAccessor a;
        u64 component = 0;
        ENGINE_TRY(accessor.RequiredUnsigned(StringView("componentType"), &component, 0xffffffffull, error));
        a.component_type = static_cast<u32>(component);
        const u32 csize = ComponentBytes(a.component_type);
        if (csize == 0)
        {
            return Fail(error, accessor.Path(), "has a componentType that is not 5120, 5121, 5122, 5123, 5125 or 5126");
        }
        StringView type_name;
        ENGINE_TRY(accessor.RequiredString(StringView("type"), &type_name, error));
        u32 columns = 1;
        u32 column_components = 1;
        if (!TypeInfo(type_name, &a.type, &a.components, &columns, &column_components))
        {
            return Fail(error, accessor.Path(), "has a type that is not SCALAR, VEC2, VEC3, VEC4, MAT2, MAT3 or MAT4");
        }
        ENGINE_TRY(accessor.RequiredUnsigned(StringView("count"), &a.count, limits.max_accessor_elements, error));
        if (a.count == 0)
        {
            return Fail(error, accessor.Path(), "has a count of zero");
        }
        ENGINE_TRY(accessor.OptionalBool(StringView("normalized"), &a.normalized, error));
        if (a.normalized && (a.component_type == 5125 || a.component_type == 5126))
        {
            return Fail(error, accessor.Path(), "is normalized, which a float or UNSIGNED_INT accessor may not be");
        }
        // One element: the columns of a matrix start on four-byte boundaries.
        u32 column_bytes = column_components * csize;
        if (columns > 1)
        {
            column_bytes = (column_bytes + 3) & ~3u;
        }
        a.element_bytes = columns * column_bytes;
        const JsonValue view_member = accessor.Get("bufferView");
        u64 offset = 0;
        ENGINE_TRY(
            accessor.OptionalUnsigned(StringView("byteOffset"), &offset, limits.max_buffer_bytes, nullptr, error));
        if (view_member.Exists())
        {
            ENGINE_TRY(view_member.AsIndex(&a.view, views.Size(), error));
            a.has_view = true;
            const GltfBufferView& view = views[a.view];
            a.stride = view.stride != 0 ? view.stride : a.element_bytes;
            if (a.stride < a.element_bytes)
            {
                return Fail(error, accessor.Path(), "has elements wider than the byteStride of its buffer view");
            }
            u64 end = 0;
            if (!ExtentOf(offset, a.count, a.element_bytes, a.stride, &end) || end > view.length)
            {
                return Fail(error, accessor.Path(), "does not lie inside its buffer view");
            }
            a.offset = offset;
        }
        else if (offset != 0)
        {
            return Fail(error, accessor.Path(), "has a byteOffset and no bufferView");
        }
        const JsonValue sparse = accessor.Get("sparse");
        if (sparse.Exists())
        {
            if (!sparse.IsObject())
            {
                return Fail(error, sparse.Path(), "must be an object");
            }
            a.sparse.present = true;
            u64 count = 0;
            ENGINE_TRY(sparse.RequiredUnsigned(StringView("count"), &count, a.count, error));
            if (count == 0)
            {
                return Fail(error, sparse.Path(), "has a count of zero");
            }
            a.sparse.count = static_cast<u32>(count);
            const JsonValue indices = sparse.Get("indices");
            const JsonValue values = sparse.Get("values");
            if (!indices.IsObject() || !values.IsObject())
            {
                return Fail(error, sparse.Path(), "needs an indices object and a values object");
            }
            u64 index_type = 0;
            u64 index_offset = 0;
            u64 value_offset = 0;
            ENGINE_TRY(indices.RequiredIndex(StringView("bufferView"), &a.sparse.index_view, views.Size(), error));
            ENGINE_TRY(indices.RequiredUnsigned(StringView("componentType"), &index_type, 0xffffffffull, error));
            ENGINE_TRY(indices.OptionalUnsigned(StringView("byteOffset"), &index_offset, limits.max_buffer_bytes,
                                                nullptr, error));
            ENGINE_TRY(values.RequiredIndex(StringView("bufferView"), &a.sparse.value_view, views.Size(), error));
            ENGINE_TRY(values.OptionalUnsigned(StringView("byteOffset"), &value_offset, limits.max_buffer_bytes,
                                               nullptr, error));
            if (index_type != 5121 && index_type != 5123 && index_type != 5125)
            {
                return Fail(error, indices.Path(), "has a componentType that is not 5121, 5123 or 5125");
            }
            a.sparse.index_type = static_cast<u32>(index_type);
            a.sparse.index_offset = index_offset;
            a.sparse.value_offset = value_offset;
            u64 end = 0;
            if (!ExtentOf(index_offset, count, ComponentBytes(a.sparse.index_type), ComponentBytes(a.sparse.index_type),
                          &end) ||
                end > views[a.sparse.index_view].length)
            {
                return Fail(error, indices.Path(), "does not lie inside its buffer view");
            }
            if (!ExtentOf(value_offset, count, a.element_bytes, a.element_bytes, &end) ||
                end > views[a.sparse.value_view].length)
            {
                return Fail(error, values.Path(), "does not lie inside its buffer view");
            }
        }
        out->Push(a);
    }
    return Status::Success();
}

}  // namespace

const char* const* SupportedRequiredExtensions(usize* count)
{
    *count = sizeof(kSupported) / sizeof(kSupported[0]);
    return kSupported;
}

Status DecodeBase64(StringView text, u64 max_bytes, Vector<byte>* out, GltfError* error)
{
    out->Clear();
    if (text.Length() % 4 != 0)
    {
        error->Set(StringView(), StringView("the base64 text is not a multiple of four characters"));
        return Status::Make(ErrorCode::Invalid, "bad base64");
    }
    usize padding = 0;
    if (text.Length() >= 1 && text[text.Length() - 1] == '=')
    {
        padding = text.Length() >= 2 && text[text.Length() - 2] == '=' ? 2 : 1;
    }
    const u64 decoded = static_cast<u64>(text.Length() / 4) * 3 - padding;
    if (decoded > max_bytes)
    {
        error->SetNumber(StringView(), StringView("the base64 text decodes to more than the limit of bytes:"),
                         max_bytes);
        return Status::Make(ErrorCode::TooLarge, "the base64 text is too long");
    }
    if (!out->TryResize(static_cast<usize>(decoded)))
    {
        error->Set(StringView(), StringView("there is no memory for the decoded base64"));
        return Status::Make(ErrorCode::LimitExceeded, "out of memory");
    }
    usize written = 0;
    for (usize i = 0; i < text.Length(); i += 4)
    {
        const bool last = i + 4 == text.Length();
        int v[4];
        for (usize k = 0; k < 4; ++k)
        {
            const char c = text[i + k];
            if (last && c == '=' && k >= 4 - padding)
            {
                v[k] = 0;
            }
            else
            {
                v[k] = Base64Digit(c);
                if (v[k] < 0)
                {
                    error->Set(StringView(),
                               StringView("the base64 text has a character outside the alphabet or a misplaced ="));
                    return Status::Make(ErrorCode::Invalid, "bad base64");
                }
            }
        }
        const u32 word = static_cast<u32>(v[0]) << 18 | static_cast<u32>(v[1]) << 12 | static_cast<u32>(v[2]) << 6 |
                         static_cast<u32>(v[3]);
        const usize bytes = last ? 3 - padding : 3;
        for (usize k = 0; k < bytes; ++k)
        {
            (*out)[written++] = static_cast<byte>(word >> (16 - 8 * k));
        }
    }
    return Status::Success();
}

Status CleanRelativeUri(StringView uri, String<HeapAllocator>* out, GltfError* error)
{
    *out = String<HeapAllocator>(HeapAllocator::Default());
    if (uri.Length() == 0 || uri.Length() > 3 * kMaxUriBytes)
    {
        error->Set(StringView(), StringView("the URI is empty or too long"));
        return Status::Make(ErrorCode::Invalid, "bad URI");
    }
    // Percent-escapes first, so that `%2e%2e` cannot hide a `..`.
    String<HeapAllocator> decoded(HeapAllocator::Default());
    for (usize i = 0; i < uri.Length(); ++i)
    {
        char c = uri[i];
        if (c == '%')
        {
            u32 hi = 0;
            u32 lo = 0;
            if (i + 2 >= uri.Length())
            {
                error->Set(StringView(), StringView("the URI has a percent-escape that is cut off"));
                return Status::Make(ErrorCode::Invalid, "bad URI");
            }
            if (!Hex(uri[i + 1], &hi) || !Hex(uri[i + 2], &lo))
            {
                error->Set(StringView(), StringView("the URI has a bad percent-escape"));
                return Status::Make(ErrorCode::Invalid, "bad URI");
            }
            c = static_cast<char>(hi * 16 + lo);
            i += 2;
        }
        if (c == '\0' || static_cast<u8>(c) < 0x20 || c == 0x7f || c == '\\')
        {
            error->Set(StringView(), StringView("the URI has a control character, a NUL or a backslash"));
            return Status::Make(ErrorCode::Invalid, "bad URI");
        }
        decoded.Append(c);
    }
    const StringView text = decoded.View();
    if (text[0] == '/')
    {
        error->Set(StringView(), StringView("the URI is an absolute path"));
        return Status::Make(ErrorCode::Invalid, "absolute URI");
    }
    // A scheme (`file:`, `http:`, `C:`) is a colon in the first segment.
    for (usize i = 0; i < text.Length() && text[i] != '/'; ++i)
    {
        if (text[i] == ':')
        {
            error->Set(
                StringView(),
                StringView(
                    "the URI has a scheme or a drive letter: only relative names inside the glTF's folder are read"));
            return Status::Make(ErrorCode::Invalid, "URI with a scheme");
        }
    }
    usize start = 0;
    for (usize i = 0; i <= text.Length(); ++i)
    {
        if (i == text.Length() || text[i] == '/')
        {
            const StringView segment = text.Substring(start, i - start);
            start = i + 1;
            if (segment.Length() == 0)
            {
                error->Set(StringView(), StringView("the URI has an empty segment"));
                return Status::Make(ErrorCode::Invalid, "bad URI");
            }
            if (segment == StringView(".."))
            {
                error->Set(StringView(), StringView("the URI leaves the glTF's folder with .."));
                return Status::Make(ErrorCode::Invalid, "URI escapes");
            }
            if (segment == StringView("."))
            {
                continue;
            }
            if (out->Length() != 0)
            {
                out->Append('/');
            }
            out->Append(segment);
        }
    }
    if (out->Length() == 0 || out->Length() > kMaxUriBytes)
    {
        error->Set(StringView(), StringView("the URI names nothing, or is longer than 512 bytes"));
        return Status::Make(ErrorCode::Invalid, "bad URI");
    }
    return Status::Success();
}

Status ParseGltf(ByteSpan file, GltfSource& source, const ImportLimits& limits, GltfDocument* out, GltfError* error)
{
    *out = GltfDocument();
    if (file.Size() > limits.max_file_bytes)
    {
        error->SetNumber(StringView(), StringView("the file is larger than the limit of bytes:"),
                         limits.max_file_bytes);
        return Status::Make(ErrorCode::TooLarge, "the file is too large");
    }
    Chunks chunks;
    ByteSpan json = file;
    if (file.Size() >= 4 && LoadU32(file.Data()) == kGlbMagic)
    {
        out->is_glb = true;
        ENGINE_TRY(SplitGlb(file, &chunks, error));
        json = chunks.json;
    }
    ENGINE_TRY(out->json.Parse(json, limits.max_json_bytes, error));
    out->root = out->json.Root();
    if (!out->root.IsObject())
    {
        return Fail(error, StringView(), "the JSON is not an object");
    }
    // asset.version
    const JsonValue asset = out->root.Get("asset");
    if (!asset.IsObject())
    {
        return Fail(error, StringView("asset"), "is required and must be an object");
    }
    StringView version;
    ENGINE_TRY(asset.RequiredString(StringView("version"), &version, error));
    if (version != StringView("2.0"))
    {
        return Fail(error, asset.Get("version").Path(), "is not 2.0: only glTF 2.0 is read");
    }
    StringView minimum;
    bool has_minimum = false;
    ENGINE_TRY(asset.OptionalString(StringView("minVersion"), &minimum, &has_minimum, error));
    if (has_minimum && minimum != StringView("2.0") && minimum != StringView("1.0"))
    {
        return Fail(error, asset.Get("minVersion").Path(), "asks for a newer glTF than 2.0");
    }
    // extensionsRequired
    const JsonValue required = out->root.Get("extensionsRequired");
    if (required.Exists() && !required.IsArray())
    {
        return Fail(error, required.Path(), "must be an array");
    }
    usize supported_count = 0;
    const char* const* supported = SupportedRequiredExtensions(&supported_count);
    for (usize i = 0; i < required.Size(); ++i)
    {
        StringView name;
        ENGINE_TRY(required.At(i).AsString(&name, error));
        bool known = false;
        for (usize k = 0; k < supported_count; ++k)
        {
            known = known || name == StringView(supported[k]);
        }
        if (!known)
        {
            char text[192];
            FormatTo(text, sizeof(text), "the extension %.*s is required and not supported",
                     static_cast<int>(name.Length()), name.Data());
            error->Set(required.Path(), StringView(text));
            return Status::Make(ErrorCode::Unsupported, "a required extension is not supported");
        }
    }
    // Counts of the arrays the importer reads.
    usize count = 0;
    ENGINE_TRY(ArrayCount(out->root, StringView("buffers"), limits.max_buffers, &count, error));
    ENGINE_TRY(ArrayCount(out->root, StringView("bufferViews"), limits.max_buffer_views, &count, error));
    ENGINE_TRY(ArrayCount(out->root, StringView("accessors"), limits.max_accessors, &count, error));
    ENGINE_TRY(ArrayCount(out->root, StringView("meshes"), limits.max_meshes, &count, error));
    ENGINE_TRY(ArrayCount(out->root, StringView("nodes"), limits.max_nodes, &count, error));
    ENGINE_TRY(ArrayCount(out->root, StringView("images"), limits.max_images, &count, error));
    ENGINE_TRY(ArrayCount(out->root, StringView("textures"), limits.max_textures, &count, error));
    ENGINE_TRY(ArrayCount(out->root, StringView("materials"), limits.max_materials, &count, error));
    ENGINE_TRY(ArrayCount(out->root, StringView("skins"), limits.max_skins, &count, error));
    ENGINE_TRY(ArrayCount(out->root, StringView("animations"), limits.max_animations, &count, error));
    ENGINE_TRY(ReadBuffers(out->root, out->is_glb, chunks, source, limits, &out->buffers, error));
    ENGINE_TRY(ReadViews(out->root, out->buffers, limits, &out->views, error));
    ENGINE_TRY(ReadAccessors(out->root, out->views, limits, &out->accessors, error));
    return Status::Success();
}

}  // namespace engine_placeholder::asset
