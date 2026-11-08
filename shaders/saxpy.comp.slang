// saxpy.comp: y[i] = a * x[i] + y[i] on unsigned integers (exact, so tests can compare bit for bit). Used by the
// RHI compute tests.
//
// Interface
//   local size 64 x 1 x 1; dispatch ceil(count / 64) workgroups (extra invocations are ignored)
//   set 0, binding 0: storage buffer { uint data[] }  (read: x)
//   set 0, binding 1: storage buffer { uint data[] }  (read and written: y)
//   push constants: uint a (4 bytes, compute stage)

struct Push {
    uint a;
};

[[vk::binding(0, 0)]] StructuredBuffer<uint> x_buf : register(t0, space0);
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> y_buf : register(u1, space0);
[[vk::push_constant]] ConstantBuffer<Push> push : register(b0, space15);

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint x_count, y_count, stride;
    x_buf.GetDimensions(x_count, stride);
    y_buf.GetDimensions(y_count, stride);
    uint i = id.x;
    if (i < y_count && i < x_count) {
        y_buf[i] = push.a * x_buf[i] + y_buf[i];
    }
}
