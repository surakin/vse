// fullscreen.vert: one triangle that covers the viewport, from the vertex index alone (no vertex buffer).
//
// Interface
//   no vertex inputs; draw 3 vertices
//   vertex output location 0: float2 uv (0..1 across the viewport, origin top-left)
//   no descriptors, no push constants

#include "target_common.slang"

struct VertexOutput {
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput main(uint vertex_index : ENGINE_VERTEX_ID) {
    float2 p = float2(float((vertex_index << 1) & 2), float(vertex_index & 2));
    VertexOutput output;
    output.uv = p;
    output.position = ENGINE_CLIP(float4(p * 2.0 - 1.0, 0.0, 1.0));
    return output;
}
