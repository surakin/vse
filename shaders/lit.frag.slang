// lit.frag: one directional light with Lambert diffuse and a constant ambient term, on a textured surface.
//
// Math (all in linear light)
//   N      = normalize(world normal)
//   L      = to_light.xyz                         unit vector from the surface toward the light (the application
//                                                 normalizes it; the zero vector means "no direct light")
//   albedo = tex.Sample(smp, uv).rgb              an sRGB texture is decoded by the hardware
//   color  = albedo * (ambient.rgb + light_color.rgb * max(dot(N, L), 0))
// `light_color` already includes the light's intensity. A surface facing away from the light gets the ambient term
// only; one facing it directly gets ambient + light_color.
//
// Interface
//   input  location 0: float3 world normal
//   input  location 1: float2 uv
//   output location 0: float4 color (attachment 0), alpha 1
//   set 0, binding 0: uniform buffer Params (same block as lit.vert)
//   set 0, binding 1: sampled texture (2D)
//   set 0, binding 2: sampler

struct Params {
    float4x4 mvp;
    float4x4 normal_matrix;
    float4 to_light;
    float4 light_color;
    float4 ambient;
};

[[vk::binding(0, 0)]] ConstantBuffer<Params> params : register(b0, space0);
[[vk::binding(1, 0)]] Texture2D tex : register(t1, space0);
[[vk::binding(2, 0)]] SamplerState smp : register(s2, space0);

struct FragmentInput {
    [[vk::location(0)]] float3 normal : NORMAL;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
};

[shader("fragment")]
float4 main(FragmentInput input) : SV_Target0 {
    float3 n = normalize(input.normal);
    float3 l = params.to_light.xyz;
    float3 albedo = tex.Sample(smp, input.uv).rgb;
    float3 diffuse = params.light_color.rgb * max(dot(n, l), 0.0);
    return float4(albedo * (params.ambient.rgb + diffuse), 1.0);
}
