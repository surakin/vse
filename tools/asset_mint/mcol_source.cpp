#include "mcol_source.h"

#include "source_text.h"
#include "cstr/format.h"

namespace engine_placeholder::asset
{
namespace
{

void SetError(McolSourceError* error, u32 line, const char* message)
{
    error->line = line;
    FormatTo(error->message.Data(), error->message.Size(), "%s", message);
}

Status Fail(McolSourceError* error, u32 line, const char* message)
{
    SetError(error, line, message);
    return Status::Make(ErrorCode::Invalid, "the mesh collider source is invalid");
}

}  // namespace

Status ParseMcolSource(StringView text, McolSource* out, McolSourceError* error)
{
    *out = McolSource();
    *error = McolSourceError();
    bool have_id = false;
    u32 line_number = 0;
    usize pos = 0;
    while (pos < text.Length())
    {
        usize end = pos;
        while (end < text.Length() && text[end] != '\n')
        {
            ++end;
        }
        ++line_number;
        const srctext::LineTokens t = srctext::Tokenize(text.Substring(pos, end - pos));
        pos = end + 1;
        if (t.count == 0 && !t.overflow)
        {
            continue;
        }
        if (t.overflow)
        {
            return Fail(error, line_number, "too many fields on the line");
        }
        const StringView kind = t.tokens[0];
        if (kind == StringView("id"))
        {
            if (have_id)
            {
                return Fail(error, line_number, "the id is given twice");
            }
            if (t.count != 2 || ParseResourceId(t.tokens[1], &out->id).Failed())
            {
                return Fail(error, line_number, "id needs one 16-digit hex resource id");
            }
            have_id = true;
        }
        else if (kind == StringView("v"))
        {
            if (!have_id)
            {
                return Fail(error, line_number, "the id line must come first");
            }
            if (t.count != 4)
            {
                return Fail(error, line_number, "v needs three numbers");
            }
            if (out->positions.Size() >= kMaxMeshColliderVertices)
            {
                SetError(error, line_number, "too many vertices");
                return Status::Make(ErrorCode::LimitExceeded, "a mesh collider source holds too many vertices");
            }
            Array<f32, 3> p = {};
            for (u32 k = 0; k < 3; ++k)
            {
                if (!srctext::ParseFloat(t.tokens[1 + k], &p[k]))
                {
                    return Fail(error, line_number, "a number is malformed or not finite");
                }
            }
            out->positions.Push(p);
        }
        else if (kind == StringView("t"))
        {
            if (!have_id)
            {
                return Fail(error, line_number, "the id line must come first");
            }
            if (t.count != 4 && t.count != 5)
            {
                return Fail(error, line_number, "t needs three indices and an optional material");
            }
            if (out->materials.Size() >= kMaxMeshColliderTriangles)
            {
                SetError(error, line_number, "too many triangles");
                return Status::Make(ErrorCode::LimitExceeded, "a mesh collider source holds too many triangles");
            }
            u32 corner[3];
            for (u32 k = 0; k < 3; ++k)
            {
                if (!srctext::ParseUnsigned(t.tokens[1 + k], &corner[k]))
                {
                    return Fail(error, line_number, "a triangle index is malformed");
                }
            }
            u32 material = 0;
            if (t.count == 5 && (!srctext::ParseUnsigned(t.tokens[4], &material) || material > 255))
            {
                return Fail(error, line_number, "a material index is malformed or above 255");
            }
            for (u32 k = 0; k < 3; ++k)
            {
                out->indices.Push(corner[k]);
            }
            out->materials.Push(static_cast<u8>(material));
        }
        else
        {
            return Fail(error, line_number, "unknown record (expected id, v or t)");
        }
    }
    if (!have_id)
    {
        return Fail(error, 0, "the source has no id line");
    }
    if (out->positions.Size() == 0 || out->materials.Size() == 0)
    {
        return Fail(error, 0, "the source has no vertices or no triangles");
    }
    return Status::Success();
}

Status MintMeshCollider(StringView text, const TypeRegistry& types, Vector<byte>* out, McolSourceError* error,
                        u32 target)
{
    McolSource source;
    ENGINE_TRY(ParseMcolSource(text, &source, error));
    out->Clear();
    const Status built = BuildMeshColliderBlob(
        types, source.id, Span<const Array<f32, 3>>(source.positions.Data(), source.positions.Size()),
        Span<const u32>(source.indices.Data(), source.indices.Size()),
        Span<const u8>(source.materials.Data(), source.materials.Size()), out, target);
    if (built.Failed())
    {
        SetError(error, 0, built.Message());
    }
    return built;
}

}  // namespace engine_placeholder::asset
