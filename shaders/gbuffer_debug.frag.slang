// gbuffer_debug.frag: shows one G-buffer layer, for debugging (GBufferDebugView). Draw with fullscreen.vert.
//
// Math (linear light; the output target encodes sRGB when it is an sRGB format)
//   layer 0 albedo:   out = albedo.rgb
//   layer 1 normal:   out = OctDecode(normal.rg) * 0.5 + 0.5
//   layer 2 material: out = material.rgb
//   layer 3 depth:    d = depth in [0, 1]; z = near * far / (far - d * (far - near)); out = z / far, as grey
//
// Interface
//   fragment input location 0: float2 uv
//   set 0, binding 0: albedo; 1: normal; 2: material; 3: depth (sampled textures); 4: sampler (nearest)
//   push constants: uint layer; float near_plane; float far_plane; uint pad  (16 bytes)

#include "gbuffer_common.slang"

[[vk::binding(0, 0)]] Texture2D<float4> albedo_tex;
[[vk::binding(1, 0)]] Texture2D<float4> normal_tex;
[[vk::binding(2, 0)]] Texture2D<float4> material_tex;
[[vk::binding(3, 0)]] Texture2D<float4> depth_tex;
[[vk::binding(4, 0)]] SamplerState point_sampler;

struct Push {
    uint layer;
    float near_plane;
    float far_plane;
    uint pad;
};
[[vk::push_constant]] ConstantBuffer<Push> push;

struct FragmentInput {
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

struct FragmentOutput {
    [[vk::location(0)]] float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput main(FragmentInput input) {
    FragmentOutput output;
    float3 rgb = float3(0.0, 0.0, 0.0);
    if (push.layer == 0u) {
        rgb = albedo_tex.Sample(point_sampler, input.uv).rgb;
    } else if (push.layer == 1u) {
        rgb = OctDecode(normal_tex.Sample(point_sampler, input.uv).rg) * 0.5 + 0.5;
    } else if (push.layer == 2u) {
        rgb = material_tex.Sample(point_sampler, input.uv).rgb;
    } else {
        float d = depth_tex.Sample(point_sampler, input.uv).r;
        float z = push.near_plane * push.far_plane / (push.far_plane - d * (push.far_plane - push.near_plane));
        float g = z / push.far_plane;
        rgb = float3(g, g, g);
    }
    output.color = float4(rgb, 1.0);
    return output;
}
