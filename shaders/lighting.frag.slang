// lighting.frag: the deferred lighting pass for directional and ambient light. Draw with fullscreen.vert; writes the HDR
// light accumulation target (RGBA16_Float).
//
// Per pixel
//   depth = depth_tex[pixel]; depth >= 1 is background: the fragment is discarded (the target was cleared to black).
//   ndc   = (2 * (pixel + 0.5) / size - 1, depth)             size = the depth texture's extent; Vulkan clip space: x right, y down, depth 0..1
//   world = inverse_view_projection * (ndc, 1), divided by w  the projection's Y flip is already in the inverse, so the
//                                                             pixel row is used as it is (no second flip)
//   N     = OctDecode(normal.rg); albedo.rgb, occlusion = albedo.a; roughness, metallic, emissive = material.rgb
//   V     = normalize(camera_position - world)
//   color = ShadeAmbient(ambient) + sum over directional lights of ShadeLight(L = light.direction, radiance = light.color)
// The lighting model is in lighting_common.slang.
//
// Interface
//   fragment input location 0: float2 uv (unused; the pixel comes from SV_Position)
//   set 0, binding 0: uniform buffer FrameData (renderer::FrameData, 304 bytes)
//   set 1, binding 0: albedo; 1: normal; 2: material; 3: depth (sampled textures, read with Load); 4: storage buffer of
//       LightData (48 bytes each, directional lights first)
//   push constants (32 bytes): float4 ambient (rgb); uint directional_count; 3 pad

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

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame : register(b0, space0);
[[vk::binding(0, 1)]] Texture2D<float4> albedo_tex : register(t0, space1);
[[vk::binding(1, 1)]] Texture2D<float4> normal_tex : register(t1, space1);
[[vk::binding(2, 1)]] Texture2D<float4> material_tex : register(t2, space1);
[[vk::binding(3, 1)]] Texture2D<float4> depth_tex : register(t3, space1);
[[vk::binding(4, 1)]] StructuredBuffer<LightData> lights : register(t4, space1);

struct Push {
    float4 ambient;
    uint directional_count;
    uint pad0;
    uint pad1;
    uint pad2;
};
[[vk::push_constant]] ConstantBuffer<Push> push : register(b0, space15);

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
    float depth = depth_tex.Load(int3(pixel, 0)).r;
    if (depth >= 1.0) {
        discard;
    }
    uint width, height;
    depth_tex.GetDimensions(width, height);
    float2 ndc_xy = input.position.xy / float2(width, height) * 2.0 - 1.0;
    float4 clip = mul(frame.inverse_view_projection, float4(ndc_xy, depth, 1.0));
    float3 world = clip.xyz / clip.w;

    float4 albedo_texel = albedo_tex.Load(int3(pixel, 0));
    float3 material = material_tex.Load(int3(pixel, 0)).rgb;
    Surface s;
    s.albedo = albedo_texel.rgb;
    s.occlusion = albedo_texel.a;
    s.normal = OctDecode(normal_tex.Load(int3(pixel, 0)).rg);
    s.roughness = material.r;
    s.metallic = material.g;
    s.emissive_scale = material.b;

    float3 V = normalize(frame.camera_position.xyz - world);
    float3 color = ShadeAmbient(s, push.ambient.rgb);
    for (uint i = 0u; i < push.directional_count; ++i) {
        color += ShadeLight(s, V, lights[i].direction.xyz, lights[i].color.rgb);
    }
    FragmentOutput output;
    output.color = float4(color, 1.0);
    return output;
}
