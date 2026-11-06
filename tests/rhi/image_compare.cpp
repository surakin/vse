#include "image_compare.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <unistd.h>

#ifndef ENGINE_RHI_GOLDEN_DIR
#define ENGINE_RHI_GOLDEN_DIR "tests/rhi/golden"
#endif

namespace gpu_test {

namespace {

int MaxChannelDifference(const unsigned char* a, const unsigned char* b) {
    int worst = 0;
    for (int c = 0; c < 4; ++c) {
        const int d = std::abs(static_cast<int>(a[c]) - static_cast<int>(b[c]));
        worst = d > worst ? d : worst;
    }
    return worst;
}

// A pixel is an edge pixel when the golden changes by more than `step` between it and a 4-neighbour.
bool IsEdge(const Image& g, unsigned x, unsigned y, int step) {
    const unsigned char* p = g.Pixel(x, y);
    if (x > 0 && MaxChannelDifference(p, g.Pixel(x - 1, y)) > step) return true;
    if (x + 1 < g.width && MaxChannelDifference(p, g.Pixel(x + 1, y)) > step) return true;
    if (y > 0 && MaxChannelDifference(p, g.Pixel(x, y - 1)) > step) return true;
    if (y + 1 < g.height && MaxChannelDifference(p, g.Pixel(x, y + 1)) > step) return true;
    return false;
}

} // namespace

std::string DiffPath(const char* name) { return TempPath((std::string("diff_") + name + ".ppm").c_str()); }

CompareResult Compare(const Image& golden, const Image& actual, const CompareOptions& options, const char* name) {
    CompareResult r;
    if (!golden.Valid() || !actual.Valid()) {
        r.message = "an image is empty or malformed";
        return r;
    }
    if (golden.width != actual.width || golden.height != actual.height) {
        char text[128];
        std::snprintf(text, sizeof(text), "size differs: golden %ux%u, actual %ux%u", golden.width, golden.height, actual.width, actual.height);
        r.message = text;
        return r;
    }
    Image diff;
    diff.width = golden.width;
    diff.height = golden.height;
    diff.rgba.assign(golden.rgba.size(), 255);
    unsigned interior = 0;
    unsigned interior_over_limit = 0;
    for (unsigned y = 0; y < golden.height; ++y) {
        for (unsigned x = 0; x < golden.width; ++x) {
            const unsigned char* g = golden.Pixel(x, y);
            const unsigned char* a = actual.Pixel(x, y);
            const int d = MaxChannelDifference(g, a);
            r.max_difference = d > r.max_difference ? d : r.max_difference;
            const bool edge = IsEdge(golden, x, y, options.edge_step);
            unsigned char* out = &diff.rgba[(static_cast<size_t>(y) * golden.width + x) * 4];
            const int shown = d * 8 > 255 ? 255 : d * 8;
            out[0] = static_cast<unsigned char>(shown);
            out[1] = out[2] = static_cast<unsigned char>(edge ? shown / 2 : 0);
            if (edge) {
                ++r.edge_pixels;
                if (d > options.edge_tolerance) ++r.edge_misses;
            } else {
                ++interior;
                if (d > options.tolerance) ++r.interior_misses;
                if (d > options.interior_miss_limit) ++interior_over_limit;
            }
        }
    }
    const bool interior_ok = interior_over_limit == 0 && r.interior_misses <= static_cast<unsigned>(options.max_interior_miss * interior);
    const bool edge_ok = r.edge_misses <= static_cast<unsigned>(options.max_edge_miss * r.edge_pixels);
    r.same = interior_ok && edge_ok;
    if (!r.same) {
        const std::string path = DiffPath(name);
        const bool wrote = WritePpm(path.c_str(), diff);
        char text[256];
        std::snprintf(text, sizeof(text), "%u interior pixels differ by more than %d, %u of %u edge pixels by more than %d, worst %d%s%s",
                      r.interior_misses, options.tolerance, r.edge_misses, r.edge_pixels, options.edge_tolerance, r.max_difference,
                      wrote ? "; diff image: " : "", wrote ? path.c_str() : "");
        r.message = text;
    }
    return r;
}

bool WritePpm(const char* path, const Image& image) {
    if (!image.Valid()) return false;
    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr) return false;
    std::fprintf(f, "P6\n%u %u\n255\n", image.width, image.height);
    bool ok = true;
    for (size_t i = 0; i < static_cast<size_t>(image.width) * image.height && ok; ++i) {
        ok = std::fwrite(&image.rgba[i * 4], 1, 3, f) == 3;
    }
    return std::fclose(f) == 0 && ok;
}

namespace {

// Reads the next unsigned integer of a PPM header, skipping whitespace and '#' comments.
bool ReadHeaderNumber(std::FILE* f, unsigned* out) {
    int c = std::fgetc(f);
    for (;;) {
        while (c == ' ' || c == '\t' || c == '\r' || c == '\n') c = std::fgetc(f);
        if (c != '#') break;
        while (c != '\n' && c != EOF) c = std::fgetc(f);
    }
    if (c < '0' || c > '9') return false;
    unsigned long v = 0;
    while (c >= '0' && c <= '9') {
        v = v * 10 + static_cast<unsigned long>(c - '0');
        if (v > 1000000) return false;
        c = std::fgetc(f);
    }
    *out = static_cast<unsigned>(v);
    return true; // the character after the number (one whitespace after the last) is consumed
}

} // namespace

bool ReadPpm(const char* path, Image* image) {
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) return false;
    unsigned w = 0, h = 0, max = 0;
    bool ok = std::fgetc(f) == 'P' && std::fgetc(f) == '6' && ReadHeaderNumber(f, &w) && ReadHeaderNumber(f, &h) && ReadHeaderNumber(f, &max) &&
              max == 255 && w > 0 && h > 0;
    if (ok) {
        // The file must hold the whole image before anything is allocated for it.
        const long data_start = std::ftell(f);
        ok = data_start >= 0 && std::fseek(f, 0, SEEK_END) == 0;
        const long size = ok ? std::ftell(f) : -1;
        ok = ok && size >= data_start && static_cast<unsigned long long>(size - data_start) >= static_cast<unsigned long long>(w) * h * 3 &&
             std::fseek(f, data_start, SEEK_SET) == 0;
    }
    if (ok) {
        image->width = w;
        image->height = h;
        image->rgba.assign(static_cast<size_t>(w) * h * 4, 255);
        for (size_t i = 0; i < static_cast<size_t>(w) * h && ok; ++i) {
            ok = std::fread(&image->rgba[i * 4], 1, 3, f) == 3;
        }
    }
    std::fclose(f);
    return ok;
}

bool UpdatingGoldens() {
    const char* v = std::getenv("ENGINE_RHI_UPDATE_GOLDEN");
    return v != nullptr && v[0] == '1';
}

std::string TempPath(const char* name) {
    const char* dir = std::getenv("TMPDIR");
    std::string path = dir != nullptr && dir[0] != '\0' ? dir : "/tmp";
    path += "/rhi_" + std::to_string(static_cast<long>(getpid())) + "_" + name;
    return path;
}

std::string GoldenPath(const char* file) { return std::string(ENGINE_RHI_GOLDEN_DIR) + "/" + file; }

} // namespace gpu_test
