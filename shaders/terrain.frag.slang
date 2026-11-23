// terrain.frag: the terrain's G-buffer fragment shader, writing the four targets the mesh pass writes (see gbuffer.frag).
//
// Math (the CPU twin is renderer::TerrainLayerWeights)
//   w          the splat texel's four weights, those of layers at or above layer_count zeroed, renormalised (all zero: layer 0)
//   slope      t = smoothstep(slope_start, slope_end, 1 - normal.y); w' = w * (1 - t), then w'[slope_layer] += t
//   fade       f = saturate((distance - fade_start) / (fade_end - fade_start))
//   layer i    detail = layers[uv = xz / tiling_i, i]; far = the same at the last mip (the layer's average colour)
//              texel = lerp(detail, far, f)
//   albedo     = sum w'_i * texel_i.rgb * tint_i;  roughness = sum w'_i * texel_i.a * roughness_i;  metallic = sum w'_i * metallic_i
//   flags 0, occlusion none, no emission: the terrain has none of a mesh material's slots
//
// Interface
//   input  location 0: float3 world normal; 1: float2 uv (0..1 across the terrain); 2: float4 (terrain x, terrain z, distance, 0)
//   set 1, binding 3: uniform buffer TerrainParams; 4: splat texture (RGBA8); 5: layer array (RGBA8 sRGB); 6: repeat sampler; 7: clamp sampler

#include "target_common.slang"
#include "gbuffer_common.slang"

struct TerrainParams {
    float4 tiling;         // metres a repeat, per layer
    float4 tint_rough[4];  // per layer: tint rgb, roughness scale
    float4 metallic;       // per layer
    float4 slope;          // slope layer (-1 none), start, end, layer count
    float4 fade;           // fade start, fade end, last mip of the layer array, unused
};

[[vk::binding(3, 1)]] ConstantBuffer<TerrainParams> terrain ENGINE_REGISTER(b1, space1);
[[vk::binding(4, 1)]] Texture2D<float4> splat_tex ENGINE_REGISTER(t4, space1);
[[vk::binding(5, 1)]] Texture2DArray<float4> layer_tex ENGINE_REGISTER(t5, space1);
[[vk::binding(6, 1)]] SamplerState repeat_sampler ENGINE_REGISTER(s6, space1);
[[vk::binding(7, 1)]] SamplerState clamp_sampler ENGINE_REGISTER(s7, space1);

struct FragmentInput {
    [[vk::location(0)]] float3 normal : NORMAL;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float4 terrain_pos : TANGENT;
};

struct FragmentOutput {
    [[vk::location(0)]] float4 albedo : SV_Target0;
    [[vk::location(1)]] float2 normal : SV_Target1;
    [[vk::location(2)]] float4 material : SV_Target2;
    [[vk::location(3)]] float4 emissive : SV_Target3;
};

[shader("fragment")]
FragmentOutput main(FragmentInput input) {
    const float3 n = normalize(input.normal);
    const uint count = uint(terrain.slope.w);
    float4 s = splat_tex.Sample(clamp_sampler, input.uv);
    float w[4] = {s.r, s.g, s.b, s.a};
    float sum = 0.0;
    for (uint i = 0; i < 4; ++i) {
        w[i] = i < count ? max(w[i], 0.0) : 0.0;
        sum += w[i];
    }
    if (sum <= 0.0) {
        w[0] = 1.0;
        sum = 1.0;
    }
    float t = 0.0;
    if (terrain.slope.x >= 0.0) {
        t = smoothstep(terrain.slope.y, terrain.slope.z, 1.0 - n.y);
    }
    for (uint i = 0; i < 4; ++i) {
        w[i] = w[i] / sum * (1.0 - t);
        if (float(i) == terrain.slope.x) {
            w[i] += t;
        }
    }
    const float fade = saturate((input.terrain_pos.z - terrain.fade.x) / max(terrain.fade.y - terrain.fade.x, 1e-4));
    float3 albedo = float3(0.0, 0.0, 0.0);
    float roughness = 0.0;
    float metallic = 0.0;
    for (uint i = 0; i < 4; ++i) {
        // every layer is sampled, whatever its weight: an implicit-derivative sample inside a branch that pixels of a
        // quad take differently has no defined mip level
        const float3 uvw = float3(input.terrain_pos.xy / terrain.tiling[i], min(float(i), terrain.fade.w));  // an unused layer reads the last one, weight 0
        const float4 detail = layer_tex.Sample(repeat_sampler, uvw);
        const float4 far_texel = layer_tex.SampleLevel(repeat_sampler, uvw, terrain.fade.z);
        const float4 texel = lerp(detail, far_texel, fade);
        albedo += w[i] * texel.rgb * terrain.tint_rough[i].rgb;
        roughness += w[i] * texel.a * terrain.tint_rough[i].a;
        metallic += w[i] * terrain.metallic[i];
    }
    FragmentOutput output;
    output.albedo = float4(albedo, 1.0);
    output.normal = OctEncode(n);
    output.material = float4(roughness, metallic, 0.0, 0.0);
    output.emissive = float4(0.0, 0.0, 0.0, 1.0);
    return output;
}
