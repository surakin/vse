#include "asset_import/gltf_accessor.h"

#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "lang/scalar.h"

namespace vse::asset
{

namespace
{

u32 CompBytes(u32 component_type)
{
    switch (component_type)
    {
    case 5120:
    case 5121: return 1;
    case 5122:
    case 5123: return 2;
    default: return 4;
    }
}

u32 Load16(const byte* p)
{
    return static_cast<u32>(p[0]) | static_cast<u32>(p[1]) << 8;
}

u32 Load32(const byte* p)
{
    return Load16(p) | static_cast<u32>(p[2]) << 16 | static_cast<u32>(p[3]) << 24;
}

// One component as a float: an integer as itself, or, normalized, mapped to its range; a float as it is.
f32 ToFloat(const byte* p, u32 component_type, bool normalized)
{
    switch (component_type)
    {
    case 5120:
    {
        const i32 v = static_cast<i8>(p[0]);
        return normalized ? FMax(static_cast<f32>(v) / 127.0f, -1.0f) : static_cast<f32>(v);
    }
    case 5121: return normalized ? static_cast<f32>(p[0]) / 255.0f : static_cast<f32>(p[0]);
    case 5122:
    {
        const i32 v = static_cast<i16>(Load16(p));
        return normalized ? FMax(static_cast<f32>(v) / 32767.0f, -1.0f) : static_cast<f32>(v);
    }
    case 5123:
    {
        const u32 v = Load16(p);
        return normalized ? static_cast<f32>(v) / 65535.0f : static_cast<f32>(v);
    }
    case 5125: return static_cast<f32>(Load32(p));
    default:
    {
        const u32 bits = Load32(p);
        f32 value = 0;
        MemCopy(&value, &bits, 4);
        return value;
    }
    }
}

u32 ToUnsigned(const byte* p, u32 component_type)
{
    return component_type == 5121 ? static_cast<u32>(p[0]) : (component_type == 5123 ? Load16(p) : Load32(p));
}

// The columns of an element: a matrix has columns padded to four bytes, anything else is one column.
void Shape(const GltfAccessor& a, u32* columns, u32* column_components, u32* column_bytes)
{
    switch (a.type)
    {
    case GltfAccessorType::Mat2:
        *columns = 2;
        *column_components = 2;
        break;
    case GltfAccessorType::Mat3:
        *columns = 3;
        *column_components = 3;
        break;
    case GltfAccessorType::Mat4:
        *columns = 4;
        *column_components = 4;
        break;
    default:
        *columns = 1;
        *column_components = a.components;
        break;
    }
    *column_bytes = a.element_bytes / *columns;
}

Status SparseFail(GltfError* error, u32 index, const char* what)
{
    char where[48];
    FormatTo(where, sizeof(where), "accessors[%u].sparse", static_cast<unsigned>(index));
    error->Set(StringView(where), StringView(what));
    return Status::Make(ErrorCode::Invalid, "a sparse accessor is not valid");
}

// Reads the accessor into `out` (`count * components` values), through `convert(pointer to a component)`.
template <typename Out, typename Convert>
Status ReadElements(const GltfDocument& doc, u32 index, Vector<Out>* out, GltfError* error, Convert convert)
{
    const GltfAccessor& a = doc.accessors[index];
    u64 total = 0;
    if (a.count > ~u64{0} / a.components)
    {
        return SparseFail(error, index, "has too many elements");
    }
    total = a.count * a.components;
    if (!out->TryResize(static_cast<usize>(total)))
    {
        error->Set(StringView(), StringView("there is no memory for the accessor"));
        return Status::Make(ErrorCode::LimitExceeded, "out of memory");
    }
    u32 columns = 1;
    u32 column_components = 1;
    u32 column_bytes = 0;
    Shape(a, &columns, &column_components, &column_bytes);
    const u32 csize = CompBytes(a.component_type);
    if (!a.has_view)
    {
        for (u64 i = 0; i < total; ++i)
        {
            (*out)[static_cast<usize>(i)] = Out();
        }
    }
    else
    {
        const GltfBufferView& view = doc.views[a.view];
        const byte* base = doc.buffers[view.buffer].Data() + view.offset + a.offset;
        for (u64 i = 0; i < a.count; ++i)
        {
            const byte* element = base + i * a.stride;
            for (u32 col = 0; col < columns; ++col)
            {
                for (u32 r = 0; r < column_components; ++r)
                {
                    const byte* component = element + col * column_bytes + r * csize;
                    (*out)[static_cast<usize>(i * a.components + col * column_components + r)] = convert(component);
                }
            }
        }
    }
    if (a.sparse.present)
    {
        const GltfBufferView& index_view = doc.views[a.sparse.index_view];
        const GltfBufferView& value_view = doc.views[a.sparse.value_view];
        const byte* indices = doc.buffers[index_view.buffer].Data() + index_view.offset + a.sparse.index_offset;
        const byte* values = doc.buffers[value_view.buffer].Data() + value_view.offset + a.sparse.value_offset;
        const u32 index_bytes = CompBytes(a.sparse.index_type);
        u64 previous = 0;
        for (u32 j = 0; j < a.sparse.count; ++j)
        {
            const u64 target = ToUnsigned(indices + static_cast<u64>(j) * index_bytes, a.sparse.index_type);
            if (target >= a.count)
            {
                return SparseFail(error, index, "has an index past the accessor's count");
            }
            if (j != 0 && target <= previous)
            {
                return SparseFail(error, index, "has indices that are not strictly increasing");
            }
            previous = target;
            const byte* element = values + static_cast<u64>(j) * a.element_bytes;
            for (u32 col = 0; col < columns; ++col)
            {
                for (u32 r = 0; r < column_components; ++r)
                {
                    const byte* component = element + col * column_bytes + r * csize;
                    (*out)[static_cast<usize>(target * a.components + col * column_components + r)] = convert(
                        component);
                }
            }
        }
    }
    return Status::Success();
}

}  // namespace

Status ReadAccessorFloats(const GltfDocument& document, u32 index, Vector<f32>* out, GltfError* error)
{
    if (index >= document.accessors.Size())
    {
        error->SetNumber(StringView("accessors"), StringView("has no entry"), index);
        return Status::Make(ErrorCode::Invalid, "no such accessor");
    }
    const GltfAccessor& a = document.accessors[index];
    const u32 type = a.component_type;
    const bool normalized = a.normalized;
    ENGINE_TRY(ReadElements<f32>(document, index, out, error,
                                 [type, normalized](const byte* p) { return ToFloat(p, type, normalized); }));
    for (usize i = 0; i < out->Size(); ++i)
    {
        if (!IsFinite((*out)[i]))
        {
            char where[48];
            FormatTo(where, sizeof(where), "accessors[%u]", static_cast<unsigned>(index));
            error->SetNumber(StringView(where), StringView("has a value that is not finite at component"), i);
            return Status::Make(ErrorCode::Invalid, "an accessor holds a value that is not finite");
        }
    }
    return Status::Success();
}

Status ReadAccessorUnsigned(const GltfDocument& document, u32 index, Vector<u32>* out, GltfError* error)
{
    if (index >= document.accessors.Size())
    {
        error->SetNumber(StringView("accessors"), StringView("has no entry"), index);
        return Status::Make(ErrorCode::Invalid, "no such accessor");
    }
    const GltfAccessor& a = document.accessors[index];
    if (a.normalized || (a.component_type != 5121 && a.component_type != 5123 && a.component_type != 5125))
    {
        char where[48];
        FormatTo(where, sizeof(where), "accessors[%u]", static_cast<unsigned>(index));
        error->Set(
            StringView(where),
            StringView(
                "is not an unsigned integer accessor (UNSIGNED_BYTE, UNSIGNED_SHORT or UNSIGNED_INT, not normalized)"));
        return Status::Make(ErrorCode::Invalid, "not an unsigned accessor");
    }
    const u32 type = a.component_type;
    return ReadElements<u32>(document, index, out, error, [type](const byte* p) { return ToUnsigned(p, type); });
}

}  // namespace vse::asset
