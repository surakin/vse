// terrain.frag: the terrain's G-buffer fragment shader, writing the four targets the mesh pass writes (see gbuffer.frag).
// A flat grey surface for now: albedo (0.5, 0.5, 0.5), full occlusion value 1, roughness 0.9, metallic 0, no flags, no
// emission. The terrain's layered material replaces it.
//
// Interface
//   input  location 0: float3 world normal; 1: float2 uv; 2: float4 world tangent (unused)

#include "target_common.slang"
#include "gbuffer_common.slang"

struct FragmentInput {
    [[vk::location(0)]] float3 normal : NORMAL;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float4 tangent : TANGENT;
};

struct FragmentOutput {
    [[vk::location(0)]] float4 albedo : SV_Target0;
    [[vk::location(1)]] float2 normal : SV_Target1;
    [[vk::location(2)]] float4 material : SV_Target2;
    [[vk::location(3)]] float4 emissive : SV_Target3;
};

[shader("fragment")]
FragmentOutput main(FragmentInput input) {
    FragmentOutput output;
    output.albedo = float4(0.5, 0.5, 0.5, 1.0);
    output.normal = OctEncode(normalize(input.normal));
    output.material = float4(0.9, 0.0, 0.0, 0.0);
    output.emissive = float4(0.0, 0.0, 0.0, 1.0);
    return output;
}
