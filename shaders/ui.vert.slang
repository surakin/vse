// ui.vert: places one corner of a UI primitive given in pixels and passes the primitive's parameters on to the fragment shader.
//
// Coordinates and colour follow text.vert: positions are pixels of the target with the origin at its top-left corner, X right and Y down;
// the 8-bit vertex colour is decoded to linear when the target is an sRGB format (`srgb_target` is 1).
//
// Interface (the layout of ui::Vertex, 40 bytes)
//   vertex input  location 0: float2 position (pixels)
//   vertex input  location 1: float2 uv (a texture coordinate, or the offset from the box centre in pixels in rectangle mode)
//   vertex input  location 2: float4 color (UNORM8x4)
//   vertex input  location 3: float4 shape (half width, half height, corner radius, border or blur; pixels)
//   vertex input  location 4: uint flags (mode in the low two bits, then the ring and shadow flags)
//   vertex output location 0: float2 uv
//   vertex output location 1: float4 color (rgb linear when the target is sRGB, alpha as written)
//   vertex output location 2: float4 shape
//   vertex output location 3: nointerpolation uint flags
//   push constants (16 bytes, vertex stage): float2 pixel_to_clip, float srgb_target, float pad

#include "target_common.slang"

struct Params {
    float2 pixel_to_clip; // (2 / width, 2 / height)
    float srgb_target;    // 1 when the color target is an sRGB format
    float pad;
};

[[vk::push_constant]] ConstantBuffer<Params> params ENGINE_REGISTER(b0, space15);

struct VertexInput {
    [[vk::location(0)]] float2 position : TEXCOORD0;
    [[vk::location(1)]] float2 uv : TEXCOORD1;
    [[vk::location(2)]] float4 color : TEXCOORD2;
    [[vk::location(3)]] float4 shape : TEXCOORD3;
    [[vk::location(4)]] uint flags : TEXCOORD4;
};

struct VertexOutput {
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float4 color : COLOR;
    [[vk::location(2)]] float4 shape : TEXCOORD2;
    [[vk::location(3)]] nointerpolation uint flags : TEXCOORD3;
    float4 position : SV_Position;
};

float3 SrgbToLinear(float3 c) {
    float3 low = c / 12.92;
    float3 high = pow((c + 0.055) / 1.055, float3(2.4, 2.4, 2.4));
    return float3(c.x <= 0.04045 ? low.x : high.x, c.y <= 0.04045 ? low.y : high.y, c.z <= 0.04045 ? low.z : high.z);
}

[shader("vertex")]
VertexOutput main(VertexInput input) {
    VertexOutput output;
    float2 clip = input.position * params.pixel_to_clip - float2(1.0, 1.0);
    output.position = ENGINE_CLIP(float4(clip, 0.0, 1.0));
    output.uv = input.uv;
    float3 rgb = params.srgb_target > 0.5 ? SrgbToLinear(input.color.rgb) : input.color.rgb;
    output.color = float4(rgb, input.color.a);
    output.shape = input.shape;
    output.flags = input.flags;
    return output;
}
