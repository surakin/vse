// gbuffer.vert: the G-buffer geometry pass's vertex shader (set 0 the frame uniform, set 2 the per-object storage buffer
// indexed by instance, as in the renderer's convention).
//
// Math
//   world   = objects[instance].world * position
//   clip    = frame.view_projection * world
//   normal  = normalize-later(float3x3(objects[instance].normal) * in_normal)   (unnormalized; the fragment shader normalizes)
//   tangent = (float3x3(objects[instance].world) * in_tangent.xyz, in_tangent.w)
// Option VERTEX_COLOR reads the mesh's per-vertex color (location 4, UNorm8x4, sRGB-encoded) and passes it on unchanged;
// the fragment shader converts it to linear.
//
// Interface
//   vertex input  location 0: float3 position; 1: float3 normal; 2: float4 tangent; 3: float2 uv;
//                 4: float4 color (only with VERTEX_COLOR)
//   vertex output location 0: float3 world normal; 1: float2 uv; 2: float4 world tangent;
//                 3: float4 color (only with VERTEX_COLOR)
//   set 0, binding 0: uniform buffer FrameData (renderer::FrameData, 304 bytes)
//   set 2, binding 0: storage buffer of ObjectData (renderer::ObjectData, 128 bytes each)

// permutation-options: VERTEX_COLOR

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

struct ObjectData {
    float4x4 world;
    float4x4 normal;
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame : register(b0, space0);
[[vk::binding(0, 2)]] StructuredBuffer<ObjectData> objects : register(t0, space2);

struct VertexInput {
    [[vk::location(0)]] float3 position : TEXCOORD0;
    [[vk::location(1)]] float3 normal : TEXCOORD1;
    [[vk::location(2)]] float4 tangent : TEXCOORD2;
    [[vk::location(3)]] float2 uv : TEXCOORD3;
#ifdef VERTEX_COLOR
    [[vk::location(4)]] float4 color : TEXCOORD4;
#endif
#ifdef ENGINE_OBJECT_INDEX_STREAM
    [[vk::location(15)]] uint instance : TEXCOORD15; // renderer::ObjectIndexStream (D3D12: no base instance)
#else
    uint instance : SV_VulkanInstanceID;
#endif
};

struct VertexOutput {
    [[vk::location(0)]] float3 normal : NORMAL;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float4 tangent : TANGENT;
#ifdef VERTEX_COLOR
    [[vk::location(3)]] float4 color : COLOR0;
#endif
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput main(VertexInput input) {
    ObjectData object = objects[input.instance];
    VertexOutput output;
    output.position = ENGINE_CLIP(mul(frame.view_projection, mul(object.world, float4(input.position, 1.0))));
    output.normal = mul((float3x3)object.normal, input.normal);
    output.uv = input.uv;
    output.tangent = float4(mul((float3x3)object.world, input.tangent.xyz), input.tangent.w);
#ifdef VERTEX_COLOR
    output.color = input.color;
#endif
    return output;
}
