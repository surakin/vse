// material_test.vert: the smallest vertex shader that follows the renderer's descriptor-set convention (set 0 the frame
// uniform, set 2 the per-object storage buffer indexed by instance). Used by the material tests.
//
// Math
//   world    = objects[instance].world * position
//   clip     = frame.view_projection * world
//   normal   = float3x3(objects[instance].normal) * in_normal
// The object index is the draw's first_instance plus the instance within the draw: the renderer draws object i with
// first_instance = i, so no per-draw bind group is needed.
//
// Interface
//   vertex input  location 0: float3 position
//   vertex input  location 1: float3 normal
//   vertex input  location 2: float4 tangent
//   vertex input  location 3: float2 uv
//   vertex output location 0: float3 world normal (unnormalized)
//   vertex output location 1: float2 uv
//   vertex output location 2: float4 tangent
//   set 0, binding 0: uniform buffer FrameData (renderer::FrameData, 304 bytes)
//   set 2, binding 0: storage buffer of ObjectData (renderer::ObjectData, 128 bytes each)

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

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame ENGINE_REGISTER(b0, space0);
[[vk::binding(0, 2)]] StructuredBuffer<ObjectData> objects ENGINE_REGISTER(t0, space2);

struct VertexInput {
    [[vk::location(0)]] float3 position : TEXCOORD0;
    [[vk::location(1)]] float3 normal : TEXCOORD1;
    [[vk::location(2)]] float4 tangent : TEXCOORD2;
    [[vk::location(3)]] float2 uv : TEXCOORD3;
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
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput main(VertexInput input) {
    // SV_VulkanInstanceID is gl_InstanceIndex, which already includes the draw's first instance (SV_InstanceID would not,
    // and its base-instance subtraction needs the shaderDrawParameters feature).
    ObjectData object = objects[input.instance];
    VertexOutput output;
    output.position = ENGINE_CLIP(mul(frame.view_projection, mul(object.world, float4(input.position, 1.0))));
    output.normal = mul((float3x3)object.normal, input.normal);
    output.uv = input.uv;
    output.tangent = input.tangent;
    return output;
}
