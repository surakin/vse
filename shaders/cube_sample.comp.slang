// cube_sample.comp: samples a cube texture along the six axis directions (+X, -X, +Y, -Y, +Z, -Z, the order of the
// cube's faces) at one mip level and writes the six colors. Used by the RHI cube texture tests.
//
// Interface
//   local size 8 x 1 x 1; dispatch 1 workgroup (invocations 6 and above are ignored)
//   set 0, binding 0: sampled texture (cube), compute stage
//   set 0, binding 1: sampler, compute stage
//   set 0, binding 2: storage buffer { float4 colors[6] }  (written)
//   push constants: float lod (4 bytes, compute stage)

#include "target_common.slang"  // ENGINE_REGISTER

struct Push {
    float lod;
};

[[vk::binding(0, 0)]] TextureCube<float4> cube ENGINE_REGISTER(t0, space0);
[[vk::binding(1, 0)]] SamplerState smp ENGINE_REGISTER(s1, space0);
[[vk::binding(2, 0)]] RWStructuredBuffer<float4> colors ENGINE_REGISTER(u2, space0);
[[vk::push_constant]] ConstantBuffer<Push> push ENGINE_REGISTER(b0, space15);

[shader("compute")]
[numthreads(8, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint i = id.x;
    if (i >= 6) {
        return;
    }
    float3 directions[6] = {
        float3(1.0, 0.0, 0.0), float3(-1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0),
        float3(0.0, -1.0, 0.0), float3(0.0, 0.0, 1.0), float3(0.0, 0.0, -1.0)};
    colors[i] = cube.SampleLevel(smp, directions[i], push.lod);
}
