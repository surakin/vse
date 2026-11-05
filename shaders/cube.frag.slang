// cube.frag: writes the interpolated vertex color.
//
// Interface
//   input  location 0: float3 color
//   input  location 1: float2 uv (unused for now)
//   output location 0: float4 color (attachment 0)
//   no descriptors, no push constants

struct FragmentInput {
    [[vk::location(0)]] float3 color : COLOR;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
};

[shader("fragment")]
float4 main(FragmentInput input) : SV_Target0 {
    return float4(input.color, 1.0);
}
