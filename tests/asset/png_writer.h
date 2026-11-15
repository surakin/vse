#pragma once

// Phase 22 Day 338: a minimal PNG writer for the importer's tests: stored (uncompressed) deflate blocks, no filtering,
// optional palette, transparency and Adam7 interlacing for 8-bit RGBA.

#include "containers/vector.h"
#include "types.h"

namespace engine_placeholder::asset::test
{

inline u32 PngCrc(const byte* data, usize size, u32 crc = 0xffffffffu)
{
    for (usize i = 0; i < size; ++i)
    {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k)
        {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return crc;
}

inline void PngPut32(Vector<byte>* out, u32 value)
{
    out->Push(static_cast<byte>(value >> 24));
    out->Push(static_cast<byte>(value >> 16));
    out->Push(static_cast<byte>(value >> 8));
    out->Push(static_cast<byte>(value));
}

inline void PngChunk(Vector<byte>* out, const char* type, const Vector<byte>& data)
{
    PngPut32(out, static_cast<u32>(data.Size()));
    Vector<byte> body;
    for (int i = 0; i < 4; ++i)
    {
        body.Push(static_cast<byte>(type[i]));
    }
    for (usize i = 0; i < data.Size(); ++i)
    {
        body.Push(data[i]);
    }
    for (usize i = 0; i < body.Size(); ++i)
    {
        out->Push(body[i]);
    }
    PngPut32(out, ~PngCrc(body.Data(), body.Size()));
}

// `scanlines`: every row with its filter byte (0). Returns a zlib stream of stored blocks.
inline Vector<byte> PngZlib(const Vector<byte>& scanlines)
{
    Vector<byte> out;
    out.Push(0x78);
    out.Push(0x01);
    usize offset = 0;
    do
    {
        const usize n = scanlines.Size() - offset < 65535 ? scanlines.Size() - offset : 65535;
        const bool last = offset + n == scanlines.Size();
        out.Push(last ? 1 : 0);
        out.Push(static_cast<byte>(n));
        out.Push(static_cast<byte>(n >> 8));
        out.Push(static_cast<byte>(~n));
        out.Push(static_cast<byte>((~n) >> 8));
        for (usize i = 0; i < n; ++i)
        {
            out.Push(scanlines[offset + i]);
        }
        offset += n;
    } while (offset < scanlines.Size());
    u32 a = 1;
    u32 b = 0;
    for (usize i = 0; i < scanlines.Size(); ++i)
    {
        a = (a + scanlines[i]) % 65521;
        b = (b + a) % 65521;
    }
    PngPut32(&out, (b << 16) | a);
    return out;
}

struct PngSpec
{
    u32 width = 1;
    u32 height = 1;
    int color_type = 6;  // 0 gray, 2 RGB, 3 palette, 4 gray-alpha, 6 RGBA
    int bit_depth = 8;
    bool interlaced = false;    // only for 8-bit RGBA
    Vector<byte> rows;          // the packed bytes of every row, rows one after the other, no filter bytes
    Vector<byte> palette;       // PLTE: r, g, b per entry
    Vector<byte> transparency;  // tRNS
};

inline Vector<byte> MakePng(const PngSpec& spec)
{
    Vector<byte> file;
    const byte signature[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    for (byte b : signature)
    {
        file.Push(b);
    }
    Vector<byte> header;
    PngPut32(&header, spec.width);
    PngPut32(&header, spec.height);
    header.Push(static_cast<byte>(spec.bit_depth));
    header.Push(static_cast<byte>(spec.color_type));
    header.Push(0);
    header.Push(0);
    header.Push(spec.interlaced ? 1 : 0);
    PngChunk(&file, "IHDR", header);
    if (spec.palette.Size() != 0)
    {
        PngChunk(&file, "PLTE", spec.palette);
    }
    if (spec.transparency.Size() != 0)
    {
        PngChunk(&file, "tRNS", spec.transparency);
    }
    Vector<byte> scanlines;
    if (!spec.interlaced)
    {
        const usize row_bytes = spec.rows.Size() / spec.height;
        for (u32 y = 0; y < spec.height; ++y)
        {
            scanlines.Push(0);
            for (usize i = 0; i < row_bytes; ++i)
            {
                scanlines.Push(spec.rows[y * row_bytes + i]);
            }
        }
    }
    else
    {
        static const u32 passes[7][4] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4},
                                         {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};
        for (const auto& pass : passes)
        {
            if (pass[0] >= spec.width || pass[1] >= spec.height)
            {
                continue;
            }
            for (u32 y = pass[1]; y < spec.height; y += pass[3])
            {
                scanlines.Push(0);
                for (u32 x = pass[0]; x < spec.width; x += pass[2])
                {
                    for (u32 c = 0; c < 4; ++c)
                    {
                        scanlines.Push(spec.rows[(static_cast<usize>(y) * spec.width + x) * 4 + c]);
                    }
                }
            }
        }
    }
    PngChunk(&file, "IDAT", PngZlib(scanlines));
    PngChunk(&file, "IEND", Vector<byte>());
    return file;
}

}  // namespace engine_placeholder::asset::test
