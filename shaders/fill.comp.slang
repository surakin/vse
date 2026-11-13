// fill.comp: out[i] = i * k. Used by the RHI compute tests.
//
// Interface
//   local size 64 x 1 x 1; dispatch ceil(count / 64) workgroups (extra invocations are ignored)
//   set 0, binding 0: storage buffer { uint data[] }  (written)
//   push constants: uint k, uint count (8 bytes, compute stage). The element count is passed, not asked of the buffer
//   (GetDimensions), because Metal Shading Language cannot ask a buffer its length (Phase 21 Day 299). Where the target can
//   ask (SPIR-V, DXIL) the count is also bounded by the buffer's length; on Metal the caller's count is the bound.

#include "target_common.slang"  // ENGINE_REGISTER

struct Push {
    uint k;
    uint count;
};

[[vk::binding(0, 0)]] RWStructuredBuffer<uint> out_buf ENGINE_REGISTER(u0, space0);
[[vk::push_constant]] ConstantBuffer<Push> push ENGINE_REGISTER(b0, space15);

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint i = id.x;
    uint limit = push.count;
#ifndef ENGINE_TARGET_METAL
    uint length, stride;
    out_buf.GetDimensions(length, stride);
    limit = min(limit, length);
#endif
    if (i < limit) {
        out_buf[i] = i * push.k;
    }
}
