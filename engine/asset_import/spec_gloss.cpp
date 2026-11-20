#include "asset_import/spec_gloss.h"

#include "algorithm.h"
#include "lang/scalar.h"
#include "lang/status_try.h"
#include "log/assert.h"

namespace engine_placeholder::asset
{

namespace
{

constexpr f32 kDielectricSpecular = 0.04f;  // the reflectance of a dielectric at normal incidence
constexpr f32 kEpsilon = 1e-6f;
constexpr u32 kEncodeSteps = 4096;  // linear to sRGB: a table of 4096 steps is finer than 8 bits everywhere

f32 PerceivedBrightness(f32 r, f32 g, f32 b)
{
    return Sqrt(0.299f * r * r + 0.587f * g * g + 0.114f * b * b);
}

// The metallic value that gives `specular`'s brightness with `diffuse`'s: the root of the Khronos quadratic.
f32 SolveMetallic(f32 diffuse, f32 specular, f32 one_minus_specular_strength)
{
    if (specular < kDielectricSpecular)
    {
        return 0.0f;
    }
    const f32 a = kDielectricSpecular;
    const f32 b = diffuse * one_minus_specular_strength / (1.0f - a) + specular - 2.0f * a;
    const f32 c = a - specular;
    const f32 d = Max(b * b - 4.0f * a * c, 0.0f);
    return Clamp((-b + Sqrt(d)) / (2.0f * a), 0.0f, 1.0f);
}

f32 SrgbToLinear(f32 c)
{
    return c <= 0.04045f ? c / 12.92f : Pow((c + 0.055f) / 1.055f, 2.4f);
}

f32 LinearToSrgb(f32 l)
{
    return l <= 0.0031308f ? l * 12.92f : 1.055f * Pow(l, 1.0f / 2.4f) - 0.055f;
}

struct SrgbTables
{
    Array<f32, 256> decode;          // an sRGB byte to linear
    Array<u8, kEncodeSteps> encode;  // linear in 1/4095 steps to an sRGB byte
};

void MakeTables(SrgbTables* out)
{
    for (u32 i = 0; i < 256; ++i)
    {
        out->decode[i] = SrgbToLinear(static_cast<f32>(i) / 255.0f);
    }
    for (u32 i = 0; i < kEncodeSteps; ++i)
    {
        const f32 s = LinearToSrgb(static_cast<f32>(i) / static_cast<f32>(kEncodeSteps - 1));
        out->encode[i] = static_cast<u8>(Round(Clamp(s, 0.0f, 1.0f) * 255.0f));
    }
}

u8 EncodeSrgb(const SrgbTables& tables, f32 linear)
{
    return tables.encode[static_cast<u32>(Round(Clamp(linear, 0.0f, 1.0f) * static_cast<f32>(kEncodeSteps - 1)))];
}

u8 EncodeLinear(f32 value)
{
    return static_cast<u8>(Round(Clamp(value, 0.0f, 1.0f) * 255.0f));
}

// The image's sample at the centre of texel (x, y) of a w by h output: RGB decoded from sRGB, A linear. Bilinear,
// clamped at the edges; exact when the image is the output's size.
void Sample(const DecodedImage& image, const SrgbTables& tables, u32 x, u32 y, u32 w, u32 h, f32 out[4])
{
    const byte* rgba = image.rgba.Data();
    if (image.width == w && image.height == h)
    {
        const byte* p = rgba + (static_cast<usize>(y) * w + x) * 4;
        out[0] = tables.decode[static_cast<u8>(p[0])];
        out[1] = tables.decode[static_cast<u8>(p[1])];
        out[2] = tables.decode[static_cast<u8>(p[2])];
        out[3] = static_cast<f32>(static_cast<u8>(p[3])) / 255.0f;
        return;
    }
    const f32 max_u = static_cast<f32>(image.width - 1);
    const f32 max_v = static_cast<f32>(image.height - 1);
    const f32 u = Clamp((static_cast<f32>(x) + 0.5f) * static_cast<f32>(image.width) / static_cast<f32>(w) - 0.5f, 0.0f,
                        max_u);
    const f32 v = Clamp((static_cast<f32>(y) + 0.5f) * static_cast<f32>(image.height) / static_cast<f32>(h) - 0.5f,
                        0.0f, max_v);
    const u32 x0 = static_cast<u32>(Floor(u));
    const u32 y0 = static_cast<u32>(Floor(v));
    const u32 x1 = Min(x0 + 1, image.width - 1);
    const u32 y1 = Min(y0 + 1, image.height - 1);
    const f32 fx = u - static_cast<f32>(x0);
    const f32 fy = v - static_cast<f32>(y0);
    const u32 xs[4] = {x0, x1, x0, x1};
    const u32 ys[4] = {y0, y0, y1, y1};
    const f32 weights[4] = {(1.0f - fx) * (1.0f - fy), fx * (1.0f - fy), (1.0f - fx) * fy, fx * fy};
    out[0] = out[1] = out[2] = out[3] = 0.0f;
    for (u32 k = 0; k < 4; ++k)
    {
        const byte* p = rgba + (static_cast<usize>(ys[k]) * image.width + xs[k]) * 4;
        out[0] += weights[k] * tables.decode[static_cast<u8>(p[0])];
        out[1] += weights[k] * tables.decode[static_cast<u8>(p[1])];
        out[2] += weights[k] * tables.decode[static_cast<u8>(p[2])];
        out[3] += weights[k] * static_cast<f32>(static_cast<u8>(p[3])) / 255.0f;
    }
}

Status MakeImage(u32 w, u32 h, DecodedImage* out)
{
    *out = DecodedImage();
    out->width = w;
    out->height = h;
    if (!out->rgba.TryResize(static_cast<usize>(w) * h * 4))
    {
        return Status::Make(ErrorCode::LimitExceeded, "out of memory");
    }
    return Status::Success();
}

}  // namespace

MetalRoughSample SpecGlossToMetalRough(const SpecGlossSample& in)
{
    const f32 a = kDielectricSpecular;
    const Array<f32, 4>& d = in.diffuse;
    const Array<f32, 3>& s = in.specular;
    const f32 one_minus_specular_strength = Max(1.0f - Max(Max(s[0], s[1]), s[2]), 0.0f);
    const f32 metallic = SolveMetallic(PerceivedBrightness(d[0], d[1], d[2]), PerceivedBrightness(s[0], s[1], s[2]),
                                       one_minus_specular_strength);
    const f32 blend = metallic * metallic;
    MetalRoughSample out;
    for (u32 c = 0; c < 3; ++c)
    {
        const f32 from_diffuse = d[c] * one_minus_specular_strength / (1.0f - a) / Max(1.0f - metallic, kEpsilon);
        const f32 from_specular = (s[c] - a * (1.0f - metallic)) / Max(metallic, kEpsilon);
        out.base_color[c] = Clamp(from_diffuse + (from_specular - from_diffuse) * blend, 0.0f, 1.0f);
    }
    out.base_color[3] = d[3];
    out.metallic = metallic;
    out.roughness = 1.0f - Clamp(in.glossiness, 0.0f, 1.0f);
    return out;
}

bool SpecularIsDielectric(const Array<f32, 3>& specular)
{
    return PerceivedBrightness(specular[0], specular[1], specular[2]) < kDielectricSpecular;
}

Status ConvertSpecGlossImages(const SpecGlossImages& in, u64 max_pixels, DecodedImage* base_color,
                              DecodedImage* metal_rough)
{
    ENGINE_ASSERT(in.diffuse != nullptr || in.specular_glossiness != nullptr, "a conversion needs an image");
    u32 w = 0;
    u32 h = 0;
    const Array<const DecodedImage*, 2> images = {{in.diffuse, in.specular_glossiness}};
    for (const DecodedImage* image : images)
    {
        if (image != nullptr)
        {
            w = Max(w, image->width);
            h = Max(h, image->height);
        }
    }
    if (static_cast<u64>(w) * h > max_pixels)
    {
        return Status::Make(ErrorCode::TooLarge, "the converted texture is above the pixel limit");
    }
    if (base_color != nullptr)
    {
        ENGINE_TRY(MakeImage(w, h, base_color));
    }
    if (metal_rough != nullptr)
    {
        ENGINE_TRY(MakeImage(w, h, metal_rough));
    }
    SrgbTables tables;
    MakeTables(&tables);
    bool has_alpha = false;
    for (u32 y = 0; y < h; ++y)
    {
        for (u32 x = 0; x < w; ++x)
        {
            f32 diffuse[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            f32 specular[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            if (in.diffuse != nullptr)
            {
                Sample(*in.diffuse, tables, x, y, w, h, diffuse);
            }
            if (in.specular_glossiness != nullptr)
            {
                Sample(*in.specular_glossiness, tables, x, y, w, h, specular);
            }
            SpecGlossSample sample;
            for (u32 c = 0; c < 4; ++c)
            {
                sample.diffuse[c] = diffuse[c] * in.diffuse_factor[c];
            }
            for (u32 c = 0; c < 3; ++c)
            {
                sample.specular[c] = specular[c] * in.specular_factor[c];
            }
            sample.glossiness = specular[3] * in.glossiness_factor;
            const MetalRoughSample converted = SpecGlossToMetalRough(sample);
            const usize at = (static_cast<usize>(y) * w + x) * 4;
            if (base_color != nullptr)
            {
                byte* p = base_color->rgba.Data() + at;
                p[0] = static_cast<byte>(EncodeSrgb(tables, converted.base_color[0]));
                p[1] = static_cast<byte>(EncodeSrgb(tables, converted.base_color[1]));
                p[2] = static_cast<byte>(EncodeSrgb(tables, converted.base_color[2]));
                p[3] = static_cast<byte>(EncodeLinear(converted.base_color[3]));
                has_alpha = has_alpha || static_cast<u8>(p[3]) != 255;
            }
            if (metal_rough != nullptr)
            {
                byte* p = metal_rough->rgba.Data() + at;
                p[0] = static_cast<byte>(255);
                p[1] = static_cast<byte>(EncodeLinear(converted.roughness));
                p[2] = static_cast<byte>(EncodeLinear(converted.metallic));
                p[3] = static_cast<byte>(255);
            }
        }
    }
    if (base_color != nullptr)
    {
        base_color->has_alpha = has_alpha;
    }
    return Status::Success();
}

}  // namespace engine_placeholder::asset
