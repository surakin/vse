#include "physics/height_field.h"

#include "algorithm.h"
#include "containers/hash.h"
#include "cstr/mem_ops.h"
#include "lang/scalar.h"
#include "physics/check.h"

namespace vse::physics
{
namespace
{

constexpr f32 kBigT = 1e30f;

bool Finite3(const Vec3& v)
{
    return IsFinite(v.x) && IsFinite(v.y) && IsFinite(v.z);
}

// The sample position of a metre coordinate of the map, clamped to [0, last]: the function of the terrain asset.
f32 GridCoordinate(f32 metres, f32 cell, u32 count)
{
    f32 g = metres / cell;
    if (!(g > 0.0f))
    {
        g = 0.0f;
    }
    const f32 last = static_cast<f32>(count - 1);
    return g < last ? g : last;
}

// Clips the parameter range [*t0, *t1] of a ray to the slab [lo, hi] of one axis.
bool ClipSlab(f32 pos, f32 dir, f32 lo, f32 hi, f32* t0, f32* t1)
{
    if (Abs(dir) < 1e-20f)
    {
        return pos >= lo && pos <= hi;
    }
    f32 a = (lo - pos) / dir;
    f32 b = (hi - pos) / dir;
    if (a > b)
    {
        const f32 swap = a;
        a = b;
        b = swap;
    }
    *t0 = Max(*t0, a);
    *t1 = Min(*t1, b);
    return *t0 <= *t1;
}

// A ray against a triangle from either side.
bool RayTriangleBothSides(const Vec3& o, const Vec3& d, f32 tmax, const Vec3& a, const Vec3& b, const Vec3& c,
                          f32* t_out)
{
    const Vec3 e1 = b - a;
    const Vec3 e2 = c - a;
    const Vec3 p = d.Cross(e2);
    const f32 det = e1.Dot(p);
    if (Abs(det) < 1e-12f)
    {
        return false;
    }
    const f32 inv = 1.0f / det;
    const Vec3 s = o - a;
    const f32 u = s.Dot(p) * inv;
    if (u < 0.0f || u > 1.0f)
    {
        return false;
    }
    const Vec3 q = s.Cross(e1);
    const f32 v = d.Dot(q) * inv;
    if (v < 0.0f || u + v > 1.0f)
    {
        return false;
    }
    const f32 t = e2.Dot(q) * inv;
    if (t < 0.0f || t > tmax)
    {
        return false;
    }
    *t_out = t;
    return true;
}

}  // namespace

HeightFieldConstPtr HeightField::Create(const HeightFieldSpec& spec, Validation* result)
{
    Validation ignored;
    Validation& r = result != nullptr ? *result : ignored;
    r = Validation::Ok;
    if (!spec.bytes || spec.width < kMinHeightFieldSide || spec.width > kMaxHeightFieldSide ||
        spec.height < kMinHeightFieldSide || spec.height > kMaxHeightFieldSide)
    {
        r = Validation::OutOfRange;
        return HeightFieldConstPtr();
    }
    if (!IsFinite(spec.range_min) || !IsFinite(spec.range_max) || !IsFinite(spec.cell_size))
    {
        r = Validation::NotFinite;
        return HeightFieldConstPtr();
    }
    if (!(spec.cell_size > 0.0f) || spec.range_max < spec.range_min)
    {
        r = Validation::NotPositive;
        return HeightFieldConstPtr();
    }
    const f32 half_x = static_cast<f32>(spec.width - 1) * spec.cell_size * 0.5f;
    const f32 half_z = static_cast<f32>(spec.height - 1) * spec.cell_size * 0.5f;
    const usize need = spec.offset + usize{spec.width} * spec.height * 2;
    if (spec.offset > spec.bytes->Bytes().Size() || need > spec.bytes->Bytes().Size() || !IsFinite(half_x) ||
        !IsFinite(half_z) || half_x > kMaxExtent || half_z > kMaxExtent || Abs(spec.range_min) > kMaxExtent ||
        Abs(spec.range_max) > kMaxExtent)
    {
        r = Validation::OutOfRange;
        return HeightFieldConstPtr();
    }
    SharedPtr<HeightField> field = MakeShared<HeightField>();
    field->bytes_ = spec.bytes;
    field->offset_ = spec.offset;
    field->width_ = spec.width;
    field->depth_ = spec.height;
    field->range_min_ = spec.range_min;
    field->range_max_ = spec.range_max;
    field->cell_size_ = spec.cell_size;
    field->half_x_ = half_x;
    field->half_z_ = half_z;
    field->BuildPyramid();
    return field;
}

HeightFieldConstPtr HeightField::CreateFromSamples(Span<const u16> samples, u32 width, u32 height, f32 range_min,
                                                   f32 range_max, f32 cell_size, Validation* result)
{
    if (width < kMinHeightFieldSide || width > kMaxHeightFieldSide || height < kMinHeightFieldSide ||
        height > kMaxHeightFieldSide || samples.Size() != usize{width} * height)
    {
        if (result != nullptr)
        {
            *result = Validation::OutOfRange;
        }
        return HeightFieldConstPtr();
    }
    Vector<byte> bytes;
    bytes.Resize(samples.Size() * 2);
    for (usize i = 0; i < samples.Size(); ++i)
    {
        bytes[i * 2] = static_cast<byte>(samples[i] & 0xFFu);
        bytes[i * 2 + 1] = static_cast<byte>(samples[i] >> 8);
    }
    HeightFieldSpec spec;
    spec.bytes = MakeShared<OwnedBytes>(static_cast<Vector<byte>&&>(bytes));
    spec.offset = 0;
    spec.width = width;
    spec.height = height;
    spec.range_min = range_min;
    spec.range_max = range_max;
    spec.cell_size = cell_size;
    return Create(spec, result);
}

u16 HeightField::SampleAt(u32 x, u32 z) const
{
    const ByteSpan all = bytes_->Bytes();
    const usize at = offset_ + (usize{z} * width_ + x) * 2;
    return static_cast<u16>(static_cast<u32>(all[at]) | (static_cast<u32>(all[at + 1]) << 8));
}

u16 HeightField::Sample(u32 x, u32 z) const
{
    ENGINE_ASSERT(x < width_ && z < depth_, "HeightField::Sample: out of range");
    return SampleAt(x, z);
}

f32 HeightField::SampleToHeight(u16 sample) const
{
    return range_min_ + static_cast<f32>(sample) * ((range_max_ - range_min_) / 65535.0f);
}

void HeightField::BuildPyramid()
{
    blocks_x_ = (width_ - 1 + kHeightFieldBlockCells - 1) / kHeightFieldBlockCells;
    blocks_z_ = (depth_ - 1 + kHeightFieldBlockCells - 1) / kHeightFieldBlockCells;
    block_min_.Resize(usize{blocks_x_} * blocks_z_);
    block_max_.Resize(usize{blocks_x_} * blocks_z_);
    u16 lowest = 65535;
    u16 highest = 0;
    for (u32 bz = 0; bz < blocks_z_; ++bz)
    {
        for (u32 bx = 0; bx < blocks_x_; ++bx)
        {
            const u32 x1 = Min((bx + 1) * kHeightFieldBlockCells, width_ - 1);
            const u32 z1 = Min((bz + 1) * kHeightFieldBlockCells, depth_ - 1);
            u16 lo = 65535;
            u16 hi = 0;
            for (u32 z = bz * kHeightFieldBlockCells; z <= z1; ++z)
            {
                for (u32 x = bx * kHeightFieldBlockCells; x <= x1; ++x)
                {
                    const u16 s = SampleAt(x, z);
                    lo = Min(lo, s);
                    hi = Max(hi, s);
                }
            }
            block_min_[usize{bz} * blocks_x_ + bx] = lo;
            block_max_[usize{bz} * blocks_x_ + bx] = hi;
            lowest = Min(lowest, lo);
            highest = Max(highest, hi);
        }
    }
    const f32 y0 = SampleToHeight(lowest);
    const f32 y1 = SampleToHeight(highest);
    bounds_ = Aabb::FromMinMax(Vec3(-half_x_, y0, -half_z_), Vec3(half_x_, y1, half_z_));
    extent_ = Vec3(Max(half_x_, 1e-3f), Max(Max(Abs(y0), Abs(y1)), 1e-3f), Max(half_z_, 1e-3f));
}

f32 HeightField::HeightAtMap(f32 x, f32 z) const
{
    const f32 gx = GridCoordinate(x, cell_size_, width_);
    const f32 gz = GridCoordinate(z, cell_size_, depth_);
    u32 cx = static_cast<u32>(Floor(gx));
    u32 cz = static_cast<u32>(Floor(gz));
    cx = cx < width_ - 1 ? cx : width_ - 2;
    cz = cz < depth_ - 1 ? cz : depth_ - 2;
    const f32 fx = gx - static_cast<f32>(cx);
    const f32 fz = gz - static_cast<f32>(cz);
    const f32 h00 = SampleToHeight(SampleAt(cx, cz));
    const f32 h10 = SampleToHeight(SampleAt(cx + 1, cz));
    const f32 h01 = SampleToHeight(SampleAt(cx, cz + 1));
    const f32 h11 = SampleToHeight(SampleAt(cx + 1, cz + 1));
    if (fx >= fz)
    {
        return h00 + fx * (h10 - h00) + fz * (h11 - h10);
    }
    return h00 + fz * (h01 - h00) + fx * (h11 - h01);
}

f32 HeightField::HeightAt(f32 x, f32 z) const
{
    return HeightAtMap(x + half_x_, z + half_z_);
}

void HeightField::Triangle(u32 t, Vec3* a, Vec3* b, Vec3* c) const
{
    ENGINE_ASSERT(t < TriangleCount(), "HeightField::Triangle: out of range");
    const u32 cell = t >> 1;
    const u32 cx = cell % (width_ - 1);
    const u32 cz = cell / (width_ - 1);
    auto corner = [&](u32 dx, u32 dz)
    {
        return Vec3(static_cast<f32>(cx + dx) * cell_size_ - half_x_, SampleToHeight(SampleAt(cx + dx, cz + dz)),
                    static_cast<f32>(cz + dz) * cell_size_ - half_z_);
    };
    if ((t & 1u) == 0)
    {
        *a = corner(0, 0);
        *b = corner(1, 1);
        *c = corner(1, 0);
    }
    else
    {
        *a = corner(0, 0);
        *b = corner(0, 1);
        *c = corner(1, 1);
    }
}

Vec3 HeightField::Normal(u32 t) const
{
    Vec3 a;
    Vec3 b;
    Vec3 c;
    Triangle(t, &a, &b, &c);
    const Vec3 n = (b - a).Cross(c - a);
    const f32 length = Sqrt(n.Dot(n));
    return length > 0.0f ? n * (1.0f / length) : Vec3(0.0f, 1.0f, 0.0f);
}

u32 HeightField::Neighbor(u32 t, u32 edge) const
{
    ENGINE_ASSERT(t < TriangleCount() && edge < 3, "HeightField::Neighbor: out of range");
    const u32 across = width_ - 1;
    const u32 down = depth_ - 1;
    const u32 cell = t >> 1;
    const u32 cx = cell % across;
    const u32 cz = cell / across;
    if ((t & 1u) == 0)
    {
        // Corners (0,0) (1,1) (1,0): the diagonal, the maximum-x edge, the minimum-z edge.
        if (edge == 0)
        {
            return t + 1;
        }
        if (edge == 1)
        {
            return cx + 1 < across ? 2 * (cz * across + cx + 1) + 1 : kNoFieldTriangle;
        }
        return cz > 0 ? 2 * ((cz - 1) * across + cx) + 1 : kNoFieldTriangle;
    }
    // Corners (0,0) (0,1) (1,1): the minimum-x edge, the maximum-z edge, the diagonal.
    if (edge == 0)
    {
        return cx > 0 ? 2 * (cz * across + cx - 1) : kNoFieldTriangle;
    }
    if (edge == 1)
    {
        return cz + 1 < down ? 2 * ((cz + 1) * across + cx) : kNoFieldTriangle;
    }
    return t - 1;
}

u32 HeightField::QueryBox(const Aabb& box, Vector<u32>* out, u32 limit) const
{
    const usize first = out->Size();
    if (box.IsEmpty() || !Finite3(box.min) || !Finite3(box.max))
    {
        return 0;
    }
    const f32 mx0 = box.min.x + half_x_;
    const f32 mx1 = box.max.x + half_x_;
    const f32 mz0 = box.min.z + half_z_;
    const f32 mz1 = box.max.z + half_z_;
    if (mx1 < 0.0f || mz1 < 0.0f || mx0 > static_cast<f32>(width_ - 1) * cell_size_ ||
        mz0 > static_cast<f32>(depth_ - 1) * cell_size_ || box.max.y < bounds_.min.y || box.min.y > bounds_.max.y)
    {
        return 0;
    }
    const u32 cx0 = Min(static_cast<u32>(Floor(GridCoordinate(mx0, cell_size_, width_))), width_ - 2);
    const u32 cx1 = Min(static_cast<u32>(Floor(GridCoordinate(mx1, cell_size_, width_))), width_ - 2);
    const u32 cz0 = Min(static_cast<u32>(Floor(GridCoordinate(mz0, cell_size_, depth_))), depth_ - 2);
    const u32 cz1 = Min(static_cast<u32>(Floor(GridCoordinate(mz1, cell_size_, depth_))), depth_ - 2);
    for (u32 cz = cz0; cz <= cz1; ++cz)
    {
        u32 cx = cx0;
        while (cx <= cx1)
        {
            if (out->Size() - first + 2 > limit)
            {
                return static_cast<u32>(out->Size() - first);
            }
            const u32 bx = cx / kHeightFieldBlockCells;
            const u32 bz = cz / kHeightFieldBlockCells;
            if (SampleToHeight(BlockMax(bx, bz)) < box.min.y || SampleToHeight(BlockMin(bx, bz)) > box.max.y)
            {
                cx = (bx + 1) * kHeightFieldBlockCells;  // the whole block is above or below the box
                continue;
            }
            const u16 s00 = SampleAt(cx, cz);
            const u16 s10 = SampleAt(cx + 1, cz);
            const u16 s01 = SampleAt(cx, cz + 1);
            const u16 s11 = SampleAt(cx + 1, cz + 1);
            const f32 lo = SampleToHeight(Min(Min(s00, s10), Min(s01, s11)));
            const f32 hi = SampleToHeight(Max(Max(s00, s10), Max(s01, s11)));
            if (hi >= box.min.y && lo <= box.max.y)
            {
                const u32 cell = cz * (width_ - 1) + cx;
                out->Push(cell * 2);
                out->Push(cell * 2 + 1);
            }
            ++cx;
        }
    }
    return static_cast<u32>(out->Size() - first);
}

bool HeightField::RayCast(const Vec3& origin, const Vec3& direction, f32 max_t, f32* t_out, u32* triangle) const
{
    if (!Finite3(origin) || !Finite3(direction) || !IsFinite(max_t) || max_t < 0.0f ||
        direction.Dot(direction) < 1e-30f)
    {
        return false;
    }
    // Grid units: one cell is 1, the map's first sample is 0.
    const f32 gx = (origin.x + half_x_) / cell_size_;
    const f32 gz = (origin.z + half_z_) / cell_size_;
    const f32 dgx = direction.x / cell_size_;
    const f32 dgz = direction.z / cell_size_;
    f32 t0 = 0.0f;
    f32 t1 = max_t;
    if (!ClipSlab(gx, dgx, 0.0f, static_cast<f32>(width_ - 1), &t0, &t1) ||
        !ClipSlab(gz, dgz, 0.0f, static_cast<f32>(depth_ - 1), &t0, &t1) ||
        !ClipSlab(origin.y, direction.y, bounds_.min.y, bounds_.max.y, &t0, &t1))
    {
        return false;
    }
    const f32 px = gx + dgx * t0;
    const f32 pz = gz + dgz * t0;
    auto start_cell = [](f32 p, f32 d, u32 cells)
    {
        f32 i = Floor(p);
        if (d < 0.0f && p == i)
        {
            i -= 1.0f;
        }
        i = Max(i, 0.0f);
        return Min(static_cast<u32>(i), cells - 1);
    };
    u32 ix = start_cell(px, dgx, width_ - 1);
    u32 iz = start_cell(pz, dgz, depth_ - 1);
    const bool step_x_pos = dgx >= 0.0f;
    const bool step_z_pos = dgz >= 0.0f;
    f32 next_x = kBigT;
    f32 next_z = kBigT;
    f32 cell_t_x = kBigT;
    f32 cell_t_z = kBigT;
    if (dgx != 0.0f)
    {
        next_x = ((step_x_pos ? static_cast<f32>(ix + 1) : static_cast<f32>(ix)) - gx) / dgx;
        cell_t_x = 1.0f / Abs(dgx);
    }
    if (dgz != 0.0f)
    {
        next_z = ((step_z_pos ? static_cast<f32>(iz + 1) : static_cast<f32>(iz)) - gz) / dgz;
        cell_t_z = 1.0f / Abs(dgz);
    }
    constexpr f32 kSlack = 1e-3f;
    auto ray_y = [&](f32 a, f32 b, f32* lo, f32* hi)
    {
        const f32 ya = origin.y + direction.y * a;
        const f32 yb = origin.y + direction.y * b;
        *lo = Min(ya, yb) - kSlack;
        *hi = Max(ya, yb) + kSlack;
    };
    u32 block_x = 0xFFFFFFFFu;
    u32 block_z = 0xFFFFFFFFu;
    bool block_skipped = false;
    f32 ts = t0;
    for (;;)
    {
        const f32 te = Min(Min(next_x, next_z), t1);
        const u32 bx = ix / kHeightFieldBlockCells;
        const u32 bz = iz / kHeightFieldBlockCells;
        if (bx != block_x || bz != block_z)
        {
            // Entering a block: when the ray stays above or below everything in it, the block's cells are passed.
            block_x = bx;
            block_z = bz;
            f32 exit_t = t1;
            if (dgx != 0.0f)
            {
                const f32 edge = static_cast<f32>(step_x_pos ? Min((bx + 1) * kHeightFieldBlockCells, width_ - 1)
                                                             : bx * kHeightFieldBlockCells);
                exit_t = Min(exit_t, (edge - gx) / dgx);
            }
            if (dgz != 0.0f)
            {
                const f32 edge = static_cast<f32>(step_z_pos ? Min((bz + 1) * kHeightFieldBlockCells, depth_ - 1)
                                                             : bz * kHeightFieldBlockCells);
                exit_t = Min(exit_t, (edge - gz) / dgz);
            }
            f32 lo;
            f32 hi;
            ray_y(ts, Max(exit_t, ts), &lo, &hi);
            block_skipped = hi < SampleToHeight(BlockMin(bx, bz)) || lo > SampleToHeight(BlockMax(bx, bz));
        }
        if (!block_skipped)
        {
            const u16 s00 = SampleAt(ix, iz);
            const u16 s10 = SampleAt(ix + 1, iz);
            const u16 s01 = SampleAt(ix, iz + 1);
            const u16 s11 = SampleAt(ix + 1, iz + 1);
            const f32 cell_lo = SampleToHeight(Min(Min(s00, s10), Min(s01, s11)));
            const f32 cell_hi = SampleToHeight(Max(Max(s00, s10), Max(s01, s11)));
            f32 lo;
            f32 hi;
            ray_y(ts, te, &lo, &hi);
            if (hi >= cell_lo && lo <= cell_hi)
            {
                const u32 cell = iz * (width_ - 1) + ix;
                bool hit = false;
                f32 best = 0.0f;
                u32 best_triangle = 0;
                for (u32 half = 0; half < 2; ++half)
                {
                    Vec3 a;
                    Vec3 b;
                    Vec3 c;
                    Triangle(cell * 2 + half, &a, &b, &c);
                    f32 t;
                    if (RayTriangleBothSides(origin, direction, max_t, a, b, c, &t) && (!hit || t < best))
                    {
                        hit = true;
                        best = t;
                        best_triangle = cell * 2 + half;
                    }
                }
                if (hit)
                {
                    *t_out = best;
                    if (triangle != nullptr)
                    {
                        *triangle = best_triangle;
                    }
                    return true;
                }
            }
        }
        if (te >= t1)
        {
            return false;
        }
        ts = te;
        if (next_x < next_z)
        {
            if (step_x_pos ? ix + 1 >= width_ - 1 : ix == 0)
            {
                return false;
            }
            ix = step_x_pos ? ix + 1 : ix - 1;
            next_x += cell_t_x;
        }
        else
        {
            if (step_z_pos ? iz + 1 >= depth_ - 1 : iz == 0)
            {
                return false;
            }
            iz = step_z_pos ? iz + 1 : iz - 1;
            next_z += cell_t_z;
        }
    }
}

u64 HeightField::ContentHash() const
{
    auto bits = [](f32 v)
    {
        u32 b = 0;
        MemCopy(&b, &v, sizeof(b));
        return static_cast<u64>(b);
    };
    Fnv1a64Builder h;
    h.AddWord(width_);
    h.AddWord(depth_);
    h.AddWord(bits(range_min_));
    h.AddWord(bits(range_max_));
    h.AddWord(bits(cell_size_));
    const ByteSpan all = bytes_->Bytes();
    h.AddBytes(ByteSpan(all.Data() + offset_, usize{width_} * depth_ * 2));
    return h.Value();
}

u32 HeightFieldLibrary::Add(const HeightFieldConstPtr& field)
{
    if (!field)
    {
        return 0;
    }
    fields_.Push(field);
    return static_cast<u32>(fields_.Size());
}

const HeightField* HeightFieldLibrary::Find(u32 id) const
{
    return (id >= 1 && id <= fields_.Size()) ? fields_[id - 1].Get() : nullptr;
}

HeightFieldConstPtr HeightFieldLibrary::Share(u32 id) const
{
    return (id >= 1 && id <= fields_.Size()) ? fields_[id - 1] : HeightFieldConstPtr();
}

}  // namespace vse::physics
