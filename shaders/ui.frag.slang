// ui.frag: one shader for every UI primitive. The mode in the low two bits of `flags` picks what the fragment is:
//   0 solid:  the vertex colour.
//   1 rect:   the signed distance to a rounded box (half size and radius in `shape`, the position from the box centre in `uv`), shaded with
//             analytic anti-aliasing: one pixel of ramp from the distance. Flag 4 shades only the ring of `shape.w` pixels inside the edge
//             (a border); flag 8 fades the distance out over `shape.w` pixels outside the edge (a drop shadow).
//   2 glyph:  the coverage from an R8 atlas page (red channel) times the colour.
//   3 image:  the colour (a tint) times the image.
// The result is premultiplied, so the pipeline blends with source One and destination OneMinusSrcAlpha.
//
// Interface
//   input  location 0: float2 uv
//   input  location 1: float4 color
//   input  location 2: float4 shape
//   input  location 3: nointerpolation uint flags
//   output location 0: float4 color (attachment 0, premultiplied)
//   set 0, binding 0: sampled texture (2D), fragment stage (a glyph page, an image, or any texture when the command samples none)
//   set 0, binding 1: sampler, fragment stage

[[vk::binding(0, 0)]] Texture2D page : register(t0, space0);
[[vk::binding(1, 0)]] SamplerState smp : register(s1, space0);

struct FragmentInput {
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float4 color : COLOR;
    [[vk::location(2)]] float4 shape : TEXCOORD2;
    [[vk::location(3)]] nointerpolation uint flags : TEXCOORD3;
};

// The signed distance, in pixels, from `p` (relative to the box centre) to a box of half size `half_size` with corner radius `radius`.
float BoxDistance(float2 p, float2 half_size, float radius) {
    float r = min(radius, min(half_size.x, half_size.y));
    float2 q = abs(p) - half_size + float2(r, r);
    return length(max(q, float2(0.0, 0.0))) + min(max(q.x, q.y), 0.0) - r;
}

[shader("fragment")]
float4 main(FragmentInput input) : SV_Target0 {
    uint mode = input.flags & 3u;
    float4 c = input.color;
    float alpha = c.a;
    float3 rgb = c.rgb;
    if (mode == 1u) {
        float d = BoxDistance(input.uv, input.shape.xy, input.shape.z);
        if ((input.flags & 8u) != 0u) {
            float blur = max(input.shape.w, 1.0);
            alpha *= 1.0 - smoothstep(0.0, blur, max(d, 0.0));
        } else {
            float outer = saturate(0.5 - d);
            if ((input.flags & 4u) != 0u) {
                float inner = saturate(0.5 - (d + input.shape.w));
                alpha *= outer * (1.0 - inner);
            } else {
                alpha *= outer;
            }
        }
    } else if (mode == 2u) {
        alpha *= page.Sample(smp, input.uv).r;
    } else if (mode == 3u) {
        float4 t = page.Sample(smp, input.uv);
        rgb *= t.rgb;
        alpha *= t.a;
    }
    return float4(rgb * alpha, alpha);
}
