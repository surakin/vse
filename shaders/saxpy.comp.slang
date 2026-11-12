// saxpy.comp: y[i] = a * x[i] + y[i] on unsigned integers (exact, so tests can compare bit for bit). Used by the
// RHI compute tests.
//
// Interface
//   local size 64 x 1 x 1; dispatch ceil(count / 64) workgroups (extra invocations are ignored)
//   set 0, binding 0: storage buffer { uint data[] }  (read: x)
//   set 0, binding 1: storage buffer { uint data[] }  (read and written: y)
//   push constants: uint a, uint count (8 bytes, compute stage; count: the elements of both buffers, passed rather than
//   asked of them, as Metal cannot: Phase 26 Day 315; where the target can ask, SPIR-V and DXIL, the count is also bounded
//   by both buffers' lengths, and on Metal the caller's count is the bound)

#include "target_common.slang"  // ENGINE_REGISTER

struct Push {
    uint a;
    uint count;
};

[[vk::binding(0, 0)]] StructuredBuffer<uint> x_buf ENGINE_REGISTER(t0, space0);
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> y_buf ENGINE_REGISTER(u1, space0);
[[vk::push_constant]] ConstantBuffer<Push> push ENGINE_REGISTER(b0, space15);

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint i = id.x;
    uint limit = push.count;
#ifndef ENGINE_TARGET_METAL
    uint x_count, y_count, stride;
    x_buf.GetDimensions(x_count, stride);
    y_buf.GetDimensions(y_count, stride);
    limit = min(limit, min(x_count, y_count));
#endif
    if (i < limit) {
        y_buf[i] = push.a * x_buf[i] + y_buf[i];
    }
}
