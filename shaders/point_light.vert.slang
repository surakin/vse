// point_light.vert: the light volume of one point light for the deferred lighting pass. Draw non-indexed with
// renderer::kLightVolumeVertexCount vertices and first_instance = the light's index in the light buffer; there is no vertex
// buffer. The vertices are a UV sphere (kLightVolumeSegments slices around Y, kLightVolumeRings bands) generated from the
// vertex index, scaled to circumscribe the light's sphere of radius R so the polyhedron never cuts into the lit region.
//
// Math
//   vertex v of the sphere:  quad = v / 6, corner = kCorner[v % 6], slice = quad % S + corner.x, ring = quad / S + corner.y
//   unit point               = (sin(pi ring / Rn) cos(2 pi slice / S), cos(pi ring / Rn), sin(pi ring / Rn) sin(2 pi slice / S))
//   world                    = light.position + unit point * light.radius * scale     scale = 1 / (cos(pi / S) cos(pi / Rn))
//   clip.z is clamped to 0..w (no depth test is used), so the near and far planes never clip the volume.
//   Triangles are counter-clockwise seen from outside (like renderer::MakeSphereMesh); the pass culls front faces so a camera
//   inside the volume still draws the far side.
// The CPU twin is renderer::LightVolumeVertex.
//
// Interface
//   set 0, binding 0: uniform buffer FrameData (renderer::FrameData, 304 bytes)
//   set 1, binding 4: storage buffer of LightData (48 bytes each); the other set 1 bindings belong to the fragment shader
//   vertex output location 0: flat uint light index

#include "target_common.slang"

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

#define LIGHT_VOLUME_SEGMENTS 16
#define LIGHT_VOLUME_RINGS 8

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame : register(b0, space0);
[[vk::binding(4, 1)]] StructuredBuffer<LightData> lights : register(t4, space1);

struct VertexInput {
    uint vertex : ENGINE_VERTEX_ID;
#ifdef ENGINE_OBJECT_INDEX_STREAM
    [[vk::location(15)]] uint instance : TEXCOORD15; // renderer::ObjectIndexStream (D3D12: no base instance)
#else
    uint instance : SV_VulkanInstanceID;
#endif
};

struct VertexOutput {
    [[vk::location(0)]] nointerpolation uint light : TEXCOORD0;
    float4 position : SV_Position;
};

static const float kPi = 3.14159265358979323846;

[shader("vertex")]
VertexOutput main(VertexInput input) {
    // Two triangles per quad, counter-clockwise from outside: (0,0) (1,0) (0,1)  and  (1,0) (1,1) (0,1) as (slice, ring), the order of renderer::MakeSphereMesh.
    static const int2 kCorner[6] = {int2(0, 0), int2(1, 0), int2(0, 1), int2(1, 0), int2(1, 1), int2(0, 1)};
    uint quad = input.vertex / 6u;
    int2 corner = kCorner[input.vertex % 6u];
    uint slice = (quad % LIGHT_VOLUME_SEGMENTS) + (uint)corner.x;
    uint ring = (quad / LIGHT_VOLUME_SEGMENTS) + (uint)corner.y;
    float theta = kPi * (float)ring / (float)LIGHT_VOLUME_RINGS;
    float phi = 2.0 * kPi * (float)slice / (float)LIGHT_VOLUME_SEGMENTS;
    float3 unit_point = float3(sin(theta) * cos(phi), cos(theta), sin(theta) * sin(phi));
    float scale = 1.0 / (cos(kPi / (float)LIGHT_VOLUME_SEGMENTS) * cos(kPi / (float)LIGHT_VOLUME_RINGS));
    LightData light = lights[input.instance];
    float3 world = light.position_radius.xyz + unit_point * (light.position_radius.w * scale);
    VertexOutput output;
    output.light = input.instance;
    float4 clip = mul(frame.view_projection, float4(world, 1.0));
    // The volume is drawn without a depth test, so only x and y matter. Pull z into 0..w so that the near and far planes do not
    // clip it: the far side of a large light around the camera may lie beyond the far plane.
    clip.z = clamp(clip.z, 0.0, max(clip.w, 0.0));
    output.position = ENGINE_CLIP(clip);
    return output;
}
