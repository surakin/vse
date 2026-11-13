#include "image_compare.h"

#include "io/file.h"
#include "io/file_system.h"

#include "build_defs.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "lang/scalar.h"
#include "os/console.h"

#if ENGINE_PLATFORM_WINDOWS
#include <process.h>  // _getpid
#else
#include <unistd.h>  // getpid
#endif

#ifndef ENGINE_RHI_GOLDEN_DIR
#define ENGINE_RHI_GOLDEN_DIR "tests/rhi/golden"
#endif

namespace gpu_test
{

namespace
{

int MaxChannelDifference(const engine_placeholder::byte* a, const engine_placeholder::byte* b)
{
    int worst = 0;
    for (int c = 0; c < 4; ++c)
    {
        const int d = engine_placeholder::Abs(static_cast<int>(a[c]) - static_cast<int>(b[c]));
        worst = d > worst ? d : worst;
    }
    return worst;
}

// A pixel is an edge pixel when the golden changes by more than `step` between it and a 4-neighbour.
bool IsEdge(const Image& g, unsigned x, unsigned y, int step)
{
    const engine_placeholder::byte* p = g.Pixel(x, y);
    if (x > 0 && MaxChannelDifference(p, g.Pixel(x - 1, y)) > step)
    {
        return true;
    }
    if (x + 1 < g.width && MaxChannelDifference(p, g.Pixel(x + 1, y)) > step)
    {
        return true;
    }
    if (y > 0 && MaxChannelDifference(p, g.Pixel(x, y - 1)) > step)
    {
        return true;
    }
    if (y + 1 < g.height && MaxChannelDifference(p, g.Pixel(x, y + 1)) > step)
    {
        return true;
    }
    return false;
}

}  // namespace

engine_test::Text DiffPath(const char* name)
{
    return TempPath(engine_test::Concat("diff_", name, ".ppm").CStr());
}

CompareResult Compare(const Image& golden, const Image& actual, const CompareOptions& options, const char* name)
{
    CompareResult r;
    if (!golden.Valid() || !actual.Valid())
    {
        r.message = engine_test::MakeText("an image is empty or malformed");
        return r;
    }
    if (golden.width != actual.width || golden.height != actual.height)
    {
        char text[128];
        engine_placeholder::FormatTo(text, sizeof(text), "size differs: golden %ux%u, actual %ux%u", golden.width,
                                     golden.height, actual.width, actual.height);
        r.message = engine_test::MakeText(engine_placeholder::StringView(text));
        return r;
    }
    Image diff;
    diff.width = golden.width;
    diff.height = golden.height;
    diff.rgba.Assign(golden.rgba.Size(), 255);
    unsigned interior = 0;
    unsigned interior_over_limit = 0;
    for (unsigned y = 0; y < golden.height; ++y)
    {
        for (unsigned x = 0; x < golden.width; ++x)
        {
            const engine_placeholder::byte* g = golden.Pixel(x, y);
            const engine_placeholder::byte* a = actual.Pixel(x, y);
            const int d = MaxChannelDifference(g, a);
            r.max_difference = d > r.max_difference ? d : r.max_difference;
            const bool edge = IsEdge(golden, x, y, options.edge_step);
            engine_placeholder::byte*
                out = &diff.rgba[(static_cast<engine_placeholder::usize>(y) * golden.width + x) * 4];
            const int shown = d * 8 > 255 ? 255 : d * 8;
            out[0] = static_cast<engine_placeholder::byte>(shown);
            out[1] = out[2] = static_cast<engine_placeholder::byte>(edge ? shown / 2 : 0);
            if (edge)
            {
                ++r.edge_pixels;
                if (d > options.edge_tolerance)
                {
                    ++r.edge_misses;
                }
            }
            else
            {
                ++interior;
                if (d > options.tolerance)
                {
                    ++r.interior_misses;
                }
                if (d > options.interior_miss_limit)
                {
                    ++interior_over_limit;
                }
            }
        }
    }
    const bool interior_ok = interior_over_limit == 0 &&
                             r.interior_misses <= static_cast<unsigned>(options.max_interior_miss * interior);
    const bool edge_ok = r.edge_misses <= static_cast<unsigned>(options.max_edge_miss * r.edge_pixels);
    r.same = interior_ok && edge_ok;
    if (!r.same)
    {
        const engine_test::Text path = DiffPath(name);
        const bool wrote = WritePpm(path.CStr(), diff);
        char text[256];
        engine_placeholder::FormatTo(
            text, sizeof(text),
            "%u interior pixels differ by more than %d, %u of %u edge pixels by more than %d, worst %d%s%s",
            r.interior_misses, options.tolerance, r.edge_misses, r.edge_pixels, options.edge_tolerance,
            r.max_difference, wrote ? "; diff image: " : "", wrote ? path.CStr() : "");
        r.message = engine_test::MakeText(engine_placeholder::StringView(text));
    }
    // ENGINE_RHI_GOLDEN_REPORT=1 prints every comparison's numbers, passed or not (Phase 26 Day 325: the numbers of a
    // backend's goldens are recorded in its day files).
    const char* report = engine_placeholder::os::GetEnvironment("ENGINE_RHI_GOLDEN_REPORT");
    if (report != nullptr && report[0] == '1')
    {
        engine_placeholder::os::Print("golden %s: %s, worst %d, %u interior over %d, %u of %u edge over %d\n", name,
                                      r.same ? "same" : "DIFFERENT", r.max_difference, r.interior_misses,
                                      options.tolerance, r.edge_misses, r.edge_pixels, options.edge_tolerance);
    }
    return r;
}

bool WritePpm(const char* path, const Image& image)
{
    if (!image.Valid())
    {
        return false;
    }
    namespace io = engine_placeholder::io;
    io::Path file;
    if (!io::Path::Parse(engine_placeholder::StringView(path), io::NativePathStyle(), &file))
    {
        return false;
    }
    char header[64];
    const int header_length = engine_placeholder::FormatTo(header, sizeof(header), "P6\n%u %u\n255\n", image.width,
                                                           image.height);
    engine_placeholder::Vector<engine_placeholder::byte> bytes;
    const engine_placeholder::usize pixels = static_cast<engine_placeholder::usize>(image.width) * image.height;
    bytes.Reserve(static_cast<engine_placeholder::usize>(header_length) + pixels * 3);
    for (int i = 0; i < header_length; ++i)
    {
        bytes.Push(static_cast<engine_placeholder::byte>(header[i]));
    }
    for (engine_placeholder::usize i = 0; i < pixels; ++i)
    {
        for (engine_placeholder::usize c = 0; c < 3; ++c)
        {
            bytes.Push(image.rgba[i * 4 + c]);
        }
    }
    return io::WriteFile(file, bytes.Data(), bytes.Size()).Ok();
}

namespace
{

// Reads the next unsigned integer of a PPM header from `data[*pos..size)`, skipping whitespace and '#' comments. The
// character after the number (one whitespace after the last) is consumed.
bool ReadHeaderNumber(const engine_placeholder::byte* data, engine_placeholder::usize size,
                      engine_placeholder::usize* pos, unsigned* out)
{
    auto next = [&]() -> int { return *pos < size ? data[(*pos)++] : -1; };
    int c = next();
    for (;;)
    {
        while (c == ' ' || c == '\t' || c == '\r' || c == '\n')
        {
            c = next();
        }
        if (c != '#')
        {
            break;
        }
        while (c != '\n' && c != -1)
        {
            c = next();
        }
    }
    if (c < '0' || c > '9')
    {
        return false;
    }
    unsigned long v = 0;
    while (c >= '0' && c <= '9')
    {
        v = v * 10 + static_cast<unsigned long>(c - '0');
        if (v > 1000000)
        {
            return false;
        }
        c = next();
    }
    *out = static_cast<unsigned>(v);
    return true;
}

}  // namespace

bool ReadPpm(const char* path, Image* image)
{
    namespace io = engine_placeholder::io;
    io::Path file;
    engine_placeholder::Vector<engine_placeholder::u8> bytes;
    if (!io::Path::Parse(engine_placeholder::StringView(path), io::NativePathStyle(), &file) ||
        io::ReadFile(file, &bytes).Failed())
    {
        return false;
    }
    const engine_placeholder::byte* data = bytes.Data();
    const engine_placeholder::usize size = bytes.Size();
    engine_placeholder::usize pos = 0;
    unsigned w = 0, h = 0, max = 0;
    bool ok = size >= 2 && data[0] == 'P' && data[1] == '6';
    pos = 2;
    ok = ok && ReadHeaderNumber(data, size, &pos, &w) && ReadHeaderNumber(data, size, &pos, &h) &&
         ReadHeaderNumber(data, size, &pos, &max) && max == 255 && w > 0 && h > 0;
    // The file must hold the whole image before anything is allocated for it.
    ok = ok && static_cast<unsigned long long>(size - pos) >= static_cast<unsigned long long>(w) * h * 3;
    if (!ok)
    {
        return false;
    }
    image->width = w;
    image->height = h;
    image->rgba.Assign(static_cast<engine_placeholder::usize>(w) * h * 4, 255);
    for (engine_placeholder::usize i = 0; i < static_cast<engine_placeholder::usize>(w) * h; ++i)
    {
        engine_placeholder::MemCopy(&image->rgba[i * 4], data + pos + i * 3, 3);
    }
    return true;
}

bool UpdatingGoldens()
{
    const char* v = engine_placeholder::os::GetEnvironment("ENGINE_RHI_UPDATE_GOLDEN");
    return v != nullptr && v[0] == '1';
}

engine_test::Text TempPath(const char* name)
{
    namespace io = engine_placeholder::io;
    io::Path dir;
    if (io::TempDirectory(&dir).Failed())
    {
        dir = io::Path("/tmp");
    }
#if ENGINE_PLATFORM_WINDOWS
    const long pid = static_cast<long>(_getpid());
#else
    const long pid = static_cast<long>(getpid());
#endif
    const engine_test::Text leaf = engine_test::Format("rhi_%ld_%s", pid, name);
    const io::Path path = dir.Join(leaf.View());
    return engine_test::MakeText(path.View());
}

engine_test::Text GoldenPath(const char* file)
{
    return engine_test::Concat(ENGINE_RHI_GOLDEN_DIR, "/", file);
}

}  // namespace gpu_test
