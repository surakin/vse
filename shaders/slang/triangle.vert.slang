// triangle.vert: passes a 2D position and a color through.
//
// Interface
//   vertex input  location 0: float2 position (clip space)
//   vertex input  location 1: float3 color
//   vertex output location 0: float3 color (interpolated)
//   no descriptors, no push constants

struct VertexInput {
    [[vk::location(0)]] float2 position : POSITION;
    [[vk::location(1)]] float3 color : COLOR;
};

struct VertexOutput {
    [[vk::location(0)]] float3 color : COLOR;
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput main(VertexInput input) {
    VertexOutput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.color = input.color;
    return output;
}
