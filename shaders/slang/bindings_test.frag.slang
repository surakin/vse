// bindings_test.frag: exercises every kind of binding the RHI hands to a fragment shader. Used by the RHI tests.
//
//   color = tex.Sample(smp, uv) * params.scale + push.bias
//
// Interface
//   input  location 0: float2 uv
//   output location 0: float4 color (attachment 0)
//   set 0, binding 0: uniform buffer { float4 scale }  (std140)
//   set 0, binding 1: sampled texture (2D)
//   set 0, binding 2: sampler
//   push constants: float4 bias (16 bytes, fragment stage)

struct Params {
    float4 scale;
};
struct Push {
    float4 bias;
};

[[vk::binding(0, 0)]] ConstantBuffer<Params> params;
[[vk::binding(1, 0)]] Texture2D tex;
[[vk::binding(2, 0)]] SamplerState smp;
[[vk::push_constant]] ConstantBuffer<Push> push;

[shader("fragment")]
float4 main([[vk::location(0)]] float2 uv : TEXCOORD0) : SV_Target0 {
    return tex.Sample(smp, uv) * params.scale + push.bias;
}
