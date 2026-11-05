// graph_sample.frag: adds two sampled textures, out = sample(a, uv) + sample(b, uv), clamped by the render target.
// Used by the render graph's real-device test (Phase 4 Day 76); not a material shader.
//
// Interface
//   fragment input location 0: float2 uv (fullscreen.vert)
//   set 0, binding 0: sampled texture a; binding 1: sampled texture b; binding 2: sampler
//   fragment output location 0: float4 color
//   no push constants

[[vk::binding(0, 0)]] Texture2D<float4> tex_a;
[[vk::binding(1, 0)]] Texture2D<float4> tex_b;
[[vk::binding(2, 0)]] SamplerState linear_sampler;

struct FragmentInput {
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

struct FragmentOutput {
    [[vk::location(0)]] float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput main(FragmentInput input) {
    FragmentOutput output;
    output.color = tex_a.Sample(linear_sampler, input.uv) + tex_b.Sample(linear_sampler, input.uv);
    return output;
}
