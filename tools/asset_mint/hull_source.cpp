#include "hull_source.h"

#include "source_text.h"
#include "cstr/format.h"

namespace vse::asset
{
namespace
{

void SetError(HullSourceError* error, u32 line, const char* message)
{
    error->line = line;
    FormatTo(error->message.Data(), error->message.Size(), "%s", message);
}

Status Fail(HullSourceError* error, u32 line, const char* message)
{
    SetError(error, line, message);
    return Status::Make(ErrorCode::Invalid, "the hull source is invalid");
}

}  // namespace

Status ParseHullSource(StringView text, HullSource* out, HullSourceError* error)
{
    *out = HullSource();
    *error = HullSourceError();
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
            if (out->points.Size() >= kMaxHullInputPoints)
            {
                SetError(error, line_number, "more than 4096 points");
                return Status::Make(ErrorCode::LimitExceeded, "a hull source holds at most 4096 points");
            }
            Array<f32, 3> point = {};
            for (u32 k = 0; k < 3; ++k)
            {
                if (!srctext::ParseFloat(t.tokens[1 + k], &point[k]))
                {
                    return Fail(error, line_number, "a number is malformed or not finite");
                }
            }
            out->points.Push(point);
        }
        else
        {
            return Fail(error, line_number, "unknown record (expected id or v)");
        }
    }
    if (!have_id)
    {
        return Fail(error, 0, "the source has no id line");
    }
    if (out->points.Size() == 0)
    {
        return Fail(error, 0, "the source has no points");
    }
    return Status::Success();
}

Status MintHull(StringView text, const TypeRegistry& types, Vector<byte>* out, HullSourceError* error, u32 target)
{
    HullSource source;
    ENGINE_TRY(ParseHullSource(text, &source, error));
    HullGeometry hull;
    Status built = BuildHullGeometry(Span<const Array<f32, 3>>(source.points.Data(), source.points.Size()), &hull);
    if (built.Ok())
    {
        out->Clear();
        built = BuildHullBlob(types, source.id, hull, out, target);
    }
    if (built.Failed())
    {
        SetError(error, 0, built.Message());
    }
    return built;
}

}  // namespace vse::asset
