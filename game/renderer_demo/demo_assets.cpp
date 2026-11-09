#include "demo_assets.h"

#include "lang/scalar.h"

namespace renderer_demo {

namespace {

constexpr u32 kSize = 256;

byte Byte(f32 v) {
    const f32 c = v * 255.0f + 0.5f;
    return static_cast<byte>(c < 0.0f ? 0.0f : (c > 255.0f ? 255.0f : c));
}

} // namespace

demo::Pixels MakeCheckerPixels() { return demo::Checkerboard(kSize, 32, {235, 200, 90, 255}, {40, 60, 120, 255}); }

demo::Pixels MakeStripePixels() {
    demo::Pixels p;
    p.width = kSize;
    p.height = kSize;
    p.rgba.Resize(static_cast<usize>(kSize) * kSize * 4);
    for (u32 y = 0; y < kSize; ++y) {
        for (u32 x = 0; x < kSize; ++x) {
            // A smooth triangle wave along the diagonal: 8 stripes per tile, so the tile repeats seamlessly.
            const f32 t = static_cast<f32>((x + y) % (kSize / 8 * 2)) / static_cast<f32>(kSize / 8 * 2);
            const f32 wave = t < 0.5f ? t * 2.0f : 2.0f - t * 2.0f;
            const f32 k = wave * wave * (3.0f - 2.0f * wave);
            byte* px = &p.rgba[(static_cast<usize>(y) * kSize + x) * 4];
            px[0] = Byte(0.75f + 0.2f * k);
            px[1] = Byte(0.12f + 0.75f * k);
            px[2] = Byte(0.1f + 0.65f * k);
            px[3] = 255;
        }
    }
    return p;
}

demo::Pixels MakeBumpNormalPixels() {
    demo::Pixels p;
    p.width = kSize;
    p.height = kSize;
    p.rgba.Resize(static_cast<usize>(kSize) * kSize * 4);
    constexpr u32 kCell = kSize / 4;
    for (u32 y = 0; y < kSize; ++y) {
        for (u32 x = 0; x < kSize; ++x) {
            // Each cell is a hemispherical bump of radius 0.9 cell: the normal of a sphere at the point under the pixel.
            const f32 u = (static_cast<f32>(x % kCell) + 0.5f) / static_cast<f32>(kCell) * 2.0f - 1.0f;
            const f32 v = (static_cast<f32>(y % kCell) + 0.5f) / static_cast<f32>(kCell) * 2.0f - 1.0f;
            const f32 r2 = (u * u + v * v) / (0.9f * 0.9f);
            f32 nx = 0.0f, ny = 0.0f, nz = 1.0f;
            if (r2 < 1.0f) {
                const f32 h = engine_placeholder::Sqrt(1.0f - r2);
                // The normal of a sphere of radius 0.9 (in cell units) at (u, v): proportional to (u, v, height), height = 0.9 * h.
                const f32 sx = u / 0.9f;
                const f32 sy = v / 0.9f; // v grows toward +V (down the image), which is the bitangent direction
                const f32 len = engine_placeholder::Sqrt(sx * sx + sy * sy + h * h);
                nx = sx / len;
                ny = sy / len;
                nz = h / len;
            }
            byte* px = &p.rgba[(static_cast<usize>(y) * kSize + x) * 4];
            px[0] = Byte(nx * 0.5f + 0.5f);
            px[1] = Byte(ny * 0.5f + 0.5f);
            px[2] = Byte(nz * 0.5f + 0.5f);
            px[3] = 255;
        }
    }
    return p;
}

const DemoTexture* DemoTextures(u32* count) {
    static const DemoTexture kTextures[] = {
        {"checker.rtex", rhi::Format::RGBA8_SRGB, &MakeCheckerPixels},
        {"stripes.rtex", rhi::Format::RGBA8_SRGB, &MakeStripePixels},
        {"normal.rtex", rhi::Format::RGBA8_UNorm, &MakeBumpNormalPixels},
    };
    *count = static_cast<u32>(sizeof(kTextures) / sizeof(kTextures[0]));
    return kTextures;
}

} // namespace renderer_demo
