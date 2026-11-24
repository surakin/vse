#include "asset/height_map.h"

#include "cstr/mem_ops.h"
#include "lang/scalar.h"
#include "log/assert.h"

namespace vse::asset
{

namespace
{

void HmStore(byte* p, u32 value)
{
    p[0] = static_cast<byte>(value & 0xFF);
    p[1] = static_cast<byte>((value >> 8) & 0xFF);
    p[2] = static_cast<byte>((value >> 16) & 0xFF);
    p[3] = static_cast<byte>((value >> 24) & 0xFF);
}

u32 HmLoad(const byte* p)
{
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) |
           (static_cast<u32>(p[3]) << 24);
}

u32 HmFloatBits(f32 value)
{
    u32 bits = 0;
    MemCopy(&bits, &value, sizeof(bits));
    return bits;
}

f32 HmBitsFloat(u32 bits)
{
    f32 value = 0.0f;
    MemCopy(&value, &bits, sizeof(value));
    return value;
}

u16 HmLoadU16(const byte* p)
{
    return static_cast<u16>(static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8));
}

void HmStoreU16(byte* p, u32 value)
{
    p[0] = static_cast<byte>(value & 0xFF);
    p[1] = static_cast<byte>((value >> 8) & 0xFF);
}

u32 HmHalf(u32 side)
{
    return side > 1 ? side >> 1 : 1;
}

u32 HmBlocksOf(u32 samples)
{
    return (samples - 1 + kHeightMapBlockCells - 1) / kHeightMapBlockCells;
}

const char* HmCheckHeader(const HeightMapHeader& h)
{
    if (h.width < kMinHeightMapSide || h.width > kMaxHeightMapSide || h.height < kMinHeightMapSide ||
        h.height > kMaxHeightMapSide)
    {
        return "a side of the height map is outside 2 to 8192 samples";
    }
    if (!IsFinite(h.range_min) || !IsFinite(h.range_max) || h.range_min > h.range_max)
    {
        return "the height range is not finite or its minimum is above its maximum";
    }
    // A bound keeps `range_max - range_min` and a tile's extent far from overflowing a float.
    if (h.range_min < -1.0e7f || h.range_max > 1.0e7f)
    {
        return "the height range is beyond 10,000 kilometres";
    }
    if (!IsFinite(h.cell_size) || !(h.cell_size >= 1.0e-3f) || h.cell_size > 1.0e5f)
    {
        return "the cell size is not within 0.001 to 100,000 metres";
    }
    return nullptr;
}

// The bytes of all levels and of the pyramid.
u64 HmHeightsBytes(u32 width, u32 height)
{
    u64 total = 0;
    for (u32 w = width, h = height;; w = HmHalf(w), h = HmHalf(h))
    {
        total += static_cast<u64>(w) * h * 2;
        if (w == 1 && h == 1)
        {
            break;
        }
    }
    return total;
}

u64 HmBlockBytes(u32 width, u32 height)
{
    return static_cast<u64>(HmBlocksOf(width)) * HmBlocksOf(height) * 4;
}

}  // namespace

u32 HeightMapMipCount(u32 width, u32 height)
{
    u32 count = 1;
    for (u32 w = width, h = height; !(w <= 1 && h <= 1); w = HmHalf(w), h = HmHalf(h))
    {
        ++count;
    }
    return count;
}

Status BuildHeightMapBlob(const TypeRegistry& types, ResourceId id, const HeightMapHeader& header, ByteSpan samples,
                          Vector<byte>* out, u32 target)
{
    if (const char* problem = HmCheckHeader(header))
    {
        return Status::Make(ErrorCode::Invalid, problem);
    }
    const u32 w = header.width;
    const u32 h = header.height;
    if (static_cast<u64>(samples.Size()) != static_cast<u64>(w) * h * 2)
    {
        return Status::Make(ErrorCode::Invalid, "the sample bytes are not width * height 16-bit samples");
    }
    const u32 mips = HeightMapMipCount(w, h);
    Vector<byte> levels;
    levels.Resize(static_cast<usize>(HmHeightsBytes(w, h)));
    MemCopy(levels.Data(), samples.Data(), samples.Size());
    usize prev = 0;
    usize next = samples.Size();
    u32 pw = w;
    u32 ph = h;
    for (u32 level = 1; level < mips; ++level)
    {
        const u32 nw = HmHalf(pw);
        const u32 nh = HmHalf(ph);
        for (u32 z = 0; z < nh; ++z)
        {
            const u32 z0 = 2 * z < ph ? 2 * z : ph - 1;
            const u32 z1 = 2 * z + 1 < ph ? 2 * z + 1 : ph - 1;
            for (u32 x = 0; x < nw; ++x)
            {
                const u32 x0 = 2 * x < pw ? 2 * x : pw - 1;
                const u32 x1 = 2 * x + 1 < pw ? 2 * x + 1 : pw - 1;
                const byte* src = levels.Data() + prev;
                const u32 sum = static_cast<u32>(HmLoadU16(src + (static_cast<usize>(z0) * pw + x0) * 2)) +
                                HmLoadU16(src + (static_cast<usize>(z0) * pw + x1) * 2) +
                                HmLoadU16(src + (static_cast<usize>(z1) * pw + x0) * 2) +
                                HmLoadU16(src + (static_cast<usize>(z1) * pw + x1) * 2);
                HmStoreU16(levels.Data() + next + (static_cast<usize>(z) * nw + x) * 2, (sum + 2) >> 2);
            }
        }
        prev = next;
        next += static_cast<usize>(nw) * nh * 2;
        pw = nw;
        ph = nh;
    }
    const u32 bx_count = HmBlocksOf(w);
    const u32 bz_count = HmBlocksOf(h);
    Vector<byte> blocks;
    blocks.Resize(static_cast<usize>(HmBlockBytes(w, h)));
    for (u32 bz = 0; bz < bz_count; ++bz)
    {
        for (u32 bx = 0; bx < bx_count; ++bx)
        {
            const u32 x0 = bx * kHeightMapBlockCells;
            const u32 z0 = bz * kHeightMapBlockCells;
            const u32 x1 = x0 + kHeightMapBlockCells < w - 1 ? x0 + kHeightMapBlockCells : w - 1;
            const u32 z1 = z0 + kHeightMapBlockCells < h - 1 ? z0 + kHeightMapBlockCells : h - 1;
            u32 lo = 0xFFFF;
            u32 hi = 0;
            for (u32 z = z0; z <= z1; ++z)
            {
                for (u32 x = x0; x <= x1; ++x)
                {
                    const u32 s = HmLoadU16(levels.Data() + (static_cast<usize>(z) * w + x) * 2);
                    lo = s < lo ? s : lo;
                    hi = s > hi ? s : hi;
                }
            }
            byte* at = blocks.Data() + (static_cast<usize>(bz) * bx_count + bx) * 4;
            HmStoreU16(at, lo);
            HmStoreU16(at + 2, hi);
        }
    }
    byte head[kHeightMapHeaderBytes];
    MemSet(head, 0, sizeof(head));
    HmStore(head, w);
    HmStore(head + 4, h);
    HmStore(head + 8, mips);
    HmStore(head + 12, kHeightMapBlockCells);
    HmStore(head + 16, HmFloatBits(header.range_min));
    HmStore(head + 20, HmFloatBits(header.range_max));
    HmStore(head + 24, HmFloatBits(header.cell_size));
    BlobWriter writer(types, types::kHeightMap, id, target);
    ENGINE_TRY(writer.AddSection(kHeightMapSectionHeader, ByteSpan(head, sizeof(head))));
    ENGINE_TRY(writer.AddSection(kHeightMapSectionHeights, ByteSpan(levels.Data(), levels.Size())));
    ENGINE_TRY(writer.AddSection(kHeightMapSectionBlocks, ByteSpan(blocks.Data(), blocks.Size())));
    return writer.Finish(out);
}

Status HeightMapView::Open(const SharedBytesPtr& bytes, const TypeRegistry& types)
{
    *this = HeightMapView();
    if (!bytes)
    {
        return Status::Make(ErrorCode::Invalid, "no bytes were given");
    }
    const ByteSpan data = bytes->Bytes();
    BlobReader blob;
    ENGINE_TRY(blob.Open(data, types));
    if (blob.Type() != types::kHeightMap)
    {
        return Status::Make(ErrorCode::TypeMismatch, "the blob is not a height map");
    }
    ByteSpan head;
    if (!blob.FindSection(kHeightMapSectionHeader, &head) || head.Size() != kHeightMapHeaderBytes)
    {
        return Status::Make(ErrorCode::Corrupt, "the height map header section is missing or has the wrong size");
    }
    HeightMapHeader hd;
    hd.width = HmLoad(head.Data());
    hd.height = HmLoad(head.Data() + 4);
    hd.range_min = HmBitsFloat(HmLoad(head.Data() + 16));
    hd.range_max = HmBitsFloat(HmLoad(head.Data() + 20));
    hd.cell_size = HmBitsFloat(HmLoad(head.Data() + 24));
    if (const char* problem = HmCheckHeader(hd))
    {
        return Status::Make(ErrorCode::Corrupt, problem);
    }
    if (HmLoad(head.Data() + 8) != HeightMapMipCount(hd.width, hd.height) ||
        HmLoad(head.Data() + 12) != kHeightMapBlockCells || HmLoad(head.Data() + 28) != 0)
    {
        return Status::Make(ErrorCode::Corrupt, "a height map header field holds a value this version does not define");
    }
    ByteSpan heights;
    if (!blob.FindSection(kHeightMapSectionHeights, &heights) ||
        static_cast<u64>(heights.Size()) != HmHeightsBytes(hd.width, hd.height))
    {
        return Status::Make(ErrorCode::Corrupt, "the height map sample section is missing or has the wrong size");
    }
    ByteSpan blocks;
    if (!blob.FindSection(kHeightMapSectionBlocks, &blocks) ||
        static_cast<u64>(blocks.Size()) != HmBlockBytes(hd.width, hd.height))
    {
        return Status::Make(ErrorCode::Corrupt, "the height map block section is missing or has the wrong size");
    }
    for (usize i = 0; i < blocks.Size(); i += 4)
    {
        if (HmLoadU16(blocks.Data() + i) > HmLoadU16(blocks.Data() + i + 2))
        {
            return Status::Make(ErrorCode::Corrupt, "a block of the height map has a minimum above its maximum");
        }
    }
    header_ = hd;
    id_ = blob.Id();
    bytes_ = bytes;
    mips_ = HeightMapMipCount(hd.width, hd.height);
    heights_ = static_cast<usize>(heights.Data() - data.Data());
    blocks_ = static_cast<usize>(blocks.Data() - data.Data());
    blocks_x_ = HmBlocksOf(hd.width);
    blocks_z_ = HmBlocksOf(hd.height);
    return Status::Success();
}

u32 HeightMapView::MipWidth(u32 level) const
{
    ENGINE_ASSERT(level < mips_, "HeightMapView::MipWidth: the level is out of range");
    u32 w = header_.width;
    for (u32 i = 0; i < level; ++i)
    {
        w = HmHalf(w);
    }
    return w;
}

u32 HeightMapView::MipHeight(u32 level) const
{
    ENGINE_ASSERT(level < mips_, "HeightMapView::MipHeight: the level is out of range");
    u32 h = header_.height;
    for (u32 i = 0; i < level; ++i)
    {
        h = HmHalf(h);
    }
    return h;
}

usize HeightMapView::MipOffset(u32 level) const
{
    ENGINE_ASSERT(level < mips_, "HeightMapView::MipOffset: the level is out of range");
    usize offset = heights_;
    u32 w = header_.width;
    u32 h = header_.height;
    for (u32 i = 0; i < level; ++i)
    {
        offset += static_cast<usize>(w) * h * 2;
        w = HmHalf(w);
        h = HmHalf(h);
    }
    return offset;
}

u16 HeightMapView::Sample(u32 x, u32 z) const
{
    ENGINE_ASSERT(bytes_ && x < header_.width && z < header_.height, "HeightMapView::Sample: out of range");
    return HmLoadU16(bytes_->Bytes().Data() + heights_ + (static_cast<usize>(z) * header_.width + x) * 2);
}

f32 HeightMapView::SampleToHeight(u16 sample) const
{
    return header_.range_min + static_cast<f32>(sample) * ((header_.range_max - header_.range_min) / 65535.0f);
}

namespace
{

// The sample position of a metre coordinate, clamped to [0, last].
f32 HmGridCoordinate(f32 metres, f32 cell, u32 count)
{
    f32 g = metres / cell;
    if (!(g > 0.0f))
    {
        g = 0.0f;
    }
    const f32 last = static_cast<f32>(count - 1);
    return g < last ? g : last;
}

}  // namespace

f32 HeightMapView::HeightAt(f32 x, f32 z) const
{
    ENGINE_ASSERT(bytes_, "HeightMapView::HeightAt: the view is empty");
    const f32 gx = HmGridCoordinate(x, header_.cell_size, header_.width);
    const f32 gz = HmGridCoordinate(z, header_.cell_size, header_.height);
    u32 cx = static_cast<u32>(Floor(gx));
    u32 cz = static_cast<u32>(Floor(gz));
    cx = cx < header_.width - 1 ? cx : header_.width - 2;
    cz = cz < header_.height - 1 ? cz : header_.height - 2;
    const f32 fx = gx - static_cast<f32>(cx);
    const f32 fz = gz - static_cast<f32>(cz);
    const f32 h00 = SampleToHeight(Sample(cx, cz));
    const f32 h10 = SampleToHeight(Sample(cx + 1, cz));
    const f32 h01 = SampleToHeight(Sample(cx, cz + 1));
    const f32 h11 = SampleToHeight(Sample(cx + 1, cz + 1));
    if (fx >= fz)
    {
        return h00 + fx * (h10 - h00) + fz * (h11 - h10);
    }
    return h00 + fz * (h01 - h00) + fx * (h11 - h01);
}

Vec3 HeightMapView::NormalAt(f32 x, f32 z) const
{
    ENGINE_ASSERT(bytes_, "HeightMapView::NormalAt: the view is empty");
    const f32 gx = HmGridCoordinate(x, header_.cell_size, header_.width);
    const f32 gz = HmGridCoordinate(z, header_.cell_size, header_.height);
    const u32 ix = static_cast<u32>(Floor(gx + 0.5f));
    const u32 iz = static_cast<u32>(Floor(gz + 0.5f));
    const u32 x0 = ix > 0 ? ix - 1 : ix;
    const u32 x1 = ix < header_.width - 1 ? ix + 1 : ix;
    const u32 z0 = iz > 0 ? iz - 1 : iz;
    const u32 z1 = iz < header_.height - 1 ? iz + 1 : iz;
    const f32 dx = (SampleToHeight(Sample(x1, iz)) - SampleToHeight(Sample(x0, iz))) /
                   (static_cast<f32>(x1 - x0) * header_.cell_size);
    const f32 dz = (SampleToHeight(Sample(ix, z1)) - SampleToHeight(Sample(ix, z0))) /
                   (static_cast<f32>(z1 - z0) * header_.cell_size);
    const f32 length = Sqrt(dx * dx + 1.0f + dz * dz);
    return Vec3(-dx / length, 1.0f / length, -dz / length);
}

void HeightMapView::MakeTilesDesc(renderer::TerrainTilesDesc* out) const
{
    ENGINE_ASSERT(bytes_, "HeightMapView::MakeTilesDesc: the view is empty");
    *out = renderer::TerrainTilesDesc();
    out->samples_x = header_.width;
    out->samples_z = header_.height;
    out->cell_size = header_.cell_size;
    out->range_min = header_.range_min;
    out->range_max = header_.range_max;
    out->blocks_x = blocks_x_;
    out->blocks_z = blocks_z_;
    out->block_min.Resize(static_cast<usize>(blocks_x_) * blocks_z_);
    out->block_max.Resize(out->block_min.Size());
    for (u32 bz = 0; bz < blocks_z_; ++bz)
    {
        for (u32 bx = 0; bx < blocks_x_; ++bx)
        {
            out->block_min[static_cast<usize>(bz) * blocks_x_ + bx] = BlockMin(bx, bz);
            out->block_max[static_cast<usize>(bz) * blocks_x_ + bx] = BlockMax(bx, bz);
        }
    }
}

u16 HeightMapView::BlockMin(u32 bx, u32 bz) const
{
    ENGINE_ASSERT(bytes_ && bx < blocks_x_ && bz < blocks_z_, "HeightMapView::BlockMin: out of range");
    return HmLoadU16(bytes_->Bytes().Data() + blocks_ + (static_cast<usize>(bz) * blocks_x_ + bx) * 4);
}

u16 HeightMapView::BlockMax(u32 bx, u32 bz) const
{
    ENGINE_ASSERT(bytes_ && bx < blocks_x_ && bz < blocks_z_, "HeightMapView::BlockMax: out of range");
    return HmLoadU16(bytes_->Bytes().Data() + blocks_ + (static_cast<usize>(bz) * blocks_x_ + bx) * 4 + 2);
}

Status VerifyHeightMap(const HeightMapView& view)
{
    if (view.MipCount() == 0)
    {
        return Status::Make(ErrorCode::Invalid, "the height map view is empty");
    }
    const byte* base = view.Bytes()->Bytes().Data();
    for (u32 level = 1; level < view.MipCount(); ++level)
    {
        const u32 pw = view.MipWidth(level - 1);
        const u32 ph = view.MipHeight(level - 1);
        const u32 nw = view.MipWidth(level);
        const u32 nh = view.MipHeight(level);
        const byte* src = base + view.MipOffset(level - 1);
        const byte* dst = base + view.MipOffset(level);
        for (u32 z = 0; z < nh; ++z)
        {
            const u32 z0 = 2 * z < ph ? 2 * z : ph - 1;
            const u32 z1 = 2 * z + 1 < ph ? 2 * z + 1 : ph - 1;
            for (u32 x = 0; x < nw; ++x)
            {
                const u32 x0 = 2 * x < pw ? 2 * x : pw - 1;
                const u32 x1 = 2 * x + 1 < pw ? 2 * x + 1 : pw - 1;
                const u32 sum = static_cast<u32>(HmLoadU16(src + (static_cast<usize>(z0) * pw + x0) * 2)) +
                                HmLoadU16(src + (static_cast<usize>(z0) * pw + x1) * 2) +
                                HmLoadU16(src + (static_cast<usize>(z1) * pw + x0) * 2) +
                                HmLoadU16(src + (static_cast<usize>(z1) * pw + x1) * 2);
                if (HmLoadU16(dst + (static_cast<usize>(z) * nw + x) * 2) != ((sum + 2) >> 2))
                {
                    return Status::Make(ErrorCode::Corrupt, "a mip level is not the average of the level above it");
                }
            }
        }
    }
    const u32 w = view.MipWidth(0);
    const u32 h = view.MipHeight(0);
    for (u32 bz = 0; bz < view.BlocksZ(); ++bz)
    {
        for (u32 bx = 0; bx < view.BlocksX(); ++bx)
        {
            const u32 x0 = bx * kHeightMapBlockCells;
            const u32 z0 = bz * kHeightMapBlockCells;
            const u32 x1 = x0 + kHeightMapBlockCells < w - 1 ? x0 + kHeightMapBlockCells : w - 1;
            const u32 z1 = z0 + kHeightMapBlockCells < h - 1 ? z0 + kHeightMapBlockCells : h - 1;
            u32 lo = 0xFFFF;
            u32 hi = 0;
            for (u32 z = z0; z <= z1; ++z)
            {
                for (u32 x = x0; x <= x1; ++x)
                {
                    const u32 s = view.Sample(x, z);
                    lo = s < lo ? s : lo;
                    hi = s > hi ? s : hi;
                }
            }
            if (view.BlockMin(bx, bz) != lo || view.BlockMax(bx, bz) != hi)
            {
                return Status::Make(ErrorCode::Corrupt, "a block's minimum or maximum is not that of its samples");
            }
        }
    }
    return Status::Success();
}

}  // namespace vse::asset
