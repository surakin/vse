#include "asset/terrain_splat.h"

#include "renderer/terrain_material.h"

namespace vse::asset
{

Status NormalizeSplatImage(u32 width, u32 height, ByteSpan rgba, Vector<byte>* out)
{
    if (width < 1 || width > 16384 || height < 1 || height > 16384)
    {
        return Status::Make(ErrorCode::Invalid, "the splat image size is not 1 to 16384");
    }
    const usize bytes = static_cast<usize>(width) * height * 4;
    if (rgba.Size() != bytes)
    {
        return Status::Make(ErrorCode::Invalid, "the splat image is not width * height * 4 bytes");
    }
    Vector<byte> result;
    if (!result.TryResize(bytes))
    {
        return Status::Make(ErrorCode::LimitExceeded, "out of memory for the splat image");
    }
    for (usize i = 0; i < bytes; i += 4)
    {
        u8 w[4] = {static_cast<u8>(rgba[i]), static_cast<u8>(rgba[i + 1]), static_cast<u8>(rgba[i + 2]),
                   static_cast<u8>(rgba[i + 3])};
        renderer::TerrainNormalizeWeights(w);
        for (u32 c = 0; c < 4; ++c)
        {
            result[i + c] = static_cast<byte>(w[c]);
        }
    }
    *out = Move(result);
    return Status::Success();
}

}  // namespace vse::asset
