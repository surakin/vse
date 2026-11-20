#include "asset/texture.h"

#include "cstr/mem_ops.h"
#include "log/assert.h"

namespace vse::asset
{
namespace
{

constexpr usize kTextureHeaderBytes = 64;
constexpr usize kTextureEntryBytes = 16;
constexpr u32 kMaxAlignment = 4096;

void TexStore(byte* p, u64 value, u32 width)
{
    for (u32 i = 0; i < width; ++i)
    {
        p[i] = static_cast<byte>(value >> (8 * i));
    }
}

u64 TexLoad(const byte* p, u32 width)
{
    u64 v = 0;
    for (u32 i = 0; i < width; ++i)
    {
        v |= static_cast<u64>(p[i]) << (8 * i);
    }
    return v;
}

Status TexInvalid(const char* message)
{
    return Status::Make(ErrorCode::Invalid, message);
}

Status TexCorrupt(const char* message)
{
    return Status::Make(ErrorCode::Corrupt, message);
}

bool TexPowerOfTwo(u64 v)
{
    return v != 0 && (v & (v - 1)) == 0;
}

u64 TexAlignUp(u64 v, u64 a)
{
    return (v + a - 1) / a * a;
}

u32 TexExtent(u32 base, u32 level)
{
    const u32 v = base >> level;
    return v == 0 ? 1 : v;
}

u32 TexTexelBytes(rhi::Format format)
{
    return static_cast<u32>(TightRowBytes(format, 1));  // a texel, or the 8 or 16 bytes of one block
}

// The row pitch of a level `width` texels across: the row size rounded up to the row alignment and the texel size.
u64 TexRowPitch(rhi::Format format, u32 width, const TextureLayout& layout)
{
    const u64 texel = TexTexelBytes(format);
    const u64 unit = layout.row_alignment > texel ? layout.row_alignment : texel;
    return TexAlignUp(TightRowBytes(format, width), unit);
}

bool TexShapeOk(TextureKind kind, u32 width, u32 height, u32 layers)
{
    switch (kind)
    {
    case TextureKind::Tex2D: return layers == 1;
    case TextureKind::Cube: return layers == kCubeFaces && width == height;
    case TextureKind::Array2D: return layers >= 1 && layers <= kMaxTextureLayers;
    }
    return false;
}

}  // namespace

Status TextureLayoutForTarget(u32 target, TextureLayout* layout)
{
    switch (target)
    {
    case 0: *layout = TextureLayout{4, 16}; return Status::Success();
    case 1: *layout = TextureLayout{256, 512}; return Status::Success();
    case 2: *layout = TextureLayout{16, 16}; return Status::Success();
    default: return TexInvalid("the texture layout of this target tag is not known to this version");
    }
}

Status BuildTextureBlob(const TypeRegistry& types, ResourceId id, const TextureBuildInput& input, Vector<byte>* out,
                        u32 target)
{
    TextureLayout layout;
    ENGINE_TRY(TextureLayoutForTarget(target, &layout));
    if (!IsTextureFormat(input.format) || input.width == 0 || input.height == 0 || input.width > kMaxTextureSize ||
        input.height > kMaxTextureSize)
    {
        return TexInvalid("the texture format is unsupported or its size is outside 1 to 16384");
    }
    if (!TexShapeOk(input.kind, input.width, input.height, input.layers))
    {
        return TexInvalid("the layer count does not fit the kind (one for 2D, six square faces for a cube)");
    }
    if (input.mip_count == 0 || input.mip_count > FullMipCount(input.width, input.height))
    {
        return TexInvalid("the mip count is 0 or above the full chain");
    }
    const u64 count = static_cast<u64>(input.layers) * input.mip_count;
    if (input.subresources.Size() != count)
    {
        return TexInvalid("the number of subresources is not layers times mips");
    }
    Vector<byte> table;
    if (!table.TryResize(static_cast<usize>(count) * kTextureEntryBytes))
    {
        return Status::Make(ErrorCode::LimitExceeded, "out of memory");
    }
    // Place the subresources.
    u64 total = 0;
    for (u32 layer = 0; layer < input.layers; ++layer)
    {
        for (u32 mip = 0; mip < input.mip_count; ++mip)
        {
            const u32 w = TexExtent(input.width, mip);
            const u32 h = TexExtent(input.height, mip);
            if (input.subresources[layer * input.mip_count + mip].Size() != TightLevelBytes(input.format, w, h))
            {
                return TexInvalid("a subresource does not have the size of its level");
            }
            const u64 pitch = TexRowPitch(input.format, w, layout);
            const u64 size = pitch * LevelRows(input.format, h);
            total = TexAlignUp(total, layout.subresource_alignment);
            if (total + size > (u64{1} << 40) || size > 0xffffffffull)
            {
                return Status::Make(ErrorCode::TooLarge, "the texture is larger than a blob may be");
            }
            byte* e = table.Data() + (static_cast<usize>(layer) * input.mip_count + mip) * kTextureEntryBytes;
            TexStore(e, total, 8);
            TexStore(e + 8, size, 4);
            TexStore(e + 12, pitch, 4);
            total += size;
        }
    }
    Vector<byte> data;
    if (!data.TryResize(static_cast<usize>(total)))
    {
        return Status::Make(ErrorCode::LimitExceeded, "out of memory");
    }
    for (u32 layer = 0; layer < input.layers; ++layer)
    {
        for (u32 mip = 0; mip < input.mip_count; ++mip)
        {
            const u32 w = TexExtent(input.width, mip);
            const u32 h = TexExtent(input.height, mip);
            const byte* e = table.Data() + (static_cast<usize>(layer) * input.mip_count + mip) * kTextureEntryBytes;
            const u64 offset = TexLoad(e, 8);
            const u64 pitch = TexLoad(e + 12, 4);
            const ByteSpan src = input.subresources[layer * input.mip_count + mip];
            const usize row_bytes = static_cast<usize>(TightRowBytes(input.format, w));
            for (u32 y = 0; y < LevelRows(input.format, h); ++y)
            {
                MemCopy(data.Data() + offset + y * pitch, src.Data() + static_cast<usize>(y) * row_bytes, row_bytes);
            }
        }
    }
    Vector<byte> header;
    header.Resize(kTextureHeaderBytes);
    MemSet(header.Data(), 0, kTextureHeaderBytes);
    byte* h = header.Data();
    TexStore(h, static_cast<u32>(input.kind), 4);
    TexStore(h + 4, static_cast<u32>(input.format), 4);
    TexStore(h + 8, input.width, 4);
    TexStore(h + 12, input.height, 4);
    TexStore(h + 16, input.layers, 4);
    TexStore(h + 20, input.mip_count, 4);
    TexStore(h + 24, static_cast<u32>(input.filter), 4);
    TexStore(h + 28, static_cast<u32>(input.alpha), 4);
    TexStore(h + 32, layout.row_alignment, 4);
    TexStore(h + 36, layout.subresource_alignment, 4);

    BlobWriter writer(types, types::kTexture, id, target);
    ENGINE_TRY(writer.AddSection(kTextureSectionHeader, ByteSpan(header.Data(), header.Size())));
    ENGINE_TRY(writer.AddSection(kTextureSectionLevels, ByteSpan(table.Data(), table.Size())));
    ENGINE_TRY(writer.AddSection(kTextureSectionData, ByteSpan(data.Data(), data.Size())));
    return writer.Finish(out);
}

Status TextureView::Open(ByteSpan data, const TypeRegistry& types)
{
    *this = TextureView();
    BlobReader blob;
    ENGINE_TRY(blob.Open(data, types));
    if (blob.Type() != types::kTexture)
    {
        return Status::Make(ErrorCode::TypeMismatch, "the blob is not a texture");
    }
    ByteSpan header;
    if (!blob.FindSection(kTextureSectionHeader, &header) || header.Size() != kTextureHeaderBytes)
    {
        return TexCorrupt("the texture header section is missing or has the wrong size");
    }
    const byte* h = header.Data();
    const u64 kind = TexLoad(h, 4);
    const u64 format = TexLoad(h + 4, 4);
    const u64 width = TexLoad(h + 8, 4);
    const u64 height = TexLoad(h + 12, 4);
    const u64 layers = TexLoad(h + 16, 4);
    const u64 mips = TexLoad(h + 20, 4);
    const u64 filter = TexLoad(h + 24, 4);
    const u64 alpha = TexLoad(h + 28, 4);
    const u64 row_alignment = TexLoad(h + 32, 4);
    const u64 sub_alignment = TexLoad(h + 36, 4);
    if (kind > 2 || format >= static_cast<u64>(rhi::Format::Count) ||
        !IsTextureFormat(static_cast<rhi::Format>(format)))
    {
        return TexCorrupt("the texture kind or format is not one this version knows");
    }
    if (width == 0 || height == 0 || width > kMaxTextureSize || height > kMaxTextureSize)
    {
        return TexCorrupt("the texture size is outside 1 to 16384");
    }
    if (!TexShapeOk(static_cast<TextureKind>(kind), static_cast<u32>(width), static_cast<u32>(height),
                    static_cast<u32>(layers)))
    {
        return TexCorrupt("the layer count does not fit the texture kind");
    }
    if (mips == 0 || mips > FullMipCount(static_cast<u32>(width), static_cast<u32>(height)))
    {
        return TexCorrupt("the mip count is 0 or above the full chain");
    }
    if (filter > 1 || alpha > 2 || !TexPowerOfTwo(row_alignment) || !TexPowerOfTwo(sub_alignment) ||
        row_alignment > kMaxAlignment || sub_alignment > kMaxAlignment)
    {
        return TexCorrupt("a texture filter, alpha mode or alignment is not valid");
    }
    const rhi::Format fmt = static_cast<rhi::Format>(format);
    const TextureLayout layout{static_cast<u32>(row_alignment), static_cast<u32>(sub_alignment)};
    ByteSpan table;
    ByteSpan texels;
    const u64 count = layers * mips;
    if (!blob.FindSection(kTextureSectionLevels, &table) || table.Size() != count * kTextureEntryBytes ||
        !blob.FindSection(kTextureSectionData, &texels))
    {
        return TexCorrupt("the texture level table or data section is missing or has the wrong size");
    }
    // Every subresource is where the header says it must be: aligned, the pitch and size implied, in order.
    u64 floor_offset = 0;
    for (u64 i = 0; i < count; ++i)
    {
        const u32 mip = static_cast<u32>(i % mips);
        const byte* e = table.Data() + i * kTextureEntryBytes;
        const u64 offset = TexLoad(e, 8);
        const u64 size = TexLoad(e + 8, 4);
        const u64 pitch = TexLoad(e + 12, 4);
        const u32 w = TexExtent(static_cast<u32>(width), mip);
        const u32 hh = TexExtent(static_cast<u32>(height), mip);
        if (pitch != TexRowPitch(fmt, w, layout) || size != pitch * LevelRows(fmt, hh) || offset % sub_alignment != 0 ||
            offset < floor_offset || offset > texels.Size() || size > texels.Size() - offset)
        {
            return TexCorrupt("a texture subresource has the wrong pitch, size or place");
        }
        floor_offset = offset + size;
    }
    id_ = blob.Id();
    kind_ = static_cast<TextureKind>(kind);
    format_ = fmt;
    width_ = static_cast<u32>(width);
    height_ = static_cast<u32>(height);
    layers_ = static_cast<u32>(layers);
    mip_count_ = static_cast<u32>(mips);
    filter_ = static_cast<MipFilter>(filter);
    alpha_ = static_cast<MipAlpha>(alpha);
    layout_ = layout;
    table_ = table;
    data_ = texels;
    return Status::Success();
}

TextureLevel TextureView::Level(u32 layer, u32 mip) const
{
    ENGINE_ASSERT(layer < layers_ && mip < mip_count_, "TextureView::Level: layer or mip out of range");
    const byte* e = table_.Data() + (static_cast<usize>(layer) * mip_count_ + mip) * kTextureEntryBytes;
    TextureLevel level;
    const u64 offset = TexLoad(e, 8);
    const u64 size = TexLoad(e + 8, 4);
    level.bytes = ByteSpan(data_.Data() + offset, static_cast<usize>(size));
    level.row_pitch = static_cast<u32>(TexLoad(e + 12, 4));
    level.width = TexExtent(width_, mip);
    level.height = TexExtent(height_, mip);
    return level;
}

rhi::TextureDesc TextureView::Desc(rhi::TextureUsage usage) const
{
    rhi::TextureDesc desc;
    desc.dimension = rhi::TextureDimension::Tex2D;
    desc.width = width_;
    desc.height = height_;
    desc.mip_levels = mip_count_;
    desc.array_layers = layers_;
    desc.format = format_;
    desc.usage = usage;
    return desc;
}

Status RetargetTextureBlob(const TypeRegistry& types, ByteSpan blob, u32 target, Vector<byte>* out)
{
    TextureView view;
    ENGINE_TRY(view.Open(blob, types));
    const usize count = static_cast<usize>(view.Layers()) * view.MipCount();
    Vector<byte> tight;
    Vector<usize> starts;
    usize used = 0;
    if (!starts.TryReserve(count))
    {
        return Status::Make(ErrorCode::LimitExceeded, "out of memory");
    }
    for (u32 layer = 0; layer < view.Layers(); ++layer)
    {
        for (u32 mip = 0; mip < view.MipCount(); ++mip)
        {
            const TextureLevel level = view.Level(layer, mip);
            const usize row_bytes = static_cast<usize>(TightRowBytes(view.Format(), level.width));
            const usize rows = LevelRows(view.Format(), level.height);
            const usize bytes = row_bytes * rows;
            if (!tight.TryResize(used + bytes))
            {
                return Status::Make(ErrorCode::LimitExceeded, "out of memory");
            }
            for (usize y = 0; y < rows; ++y)
            {
                MemCopy(tight.Data() + used + static_cast<usize>(y) * row_bytes,
                        level.bytes.Data() + static_cast<usize>(y) * level.row_pitch, row_bytes);
            }
            starts.Push(used);
            used += bytes;
        }
    }
    Vector<ByteSpan> spans;
    if (!spans.TryReserve(count))
    {
        return Status::Make(ErrorCode::LimitExceeded, "out of memory");
    }
    for (usize i = 0; i < count; ++i)
    {
        const usize end = i + 1 < count ? starts[i + 1] : used;
        spans.Push(ByteSpan(tight.Data() + starts[i], end - starts[i]));
    }
    TextureBuildInput input;
    input.kind = view.Kind();
    input.format = view.Format();
    input.width = view.Width();
    input.height = view.Height();
    input.layers = view.Layers();
    input.mip_count = view.MipCount();
    input.subresources = Span<const ByteSpan>(spans.Data(), spans.Size());
    input.filter = view.Filter();
    input.alpha = view.Alpha();
    return BuildTextureBlob(types, view.Id(), input, out, target);
}

}  // namespace vse::asset
