#include "texture_source.h"

#include "asset/block_compress.h"
#include "asset/texture_mint.h"
#include "cstr/cstr.h"
#include "asset/cube_map.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "source_text.h"

namespace vse::asset
{
namespace
{

constexpr usize kRawHeaderBytes = 24;

void TxSetError(TextureSourceError* error, u32 line, const char* message)
{
    error->line = line;
    FormatTo(error->message.Data(), error->message.Size(), "%s", message);
}

Status TxFail(TextureSourceError* error, u32 line, const char* message)
{
    TxSetError(error, line, message);
    return Status::Make(ErrorCode::Invalid, "the texture source is invalid");
}

u64 TxLoad(const byte* p, u32 width)
{
    u64 v = 0;
    for (u32 i = 0; i < width; ++i)
    {
        v |= static_cast<u64>(p[i]) << (8 * i);
    }
    return v;
}

void TxStore(byte* p, u64 value, u32 width)
{
    for (u32 i = 0; i < width; ++i)
    {
        p[i] = static_cast<byte>(value >> (8 * i));
    }
}

}  // namespace

Status ParseTextureSource(StringView text, TextureSource* out, TextureSourceError* error)
{
    *out = TextureSource();
    *error = TextureSourceError();
    bool have_id = false;
    bool have_kind = false;
    bool have_format = false;
    bool have_srgb = false;
    bool have_mips = false;
    bool have_filter = false;
    bool have_alpha = false;
    bool have_input = false;
    bool have_projection = false;
    bool have_environment = false;
    bool have_face_size = false;
    bool alpha_given = false;
    rhi::Format base = rhi::Format::RGBA8_UNorm;
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
            return TxFail(error, line_number, "too many fields on the line");
        }
        const StringView kind = t.tokens[0];
        if (kind == StringView("id"))
        {
            if (have_id)
            {
                return TxFail(error, line_number, "the id is given twice");
            }
            if (t.count != 2 || ParseResourceId(t.tokens[1], &out->id).Failed())
            {
                return TxFail(error, line_number, "id needs one 16-digit hex resource id");
            }
            have_id = true;
            continue;
        }
        if (!have_id)
        {
            return TxFail(error, line_number, "the id line must come first (asset_mint adopt writes it)");
        }
        if (kind == StringView("source"))
        {
            if (t.count != 2 || t.tokens[1].Length() > 255)
            {
                return TxFail(error, line_number, "source needs one file name of at most 255 bytes");
            }
            if (out->sources.Size() >= kMaxTextureLayers)
            {
                return TxFail(error, line_number, "more source files than an array texture may have");
            }
            TextureSourceFile file;
            MemCopy(file.name.Data(), t.tokens[1].Data(), t.tokens[1].Length());
            file.line = line_number;
            out->sources.Push(file);
            continue;
        }
        bool* seen = nullptr;
        if (kind == StringView("kind"))
        {
            seen = &have_kind;
        }
        else if (kind == StringView("format"))
        {
            seen = &have_format;
        }
        else if (kind == StringView("srgb"))
        {
            seen = &have_srgb;
        }
        else if (kind == StringView("mips"))
        {
            seen = &have_mips;
        }
        else if (kind == StringView("filter"))
        {
            seen = &have_filter;
        }
        else if (kind == StringView("alpha"))
        {
            seen = &have_alpha;
        }
        else if (kind == StringView("input"))
        {
            seen = &have_input;
        }
        else if (kind == StringView("projection"))
        {
            seen = &have_projection;
        }
        else if (kind == StringView("face_size"))
        {
            seen = &have_face_size;
        }
        else if (kind == StringView("environment"))
        {
            seen = &have_environment;
        }
        else
        {
            return TxFail(error, line_number, "unknown record");
        }
        if (*seen)
        {
            return TxFail(error, line_number, "the record is given twice");
        }
        *seen = true;
        if (t.count != 2)
        {
            return TxFail(error, line_number, "the record needs exactly one value");
        }
        const StringView v = t.tokens[1];
        if (kind == StringView("kind"))
        {
            if (v == StringView("2d"))
            {
                out->kind = TextureKind::Tex2D;
            }
            else if (v == StringView("cube"))
            {
                out->kind = TextureKind::Cube;
            }
            else if (v == StringView("array"))
            {
                out->kind = TextureKind::Array2D;
            }
            else
            {
                return TxFail(error, line_number, "kind is 2d, cube or array");
            }
        }
        else if (kind == StringView("format"))
        {
            if (v == StringView("r8"))
            {
                base = rhi::Format::R8_UNorm;
            }
            else if (v == StringView("rg8"))
            {
                base = rhi::Format::RG8_UNorm;
            }
            else if (v == StringView("rgba8"))
            {
                base = rhi::Format::RGBA8_UNorm;
            }
            else if (v == StringView("r16f"))
            {
                base = rhi::Format::R16_Float;
            }
            else if (v == StringView("rgba16f"))
            {
                base = rhi::Format::RGBA16_Float;
            }
            else if (v == StringView("bc1"))
            {
                base = rhi::Format::BC1_UNorm;
            }
            else if (v == StringView("bc7") || v == StringView("bc7-alpha"))
            {
                base = rhi::Format::BC7_UNorm;
                out->drop_alpha = v == StringView("bc7");
            }
            else if (v == StringView("bc4"))
            {
                base = rhi::Format::BC4_UNorm;
            }
            else if (v == StringView("bc5"))
            {
                base = rhi::Format::BC5_UNorm;
            }
            else
            {
                return TxFail(error, line_number,
                              "format is r8, rg8, rgba8, r16f, rgba16f, bc1, bc4, bc5, bc7 or bc7-alpha");
            }
        }
        else if (kind == StringView("srgb"))
        {
            if (v == StringView("true"))
            {
                out->srgb = true;
            }
            else if (v == StringView("false"))
            {
                out->srgb = false;
            }
            else
            {
                return TxFail(error, line_number, "srgb is true or false");
            }
        }
        else if (kind == StringView("mips"))
        {
            u32 n = 0;
            if (v == StringView("full"))
            {
                out->mips = 0;
            }
            else if (srctext::ParseUnsigned(v, &n) && n >= 1 && n <= 15)
            {
                out->mips = n;
            }
            else
            {
                return TxFail(error, line_number, "mips is full or a count from 1 to 15");
            }
        }
        else if (kind == StringView("filter"))
        {
            if (v == StringView("box"))
            {
                out->filter = MipFilter::Box;
            }
            else if (v == StringView("kaiser"))
            {
                out->filter = MipFilter::Kaiser;
            }
            else
            {
                return TxFail(error, line_number, "filter is box or kaiser");
            }
        }
        else if (kind == StringView("input"))
        {
            if (v == StringView("rgba8"))
            {
                out->half_input = false;
            }
            else if (v == StringView("rgba16f"))
            {
                out->half_input = true;
            }
            else
            {
                return TxFail(error, line_number, "input is rgba8 or rgba16f");
            }
        }
        else if (kind == StringView("projection"))
        {
            if (v == StringView("faces"))
            {
                out->equirect = false;
            }
            else if (v == StringView("equirect"))
            {
                out->equirect = true;
            }
            else
            {
                return TxFail(error, line_number, "projection is faces or equirect");
            }
        }
        else if (kind == StringView("environment"))
        {
            if (v == StringView("yes"))
            {
                out->environment = true;
            }
            else if (v == StringView("no"))
            {
                out->environment = false;
            }
            else
            {
                return TxFail(error, line_number, "environment is yes or no");
            }
        }
        else if (kind == StringView("face_size"))
        {
            u32 n = 0;
            if (!srctext::ParseUnsigned(v, &n) || n < 1 || n > 4096)
            {
                return TxFail(error, line_number, "face_size is a count from 1 to 4096");
            }
            out->face_size = n;
        }
        else
        {
            if (v == StringView("straight"))
            {
                out->alpha = MipAlpha::Straight;
            }
            else if (v == StringView("premultiply"))
            {
                out->alpha = MipAlpha::Premultiply;
            }
            else if (v == StringView("ignore"))
            {
                out->alpha = MipAlpha::Ignore;
            }
            else
            {
                return TxFail(error, line_number, "alpha is straight, premultiply or ignore");
            }
            alpha_given = true;
        }
    }
    if (!have_id)
    {
        return TxFail(error, 0, "the texture source has no id line (asset_mint adopt writes it)");
    }
    const usize n = out->sources.Size();
    if (out->equirect && (out->kind != TextureKind::Cube || !out->half_input || out->face_size == 0))
    {
        return TxFail(error, 0, "projection equirect needs kind cube, input rgba16f and a face_size");
    }
    if (out->environment && (out->kind != TextureKind::Cube || !out->half_input))
    {
        return TxFail(error, 0, "environment yes needs kind cube and input rgba16f");
    }
    if (!out->equirect && out->face_size != 0)
    {
        return TxFail(error, 0, "face_size is for projection equirect");
    }
    if (out->half_input && base != rhi::Format::RGBA16_Float)
    {
        return TxFail(error, 0, "input rgba16f needs format rgba16f");
    }
    const bool count_ok = out->equirect
                              ? n == 1
                              : (out->kind == TextureKind::Tex2D ? n == 1
                                                                 : (out->kind == TextureKind::Cube ? n == 6 : n >= 1));
    if (!count_ok)
    {
        return TxFail(error, 0,
                      "the number of source records does not fit the kind (1 for 2d, 6 for a cube, 1 for equirect)");
    }
    const bool half = base == rhi::Format::R16_Float || base == rhi::Format::RGBA16_Float;
    if (out->srgb && (base == rhi::Format::R8_UNorm || base == rhi::Format::RG8_UNorm ||
                      base == rhi::Format::BC4_UNorm || base == rhi::Format::BC5_UNorm))
    {
        return TxFail(error, 0, "srgb needs rgba8, bc1 or a half-float format");
    }
    out->format = (base == rhi::Format::RGBA8_UNorm && out->srgb) ? rhi::Format::RGBA8_SRGB : base;
    if (base == rhi::Format::BC1_UNorm && out->srgb)
    {
        out->format = rhi::Format::BC1_SRGB;
    }
    if (base == rhi::Format::BC7_UNorm && out->srgb)
    {
        out->format = rhi::Format::BC7_SRGB;
    }
    if (!alpha_given)
    {
        const bool keeps_alpha = base == rhi::Format::RGBA8_UNorm || base == rhi::Format::BC1_UNorm ||
                                 (base == rhi::Format::BC7_UNorm && !out->drop_alpha);
        out->alpha = keeps_alpha ? MipAlpha::Straight : MipAlpha::Ignore;
    }
    if (half && out->alpha == MipAlpha::Straight)
    {
        return TxFail(error, 0, "straight alpha filtering is for rgba8; use premultiply or ignore for half floats");
    }
    return Status::Success();
}

Status ParseRawImage(ByteSpan bytes, RawImage* out)
{
    *out = RawImage();
    const bool rgba8 = bytes.Size() >= kRawHeaderBytes && bytes[0] == 'R' && bytes[1] == 'A' && bytes[2] == 'W' &&
                       bytes[3] == '1';
    const bool half = bytes.Size() >= kRawHeaderBytes && bytes[0] == 'R' && bytes[1] == 'A' && bytes[2] == 'W' &&
                      bytes[3] == 'H';
    if (!rgba8 && !half)
    {
        return Status::Make(ErrorCode::Corrupt, "the raw image has no RAW1 or RAWH header");
    }
    const u64 w = TxLoad(bytes.Data() + 4, 4);
    const u64 h = TxLoad(bytes.Data() + 8, 4);
    const u64 texel = half ? 8 : 4;
    if (w == 0 || h == 0 || w > kMaxTextureSize || h > kMaxTextureSize ||
        bytes.Size() - kRawHeaderBytes != w * h * texel)
    {
        return Status::Make(ErrorCode::Corrupt, "the raw image size is outside 1 to 16384 or its pixels do not fit it");
    }
    out->width = static_cast<u32>(w);
    out->height = static_cast<u32>(h);
    out->id = TxLoad(bytes.Data() + 12, 8);
    out->half = half;
    out->pixels = ByteSpan(bytes.Data() + kRawHeaderBytes, static_cast<usize>(w * h * texel));
    return Status::Success();
}

namespace
{

Status BuildRawFile(char magic, u32 texel_bytes, u32 width, u32 height, ByteSpan pixels, u64 id, Vector<byte>* out)
{
    if (width == 0 || height == 0 || width > kMaxTextureSize || height > kMaxTextureSize ||
        pixels.Size() != static_cast<u64>(width) * height * texel_bytes)
    {
        return Status::Make(ErrorCode::Invalid, "the raw image size is outside 1 to 16384 or the pixels do not fit it");
    }
    Vector<byte> result;
    if (!result.TryResize(kRawHeaderBytes + pixels.Size()))
    {
        return Status::Make(ErrorCode::LimitExceeded, "out of memory");
    }
    byte* p = result.Data();
    p[0] = 'R';
    p[1] = 'A';
    p[2] = 'W';
    p[3] = static_cast<byte>(magic);
    TxStore(p + 4, width, 4);
    TxStore(p + 8, height, 4);
    TxStore(p + 12, id, 8);
    TxStore(p + 20, 0, 4);
    MemCopy(p + kRawHeaderBytes, pixels.Data(), pixels.Size());
    *out = static_cast<Vector<byte>&&>(result);
    return Status::Success();
}

}  // namespace

Status BuildRawImage(u32 width, u32 height, ByteSpan rgba, u64 id, Vector<byte>* out)
{
    return BuildRawFile('1', 4, width, height, rgba, id, out);
}

Status BuildRawHalfImage(u32 width, u32 height, ByteSpan rgba16f, u64 id, Vector<byte>* out)
{
    return BuildRawFile('H', 8, width, height, rgba16f, id, out);
}

Status MintTexture(StringView text, Span<const ByteSpan> images, const TypeRegistry& types, Vector<byte>* out,
                   TextureSourceError* error, u32 target, jobs::Scheduler* scheduler)
{
    TextureSource source;
    const Status parsed = ParseTextureSource(text, &source, error);
    if (parsed.Failed())
    {
        return parsed;
    }
    if (images.Size() != source.sources.Size())
    {
        TxSetError(error, 0, "the number of image files is not the number of source records");
        return Status::Make(ErrorCode::Invalid, "the texture source is invalid");
    }
    u32 width = 0;
    u32 height = 0;
    for (usize layer = 0; layer < images.Size(); ++layer)
    {
        const u32 line = source.sources[layer].line;
        RawImage raw;
        if (ParseRawImage(images[layer], &raw).Failed())
        {
            return TxFail(error, line, "the raw image file is malformed");
        }
        if (raw.half != source.half_input)
        {
            return TxFail(error, line,
                          source.half_input ? "the raw image is RAW1, but the source says input rgba16f"
                                            : "the raw image is RAWH, but the source says input rgba8");
        }
        if (layer == 0)
        {
            width = raw.width;
            height = raw.height;
            if (source.kind == TextureKind::Cube && !source.equirect && width != height)
            {
                return TxFail(error, line, "a cube map's faces must be square");
            }
        }
        else if (raw.width != width || raw.height != height)
        {
            return TxFail(error, line, "the image has a different size from the first one");
        }
    }
    Vector<ByteSpan> layers;
    for (usize layer = 0; layer < images.Size(); ++layer)
    {
        RawImage raw;
        if (ParseRawImage(images[layer], &raw).Failed())
        {
            return TxFail(error, 0, "the raw image file is malformed");
        }
        layers.Push(raw.pixels);
    }
    TextureMintSpec spec;
    spec.kind = source.kind;
    spec.format = source.format;
    spec.drop_alpha = source.drop_alpha;
    spec.srgb = source.srgb;
    spec.mips = source.mips;
    spec.filter = source.filter;
    spec.alpha = source.alpha;
    spec.environment = source.environment;
    Status built = Status::Success();
    if (source.equirect)
    {
        Vector<Vector<byte>> faces;
        built = EquirectangularToCubeFaces(layers[0], width, height, source.face_size, &faces);
        if (built.Ok())
        {
            Vector<ByteSpan> face_spans;
            for (usize face = 0; face < faces.Size(); ++face)
            {
                face_spans.Push(ByteSpan(faces[face].Data(), faces[face].Size()));
            }
            built = MintTextureFromHalfPixels(types, source.id, spec, source.face_size, source.face_size,
                                              Span<const ByteSpan>(face_spans.Data(), face_spans.Size()), out, target);
        }
    }
    else if (source.half_input)
    {
        built = MintTextureFromHalfPixels(types, source.id, spec, width, height,
                                          Span<const ByteSpan>(layers.Data(), layers.Size()), out, target);
    }
    else
    {
        built = MintTextureFromPixels(types, source.id, spec, width, height,
                                      Span<const ByteSpan>(layers.Data(), layers.Size()), out, target, scheduler);
    }
    if (built.Failed())
    {
        TxSetError(error, 0, built.Message());
    }
    return built;
}

}  // namespace vse::asset
