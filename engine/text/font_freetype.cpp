// The one file that names FreeType (platform_neutral_check, tools/platform_neutral_check/library_boundary.txt).
#include "sync/atomic.h"
#include "lang/new.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H
#include FT_OUTLINE_H

#include "io/file.h"
#include "log/assert.h"
#include "mem/heap_allocator.h"
#include "text/font.h"
#include "text/library.h"
#include "cstr/mem_ops.h"

namespace engine_placeholder::text {

// Named (not anonymous) because FontLibrary::Impl has a member of this type.
namespace internal {

struct Counters {
    Atomic<u64> allocations{0};
    Atomic<u64> frees{0};
    Atomic<u64> live{0};
    Atomic<u64> peak{0};
    Atomic<u64> refused{0};         // allocations that returned null (injected or real)
    Atomic<i64> succeed_count{-1}; // allocations that still succeed; negative: no injection
};

} // namespace internal

namespace {

using internal::Counters;

// FreeType frees by pointer alone, so every block carries a header that remembers its size (16 bytes keeps the 16-byte alignment).
constexpr usize kHeader = 16;

void* AllocBlock(Counters& c, usize size) {
    i64 left = c.succeed_count.Load(MemoryOrder::Relaxed);
    while (left >= 0) {
        if (left == 0) {
            c.refused.FetchAdd(1, MemoryOrder::Relaxed);
            return nullptr;
        }
        if (c.succeed_count.CompareExchangeWeak(left, left - 1, MemoryOrder::Relaxed)) break;
    }
    void* base = HeapAllocator::Default().Alloc(size + kHeader, 16);
    if (base == nullptr) {
        c.refused.FetchAdd(1, MemoryOrder::Relaxed);
        return nullptr;
    }
    *static_cast<usize*>(base) = size;
    c.allocations.FetchAdd(1, MemoryOrder::Relaxed);
    const u64 live = c.live.FetchAdd(size, MemoryOrder::Relaxed) + size;
    u64 peak = c.peak.Load(MemoryOrder::Relaxed);
    while (live > peak && !c.peak.CompareExchangeWeak(peak, live, MemoryOrder::Relaxed)) {}
    return static_cast<byte*>(base) + kHeader;
}

void FreeBlock(Counters& c, void* block) {
    if (block == nullptr) return;
    void* base = static_cast<byte*>(block) - kHeader;
    const usize size = *static_cast<usize*>(base);
    c.frees.FetchAdd(1, MemoryOrder::Relaxed);
    c.live.FetchSub(size, MemoryOrder::Relaxed);
    HeapAllocator::Default().Free(base, size + kHeader, 16);
}

void* FtAlloc(FT_Memory memory, long size) {
    return AllocBlock(*static_cast<Counters*>(memory->user), static_cast<usize>(size));
}

void FtFree(FT_Memory memory, void* block) { FreeBlock(*static_cast<Counters*>(memory->user), block); }

void* FtRealloc(FT_Memory memory, long cur_size, long new_size, void* block) {
    Counters& c = *static_cast<Counters*>(memory->user);
    void* fresh = AllocBlock(c, static_cast<usize>(new_size));
    if (fresh == nullptr) return nullptr; // FreeType keeps the old block when a reallocation fails
    if (block != nullptr) {
        const usize keep = static_cast<usize>(cur_size < new_size ? cur_size : new_size);
        MemCopy(fresh, block, keep);
        FreeBlock(c, block);
    }
    return fresh;
}

LibraryAllocationStats Snapshot(const Counters& c) {
    LibraryAllocationStats stats;
    stats.allocations = c.allocations.Load();
    stats.frees = c.frees.Load();
    stats.peak_bytes = c.peak.Load();
    stats.live_bytes_at_end = c.live.Load();
    return stats;
}

template <typename T>
T* NewObject() {
    void* mem = HeapAllocator::Default().Alloc(sizeof(T), alignof(T));
    return mem == nullptr ? nullptr : new (mem) T();
}

template <typename T>
void DeleteObject(T* object) {
    if (object == nullptr) return;
    object->~T();
    HeapAllocator::Default().Free(object, sizeof(T), alignof(T));
}

constexpr f32 kFixed26_6 = 1.0f / 64.0f;

f32 FromFixed(long value) { return static_cast<f32>(value) * kFixed26_6; }

Status MakeStatus(Error error, const char* message) {
    Status status;
    status.error = error;
    status.message = message;
    return status;
}

Status FromFreeType(FT_Error error) {
    switch (error) {
    case 0: return Status{};
    case FT_Err_Out_Of_Memory: return MakeStatus(Error::OutOfMemory, "out of memory while reading the font");
    case FT_Err_Unknown_File_Format: return MakeStatus(Error::NotAFont, "the data is not in a font format");
    case FT_Err_Invalid_Argument: return MakeStatus(Error::Unsupported, "the face index is out of range");
    default: return MakeStatus(Error::Corrupt, "the font is damaged or truncated");
    }
}

u32 LoadFlags(Hinting hinting) {
    switch (hinting) {
    case Hinting::None: return FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP;
    case Hinting::Light: return FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_BITMAP;
    case Hinting::Normal: return FT_LOAD_TARGET_NORMAL | FT_LOAD_NO_BITMAP;
    case Hinting::Bytecode: return FT_LOAD_TARGET_NORMAL | FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_BITMAP;
    }
    return FT_LOAD_NO_BITMAP;
}

Atomic<u64> g_next_font_id{1};

u32 ReadBe32(const byte* p) { return (u32{p[0]} << 24) | (u32{p[1]} << 16) | (u32{p[2]} << 8) | u32{p[3]}; }

// FreeType loads sfnt tables lazily, so a font cut short opens fine and fails on a later glyph. A plain sfnt file (TrueType or OpenType) lists its tables with
// offset and length up front; every one must lie inside the data. Collections and other formats are left to FreeType.
bool SfntTablesFit(ByteSpan data) {
    if (data.Size() < 12) return false;
    const byte* d = data.Data();
    const u32 tag = ReadBe32(d);
    if (tag != 0x00010000u && tag != 0x4F54544Fu /* OTTO */ && tag != 0x74727565u /* true */) return true;
    const u32 count = (u32{d[4]} << 8) | u32{d[5]};
    if (data.Size() < 12 + usize{count} * 16) return false;
    for (u32 i = 0; i < count; ++i) {
        const byte* record = d + 12 + usize{i} * 16;
        const u64 end = u64{ReadBe32(record + 8)} + u64{ReadBe32(record + 12)};
        if (end > data.Size()) return false;
    }
    return true;
}

} // namespace

// --- FontLibrary ----------------------------------------------------------------------------------------------------------------

struct FontLibrary::Impl {
    Counters counters;
    FT_MemoryRec_ memory{};
    FT_Library library = nullptr;
    Atomic<u32> open_fonts{0};
};

FontLibrary::FontLibrary() = default;

FontLibrary::~FontLibrary() {
    if (impl_ == nullptr) return;
    ENGINE_ASSERT(impl_->open_fonts.Load() == 0, "FontLibrary destroyed while a Font opened from it is still alive");
    if (impl_->library != nullptr) FT_Done_Library(impl_->library);
    DeleteObject(impl_);
}

Status FontLibrary::Initialize() {
    if (impl_ != nullptr) return Status{};
    impl_ = NewObject<Impl>();
    if (impl_ == nullptr) return MakeStatus(Error::OutOfMemory, "out of memory creating the font library");
    impl_->memory.user = &impl_->counters;
    impl_->memory.alloc = &FtAlloc;
    impl_->memory.free = &FtFree;
    impl_->memory.realloc = &FtRealloc;
    if (FT_New_Library(&impl_->memory, &impl_->library) != 0) {
        DeleteObject(impl_);
        impl_ = nullptr;
        return MakeStatus(Error::OutOfMemory, "FreeType could not start");
    }
    FT_Add_Default_Modules(impl_->library);
    return Status{};
}

bool FontLibrary::IsInitialized() const { return impl_ != nullptr; }

LibraryVersion FontLibrary::Version() const {
    LibraryVersion version;
    if (impl_ == nullptr) return version;
    FT_Int major = 0;
    FT_Int minor = 0;
    FT_Int patch = 0;
    FT_Library_Version(impl_->library, &major, &minor, &patch);
    version.major = static_cast<u32>(major);
    version.minor = static_cast<u32>(minor);
    version.patch = static_cast<u32>(patch);
    return version;
}

LibraryAllocationStats FontLibrary::Stats() const {
    return impl_ == nullptr ? LibraryAllocationStats{} : Snapshot(impl_->counters);
}

void FontLibrary::FailAllocationsAfter(u64 count) {
    if (impl_ == nullptr) return;
    impl_->counters.succeed_count.Store(count == ~u64{0} ? -1 : static_cast<i64>(count));
}

bool ProbeLibrary(LibraryVersion* version, LibraryAllocationStats* stats) {
    FontLibrary library;
    if (!library.Initialize().Ok()) return false;
    const LibraryVersion v = library.Version();
    const bool done = FT_Done_Library(library.impl_->library) == 0; // shut FreeType down now so the counts see everything go back
    library.impl_->library = nullptr;
    if (version != nullptr) *version = v;
    if (stats != nullptr) *stats = Snapshot(library.impl_->counters);
    return done;
}

// --- Font -----------------------------------------------------------------------------------------------------------------------

struct Font::Impl {
    FontLibrary::Impl* library = nullptr;
    FT_Face face = nullptr;
    Vector<byte> data;
    FontOptions options;
    u32 pixel_size = 0;
    u64 id = 0;
    FontMetrics metrics;
};

Font::~Font() { Close(); }

Font::Font(Font&& other) noexcept : impl_(other.impl_) { other.impl_ = nullptr; }

Font& Font::operator=(Font&& other) noexcept {
    if (this != &other) {
        Close();
        impl_ = other.impl_;
        other.impl_ = nullptr;
    }
    return *this;
}

void Font::Close() {
    if (impl_ == nullptr) return;
    if (impl_->face != nullptr) FT_Done_Face(impl_->face);
    impl_->library->open_fonts.FetchSub(1);
    DeleteObject(impl_);
    impl_ = nullptr;
}

bool Font::IsOpen() const { return impl_ != nullptr; }

Status Font::Open(FontLibrary& library, ByteSpan data, const FontOptions& options, Font* out) {
    ENGINE_ASSERT(out != nullptr, "Font::Open: out is null");
    Vector<byte> copy;
    if (data.Size() != 0) {
        if (!copy.TryResizeUninitialized(data.Size())) {
            out->Close();
            return MakeStatus(Error::OutOfMemory, "out of memory copying the font data");
        }
        MemCopy(copy.Data(), data.Data(), data.Size());
    }
    return OpenOwned(library, static_cast<Vector<byte>&&>(copy), options, out);
}

Status Font::OpenOwned(FontLibrary& library, Vector<byte>&& bytes, const FontOptions& options, Font* out) {
    out->Close();
    if (library.impl_ == nullptr) return MakeStatus(Error::InvalidArgument, "the font library is not initialized");
    const ByteSpan data(bytes.Data(), bytes.Size());
    if (data.Size() == 0) return MakeStatus(Error::Empty, "the font data has zero bytes");
    if (!SfntTablesFit(data)) return MakeStatus(Error::Corrupt, "the font is truncated: a table lies beyond the end of the data");

    Impl* impl = NewObject<Impl>();
    if (impl == nullptr) return MakeStatus(Error::OutOfMemory, "out of memory opening the font");
    impl->library = library.impl_;
    impl->options = options;
    impl->data = static_cast<Vector<byte>&&>(bytes);

    const u64 refused_before = impl->library->counters.refused.Load();
    FT_Error error = FT_New_Memory_Face(impl->library->library, impl->data.Data(), static_cast<FT_Long>(impl->data.Size()),
                                        static_cast<FT_Long>(options.face_index), &impl->face);
    if (error == 0 && impl->library->counters.refused.Load() != refused_before) {
        // FreeType shrugs off some failed allocations while opening (an optional table is left unset) and then crashes in a later call that needs it.
        FT_Done_Face(impl->face);
        impl->face = nullptr;
        error = FT_Err_Out_Of_Memory;
    }
    if (error != 0) {
        // A file with no font signature that FreeType rejects as too short or malformed is not a font at all, rather than a damaged one.
        const u32 tag = data.Size() >= 4 ? ReadBe32(data.Data()) : 0;
        const bool known_signature = tag == 0x00010000u || tag == 0x4F54544Fu || tag == 0x74727565u || tag == 0x74746366u /* ttcf */ || tag == 0x74797031u /* typ1 */;
        const Status status = (!known_signature && (error == FT_Err_Invalid_File_Format || error == FT_Err_Invalid_Stream_Operation))
                                  ? MakeStatus(Error::NotAFont, "the data is not in a font format")
                                  : FromFreeType(error);
        DeleteObject(impl);
        return status;
    }
    if (!FT_IS_SCALABLE(impl->face)) {
        FT_Done_Face(impl->face);
        DeleteObject(impl);
        return MakeStatus(Error::Unsupported, "the font has no scalable outlines");
    }
    impl->library->open_fonts.FetchAdd(1);
    impl->id = g_next_font_id.FetchAdd(1);
    out->impl_ = impl;
    const Status size = out->SetPixelSize(16);
    if (!size.Ok()) {
        out->Close();
        return size.Is(Error::InvalidArgument) ? MakeStatus(Error::Corrupt, "the font rejected its first size") : size;
    }
    return Status{};
}

Status Font::OpenFile(FontLibrary& library, const io::Path& path, const FontOptions& options, Font* out) {
    ENGINE_ASSERT(out != nullptr, "Font::OpenFile: out is null");
    Vector<byte> bytes;
    const io::Status read = io::ReadFile(path, &bytes);
    if (read.Failed()) {
        Status status = MakeStatus(read.Is(io::Error::OutOfMemory) ? Error::OutOfMemory : Error::NotFound, "the font file could not be read");
        status.io_error = read.error;
        out->Close();
        return status;
    }
    return OpenOwned(library, static_cast<Vector<byte>&&>(bytes), options, out);
}

Status Font::SetPixelSize(u32 pixels) {
    if (impl_ == nullptr) return MakeStatus(Error::InvalidArgument, "the font is not open");
    if (pixels == 0 || pixels > kMaxPixelSize) return MakeStatus(Error::InvalidArgument, "the pixel size is 0 or above the sanity limit");
    const FT_Error error = FT_Set_Pixel_Sizes(impl_->face, 0, pixels);
    if (error != 0) return FromFreeType(error);
    impl_->pixel_size = pixels;
    const FT_Size_Metrics& m = impl_->face->size->metrics; // 26.6 fixed point: converted to pixels here and nowhere else
    impl_->metrics.ascent = FromFixed(m.ascender);
    impl_->metrics.descent = FromFixed(-m.descender);
    impl_->metrics.line_height = FromFixed(m.height);
    impl_->metrics.units_per_em = impl_->face->units_per_EM;
    return Status{};
}

u32 Font::PixelSize() const { return impl_ == nullptr ? 0 : impl_->pixel_size; }
FontMetrics Font::Metrics() const { return impl_ == nullptr ? FontMetrics{} : impl_->metrics; }
FontOptions Font::Options() const { return impl_ == nullptr ? FontOptions{} : impl_->options; }
u32 Font::GlyphCount() const { return impl_ == nullptr ? 0 : static_cast<u32>(impl_->face->num_glyphs); }
u64 Font::Id() const { return impl_ == nullptr ? 0 : impl_->id; }

GlyphId Font::FindGlyph(char32 codepoint) const {
    if (impl_ == nullptr) return 0;
    if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) return 0;
    return FT_Get_Char_Index(impl_->face, codepoint);
}

bool Font::HasGlyph(char32 codepoint) const { return FindGlyph(codepoint) != 0; }

GlyphMetrics Font::Metrics(GlyphId glyph) const {
    GlyphMetrics out;
    if (impl_ == nullptr || glyph >= GlyphCount()) return out;
    if (FT_Load_Glyph(impl_->face, glyph, LoadFlags(impl_->options.hinting)) != 0) return out;
    const FT_Glyph_Metrics& m = impl_->face->glyph->metrics;
    out.advance = FromFixed(impl_->face->glyph->advance.x);
    out.bearing_x = FromFixed(m.horiBearingX);
    out.bearing_y = FromFixed(m.horiBearingY);
    // The whole-pixel box that contains the outline: from floor(left) to ceil(right), from floor(bottom) to ceil(top), as the rasterizer's bitmap does.
    const long left = m.horiBearingX & ~63L;
    const long right = (m.horiBearingX + m.width + 63) & ~63L;
    const long top = (m.horiBearingY + 63) & ~63L;
    const long bottom = (m.horiBearingY - m.height) & ~63L;
    out.width = static_cast<u32>((right - left) / 64);
    out.height = static_cast<u32>((top - bottom) / 64);
    return out;
}

f32 Font::Kerning(GlyphId left, GlyphId right) const {
    if (impl_ == nullptr || !FT_HAS_KERNING(impl_->face)) return 0;
    FT_Vector delta{};
    if (FT_Get_Kerning(impl_->face, left, right, FT_KERNING_UNFITTED, &delta) != 0) return 0;
    return FromFixed(delta.x);
}

Status Font::Render(GlyphId glyph, GlyphBitmap* out) const {
    ENGINE_ASSERT(out != nullptr, "Font::Render: out is null");
    out->width = out->height = 0;
    out->left = out->top = 0;
    out->advance = 0;
    out->pixels.Clear();
    if (impl_ == nullptr || glyph >= GlyphCount()) return MakeStatus(Error::InvalidArgument, "the glyph id is out of range");
    if (impl_->pixel_size > kMaxAtlasPixelSize) return MakeStatus(Error::TooLarge, "the pixel size is above the largest rasterized size");
    const FT_Error error = FT_Load_Glyph(impl_->face, glyph, LoadFlags(impl_->options.hinting) | FT_LOAD_RENDER);
    if (error != 0) return FromFreeType(error);
    const FT_GlyphSlot slot = impl_->face->glyph;
    out->advance = FromFixed(slot->advance.x);
    const FT_Bitmap& bitmap = slot->bitmap;
    if (bitmap.width == 0 || bitmap.rows == 0) return Status{};
    if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY || bitmap.pitch < static_cast<int>(bitmap.width)) return MakeStatus(Error::Unsupported, "the glyph did not render as 8-bit coverage");
    const usize count = usize{bitmap.width} * usize{bitmap.rows};
    if (!out->pixels.TryResizeUninitialized(count)) return MakeStatus(Error::OutOfMemory, "out of memory rasterizing the glyph");
    for (u32 row = 0; row < bitmap.rows; ++row) {
        MemCopy(out->pixels.Data() + usize{row} * bitmap.width, bitmap.buffer + usize{row} * static_cast<usize>(bitmap.pitch), bitmap.width);
    }
    out->width = bitmap.width;
    out->height = bitmap.rows;
    out->left = slot->bitmap_left;
    out->top = slot->bitmap_top;
    return Status{};
}

Status Font::LoadOutline(GlyphId glyph, Outline* out) const {
    ENGINE_ASSERT(out != nullptr, "Font::LoadOutline: out is null");
    out->points.Clear();
    out->contour_ends.Clear();
    out->x_min = out->y_min = out->x_max = out->y_max = 0;
    if (impl_ == nullptr || glyph >= GlyphCount()) return MakeStatus(Error::InvalidArgument, "the glyph id is out of range");
    const FT_Error error = FT_Load_Glyph(impl_->face, glyph, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP);
    if (error != 0) return FromFreeType(error);
    const FT_GlyphSlot slot = impl_->face->glyph;
    if (slot->format != FT_GLYPH_FORMAT_OUTLINE) return MakeStatus(Error::Unsupported, "the glyph has no outline");
    const FT_Outline& o = slot->outline;
    if (!out->points.TryReserve(static_cast<usize>(o.n_points)) || !out->contour_ends.TryReserve(static_cast<usize>(o.n_contours))) {
        return MakeStatus(Error::OutOfMemory, "out of memory loading the outline");
    }
    for (int i = 0; i < o.n_points; ++i) {
        OutlinePoint point;
        point.x = static_cast<i32>(o.points[i].x);
        point.y = static_cast<i32>(o.points[i].y);
        point.on_curve = (o.tags[i] & FT_CURVE_TAG_ON) != 0;
        point.cubic = !point.on_curve && (o.tags[i] & FT_CURVE_TAG_CUBIC) != 0;
        if (i == 0) {
            out->x_min = out->x_max = point.x;
            out->y_min = out->y_max = point.y;
        } else {
            if (point.x < out->x_min) out->x_min = point.x;
            if (point.x > out->x_max) out->x_max = point.x;
            if (point.y < out->y_min) out->y_min = point.y;
            if (point.y > out->y_max) out->y_max = point.y;
        }
        out->points.Push(point);
    }
    for (int i = 0; i < o.n_contours; ++i) out->contour_ends.Push(static_cast<u32>(o.contours[i]));
    return Status{};
}

} // namespace engine_placeholder::text
