// A small program built for x86-64 and arm64 at once (Phase 21 Day 292; tests/debug/CMakeLists.txt sets its
// architectures), with a dSYM holding both: scripts_macos_test.cpp stores it in a symbol store, which files each
// architecture under its own UUID, and saves a core of it for find-core.sh and open-core.sh. It runs natively on the
// machine's processor.
#include "debug/compiler.h"

namespace
{

ENGINE_NOINLINE int UniversalLeaf(int value)
{
    return value * 3;
}

}  // namespace

int main(int argc, char**)
{
    return UniversalLeaf(argc) == 3 ? 0 : 1;
}
