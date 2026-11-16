#include "physics/determinism.h"

#include "lang/scalar.h"
#include "physics/state_hash.h"

namespace engine_placeholder::physics
{

namespace
{

// Inputs the compiler cannot see through, so no expression below is folded at compile time (folding is exact, but then
// the probe would not exercise the instructions the build emits).
volatile f32 g_one = 1.0f;
volatile f32 g_a = 1.0f + 1.0f / 4096.0f;  // 1 + 2^-12, exact in a float
volatile f32 g_c = -(1.0f + 1.0f / 2048.0f);
volatile u32 g_denormal_bits = 1u;  // the smallest positive denormal
volatile i32 g_int = 16777217;      // 2^24 + 1: not a float
volatile u32 g_big = 4294967295u;

f32 FromBits(u32 bits)
{
    f32 value = 0.0f;
    MemCopy(&value, &bits, sizeof(value));
    return value;
}

}  // namespace

bool ContractionObserved()
{
    const f32 a = g_a;
    const f32 c = g_c;
    // a * a is 1 + 2^-11 + 2^-24: the 2^-24 is half a unit in the last place of the product, so the rounded product is
    // 1 + 2^-11 (ties to even) and the sum with c is exactly zero. A fused multiply-add keeps the 2^-24 and gives
    // 2^-24.
    const f32 r = a * a + c;
    return r != 0.0f;
}

bool DenormalsPreserved()
{
    const f32 tiny = FromBits(g_denormal_bits);
    volatile f32 times_one = tiny * g_one;
    return times_one != 0.0f;
}

u64 ProbeHash()
{
    StateHasher hash;
    const f32 one = g_one;
    // Products and sums a fused multiply-add rounds differently.
    // Each product has one use and its own loads: a compiler contracts only a multiplication whose result feeds the one
    // addition (GCC leaves a product that two expressions share alone), so this is the shape that shows contraction.
    const f32 a1 = g_a;
    const f32 a2 = g_a;
    const f32 a3 = g_a;
    hash.AddF32(a1 * a1 + g_c);
    hash.AddF32(a2 * a2 - g_c);
    hash.AddF32(g_c - a3 * a3);
    f32 acc = 0.1f;
    for (u32 i = 0; i < 64; ++i)
    {
        acc = acc * g_a + g_c * -0.0007f * static_cast<f32>(i);
    }
    hash.AddF32(acc);
    // A chain of square roots (correctly rounded by IEEE 754).
    f32 root = 2.0f * one;
    for (u32 i = 0; i < 24; ++i)
    {
        root = Sqrt(root + 1.0f);
    }
    hash.AddF32(root);
    hash.AddF32(Sqrt(3.0f * one));
    // Division and reciprocal (never the hardware estimate).
    hash.AddF32(one / 3.0f);
    hash.AddF32(10.0f / (one + 6.0f));
    // Conversions: 2^24 + 1 rounds to even, the largest u32 rounds up to 2^32, truncation toward zero.
    hash.AddF32(static_cast<f32>(static_cast<i32>(g_int)));
    hash.AddF32(static_cast<f32>(static_cast<u32>(g_big)));
    hash.AddU32(static_cast<u32>(static_cast<i32>(-2.75f * one)));
    // Denormal inputs and results: kept, not flushed.
    const f32 tiny = FromBits(g_denormal_bits);
    hash.AddF32(tiny * one);
    hash.AddF32(tiny * 0.5f * one);  // rounds to zero (ties to even): a denormal result rather than a flushed one
    hash.AddF32(FromBits(0x00800000u) * 0.75f * one);  // the smallest normal times 0.75 is a denormal
    hash.AddF32(FromBits(0x00000003u) + FromBits(0x00000005u) * one);
    // Signed zeros (hashed as +0.0, but the operations must still run).
    hash.AddF32(-one * 0.0f);
    return hash.Value();
}

const char* ModeReport()
{
    if (ContractionObserved())
    {
        return DenormalsPreserved() ? "contraction=on denormals=kept" : "contraction=on denormals=flushed";
    }
    return DenormalsPreserved() ? "contraction=off denormals=kept" : "contraction=off denormals=flushed";
}

}  // namespace engine_placeholder::physics
