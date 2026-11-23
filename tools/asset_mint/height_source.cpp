#include "height_source.h"

#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "source_text.h"

namespace vse::asset
{
namespace
{

Status HsFail(HeightMapSourceError* error, u32 line, const char* message)
{
    error->line = line;
    FormatTo(error->message.Data(), error->message.Size(), "%s", message);
    return Status::Make(ErrorCode::Invalid, "the height map source is invalid");
}

}  // namespace

Status ParseHeightMapSource(StringView text, HeightMapSource* out, HeightMapSourceError* error)
{
    *out = HeightMapSource();
    *error = HeightMapSourceError();
    bool have_id = false;
    bool have_source = false;
    bool have_width = false;
    bool have_height = false;
    bool have_min = false;
    bool have_max = false;
    bool have_cell = false;
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
            return HsFail(error, line_number, "too many fields on the line");
        }
        const StringView kind = t.tokens[0];
        if (kind == StringView("id"))
        {
            if (have_id)
            {
                return HsFail(error, line_number, "the id is given twice");
            }
            if (t.count != 2 || ParseResourceId(t.tokens[1], &out->id).Failed())
            {
                return HsFail(error, line_number, "id needs one 16-digit hex resource id");
            }
            have_id = true;
            continue;
        }
        if (!have_id)
        {
            return HsFail(error, line_number, "the id line must come first (asset_mint adopt writes it)");
        }
        bool* seen = nullptr;
        if (kind == StringView("source"))
        {
            seen = &have_source;
        }
        else if (kind == StringView("width"))
        {
            seen = &have_width;
        }
        else if (kind == StringView("height"))
        {
            seen = &have_height;
        }
        else if (kind == StringView("range_min"))
        {
            seen = &have_min;
        }
        else if (kind == StringView("range_max"))
        {
            seen = &have_max;
        }
        else if (kind == StringView("cell_size"))
        {
            seen = &have_cell;
        }
        else
        {
            return HsFail(error, line_number, "an unknown record");
        }
        if (*seen)
        {
            return HsFail(error, line_number, "a record is given twice");
        }
        if (t.count != 2)
        {
            return HsFail(error, line_number, "a record needs exactly one value");
        }
        *seen = true;
        if (kind == StringView("source"))
        {
            if (t.tokens[1].Length() > 255)
            {
                return HsFail(error, line_number, "source needs one file name of at most 255 bytes");
            }
            MemCopy(out->source.Data(), t.tokens[1].Data(), t.tokens[1].Length());
            out->source_line = line_number;
        }
        else if (kind == StringView("width") || kind == StringView("height"))
        {
            u32 value = 0;
            if (!srctext::ParseUnsigned(t.tokens[1], &value))
            {
                return HsFail(error, line_number, "a size is not an unsigned number");
            }
            (kind == StringView("width") ? out->header.width : out->header.height) = value;
        }
        else
        {
            f32 value = 0.0f;
            if (!srctext::ParseFloat(t.tokens[1], &value))
            {
                return HsFail(error, line_number, "a number does not parse as a finite number");
            }
            if (kind == StringView("range_min"))
            {
                out->header.range_min = value;
            }
            else if (kind == StringView("range_max"))
            {
                out->header.range_max = value;
            }
            else
            {
                out->header.cell_size = value;
            }
        }
    }
    if (!have_id)
    {
        return HsFail(error, 0, "the id line is missing (asset_mint adopt writes it)");
    }
    if (!(have_source && have_width && have_height && have_min && have_max && have_cell))
    {
        return HsFail(error, 0, "a record is missing: source, width, height, range_min, range_max, cell_size");
    }
    const HeightMapHeader& h = out->header;
    if (h.width < kMinHeightMapSide || h.width > kMaxHeightMapSide || h.height < kMinHeightMapSide ||
        h.height > kMaxHeightMapSide)
    {
        return HsFail(error, 0, "a side is outside 2 to 8192 samples");
    }
    if (h.range_min > h.range_max)
    {
        return HsFail(error, 0, "range_min is above range_max");
    }
    if (!(h.cell_size > 0.0f))
    {
        return HsFail(error, 0, "cell_size is not above 0");
    }
    return Status::Success();
}

Status MintHeightMap(StringView text, ByteSpan samples, const TypeRegistry& types, Vector<byte>* out,
                     HeightMapSourceError* error, u32 target)
{
    HeightMapSource source;
    ENGINE_TRY(ParseHeightMapSource(text, &source, error));
    if (static_cast<u64>(samples.Size()) != static_cast<u64>(source.header.width) * source.header.height * 2)
    {
        return HsFail(error, source.source_line, "the sample file is not width * height 16-bit samples");
    }
    const Status built = BuildHeightMapBlob(types, source.id, source.header, samples, out, target);
    if (built.Failed())
    {
        return HsFail(error, 0, "the height map could not be built");
    }
    return Status::Success();
}

}  // namespace vse::asset
