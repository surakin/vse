#include "text/glyph_atlas.h"

#include "log/assert.h"
#include "cstr/mem_ops.h"

namespace engine_placeholder::text
{

namespace
{

constexpr u32 kPadding = 1;

u64 Mix(u64 x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

}  // namespace

GlyphAtlas::GlyphAtlas(u32 page_size, u32 max_pages) : page_size_(page_size), max_pages_(max_pages)
{
    ENGINE_ASSERT(page_size >= 16, "GlyphAtlas: pages smaller than 16 pixels cannot hold a glyph");
    pages_.Reserve(max_pages);  // pages never move once added
}

usize GlyphAtlas::HashKey(const Key& key)
{
    return static_cast<usize>(Mix(key.font ^ Mix((u64{key.size} << 32) | key.glyph)));
}

const GlyphEntry* GlyphAtlas::Find(const Key& key) const
{
    if (slots_.Size() == 0)
    {
        return nullptr;
    }
    const usize mask = slots_.Size() - 1;
    for (usize i = HashKey(key) & mask;; i = (i + 1) & mask)
    {
        const Slot& slot = slots_[i];
        if (!slot.used)
        {
            return nullptr;
        }
        if (slot.key.font == key.font && slot.key.size == key.size && slot.key.glyph == key.glyph)
        {
            return &slot.entry;
        }
    }
}

bool GlyphAtlas::EnsureSlot()
{
    if ((slot_count_ + 1) * 10 > slots_.Size() * 7)
    {  // load factor 0.7: grow (or create) the table
        const usize new_size = slots_.Size() == 0 ? 64 : slots_.Size() * 2;
        Vector<Slot> grown;
        if (!grown.TryResize(new_size))
        {
            return false;
        }
        for (usize i = 0; i < slots_.Size(); ++i)
        {
            if (!slots_[i].used)
            {
                continue;
            }
            const usize mask = new_size - 1;
            usize j = HashKey(slots_[i].key) & mask;
            while (grown[j].used)
            {
                j = (j + 1) & mask;
            }
            grown[j] = slots_[i];
        }
        slots_ = static_cast<Vector<Slot>&&>(grown);
    }
    return true;
}

void GlyphAtlas::Insert(const Key& key, const GlyphEntry& entry)
{
    const usize mask = slots_.Size() - 1;
    usize i = HashKey(key) & mask;
    while (slots_[i].used)
    {
        i = (i + 1) & mask;
    }
    slots_[i].key = key;
    slots_[i].entry = entry;
    slots_[i].used = true;
    ++slot_count_;
}

bool GlyphAtlas::AddPage()
{
    if (pages_.Size() >= max_pages_)
    {
        return false;
    }
    Vector<byte> pixels;
    if (!pixels.TryResize(usize{page_size_} * page_size_))
    {
        return false;
    }
    pages_.Push(Page(page_size_, kPadding));
    pages_[pages_.Size() - 1].pixels = static_cast<Vector<byte>&&>(pixels);
    stats_.pages = pages_.Size();
    stats_.bytes += usize{page_size_} * page_size_;
    return true;
}

void GlyphAtlas::MarkDirty(Page& page, const Rect& rect)
{
    if (!page.dirty)
    {
        page.dirty = true;
        page.dirty_rect = rect;
        return;
    }
    const u32 x0 = rect.x < page.dirty_rect.x ? rect.x : page.dirty_rect.x;
    const u32 y0 = rect.y < page.dirty_rect.y ? rect.y : page.dirty_rect.y;
    const u32 x1 = rect.x + rect.w > page.dirty_rect.x + page.dirty_rect.w ? rect.x + rect.w
                                                                           : page.dirty_rect.x + page.dirty_rect.w;
    const u32 y1 = rect.y + rect.h > page.dirty_rect.y + page.dirty_rect.h ? rect.y + rect.h
                                                                           : page.dirty_rect.y + page.dirty_rect.h;
    page.dirty_rect = Rect{x0, y0, x1 - x0, y1 - y0};
}

Status GlyphAtlas::GetOrAdd(Font& font, GlyphId glyph, GlyphEntry* out)
{
    ENGINE_ASSERT(out != nullptr, "GlyphAtlas::GetOrAdd: out is null");
    *out = GlyphEntry{};
    if (!font.IsOpen())
    {
        return Status::Of(Error::InvalidArgument, "the font is not open");
    }
    if (font.PixelSize() > kMaxAtlasPixelSize)
    {
        ++stats_.too_large;
        return Status::Of(Error::TooLarge, "the pixel size is above the largest rasterized size");
    }
    Key key;
    key.font = font.Id();
    key.size = font.PixelSize();
    key.glyph = glyph;
    if (const GlyphEntry* hit = Find(key))
    {
        ++stats_.hits;
        *out = *hit;
        return Status{};
    }
    if (!EnsureSlot())
    {  // make room in the index first, so a glyph is never placed in a page without an entry to find it
        ++stats_.failures;
        return Status::Of(Error::OutOfMemory, "out of memory growing the atlas index");
    }
    GlyphBitmap bitmap;
    const Status rendered = font.Render(glyph, &bitmap);
    if (!rendered.Ok())
    {
        ++stats_.failures;
        return rendered;
    }
    GlyphEntry entry;
    entry.valid = true;
    entry.left = bitmap.left;
    entry.top = bitmap.top;
    entry.advance = bitmap.advance;
    if (bitmap.width != 0)
    {
        Rect rect;
        u32 page = 0;
        bool placed = false;
        if (u64{bitmap.width} + 2 * u64{kPadding} <= page_size_ && u64{bitmap.height} + 2 * u64{kPadding} <= page_size_)
        {
            for (page = 0; page < pages_.Size() && !placed; ++page)
            {
                placed = pages_[page].packer.Place(bitmap.width, bitmap.height, &rect);
            }
            if (placed)
            {
                --page;  // the loop incremented past the page that took it
            }
            if (!placed && AddPage())
            {
                page = static_cast<u32>(pages_.Size() - 1);
                placed = pages_[page].packer.Place(bitmap.width, bitmap.height, &rect);
            }
        }
        if (!placed)
        {
            ++stats_.failures;
            return Status::Of(Error::AtlasFull, "the glyph does not fit in the atlas");
        }
        Page& target = pages_[page];
        for (u32 row = 0; row < bitmap.height; ++row)
        {
            MemCopy(target.pixels.Data() + usize{rect.y + row} * page_size_ + rect.x,
                    bitmap.pixels.Data() + usize{row} * bitmap.width, bitmap.width);
        }
        MarkDirty(target, rect);
        entry.has_pixels = true;
        entry.page = page;
        entry.rect = rect;
        const f32 inv = 1.0f / static_cast<f32>(page_size_);
        entry.u0 = static_cast<f32>(rect.x) * inv;
        entry.v0 = static_cast<f32>(rect.y) * inv;
        entry.u1 = static_cast<f32>(rect.x + rect.w) * inv;
        entry.v1 = static_cast<f32>(rect.y + rect.h) * inv;
    }
    Insert(key, entry);
    ++stats_.misses;
    ++stats_.glyphs;
    *out = entry;
    return Status{};
}

ByteSpan GlyphAtlas::PagePixels(u32 page) const
{
    ENGINE_ASSERT(page < pages_.Size(), "GlyphAtlas::PagePixels: page out of range");
    return ByteSpan(pages_[page].pixels.Data(), pages_[page].pixels.Size());
}

bool GlyphAtlas::TakeDirty(u32 page, Rect* rect)
{
    ENGINE_ASSERT(page < pages_.Size(), "GlyphAtlas::TakeDirty: page out of range");
    Page& p = pages_[page];
    if (!p.dirty)
    {
        return false;
    }
    *rect = p.dirty_rect;
    p.dirty = false;
    return true;
}

}  // namespace engine_placeholder::text
