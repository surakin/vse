// The floating-point rules as the build gives them (Phase 25 Day 374): no contraction in the project's own code,
// denormals kept, and the probe's hash equal to the committed constant (the same constant the Windows PC and the Mac
// must produce, Days 398-399).
#include <catch2/catch_test_macros.hpp>

#include "build_defs.h"
#include "fp_contract_probe.h"
#include "physics/determinism.h"

using namespace engine_placeholder;
using namespace engine_placeholder::physics;

namespace
{

// The committed value of physics::ProbeHash() on every machine that follows the rules.
constexpr u64 kProbeHash = 0x50e1be9251ea408full;

// Whether the machine can run the control probe: x86-64 builds it with -mfma, so the CPU must have the instruction;
// arm64 always has it.
bool ControlRunnable()
{
#if ENGINE_COMPILER_CLANG || ENGINE_COMPILER_GCC
#if ENGINE_ARCH_X64
    return __builtin_cpu_supports("fma") != 0;
#elif ENGINE_ARCH_ARM64
    return true;
#else
    return false;
#endif
#else
    return false;
#endif
}

}  // namespace

TEST_CASE("Determinism: the project's flags forbid contraction to a fused multiply-add", "[physics][determinism]")
{
    static const volatile f32 inputs[2] = {1.0f + 1.0f / 4096.0f, -(1.0f + 1.0f / 2048.0f)};
    INFO(ModeReport());
    CHECK_FALSE(physics_test::ProbeProject(inputs));
    CHECK_FALSE(ContractionObserved());
}

TEST_CASE("Determinism: the control probe does fuse, so the check above can fail", "[physics][determinism]")
{
    if (!ControlRunnable())
    {
        SKIP("this build or machine has no fused multiply-add to observe");
    }
    static const volatile f32 inputs[2] = {1.0f + 1.0f / 4096.0f, -(1.0f + 1.0f / 2048.0f)};
    CHECK(physics_test::ProbeControl(inputs));
}

TEST_CASE("Determinism: denormals are kept", "[physics][determinism]")
{
    CHECK(DenormalsPreserved());
}

TEST_CASE("Determinism: the probe's hash is the committed constant", "[physics][determinism]")
{
    INFO(ModeReport());
    CHECK(ProbeHash() == kProbeHash);
    CHECK(ProbeHash() == ProbeHash());
}
