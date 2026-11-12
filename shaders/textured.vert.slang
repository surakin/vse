// textured.vert: transforms a cube vertex and passes its color and uv on. Used with textured.frag.
//
// Interface
//   vertex input  location 0: float3 position
//   vertex input  location 1: float3 normal
//   vertex input  location 2: float2 uv
//   vertex output location 0: float3 color (the normal mapped to 0..1)
//   vertex output location 1: float2 uv
//   set 0, binding 0: uniform buffer { float4x4 mvp }  (std140, vertex stage)

#include "target_common.slang"

struct Params {
    float4x4 mvp;
};

[[vk::binding(0, 0)]] ConstantBuffer<Params> params ENGINE_REGISTER(b0, space0);

struct VertexInput {
    [[vk::location(0)]] float3 position : TEXCOORD0;
    [[vk::location(1)]] float3 normal : TEXCOORD1;
    [[vk::location(2)]] float2 uv : TEXCOORD2;
};

struct VertexOutput {
    [[vk::location(0)]] float3 color : COLOR;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput main(VertexInput input) {
    VertexOutput output;
    output.position = ENGINE_CLIP(mul(params.mvp, float4(input.position, 1.0)));
    output.color = 0.5 + 0.5 * input.normal;
    output.uv = input.uv;
    return output;
}
