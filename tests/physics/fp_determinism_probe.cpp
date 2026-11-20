// Prints the probe's hash and the build's floating-point mode (Phase 25 Day 386). The Windows PC and the Mac run it on
// Days 410-411 and compare the line.
#include "os/console.h"
#include "log/output.h"
#include "physics/determinism.h"

int main()
{
    vse::out::Print("fp_determinism_probe: hash %016llx %s\n",
                    static_cast<unsigned long long>(vse::physics::ProbeHash()), vse::physics::ModeReport());
    return 0;
}
