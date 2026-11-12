// mipgen.comp: writes one mip level of a texture from the level above it, a bilinear sample at each new texel's centre (a 2x2 box
// for even sizes, Vulkan's linear blit for odd ones). The Direct3D 12 backend's CommandList::GenerateMips (Phase 13 Day 168); the
// Vulkan backend blits instead and does not use it.
//
// Interface
//   local size 8 x 8 x 1; dispatch ceil(width / 8) x ceil(height / 8) x layers
//   set 0, binding 0: the level above (a view of that one level, every layer), sampled
//   set 0, binding 1: a linear, clamp-to-edge sampler
//   set 0, binding 2: the level written (a view of that one level, every layer), a storage image
//   push constants: the written level's width and height, and srgb (1: the source view decodes sRGB, so the result is encoded back
//     before it is stored through the linear view an sRGB texture's storage image has)

#include "target_common.slang"

[[vk::binding(0, 0)]] Texture2DArray<float4> src ENGINE_REGISTER(t0, space0);
[[vk::binding(1, 0)]] SamplerState linear_clamp ENGINE_REGISTER(s1, space0);
[[vk::binding(2, 0)]] RWTexture2DArray<float4> dst ENGINE_REGISTER(u2, space0);

struct Push {
    uint width;
    uint height;
    uint srgb;
    uint pad;
};
[[vk::push_constant]] ConstantBuffer<Push> push ENGINE_REGISTER(b0, space15);

float EncodeSrgb(float linear) {
    return linear <= 0.0031308 ? linear * 12.92 : 1.055 * pow(linear, 1.0 / 2.4) - 0.055;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= push.width || id.y >= push.height) {
        return;
    }
    const float2 uv = (float2(id.xy) + 0.5) / float2(push.width, push.height);
    float4 c = src.SampleLevel(linear_clamp, float3(uv, float(id.z)), 0.0);
    if (push.srgb != 0) {
        c = float4(EncodeSrgb(c.r), EncodeSrgb(c.g), EncodeSrgb(c.b), c.a);
    }
    dst[id] = c;
}
