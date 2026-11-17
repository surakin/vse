// One expression, compiled twice (Phase 25 Day 386): once with the project's flags (`ProbeProject`) and once with
// contraction allowed and, on x86-64, the FMA instructions enabled (`ProbeControl`). The control proves the probe can
// see a fused multiply-add at all: without it, "the project's build does not fuse" would be true of a probe that could
// never detect fusing.
#include "fp_contract_probe.h"

namespace engine_placeholder::physics_test
{

bool FP_PROBE_NAME(const volatile f32* inputs)
{
    const f32 a = inputs[0];
    const f32 c = inputs[1];
    // a * a is 1 + 2^-11 + 2^-24; the rounded product is 1 + 2^-11 and the sum with c is exactly zero, a fused
    // multiply-add keeps the 2^-24.
    return a * a + c != 0.0f;
}

}  // namespace engine_placeholder::physics_test
