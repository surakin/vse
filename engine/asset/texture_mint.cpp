#include "asset/texture_mint.h"

#include "asset/block_compress.h"
#include "asset/cube_map.h"
#include "cstr/mem_ops.h"
#include "lang/utility.h"
#include "lang/status_try.h"

namespace vse::asset
{

namespace
{

// Wraps the chains (one tight mip chain per layer) in a blob.
Status BuildFromChains(const TypeRegistry& types, ResourceId id, const TextureMintSpec& source, u32 width, u32 height,
                       const Vector<Vector<byte>>& chains, u32 stored, Vector<byte>* out, u32 target)
{
    Vector<ByteSpan> spans;
    if (!spans.TryReserve(chains.Size() * stored))
    {
        return Status::Make(ErrorCode::LimitExceeded, "out of memory");
    }
    for (usize layer = 0; layer < chains.Size(); ++layer)
    {
        usize offset = 0;
        u32 w = width;
        u32 h = height;
        for (u32 mip = 0; mip < stored; ++mip)
        {
            const usize bytes = static_cast<usize>(TightLevelBytes(source.format, w, h));
            spans.Push(ByteSpan(chains[layer].Data() + offset, bytes));
            offset += bytes;
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
        }
    }
    TextureBuildInput input;
    input.kind = source.kind;
    input.format = source.format;
    input.width = width;
    input.height = height;
    input.layers = static_cast<u32>(chains.Size());
    input.mip_count = stored;
    input.subresources = Span<const ByteSpan>(spans.Data(), spans.Size());
    input.filter = source.filter;
    input.alpha = source.alpha;
    return BuildTextureBlob(types, id, input, out, target);
}

}  // namespace

Status MintTextureFromPixels(const TypeRegistry& types, ResourceId id, const TextureMintSpec& source, u32 width,
                             u32 height, Span<const ByteSpan> images, Vector<byte>* out, u32 target,
                             jobs::Scheduler* scheduler)
{
    if (source.environment)
    {
        return Status::Make(ErrorCode::Invalid, "an environment texture is minted from half-float pixels");
    }
    if (source.kind == TextureKind::Cube && width != height)
    {
        return Status::Make(ErrorCode::Invalid, "a cube map's faces must be square");
    }
    Vector<Vector<byte>> chains;  // one tight chain per layer
    for (usize layer = 0; layer < images.Size(); ++layer)
    {
        if (images[layer].Size() != static_cast<u64>(width) * height * 4)
        {
            return Status::Make(ErrorCode::Invalid, "the image has a different size from the first one");
        }
        // A compressed texture is filtered as 8-bit RGBA, then each stored level is compressed.
        const bool compressed = IsBcFormat(source.format);
        const bool storage_srgb = source.format == rhi::Format::RGBA8_SRGB || source.format == rhi::Format::BC1_SRGB ||
                                  source.format == rhi::Format::BC7_SRGB;
        const rhi::Format work_format = !compressed
                                            ? source.format
                                            : (storage_srgb ? rhi::Format::RGBA8_SRGB : rhi::Format::RGBA8_UNorm);
        const bool source_srgb = source.srgb;
        Vector<byte> rgba;
        if (!rgba.TryResize(images[layer].Size()))
        {
            return Status::Make(ErrorCode::LimitExceeded, "out of memory");
        }
        MemCopy(rgba.Data(), images[layer].Data(), images[layer].Size());
        if (source.alpha == MipAlpha::Premultiply)
        {
            ENGINE_TRY(PremultiplyAlphaRgba8(source_srgb, MutableByteSpan(rgba.Data(), rgba.Size())));
        }
        Vector<byte> level0;
        ENGINE_TRY(ConvertRgba8(work_format, source_srgb || storage_srgb, width, height,
                                ByteSpan(rgba.Data(), rgba.Size()), &level0));
        MipOptions options;
        options.filter = source.filter;
        options.alpha = source.alpha;
        Vector<byte> chain;
        ENGINE_TRY(
            GenerateMipChain(work_format, width, height, ByteSpan(level0.Data(), level0.Size()), options, &chain));
        if (!chains.TryReserve(images.Size()))
        {
            return Status::Make(ErrorCode::LimitExceeded, "out of memory");
        }
        chains.Push(static_cast<Vector<byte>&&>(chain));
    }
    const u32 full = FullMipCount(width, height);
    if (source.mips > full)
    {
        return Status::Make(ErrorCode::Invalid, "the mip count is above the full chain of the image");
    }
    const u32 stored = source.mips == 0 ? full : source.mips;
    if (IsBcFormat(source.format))
    {
        // Compress the first `stored` RGBA8 levels of every chain.
        const rhi::Format work_format = (source.format == rhi::Format::BC1_SRGB ||
                                         source.format == rhi::Format::BC7_SRGB)
                                            ? rhi::Format::RGBA8_SRGB
                                            : rhi::Format::RGBA8_UNorm;
        BcEncodeOptions encode;
        encode.punchthrough_alpha = source.alpha != MipAlpha::Ignore;
        encode.drop_alpha = source.drop_alpha;
        for (usize layer = 0; layer < chains.Size(); ++layer)
        {
            Vector<byte> packed;
            usize offset = 0;
            u32 w = width;
            u32 h = height;
            for (u32 mip = 0; mip < stored; ++mip)
            {
                const usize bytes = static_cast<usize>(TightLevelBytes(work_format, w, h));
                Vector<byte> level;
                ENGINE_TRY(EncodeImage(BcKindOf(source.format), w, h, ByteSpan(chains[layer].Data() + offset, bytes),
                                       encode, scheduler, &level));
                const usize used = packed.Size();
                if (!packed.TryResize(used + level.Size()))
                {
                    return Status::Make(ErrorCode::LimitExceeded, "out of memory");
                }
                MemCopy(packed.Data() + used, level.Data(), level.Size());
                offset += bytes;
                w = w > 1 ? w / 2 : 1;
                h = h > 1 ? h / 2 : 1;
            }
            chains[layer] = Move(packed);
        }
    }
    return BuildFromChains(types, id, source, width, height, chains, stored, out, target);
}

Status MintTextureFromHalfPixels(const TypeRegistry& types, ResourceId id, const TextureMintSpec& source, u32 width,
                                 u32 height, Span<const ByteSpan> images, Vector<byte>* out, u32 target)
{
    if (source.format != rhi::Format::RGBA16_Float)
    {
        return Status::Make(ErrorCode::Invalid, "half-float pixels mint to the RGBA16_Float format only");
    }
    if (width == 0 || height == 0 || width > kMaxTextureSize || height > kMaxTextureSize)
    {
        return Status::Make(ErrorCode::Invalid, "the texture size is outside 1 to 16384");
    }
    if (source.kind == TextureKind::Cube && width != height)
    {
        return Status::Make(ErrorCode::Invalid, "a cube map's faces must be square");
    }
    if (source.alpha == MipAlpha::Premultiply)
    {
        return Status::Make(ErrorCode::Invalid, "half-float pixels are not premultiplied by the minter");
    }
    const u32 full = FullMipCount(width, height);
    if (source.mips > full)
    {
        return Status::Make(ErrorCode::Invalid, "the mip count is above the full chain of the image");
    }
    const u32 stored = source.mips == 0 ? full : source.mips;
    Vector<Vector<byte>> chains;
    if (!chains.TryReserve(images.Size()))
    {
        return Status::Make(ErrorCode::LimitExceeded, "out of memory");
    }
    for (usize layer = 0; layer < images.Size(); ++layer)
    {
        if (images[layer].Size() != static_cast<u64>(width) * height * 8)
        {
            return Status::Make(ErrorCode::Invalid, "the image has a different size from the first one");
        }
        MipOptions options;
        options.filter = source.filter;
        options.alpha = source.alpha;
        Vector<byte> chain;
        ENGINE_TRY(GenerateMipChain(rhi::Format::RGBA16_Float, width, height, images[layer], options, &chain));
        chains.Push(Move(chain));
    }
    if (source.environment)
    {
        if (source.kind != TextureKind::Cube)
        {
            return Status::Make(ErrorCode::Invalid, "an environment texture must be a cube map");
        }
        ENGINE_TRY(PrefilterEnvironmentChains(width, stored, &chains));
    }
    return BuildFromChains(types, id, source, width, height, chains, stored, out, target);
}

}  // namespace vse::asset
