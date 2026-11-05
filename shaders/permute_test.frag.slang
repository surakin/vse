// permute_test.frag: a flat color whose value depends on compile-time options. Used by the permutation tests; it is the
// smallest shader that shows every part of the mechanism (a base module, single options, a combination, one option
// that changes the interface, and two keys that must build the same module).
//
//   base    color = (0.8, 0.4, 0.2, 1)
//   INVERT  color.rgb = 1 - color.rgb
//   HALF    color.rgb *= 0.5
//   TINT    color.b *= 0.5 (not applied together with DEBUG)
//   DEBUG   color = (uv, 0, 1), which reads the interface input the other variants leave unused
//
// The options are applied in the order INVERT, HALF, TINT, DEBUG. Build the keys listed in permute_test.frag.perm.
//
// Interface
//   input  location 0: float2 uv
//   output location 0: float4 color (attachment 0)
//   no descriptors, no push constants

// permutation-options: INVERT HALF TINT DEBUG

[shader("fragment")]
float4 main([[vk::location(0)]] float2 uv : TEXCOORD0) : SV_Target0 {
    float4 color = float4(0.8, 0.4, 0.2, 1.0);
#ifdef INVERT
    color.rgb = 1.0 - color.rgb;
#endif
#ifdef HALF
    color.rgb *= 0.5;
#endif
#if defined(TINT) && !defined(DEBUG)
    color.b *= 0.5; // DEBUG replaces the color below, so TINT is left out with it: both keys build the same module
#endif
#ifdef DEBUG
    color = float4(uv, 0.0, 1.0);
#endif
    return color;
}
