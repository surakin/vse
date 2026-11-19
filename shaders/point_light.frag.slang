// point_light.frag: shades the G-buffer pixels inside one point light's volume and adds the result to the HDR accumulation
// target (additive blending in the pipeline). Drawn with point_light.vert.
//
// Per pixel
//   depth = depth_tex[pixel]; depth >= 1 is background: discarded. world is rebuilt as in lighting.frag.
//   to_light = light.position - world; d = length(to_light); where d >= radius the pixel gets nothing (attenuation is exactly 0 there)
//   L = to_light / d; V = normalize(camera_position - world)
//   color = ShadeLight(surface, V, L, light.color * PointAttenuation(d, radius))
// The lighting model and the attenuation are in lighting_common.slang.
//
// Interface
//   fragment input location 0: flat uint light index (from point_light.vert)
//   set 0, binding 0: uniform buffer FrameData; set 1, bindings 0-3: albedo, normal, material, depth (sampled, read with Load);
//   binding 4: storage buffer of LightData

#include "target_common.slang"  // ENGINE_REGISTER
#include "gbuffer_common.slang"
#include "lighting_common.slang"

struct FrameData {
    float4x4 view;
    float4x4 projection;
    float4x4 view_projection;
    float4x4 inverse_view_projection;
    float4 camera_position;
    float4 viewport;
    float4 time;
};

struct LightData {
    float4 direction;
    float4 color;
    float4 position_radius;
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame ENGINE_REGISTER(b0, space0);
[[vk::binding(0, 1)]] Texture2D<float4> albedo_tex ENGINE_REGISTER(t0, space1);
[[vk::binding(1, 1)]] Texture2D<float4> normal_tex ENGINE_REGISTER(t1, space1);
[[vk::binding(2, 1)]] Texture2D<float4> material_tex ENGINE_REGISTER(t2, space1);
[[vk::binding(3, 1)]] Texture2D<float4> depth_tex ENGINE_REGISTER(t3, space1);
[[vk::binding(4, 1)]] StructuredBuffer<LightData> lights ENGINE_REGISTER(t4, space1);

struct FragmentInput {
    [[vk::location(0)]] nointerpolation uint light : TEXCOORD0;
    float4 position : SV_Position;
};

struct FragmentOutput {
    [[vk::location(0)]] float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput main(FragmentInput input) {
    int2 pixel = int2(input.position.xy);
    float depth = depth_tex.Load(int3(pixel, 0)).r;
    if (depth >= 1.0) {
        discard;
    }
    uint width, height;
    depth_tex.GetDimensions(width, height);
    float2 ndc_xy = input.position.xy / float2(width, height) * 2.0 - 1.0;
    float4 clip = mul(frame.inverse_view_projection, float4(ndc_xy, depth, 1.0));
    float3 world = clip.xyz / clip.w;

    LightData light = lights[input.light];
    float3 to_light = light.position_radius.xyz - world;
    float dist = length(to_light);
    float atten = PointAttenuation(dist, light.position_radius.w);
    if (atten <= 0.0) {
        discard;
    }

    float4 albedo_texel = albedo_tex.Load(int3(pixel, 0));
    float4 material_texel = material_tex.Load(int3(pixel, 0));
    if ((uint(round(material_texel.a * 255.0)) & 1u) != 0u) {
        discard;  // an unlit material (flags bit 0) takes no light
    }
    float3 material = material_texel.rgb;
    Surface s;
    s.albedo = albedo_texel.rgb;
    s.occlusion = albedo_texel.a;
    s.normal = OctDecode(normal_tex.Load(int3(pixel, 0)).rg);
    s.roughness = material.r;
    s.metallic = material.g;

    float3 V = normalize(frame.camera_position.xyz - world);
    float3 L = to_light / max(dist, 1e-6);
    FragmentOutput output;
    output.color = float4(ShadeLight(s, V, L, light.color.rgb * atten), 0.0);
    return output;
}
