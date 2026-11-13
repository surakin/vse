// material_test.frag: a material with one parameter block, two textures and one sampler, in set 1 (the renderer's
// convention: set 0 frame, set 1 material, set 2 objects). Used by the material tests.
//
// Math (linear light)
//   t      = (uv_transform * float4(uv, 0, 1)).xy
//   albedo = albedo_tex.Sample(linear_sampler, t)
//   n      = normal_tex.Sample(...).xyz * 2 - 1           the default (flat) normal texture decodes to (0, 0, 1)
//   rgb    = (base_color.rgb * albedo.rgb + emissive) * (1 - 0.5 * roughness)
//   alpha  = base_color.a * n.z
//   flags bit 0 inverts rgb: rgb = 1 - rgb; bit 1 shows the transformed uv instead: rgb = (t, 0)
// With the default white albedo and flat normal textures: rgb = (base_color.rgb + emissive) * (1 - 0.5 * roughness),
// alpha = base_color.a. Option TINT multiplies rgb by (1, 0.5, 0.5) last.
//
// Interface
//   input  location 0: float3 world normal (unused beyond the interface)
//   input  location 1: float2 uv
//   input  location 2: float4 tangent (unused)
//   output location 0: float4 color
//   set 1, binding 0: uniform buffer MaterialParams (std140, and the same in Metal's layout):
//       float4 base_color @0; float roughness @16; uint flags @20; float3 emissive @32; float4x4 uv_transform @48;
//       (112 bytes). Nothing follows the float3 in its 16 bytes: Metal's float3 fills them (shaders/README.md).
//   set 1, binding 1: sampled texture albedo_tex
//   set 1, binding 2: sampled texture normal_tex
//   set 1, binding 3: sampler linear_sampler

// permutation-options: TINT

#include "target_common.slang"  // ENGINE_REGISTER

struct MaterialParams {
    float4 base_color;
    float roughness;
    uint flags;
    float3 emissive;
    float4x4 uv_transform;
};

[[vk::binding(0, 1)]] ConstantBuffer<MaterialParams> material ENGINE_REGISTER(b0, space1);
[[vk::binding(1, 1)]] Texture2D albedo_tex ENGINE_REGISTER(t1, space1);
[[vk::binding(2, 1)]] Texture2D normal_tex ENGINE_REGISTER(t2, space1);
[[vk::binding(3, 1)]] SamplerState linear_sampler ENGINE_REGISTER(s3, space1);

[shader("fragment")]
float4 main([[vk::location(0)]] float3 world_normal : NORMAL, [[vk::location(1)]] float2 uv : TEXCOORD0,
            [[vk::location(2)]] float4 tangent : TANGENT) : SV_Target0 {
    float2 t = mul(material.uv_transform, float4(uv, 0.0, 1.0)).xy;
    float4 albedo = albedo_tex.Sample(linear_sampler, t);
    float3 n = normal_tex.Sample(linear_sampler, uv).xyz * 2.0 - 1.0;
    float3 rgb = (material.base_color.rgb * albedo.rgb + material.emissive) * (1.0 - 0.5 * material.roughness);
    if ((material.flags & 1u) != 0u) {
        rgb = 1.0 - rgb;
    }
    if ((material.flags & 2u) != 0u) {
        rgb = float3(t, 0.0);
    }
#ifdef TINT
    rgb *= float3(1.0, 0.5, 0.5);
#endif
    return float4(rgb, material.base_color.a * n.z);
}
