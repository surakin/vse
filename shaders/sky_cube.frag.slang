// sky_cube.frag: the environment cube map drawn as the sky. Draw with fullscreen.vert over the light accumulation target
// (loaded, not cleared); a pixel the world covers (depth < 1) is discarded.
//
// Per pixel
//   d      = normalize(world(ndc, 1) - camera_position)              as sky.frag
//   dc     = (c d.x - s d.z, d.y, s d.x + c d.z)                      the sky turned by `rotation`: c = cos, s = sin
//   output = cube(dc, mip 0) * tint * exposure
// The CPU twin of the direction is renderer::SkyCubeDirection (engine/renderer/sky.cpp).
//
// Interface
//   fragment input location 0: float2 uv (unused)
//   set 0, binding 0: uniform buffer FrameData (renderer::FrameData)
//   set 1, binding 0: depth (sampled texture, read with Load); 1: the cube map (sampled texture); 2: sampler
//   push constants (32 bytes): float4 tint * exposure (rgb); float4 (cos, sin, 0, 0)

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
[[vk::binding(1, 1)]] TextureCube<float4> cube_tex ENGINE_REGISTER(t1, space1);
[[vk::binding(2, 1)]] SamplerState cube_smp ENGINE_REGISTER(s2, space1);

struct Push {
    float4 tint_exposure;
    float4 yaw;
};
[[vk::push_constant]] ConstantBuffer<Push> push ENGINE_REGISTER(b0, space15);

struct FragmentInput {
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    float4 position : SV_Position;
};

struct FragmentOutput {
    [[vk::location(0)]] float4 color : SV_Target0;
};

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
    float c = push.yaw.x;
    float s = push.yaw.y;
    float3 dc = float3(c * d.x - s * d.z, d.y, s * d.x + c * d.z);
    FragmentOutput output;
    output.color = float4(cube_tex.SampleLevel(cube_smp, dc, 0.0).rgb * push.tint_exposure.rgb, 1.0);
    return output;
}
