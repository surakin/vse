// triangle.frag: writes the interpolated vertex color.
//
// Interface
//   input  location 0: float3 color
//   output location 0: float4 color (attachment 0)
//   no descriptors, no push constants

[shader("fragment")]
float4 main([[vk::location(0)]] float3 color : COLOR) : SV_Target0 {
    return float4(color, 1.0);
}
