// tonemap.frag: exposure and a filmic operator, from the HDR light accumulation to the display target. Draw with
// fullscreen.vert. The target's format does the linear-to-sRGB conversion: render to an *_SRGB format (the swapchain's).
//
// Math (per channel, x = linear HDR * exposure)
//   mapped = clamp((x * (2.51 x + 0.03)) / (x * (2.43 x + 0.59) + 0.14), 0, 1)       Narkowicz's ACES fit
// The CPU twin is renderer::TonemapAces (engine/renderer/lighting.cpp).
//
// Interface
//   fragment input location 0: float2 uv (unused)
//   set 0, binding 0: sampled texture hdr (read with Load)
//   push constants (16 bytes): float exposure; 3 pad

[[vk::binding(0, 0)]] Texture2D<float4> hdr_tex;

struct Push {
    float exposure;
    float pad0;
    float pad1;
    float pad2;
};
[[vk::push_constant]] ConstantBuffer<Push> push;

struct FragmentInput {
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    float4 position : SV_Position;
};

struct FragmentOutput {
    [[vk::location(0)]] float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput main(FragmentInput input) {
    float3 x = hdr_tex.Load(int3(int2(input.position.xy), 0)).rgb * push.exposure;
    float3 mapped = saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
    FragmentOutput output;
    output.color = float4(mapped, 1.0);
    return output;
}
