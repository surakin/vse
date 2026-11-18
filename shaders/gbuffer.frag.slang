// gbuffer.frag: the G-buffer geometry pass's fragment shader. Writes three render targets (renderer R11):
//   target 0 albedo   RGBA8_SRGB     rgb = linear albedo (the attachment encodes sRGB on write), a = baked occlusion
//   target 1 normal   RG16_Float     world-space normal, octahedral (see gbuffer_common.slang)
//   target 2 material RGBA8_UNorm    r = roughness, g = metallic, b = emissive scale, a = flags / 255
//
// Math (linear light)
//   albedo = base_color.rgb * sample(albedo_tex, uv').rgb,  uv' = uv * albedo_uv.xy + albedo_uv.zw  (default (1,1,0,0))
//   albedo_uv_outside.a > 0: where uv' leaves 0..1 the texel is albedo_uv_outside.rgb (linear) instead, and inside the
//       picture uv' is kept half a texel from its edge, so the filter never reads across the edge (no repeat bleed)
//   VERTEX_COLOR:  albedo *= SrgbToLinear(vertex_color.rgb)         (vertex colors are authored in sRGB)
//   ALPHA_TEST:    discard when base_color.a * sample(albedo_tex, uv).a * vertex_color.a < alpha_cutoff
//   N = normalize(world normal); NORMAL_MAP: t = sample(normal_tex, uv).xyz * 2 - 1,
//       T = normalize(world tangent.xyz - N * dot(N, tangent.xyz)), B = cross(N, T) * tangent.w,
//       N = normalize(T * t.x + B * t.y + N * t.z)
//   emissive scale and the other material scalars are copied from the parameter block.
//
// Interface
//   input  location 0: float3 world normal; 1: float2 uv; 2: float4 world tangent; 3: float4 color (VERTEX_COLOR)
//   set 1, binding 0: uniform buffer MaterialParams (std140): float4 base_color @0; float roughness @16; float metallic @20;
//       float emissive_scale @24; float alpha_cutoff @28; float occlusion @32; uint flags @36;
//       float4 albedo_uv @48; float4 albedo_uv_outside @64
//   set 1, binding 1: sampled texture albedo_tex; binding 2: sampled texture normal_tex (NORMAL_MAP); binding 3: sampler

// permutation-options: NORMAL_MAP ALPHA_TEST VERTEX_COLOR

#include "target_common.slang"  // ENGINE_REGISTER
#include "gbuffer_common.slang"

struct MaterialParams {
    float4 base_color;
    float roughness;
    float metallic;
    float emissive_scale;
    float alpha_cutoff;
    float occlusion;
    uint flags;
    float4 albedo_uv;          // scale u, scale v, offset u, offset v
    float4 albedo_uv_outside;  // linear rgb shown outside the picture; a = 0: no mask (the sampler's address mode rules)
};

[[vk::binding(0, 1)]] ConstantBuffer<MaterialParams> material ENGINE_REGISTER(b0, space1);
[[vk::binding(1, 1)]] Texture2D albedo_tex ENGINE_REGISTER(t1, space1);
[[vk::binding(2, 1)]] Texture2D normal_tex ENGINE_REGISTER(t2, space1);
[[vk::binding(3, 1)]] SamplerState linear_sampler ENGINE_REGISTER(s3, space1);

struct FragmentInput {
    [[vk::location(0)]] float3 normal : NORMAL;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float4 tangent : TANGENT;
#ifdef VERTEX_COLOR
    [[vk::location(3)]] float4 color : COLOR0;
#endif
};

struct FragmentOutput {
    [[vk::location(0)]] float4 albedo : SV_Target0;
    [[vk::location(1)]] float2 normal : SV_Target1;
    [[vk::location(2)]] float4 material : SV_Target2;
};

[shader("fragment")]
FragmentOutput main(FragmentInput input) {
    float2 albedo_coord = input.uv * material.albedo_uv.xy + material.albedo_uv.zw;
    bool masked = false;
    if (material.albedo_uv_outside.a > 0.0) {
        uint texture_width;
        uint texture_height;
        albedo_tex.GetDimensions(texture_width, texture_height);
        const float2 half_texel = 0.5 / float2(max(texture_width, 1u), max(texture_height, 1u));
        masked = any(albedo_coord < 0.0) || any(albedo_coord > 1.0);
        albedo_coord = clamp(albedo_coord, half_texel, 1.0 - half_texel);
    }
    float4 texel = albedo_tex.Sample(linear_sampler, albedo_coord);
    if (masked) {
        texel.rgb = material.albedo_uv_outside.rgb;
    }
    float3 albedo = material.base_color.rgb * texel.rgb;
    float alpha = material.base_color.a * texel.a;
#ifdef VERTEX_COLOR
    albedo *= SrgbToLinear(input.color.rgb);
    alpha *= input.color.a;
#endif
#ifdef ALPHA_TEST
    if (alpha < material.alpha_cutoff) {
        discard;
    }
#endif
    float3 n = normalize(input.normal);
#ifdef NORMAL_MAP
    float3 t = normal_tex.Sample(linear_sampler, input.uv).xyz * 2.0 - 1.0;
    float3 tangent = normalize(input.tangent.xyz - n * dot(n, input.tangent.xyz));
    float3 bitangent = cross(n, tangent) * input.tangent.w;
    n = normalize(tangent * t.x + bitangent * t.y + n * t.z);
#endif
    FragmentOutput output;
    output.albedo = float4(albedo, material.occlusion);
    output.normal = OctEncode(n);
    output.material = float4(material.roughness, material.metallic, material.emissive_scale, float(material.flags) / 255.0);
    return output;
}
