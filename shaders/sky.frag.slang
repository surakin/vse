// sky.frag: the procedural sky, drawn behind the scene. Draw with fullscreen.vert over the light accumulation target
// (loaded, not cleared); a pixel the world covers (depth < 1) is discarded, so no draw order matters.
//
// Per pixel
//   world = inverse_view_projection * (ndc, 1, 1), divided by w      the far plane's point on the pixel's ray
//   d     = normalize(world - camera_position)
//   e     = d.y                                                      the elevation, -1 (nadir) to 1 (zenith)
//   color = e >= 0 ? lerp(horizon, zenith, smooth(e)) : lerp(horizon, ground, smooth(-e / horizon_blend))
//           + sun_color * smoothstep(cos_outer, cos_inner, dot(d, sun_direction))
//   output = color * exposure
// The CPU twin is renderer::SkyColor (engine/renderer/sky.cpp).
//
// Interface
//   fragment input location 0: float2 uv (unused)
//   set 0, binding 0: uniform buffer FrameData (renderer::FrameData)
//   set 1, binding 0: depth (sampled texture, read with Load)
//   push constants (80 bytes, five float4): zenith rgb + cos_inner; horizon rgb + cos_outer; ground rgb + horizon_blend;
//       sun direction xyz (unit) + exposure; sun color rgb + 0

#include "target_common.slang"  // ENGINE_REGISTER

struct FrameData {
    float4x4 view;
    float4x4 projection;
    float4x4 view_projection;
    float4x4 inverse_view_projection;
    float4 camera_position;
    float4 viewport;
    float4 time;
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame ENGINE_REGISTER(b0, space0);
[[vk::binding(0, 1)]] Texture2D<float4> depth_tex ENGINE_REGISTER(t0, space1);

struct Push {
    float4 zenith;       // rgb; w: cos_inner (the sun disc's solid edge)
    float4 horizon;      // rgb; w: cos_outer (the end of its soft edge)
    float4 ground;       // rgb; w: horizon_blend
    float4 sun;          // xyz: the unit direction toward the sun; w: exposure
    float4 sun_color;    // rgb; w: unused
};
[[vk::push_constant]] ConstantBuffer<Push> push ENGINE_REGISTER(b0, space15);

struct FragmentInput {
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    float4 position : SV_Position;
};

struct FragmentOutput {
    [[vk::location(0)]] float4 color : SV_Target0;
};

float Smooth(float x) {
    x = saturate(x);
    return x * x * (3.0 - 2.0 * x);
}

[shader("fragment")]
FragmentOutput main(FragmentInput input) {
    int2 pixel = int2(input.position.xy);
    if (depth_tex.Load(int3(pixel, 0)).r < 1.0) {
        discard;
    }
    uint width, height;
    depth_tex.GetDimensions(width, height);
    float2 ndc_xy = input.position.xy / float2(width, height) * 2.0 - 1.0;
    float4 clip = mul(frame.inverse_view_projection, float4(ndc_xy, 1.0, 1.0));
    float3 d = normalize(clip.xyz / clip.w - frame.camera_position.xyz);

    float3 zenith = push.zenith.rgb;
    float3 horizon = push.horizon.rgb;
    float3 ground = push.ground.rgb;
    float e = d.y;
    float3 color = e >= 0.0 ? lerp(horizon, zenith, Smooth(e)) : lerp(horizon, ground, Smooth(-e / push.ground.w));
    float c = dot(d, push.sun.xyz);
    color += push.sun_color.rgb * smoothstep(push.horizon.w, push.zenith.w, c);

    FragmentOutput output;
    output.color = float4(color * push.sun.w, 1.0);
    return output;
}
