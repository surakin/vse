// Prints the probe's hash and the build's floating-point mode (Phase 25 Day 386). The Windows PC and the Mac run it on
// Days 410-411 and compare the line.
#include "os/console.h"
#include "physics/determinism.h"

int main()
{
    engine_placeholder::os::Print("fp_determinism_probe: hash %016llx %s\n",
                                  static_cast<unsigned long long>(engine_placeholder::physics::ProbeHash()),
                                  engine_placeholder::physics::ModeReport());
    return 0;
}
