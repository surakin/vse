// textured.frag: the texture sampled at the interpolated uv, multiplied by the vertex color.
//
// Color spaces: the math is linear. A texture in an sRGB format is decoded to linear by the hardware when it is
// sampled; the color target decides how the result is stored (an sRGB target encodes on write, a UNORM target
// stores the linear value as it is).
//
// Interface
//   input  location 0: float3 color
//   input  location 1: float2 uv
//   output location 0: float4 color (attachment 0)
//   set 0, binding 1: sampled texture (2D), fragment stage
//   set 0, binding 2: sampler, fragment stage

[[vk::binding(1, 0)]] Texture2D tex;
[[vk::binding(2, 0)]] SamplerState smp;

struct FragmentInput {
    [[vk::location(0)]] float3 color : COLOR;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
};

[shader("fragment")]
float4 main(FragmentInput input) : SV_Target0 {
    return tex.Sample(smp, input.uv) * float4(input.color, 1.0);
}
