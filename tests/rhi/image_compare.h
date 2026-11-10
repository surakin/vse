#pragma once

#include "containers/vector.h"
#include "test_text.h"
#include "types.h"

// Image comparison for rendering tests: a tolerant comparison against a golden image, binary PPM (P6) I/O and a
// diff-image dump. Colors are RGBA8; PPM files carry RGB only, so alpha reads back as 255.
//
// Why a tolerance: GPUs agree on interior pixels of a triangle to within rounding of the interpolated color (a
// couple of levels per channel) but not on edge pixels, where the rasterization rules for pixel centers on an edge
// and the precision of the coverage test differ by vendor. So a pixel whose golden value differs from a
// neighbouring pixel's by more than `edge_step` (an "edge pixel": the step from background to triangle, far larger
// than the ramp of a smooth gradient) is compared with the wider `edge_tolerance`, and a bounded fraction of edge
// pixels may still miss it. Everything else must match within `tolerance`.
namespace gpu_test {

// The pixel bytes of an Image: a copyable byte buffer. The lower-case names (assign, resize, data, size, empty) are the spelling the
// renderer tests still use for the pixels they read back; the capitalized ones are the engine's. The lower-case ones go when those
// tests are ported.
class PixelBytes {
public:
    PixelBytes() = default;
    PixelBytes(const PixelBytes& other) { CopyFrom(other); }
    PixelBytes& operator=(const PixelBytes& other) {
        if (this != &other) {
            bytes_.Clear();
            CopyFrom(other);
        }
        return *this;
    }
    PixelBytes(PixelBytes&&) noexcept = default;
    PixelBytes& operator=(PixelBytes&&) noexcept = default;

    // Replaces the contents with `count` copies of `fill`.
    void Assign(engine_placeholder::usize count, engine_placeholder::byte fill) {
        bytes_.Clear();
        bytes_.Resize(count);
        for (engine_placeholder::usize i = 0; i < count; ++i) bytes_[i] = fill;
    }
    // Sets the size, zero-filling new bytes.
    void Resize(engine_placeholder::usize count) { bytes_.Resize(count); }
    engine_placeholder::byte* Data() { return bytes_.Data(); }
    const engine_placeholder::byte* Data() const { return bytes_.Data(); }
    engine_placeholder::usize Size() const { return bytes_.Size(); }
    bool Empty() const { return bytes_.Empty(); }
    engine_placeholder::byte& operator[](engine_placeholder::usize i) { return bytes_[i]; }
    const engine_placeholder::byte& operator[](engine_placeholder::usize i) const { return bytes_[i]; }
    bool operator==(const PixelBytes& other) const { return engine_test::SameBytes(bytes_, other.bytes_); }
    bool operator!=(const PixelBytes& other) const { return !(*this == other); }

    void assign(engine_placeholder::usize count, engine_placeholder::byte fill) { Assign(count, fill); }
    void resize(engine_placeholder::usize count) { Resize(count); }
    engine_placeholder::byte* data() { return Data(); }
    const engine_placeholder::byte* data() const { return Data(); }
    engine_placeholder::usize size() const { return Size(); }
    bool empty() const { return Empty(); }

private:
    void CopyFrom(const PixelBytes& other) {
        bytes_.Resize(other.bytes_.Size());
        for (engine_placeholder::usize i = 0; i < other.bytes_.Size(); ++i) bytes_[i] = other.bytes_[i];
    }
    engine_placeholder::Vector<engine_placeholder::byte> bytes_;
};

struct Image {
    unsigned width = 0;
    unsigned height = 0;
    PixelBytes rgba; // width * height * 4, row 0 at the top

    bool Valid() const { return width > 0 && height > 0 && rgba.Size() == static_cast<engine_placeholder::usize>(width) * height * 4; }
    const engine_placeholder::byte* Pixel(unsigned x, unsigned y) const { return &rgba[(static_cast<engine_placeholder::usize>(y) * width + x) * 4]; }
};

struct CompareOptions {
    int tolerance = 2;             // per-channel difference allowed on interior pixels
    int edge_step = 16;            // golden step between neighbours that makes a pixel an edge pixel
    int edge_tolerance = 64;       // per-channel difference allowed on edge pixels
    double max_edge_miss = 0.25;   // fraction of edge pixels allowed to exceed `edge_tolerance`
    double max_interior_miss = 0;  // fraction of interior pixels allowed to exceed `tolerance`
    int interior_miss_limit = 255; // no interior pixel may differ by more than this, even within the miss fraction
};

struct CompareResult {
    bool same = false;
    unsigned interior_misses = 0;
    unsigned edge_pixels = 0;
    unsigned edge_misses = 0;
    int max_difference = 0;
    engine_test::Text message = engine_test::MakeText(); // why it failed, with the diff image path when one was written
};

// Compares `actual` against `golden`. On failure writes a diff image (the amplified absolute difference) to the
// temp directory and names it in the result; `name` becomes part of the file name.
CompareResult Compare(const Image& golden, const Image& actual, const CompareOptions& options, const char* name);

// The path the diff image of `Compare(..., name)` is written to: `TempPath("diff_<name>.ppm")`.
engine_test::Text DiffPath(const char* name);

bool WritePpm(const char* path, const Image& image);
bool ReadPpm(const char* path, Image* image);

// True when ENGINE_RHI_UPDATE_GOLDEN=1: tests then write their output over the golden instead of comparing.
bool UpdatingGoldens();

// A path for a scratch file that is unique to this process (several test executables may run at once):
// $TMPDIR (or /tmp) / rhi_<pid>_<name>.
engine_test::Text TempPath(const char* name);

// The golden directory (the source tree's tests/rhi/golden).
engine_test::Text GoldenPath(const char* file);

} // namespace gpu_test
