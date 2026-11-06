#include "textures.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "rhi/rhi_log.h"

namespace demo {

using namespace engine_placeholder::rhi;

namespace {

constexpr char kMagic[4] = {'R', 'T', 'E', 'X'};
constexpr u32 kVersion = 1;
constexpr u32 kHeaderBytes = 24;
constexpr u32 kMaxDimension = 16384;

u32 LevelExtent(u32 base, u32 level) {
    const u32 e = base >> level;
    return e > 0 ? e : 1;
}

void PutU32(unsigned char* p, u32 v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<unsigned char>(v >> (8 * i));
}
u32 GetU32(const unsigned char* p) { return static_cast<u32>(p[0]) | static_cast<u32>(p[1]) << 8 | static_cast<u32>(p[2]) << 16 | static_cast<u32>(p[3]) << 24; }

// The formats these helpers handle: four 8-bit channels (the pixels are RGBA; BGRA formats are the caller's business).
bool FourByte(Format f) { return f == Format::RGBA8_UNorm || f == Format::RGBA8_SRGB || f == Format::BGRA8_UNorm || f == Format::BGRA8_SRGB; }

f32 SrgbToLinear(unsigned char c) {
    const f32 v = static_cast<f32>(c) / 255.0f;
    return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}
unsigned char LinearToSrgb(f32 l) {
    const f32 v = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
    const f32 c = v * 255.0f + 0.5f;
    return static_cast<unsigned char>(c < 0.0f ? 0.0f : (c > 255.0f ? 255.0f : c));
}

u64 PayloadBytes(u32 width, u32 height, u32 mips) {
    u64 total = 0;
    for (u32 m = 0; m < mips; ++m) {
        total += static_cast<u64>(LevelExtent(width, m)) * LevelExtent(height, m) * 4;
    }
    return total;
}

} // namespace

Pixels Checkerboard(u32 size, u32 cell, Rgba a, Rgba b) {
    Pixels p;
    p.width = p.height = size;
    p.rgba.resize(static_cast<usize>(size) * size * 4);
    for (u32 y = 0; y < size; ++y) {
        for (u32 x = 0; x < size; ++x) {
            const Rgba c = ((x / cell + y / cell) & 1) == 0 ? a : b;
            unsigned char* d = &p.rgba[(static_cast<usize>(y) * size + x) * 4];
            d[0] = c.r;
            d[1] = c.g;
            d[2] = c.b;
            d[3] = c.a;
        }
    }
    return p;
}

Pixels Solid(u32 width, u32 height, Rgba color) {
    Pixels p;
    p.width = width;
    p.height = height;
    p.rgba.resize(static_cast<usize>(width) * height * 4);
    for (usize i = 0; i < static_cast<usize>(width) * height; ++i) {
        std::memcpy(&p.rgba[i * 4], &color, 4);
    }
    return p;
}

Pixels UvGradient(u32 width, u32 height) {
    Pixels p;
    p.width = width;
    p.height = height;
    p.rgba.resize(static_cast<usize>(width) * height * 4);
    for (u32 y = 0; y < height; ++y) {
        for (u32 x = 0; x < width; ++x) {
            unsigned char* d = &p.rgba[(static_cast<usize>(y) * width + x) * 4];
            d[0] = static_cast<unsigned char>(width > 1 ? x * 255 / (width - 1) : 0);
            d[1] = static_cast<unsigned char>(height > 1 ? y * 255 / (height - 1) : 0);
            d[2] = 0;
            d[3] = 255;
        }
    }
    return p;
}

u32 FullMipCount(u32 width, u32 height) { return MaxMipLevels(width, height); }

TextureHandle CreateSampledTexture(Device* device, const Pixels& pixels, Format format, bool generate_mips, const char* debug_name) {
    if (!FourByte(format) || pixels.width == 0 || pixels.height == 0 || pixels.rgba.size() != static_cast<usize>(pixels.width) * pixels.height * 4) {
        RhiLogV(LogVerbosity::Error, "CreateSampledTexture: needs a 4-byte color format and matching pixels");
        return {};
    }
    const bool mips = generate_mips && FullMipCount(pixels.width, pixels.height) > 1 && device->CanGenerateMips(format);
    TextureDesc td;
    td.width = pixels.width;
    td.height = pixels.height;
    td.format = format;
    td.mip_levels = mips ? FullMipCount(pixels.width, pixels.height) : 1;
    td.usage = TextureUsage::Sampled | TextureUsage::CopyDst | (mips ? TextureUsage::CopySrc : TextureUsage::None);
    td.debug_name = debug_name;
    const TextureHandle texture = device->CreateTexture(td);
    if (!texture.IsValid()) {
        return {};
    }
    if (!device->UploadTexture(texture, 0, 0, pixels.rgba.data(), 0).ok) {
        device->DestroyTexture(texture);
        return {};
    }
    if (mips) {
        device->BeginFrame();
        CommandList* list = device->CreateCommandList(); // null when the device is lost
        bool submitted = false;
        if (list != nullptr) {
            list->Begin();
            list->GenerateMips(texture);
            list->End();
            CommandList* lists[] = {list};
            submitted = device->Submit(lists, 1) != 0;
        }
        device->EndFrame();
        device->WaitIdle();
        if (!submitted) {
            device->DestroyTexture(texture);
            return {};
        }
    }
    return texture;
}

RtexImage BuildRtex(const Pixels& pixels, Format format) {
    RtexImage out;
    out.width = pixels.width;
    out.height = pixels.height;
    out.format = format;
    out.mip_count = FullMipCount(pixels.width, pixels.height);
    out.data = pixels.rgba;
    usize prev_offset = 0;
    u32 pw = pixels.width;
    u32 ph = pixels.height;
    for (u32 m = 1; m < out.mip_count; ++m) {
        const u32 w = LevelExtent(pixels.width, m);
        const u32 h = LevelExtent(pixels.height, m);
        const usize offset = out.data.size();
        out.data.resize(offset + static_cast<usize>(w) * h * 4);
        for (u32 y = 0; y < h; ++y) {
            for (u32 x = 0; x < w; ++x) {
                for (u32 c = 0; c < 4; ++c) {
                    // Color channels of an sRGB format are averaged as linear light, alpha as it is.
                    const bool linear_light = FormatInfo(format).is_srgb && c < 3;
                    f32 sum = 0.0f;
                    for (u32 dy = 0; dy < 2; ++dy) {
                        for (u32 dx = 0; dx < 2; ++dx) {
                            const u32 sx = x * 2 + dx < pw ? x * 2 + dx : pw - 1;
                            const u32 sy = y * 2 + dy < ph ? y * 2 + dy : ph - 1;
                            const unsigned char v = out.data[prev_offset + (static_cast<usize>(sy) * pw + sx) * 4 + c];
                            sum += linear_light ? SrgbToLinear(v) : static_cast<f32>(v);
                        }
                    }
                    out.data[offset + (static_cast<usize>(y) * w + x) * 4 + c] =
                        linear_light ? LinearToSrgb(sum * 0.25f) : static_cast<unsigned char>(sum * 0.25f + 0.5f);
                }
            }
        }
        prev_offset = offset;
        pw = w;
        ph = h;
    }
    return out;
}

io::Status WritePpm(const io::Path& path, u32 width, u32 height, const unsigned char* rgba) {
    char header[64];
    const int header_length = std::snprintf(header, sizeof(header), "P6\n%u %u\n255\n", width, height);
    if (header_length <= 0) {
        return io::Status::Of(io::Error::Invalid);
    }
    const usize pixels = static_cast<usize>(width) * height;
    std::vector<unsigned char> bytes(static_cast<usize>(header_length) + pixels * 3);
    std::memcpy(bytes.data(), header, static_cast<usize>(header_length));
    for (usize i = 0; i < pixels; ++i) {
        std::memcpy(bytes.data() + header_length + i * 3, rgba + i * 4, 3);
    }
    return io::WriteFile(path, bytes.data(), bytes.size());
}

io::Status WriteRtex(const io::Path& path, const RtexImage& image) {
    if (!FourByte(image.format) || image.width == 0 || image.height == 0 || image.mip_count == 0 || image.data.size() != PayloadBytes(image.width, image.height, image.mip_count)) {
        return io::Status::Of(io::Error::Invalid);
    }
    std::vector<unsigned char> bytes(kHeaderBytes + image.data.size());
    std::memcpy(bytes.data(), kMagic, 4);
    PutU32(bytes.data() + 4, kVersion);
    PutU32(bytes.data() + 8, image.width);
    PutU32(bytes.data() + 12, image.height);
    PutU32(bytes.data() + 16, static_cast<u32>(image.format));
    PutU32(bytes.data() + 20, image.mip_count);
    std::memcpy(bytes.data() + kHeaderBytes, image.data.data(), image.data.size());
    return io::WriteFileAtomic(path, bytes.data(), bytes.size());
}

io::Status ReadRtex(const io::Path& path, RtexImage* image) {
    io::FilePtr file;
    if (io::Status s = io::File::Open(path, io::OpenMode::Read, io::kOpenNone, &file); s.Failed()) {
        return s;
    }
    unsigned char header[kHeaderBytes];
    if (io::Status s = io::ReadExact(*file, header, kHeaderBytes, 0); s.Failed()) {
        return s.Is(io::Error::EndOfFile) ? io::Status::Of(io::Error::Invalid) : s;
    }
    if (std::memcmp(header, kMagic, 4) != 0 || GetU32(header + 4) != kVersion) {
        return io::Status::Of(io::Error::Invalid);
    }
    RtexImage r;
    r.width = GetU32(header + 8);
    r.height = GetU32(header + 12);
    const u32 format = GetU32(header + 16);
    r.mip_count = GetU32(header + 20);
    bool ok = r.width > 0 && r.height > 0 && r.width <= kMaxDimension && r.height <= kMaxDimension && format > 0 &&
              format < static_cast<u32>(Format::Count) && r.mip_count > 0 && r.mip_count <= FullMipCount(r.width, r.height);
    r.format = ok ? static_cast<Format>(format) : Format::Undefined;
    ok = ok && FourByte(r.format);
    if (!ok) {
        return io::Status::Of(io::Error::Invalid);
    }
    // The file must hold exactly the payload before anything is allocated for it.
    const u64 payload = PayloadBytes(r.width, r.height, r.mip_count);
    u64 size = 0;
    if (io::Status s = file->Size(&size); s.Failed()) {
        return s;
    }
    if (size != kHeaderBytes + payload) {
        return io::Status::Of(io::Error::Invalid);
    }
    r.data.resize(static_cast<usize>(payload));
    if (io::Status s = io::ReadExact(*file, r.data.data(), r.data.size(), kHeaderBytes); s.Failed()) {
        return s.Is(io::Error::EndOfFile) ? io::Status::Of(io::Error::Invalid) : s;
    }
    *image = static_cast<RtexImage&&>(r);
    return io::Status::Success();
}

TextureHandle CreateTextureFromRtex(Device* device, const RtexImage& image, const char* debug_name) {
    if (!FourByte(image.format) || image.data.size() != PayloadBytes(image.width, image.height, image.mip_count)) {
        RhiLogV(LogVerbosity::Error, "CreateTextureFromRtex: the image is malformed");
        return {};
    }
    TextureDesc td;
    td.width = image.width;
    td.height = image.height;
    td.format = image.format;
    td.mip_levels = image.mip_count;
    td.usage = TextureUsage::Sampled | TextureUsage::CopyDst;
    td.debug_name = debug_name;
    const TextureHandle texture = device->CreateTexture(td);
    if (!texture.IsValid()) {
        return {};
    }
    usize offset = 0;
    for (u32 m = 0; m < image.mip_count; ++m) {
        if (!device->UploadTexture(texture, m, 0, image.data.data() + offset, 0).ok) {
            device->DestroyTexture(texture);
            return {};
        }
        offset += static_cast<usize>(LevelExtent(image.width, m)) * LevelExtent(image.height, m) * 4;
    }
    return texture;
}

} // namespace demo
