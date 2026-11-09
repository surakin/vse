// text.vert: places one corner of a glyph quad given in pixels and passes the atlas coordinate and the color on.
//
// Coordinates: positions are in pixels of the target with the origin at its top-left corner, X right and Y down (the layout's
// convention), so the quad lands where the layout put it. The parameters carry the target's size; the shader maps pixels to clip
// space (Vulkan's clip space has Y down too, so no flip is needed here; ENGINE_CLIP adds the one Direct3D 12 needs).
//
// Color: the vertex color is 8-bit RGBA as the application wrote it, which is sRGB-encoded for a person picking a color. When the
// target is an sRGB format the hardware encodes on write, so the shader decodes the color to linear first (`srgb_target` is 1);
// for a UNORM target (`srgb_target` is 0) the bytes are used as they are.
//
// Interface
//   vertex input  location 0: float2 position (pixels)
//   vertex input  location 1: float2 uv (0..1 over the atlas page)
//   vertex input  location 2: float4 color (UNORM8x4)
//   vertex output location 0: float2 uv
//   vertex output location 1: float4 color (rgb linear when the target is sRGB, alpha as written)
//   push constants (16 bytes, vertex stage): float2 pixel_to_clip, float srgb_target, float pad

#include "target_common.slang"

struct Params {
    float2 pixel_to_clip; // (2 / width, 2 / height)
    float srgb_target;    // 1 when the color target is an sRGB format
    float pad;
};

[[vk::push_constant]] ConstantBuffer<Params> params : register(b0, space15);

struct VertexInput {
    [[vk::location(0)]] float2 position : TEXCOORD0;
    [[vk::location(1)]] float2 uv : TEXCOORD1;
    [[vk::location(2)]] float4 color : TEXCOORD2;
};

struct VertexOutput {
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float4 color : COLOR;
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
    return output;
}
