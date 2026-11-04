// fill.comp: out[i] = i * k. Used by the RHI compute tests.
//
// Interface
//   local size 64 x 1 x 1; dispatch ceil(count / 64) workgroups (extra invocations are ignored)
//   set 0, binding 0: storage buffer { uint data[] }  (written)
//   push constants: uint k (4 bytes, compute stage)

struct Push {
    uint k;
};

[[vk::binding(0, 0)]] RWStructuredBuffer<uint> out_buf;
[[vk::push_constant]] ConstantBuffer<Push> push;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint count, stride;
    out_buf.GetDimensions(count, stride);
    uint i = id.x;
    if (i < count) {
        out_buf[i] = i * push.k;
    }
}
