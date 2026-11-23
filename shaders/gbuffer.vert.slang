// gbuffer.vert: the G-buffer geometry pass's vertex shader (set 0 the frame uniform, set 2 the per-object storage buffer
// indexed by instance, as in the renderer's convention).
//
// Math
//   world   = objects[instance].world * position
//   clip    = frame.view_projection * world
//   normal  = normalize-later(float3x3(objects[instance].normal) * in_normal)   (unnormalized; the fragment shader normalizes)
//   tangent = (float3x3(objects[instance].world) * in_tangent.xyz, in_tangent.w)
// Option SKINNED (Phase 22 Day 322) blends the position, normal and tangent by up to four joint matrices before the object
// transform: the mesh's skin stream (vertex binding 1) gives four joint indices and four weights (summing to 1 exactly, as
// 255 of 255 in the mesh blob, so no renormalization), the joint palette is the 3x4 row-major matrices of set 3, and the
// palette offset of the object is the translation x of its normal matrix (renderer::FrameResources::WriteObjects). The
// normal and tangent use each matrix's 3x3 part, which is correct while a pose has uniform scale per joint (non-uniform
// scale in a pose is out of scope).
// Option VERTEX_COLOR reads the mesh's per-vertex color (location 4, UNorm8x4, sRGB-encoded) and passes it on unchanged;
// the fragment shader converts it to linear.
//
// Interface
//   vertex input  location 0: float3 position; 1: float3 normal; 2: float4 tangent; 3: float2 uv;
//                 4: float4 color (only with VERTEX_COLOR); 5: uint4 joints, 6: float4 weights (only with SKINNED)
//   vertex output location 0: float3 world normal; 1: float2 uv; 2: float4 world tangent;
//                 3: float4 color (only with VERTEX_COLOR)
//   set 0, binding 0: uniform buffer FrameData (renderer::FrameData, 304 bytes)
//   set 2, binding 0: storage buffer of ObjectData (renderer::ObjectData, 128 bytes each)
//   set 3, binding 0: storage buffer of JointMatrix, 48 bytes each (only with SKINNED)

// Option INSTANCED (Phase 28 Day 497) draws an instanced mesh: the instance index (the draw's first instance plus the
// instance within it) selects a record of set 3's storage buffer (renderer::GpuInstance, 64 bytes) in place of the object
// buffer: a position, a unit quaternion and a scale (already in the space of the frame's view-projection, the set's own
// transform composed in by the CPU), a tint and a custom value. The world matrix is rebuilt here:
//   world = rotate(q, scale * position) + position_of_instance;  normal = rotate(q, normal / scale)
// The tint (sRGB-encoded like a vertex color, so the fragment shader's VERTEX_COLOR permutation multiplies the albedo by it)
// leaves through location 3. INSTANCED excludes SKINNED and VERTEX_COLOR (the tint takes the color slot).
//
// permutation-options: VERTEX_COLOR SKINNED INSTANCED

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

#ifdef INSTANCED
// One instance, as renderer::GpuInstance lays it out: 16 words, every member 4 bytes so the stride is 64 on every API.
struct GpuInstance {
    float px; float py; float pz;
    float qx; float qy; float qz; float qw;
    float sx; float sy; float sz;
    float tr; float tg; float tb; float ta;
    uint custom;
    uint pad;
};

[[vk::binding(0, 3)]] StructuredBuffer<GpuInstance> instances ENGINE_REGISTER(t0, space3);

float3 RotateByQuaternion(float4 q, float3 v) {
    float3 u = q.xyz;
    return v + 2.0 * cross(u, cross(u, v) + q.w * v);
}
#endif

#ifdef SKINNED
// One joint matrix: the three rows of a 3x4 row-major matrix (renderer::kJointMatrixBytes).
struct JointMatrix {
    float4 row0;
    float4 row1;
    float4 row2;
};

[[vk::binding(0, 3)]] StructuredBuffer<JointMatrix> joints ENGINE_REGISTER(t0, space3);
#endif

struct VertexInput {
    [[vk::location(0)]] float3 position : TEXCOORD0;
    [[vk::location(1)]] float3 normal : TEXCOORD1;
    [[vk::location(2)]] float4 tangent : TEXCOORD2;
    [[vk::location(3)]] float2 uv : TEXCOORD3;
#ifdef VERTEX_COLOR
    [[vk::location(4)]] float4 color : TEXCOORD4;
#endif
#ifdef SKINNED
    [[vk::location(5)]] uint4 joint_indices : TEXCOORD5;
    [[vk::location(6)]] float4 joint_weights : TEXCOORD6;
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
#if defined(VERTEX_COLOR) || defined(INSTANCED)
    [[vk::location(3)]] float4 color : COLOR0;
#endif
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput main(VertexInput input) {
    VertexOutput output;
#ifdef INSTANCED
    GpuInstance inst = instances[input.instance];
    float4 q = float4(inst.qx, inst.qy, inst.qz, inst.qw);
    float3 scale = float3(inst.sx, inst.sy, inst.sz);
    float3 world_position = RotateByQuaternion(q, input.position * scale) + float3(inst.px, inst.py, inst.pz);
    output.position = ENGINE_CLIP(mul(frame.view_projection, float4(world_position, 1.0)));
    output.normal = RotateByQuaternion(q, input.normal / scale);
    output.uv = input.uv;
    output.tangent = float4(RotateByQuaternion(q, input.tangent.xyz * scale), input.tangent.w);
    output.color = float4(inst.tr, inst.tg, inst.tb, inst.ta);
    return output;
#else
    ObjectData object = objects[input.instance];
    float3 position = input.position;
    float3 normal = input.normal;
    float3 tangent = input.tangent.xyz;
#ifdef SKINNED
    // The palette offset travels in the normal matrix's translation (its fourth column), which nothing else reads.
    uint base = (uint)(mul(object.normal, float4(0.0, 0.0, 0.0, 1.0)).x + 0.5);
    float3 skinned_position = float3(0.0, 0.0, 0.0);
    float3 skinned_normal = float3(0.0, 0.0, 0.0);
    float3 skinned_tangent = float3(0.0, 0.0, 0.0);
    float4 p = float4(input.position, 1.0);
    for (int k = 0; k < 4; ++k) {
        float w = input.joint_weights[k];
        JointMatrix m = joints[base + input.joint_indices[k]];
        skinned_position += w * float3(dot(m.row0, p), dot(m.row1, p), dot(m.row2, p));
        skinned_normal += w * float3(dot(m.row0.xyz, input.normal), dot(m.row1.xyz, input.normal), dot(m.row2.xyz, input.normal));
        skinned_tangent += w * float3(dot(m.row0.xyz, tangent), dot(m.row1.xyz, tangent), dot(m.row2.xyz, tangent));
    }
    position = skinned_position;
    normal = skinned_normal;
    tangent = skinned_tangent;
#endif
    output.position = ENGINE_CLIP(mul(frame.view_projection, mul(object.world, float4(position, 1.0))));
    output.normal = mul((float3x3)object.normal, normal);
    output.uv = input.uv;
    output.tangent = float4(mul((float3x3)object.world, tangent), input.tangent.w);
#ifdef VERTEX_COLOR
    output.color = input.color;
#endif
    return output;
#endif
}
