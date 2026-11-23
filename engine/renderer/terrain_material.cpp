#include "renderer/terrain_material.h"

#include "lang/scalar.h"

namespace vse::renderer
{

namespace
{

f32 TmSmoothStep(f32 a, f32 b, f32 x)
{
    const f32 t = FMin(FMax((x - a) / (b - a), 0.0f), 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

}  // namespace

const char* TerrainMaterialInvalid(const TerrainMaterial& m)
{
    if (m.layer_count < 1 || m.layer_count > kMaxTerrainLayers)
    {
        return "the layer count is not 1 to 4";
    }
    for (u32 i = 0; i < m.layer_count; ++i)
    {
        const TerrainLayer& l = m.layers[i];
        if (!(l.tiling > 0.0f) || !IsFinite(l.tiling))
        {
            return "a layer's tiling is not above 0";
        }
        if (!(l.tint.x >= 0.0f) || !(l.tint.y >= 0.0f) || !(l.tint.z >= 0.0f) || !IsFinite(l.tint.x) ||
            !IsFinite(l.tint.y) || !IsFinite(l.tint.z))
        {
            return "a layer's tint is negative or not finite";
        }
        if (!(l.roughness >= 0.0f && l.roughness <= 1.0f) || !(l.metallic >= 0.0f && l.metallic <= 1.0f))
        {
            return "a layer's roughness or metallic is outside 0 to 1";
        }
    }
    if (m.slope_layer < -1 || m.slope_layer >= static_cast<i32>(m.layer_count))
    {
        return "the slope layer is not one of the layers";
    }
    if (!(m.slope_start >= 0.0f) || !(m.slope_end > m.slope_start) || !(m.slope_end <= 1.0f))
    {
        return "the slope range is not 0 <= start < end <= 1";
    }
    if (!(m.fade_start >= 0.0f) || !(m.fade_end > m.fade_start) || !IsFinite(m.fade_end))
    {
        return "the fade range is not 0 <= start < end";
    }
    if (m.layer_array)
    {
        const rhi::TextureDesc& d = m.layer_array->Desc();
        if (d.format != rhi::Format::RGBA8_SRGB || d.dimension != rhi::TextureDimension::Tex2D)
        {
            return "the layer array is not a 2D RGBA8_SRGB texture";
        }
        if (d.array_layers < 2)
        {
            return "the layer array has one layer, which is not an array texture; give it at least 2";
        }
        if (d.array_layers > kMaxTerrainLayers)
        {
            return "the layer array has more than 4 layers";
        }
        if (d.array_layers < m.layer_count)
        {
            return "the layer array has fewer layers than the material uses";
        }
    }
    if (m.splat && (m.splat->Desc().format != rhi::Format::RGBA8_UNorm || m.splat->Desc().array_layers != 1 ||
                    m.splat->Desc().dimension != rhi::TextureDimension::Tex2D))
    {
        return "the splat map is not a single 2D RGBA8_UNorm texture";
    }
    return nullptr;
}

void TerrainNormalizeWeights(u8 weights[4])
{
    const u32 sum = static_cast<u32>(weights[0]) + weights[1] + weights[2] + weights[3];
    if (sum == 0)
    {
        weights[0] = 255;
        return;
    }
    u32 scaled[4];
    u32 rem[4];
    u32 total = 0;
    for (u32 i = 0; i < 4; ++i)
    {
        const u32 v = static_cast<u32>(weights[i]) * 255u;
        scaled[i] = v / sum;
        rem[i] = v % sum;
        total += scaled[i];
    }
    while (total < 255)
    {
        u32 best = 0;
        for (u32 i = 1; i < 4; ++i)
        {
            if (rem[i] > rem[best])
            {
                best = i;
            }
        }
        ++scaled[best];
        rem[best] = 0;
        ++total;
    }
    for (u32 i = 0; i < 4; ++i)
    {
        weights[i] = static_cast<u8>(scaled[i]);
    }
}

void TerrainLayerWeights(const TerrainMaterial& m, const f32 splat[4], f32 normal_y, f32 out[4])
{
    f32 sum = 0.0f;
    for (u32 i = 0; i < 4; ++i)
    {
        out[i] = i < m.layer_count ? FMax(splat[i], 0.0f) : 0.0f;
        sum += out[i];
    }
    if (sum <= 0.0f)
    {
        out[0] = 1.0f;
    }
    else
    {
        for (u32 i = 0; i < 4; ++i)
        {
            out[i] /= sum;
        }
    }
    if (m.slope_layer >= 0)
    {
        const f32 t = TmSmoothStep(m.slope_start, m.slope_end, 1.0f - normal_y);
        for (u32 i = 0; i < 4; ++i)
        {
            out[i] *= 1.0f - t;
        }
        out[m.slope_layer] += t;
    }
}

}  // namespace vse::renderer
