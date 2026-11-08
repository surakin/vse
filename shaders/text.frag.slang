// text.frag: the glyph's coverage from the atlas page, times the color's alpha, as a premultiplied color.
//
// The page is an 8-bit coverage texture (R8 UNORM). The result is premultiplied, so the pipeline blends it with
// source One and destination OneMinusSrcAlpha: the text lands over whatever is there, and a target that already holds
// premultiplied color stays premultiplied.
//
// Interface
//   input  location 0: float2 uv
//   input  location 1: float4 color
//   output location 0: float4 color (attachment 0, premultiplied)
//   set 0, binding 1: sampled texture (2D, R8), fragment stage
//   set 0, binding 2: sampler, fragment stage

[[vk::binding(1, 0)]] Texture2D page : register(t1, space0);
[[vk::binding(2, 0)]] SamplerState smp : register(s2, space0);

struct FragmentInput {
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float4 color : COLOR;
};

[shader("fragment")]
float4 main(FragmentInput input) : SV_Target0 {
    float coverage = page.Sample(smp, input.uv).r * input.color.a;
    return float4(input.color.rgb * coverage, coverage);
}
