#include "text/shelf_packer.h"

namespace engine_placeholder::text
{

ShelfPacker::ShelfPacker(u32 width, u32 height, u32 padding)
    : width_(width), height_(height), padding_(padding), next_y_(padding)
{
}

bool ShelfPacker::Place(u32 w, u32 h, Rect* out)
{
    if (w == 0 || h == 0)
    {
        return false;
    }
    if (u64{w} + 2 * u64{padding_} > width_ || u64{h} + 2 * u64{padding_} > height_)
    {
        return false;
    }
    // The shelf of the smallest height that fits and still has room to the right.
    usize best = shelves_.Size();
    for (usize i = 0; i < shelves_.Size(); ++i)
    {
        const Shelf& s = shelves_[i];
        if (s.height < h || u64{s.x} + w + padding_ > width_)
        {
            continue;
        }
        if (best == shelves_.Size() || s.height < shelves_[best].height)
        {
            best = i;
        }
    }
    if (best == shelves_.Size())
    {
        // A new shelf of the rounded height class, or of the exact height when only that fits above the bottom edge.
        u32 shelf_height = (h + 3) / 4 * 4;
        if (u64{next_y_} + shelf_height + padding_ > height_)
        {
            shelf_height = h;
        }
        if (u64{next_y_} + shelf_height + padding_ > height_)
        {
            return false;
        }
        Shelf s;
        s.y = next_y_;
        s.height = shelf_height;
        s.x = padding_;
        if (!shelves_.TryReserve(shelves_.Size() + 1))
        {
            return false;
        }
        shelves_.Push(s);
        next_y_ += shelf_height + padding_;
        best = shelves_.Size() - 1;
    }
    Shelf& shelf = shelves_[best];
    out->x = shelf.x;
    out->y = shelf.y;
    out->w = w;
    out->h = h;
    shelf.x += w + padding_;
    used_area_ += u64{w} * h;
    return true;
}

}  // namespace engine_placeholder::text
