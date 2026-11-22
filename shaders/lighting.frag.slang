// lighting.frag: the deferred lighting pass for directional and ambient light. Draw with fullscreen.vert; writes the HDR
// light accumulation target (RGBA16_Float).
//
// Per pixel
//   depth = depth_tex[pixel]; depth >= 1 is background: the fragment is discarded (the target was cleared to black).
//   ndc   = (2 * (pixel + 0.5) / size - 1, depth)             size = the depth texture's extent; Vulkan clip space: x right, y down, depth 0..1
//   world = inverse_view_projection * (ndc, 1), divided by w  the projection's Y flip is already in the inverse, so the
//                                                             pixel row is used as it is (no second flip)
//   N     = OctDecode(normal.rg); albedo.rgb, occlusion = albedo.a; roughness, metallic = material.rg; flags = material.a * 255 (bit 0: unlit)
//   V     = normalize(camera_position - world)
//   color = ShadeAmbient(ambient) + sum over directional lights of ShadeLight(L = light.direction, radiance = light.color)
//           + ShadeSkyReflection(sky colour, intensity)        (the constant environment reflection, 0 by default)
//           + emissive.rgb                                     (the G-buffer's emissive target, added here once)
// The lighting model is in lighting_common.slang.
//
// Interface
//   fragment input location 0: float2 uv (unused; the pixel comes from SV_Position)
//   set 0, binding 0: uniform buffer FrameData (renderer::FrameData, 304 bytes)
//   set 1, binding 0: albedo; 1: normal; 2: material; 3: depth (sampled textures, read with Load); 4: storage buffer of
//       LightData (48 bytes each, directional lights first); 5: emissive (sampled texture, read with Load)
//   push constants (32 bytes): float4 ambient (rgb; a = sky reflection intensity); uint directional_count; float sky_r, sky_g, sky_b

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
[[vk::binding(5, 1)]] Texture2D<float4> emissive_tex ENGINE_REGISTER(t5, space1);

struct Push {
    float4 ambient;            // rgb: the ambient light; a: the sky reflection's intensity (0: no reflection)
    uint directional_count;
    float sky_r;               // linear rgb of the sky the lit surfaces reflect (three scalars: a float3 would move to offset 32)
    float sky_g;
    float sky_b;
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
    float4 material_texel = material_tex.Load(int3(pixel, 0));
    FragmentOutput output;
    // An unlit material (flags bit 0, KHR_materials_unlit) is its base colour: no ambient, light or emissive term.
    if ((uint(round(material_texel.a * 255.0)) & 1u) != 0u) {
        output.color = float4(albedo_texel.rgb, 1.0);
        return output;
    }
    float3 material = material_texel.rgb;
    Surface s;
    s.albedo = albedo_texel.rgb;
    s.occlusion = albedo_texel.a;
    s.normal = OctDecode(normal_tex.Load(int3(pixel, 0)).rg);
    s.roughness = material.r;
    s.metallic = material.g;

    float3 V = normalize(frame.camera_position.xyz - world);
    float3 color = ShadeAmbient(s, push.ambient.rgb);
    for (uint i = 0u; i < push.directional_count; ++i) {
        color += ShadeLight(s, V, lights[i].direction.xyz, lights[i].color.rgb);
    }
    color += ShadeSkyReflection(s, V, float3(push.sky_r, push.sky_g, push.sky_b), push.ambient.a);
    color += emissive_tex.Load(int3(pixel, 0)).rgb;
    output.color = float4(color, 1.0);
    return output;
}
