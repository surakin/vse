#include "instance_source.h"

#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/number.h"
#include "lang/scalar.h"
#include "source_text.h"

namespace vse::asset
{
namespace
{

Status IsFail(InstanceSourceError* error, u32 line, const char* message)
{
    error->line = line;
    FormatTo(error->message.Data(), error->message.Size(), "%s", message);
    return Status::Make(ErrorCode::Invalid, "the instance source is invalid");
}

bool IsParseDouble(StringView token, f64* out)
{
    const ParseDoubleResult r = ParseDoubleExact(token.Data(), token.Data() + token.Length());
    if (!r.ok || r.end != token.Data() + token.Length() || !IsFinite(r.value))
    {
        return false;
    }
    *out = r.value;
    return true;
}

bool IsParseSingle(StringView token, f32* out)
{
    return srctext::ParseFloat(token, out);
}

}  // namespace

Status ParseInstanceSource(StringView text, InstanceSource* out, InstanceSourceError* error)
{
    *out = InstanceSource();
    *error = InstanceSourceError();
    bool have_id = false;
    bool have_cell = false;
    bool have_move = false;
    bool have_instance = false;
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
            return IsFail(error, line_number, "too many fields on the line");
        }
        const StringView kind = t.tokens[0];
        if (kind == StringView("id"))
        {
            if (have_id)
            {
                return IsFail(error, line_number, "the id is given twice");
            }
            if (t.count != 2 || ParseResourceId(t.tokens[1], &out->id).Failed())
            {
                return IsFail(error, line_number, "id needs one 16-digit hex resource id");
            }
            have_id = true;
            continue;
        }
        if (!have_id)
        {
            return IsFail(error, line_number, "the id line must come first (asset_mint adopt writes it)");
        }
        if (kind == StringView("cell_size") || kind == StringView("move_threshold"))
        {
            const bool cell = kind == StringView("cell_size");
            if ((cell ? have_cell : have_move))
            {
                return IsFail(error, line_number, "a record is given twice");
            }
            f64 value = 0.0;
            if (t.count != 2 || !IsParseDouble(t.tokens[1], &value) || (cell ? !(value > 0.0) : value < 0.0))
            {
                return IsFail(error, line_number,
                              cell ? "cell_size needs one number above 0"
                                   : "move_threshold needs one number not below 0");
            }
            if (cell)
            {
                out->desc.key_cell_size = value;
                have_cell = true;
            }
            else
            {
                out->desc.move_threshold = static_cast<f32>(value);
                have_move = true;
            }
            continue;
        }
        if (kind != StringView("instance"))
        {
            return IsFail(error, line_number, "an unknown record");
        }
        if (t.count != 11 && t.count != 16)
        {
            return IsFail(error, line_number, "instance needs 10 numbers, or 15 with a tint and a custom value");
        }
        const bool with_data = t.count == 16;
        if (have_instance && with_data != out->desc.has_data)
        {
            return IsFail(error, line_number, "every instance must have the tint and custom value, or none");
        }
        out->desc.has_data = with_data;
        have_instance = true;
        f64 position[3];
        f32 rotation[4];
        f32 scale[3];
        for (u32 k = 0; k < 3; ++k)
        {
            if (!IsParseDouble(t.tokens[1 + k], &position[k]) || Abs(position[k]) > renderer::kMaxInstanceCoordinate)
            {
                return IsFail(error, line_number, "a position is not a finite number within 1e12 metres");
            }
        }
        for (u32 k = 0; k < 4; ++k)
        {
            if (!IsParseSingle(t.tokens[4 + k], &rotation[k]))
            {
                return IsFail(error, line_number, "a rotation value is not a finite number");
            }
        }
        for (u32 k = 0; k < 3; ++k)
        {
            if (!IsParseSingle(t.tokens[8 + k], &scale[k]) || !(scale[k] > 0.0f))
            {
                return IsFail(error, line_number, "a scale is not a finite number above 0");
            }
        }
        const f32 length_squared = rotation[0] * rotation[0] + rotation[1] * rotation[1] + rotation[2] * rotation[2] +
                                   rotation[3] * rotation[3];
        if (Abs(length_squared - 1.0f) > 2.0e-3f)
        {
            return IsFail(error, line_number, "the rotation is not a unit quaternion");
        }
        renderer::InstanceTransform transform;
        transform.position = Vec3d(position[0], position[1], position[2]);
        transform.rotation = Quaternion(rotation[0], rotation[1], rotation[2], rotation[3]);
        transform.scale = Vec3(scale[0], scale[1], scale[2]);
        if (out->transforms.Size() >= renderer::kMaxInstances)
        {
            return IsFail(error, line_number, "more instances than a set holds");
        }
        out->transforms.Push(transform);
        if (with_data)
        {
            renderer::InstanceExtra extra;
            f32 tint[4];
            for (u32 k = 0; k < 4; ++k)
            {
                if (!IsParseSingle(t.tokens[11 + k], &tint[k]))
                {
                    return IsFail(error, line_number, "a tint value is not a finite number");
                }
            }
            u32 custom = 0;
            if (!srctext::ParseUnsigned(t.tokens[15], &custom))
            {
                return IsFail(error, line_number, "the custom value is not an unsigned number");
            }
            extra.tint = Vec4(tint[0], tint[1], tint[2], tint[3]);
            extra.custom = custom;
            out->data.Push(extra);
        }
    }
    if (!have_id)
    {
        return IsFail(error, 0, "the id line is missing (asset_mint adopt writes it)");
    }
    if (!have_instance)
    {
        return IsFail(error, 0, "there is no instance line");
    }
    return Status::Success();
}

Status MintInstances(StringView text, const TypeRegistry& types, Vector<byte>* out, InstanceSourceError* error,
                     u32 target)
{
    InstanceSource source;
    ENGINE_TRY(ParseInstanceSource(text, &source, error));
    const Status built = BuildInstanceBlob(
        types, source.id, source.desc,
        Span<const renderer::InstanceTransform>(source.transforms.Data(), source.transforms.Size()),
        Span<const renderer::InstanceExtra>(source.data.Data(), source.data.Size()), out, target);
    if (built.Failed())
    {
        return IsFail(error, 0, "the instance set could not be built");
    }
    return Status::Success();
}

}  // namespace vse::asset
