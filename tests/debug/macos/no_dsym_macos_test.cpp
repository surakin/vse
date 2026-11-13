// Built without a dSYM (Phase 21 Day 290, tests/debug/CMakeLists.txt: no engine_dsym for this executable): libbacktrace
// has no DWARF to read, so a frame names its function from the symbol table and has no file or line. Shows what a build
// without dsymutil gives.
#include <catch2/catch_test_macros.hpp>

#include "cstr/cstr.h"
#include "debug/compiler.h"
#include "debug/stack_trace.h"
#include "types.h"

using namespace engine_placeholder;
using namespace engine_placeholder::debug;

namespace
{

ENGINE_NOINLINE void NoDsymLeaf(StackTrace* trace)
{
    CaptureStackTrace(*trace);
    ENGINE_COMPILER_BARRIER();
}

}  // namespace

TEST_CASE("macOS without a dSYM: frames name their function but have no file or line", "[stacktrace][debug][macos]")
{
    InitDebugFacilities();
    StackTrace trace;
    NoDsymLeaf(&trace);
    FrameInfo entries[kMaxStackFrames * 2];
    const usize n = ResolveStackTrace(trace, entries, sizeof(entries) / sizeof(entries[0]));
    int leaf = -1;
    for (usize i = 0; i < n; ++i)
    {
        if (CStrFind(entries[i].function, "NoDsymLeaf") != nullptr)
        {
            leaf = static_cast<int>(i);
        }
    }
    REQUIRE(leaf >= 0);
    CHECK(entries[leaf].file[0] == '\0');
    CHECK(entries[leaf].line == 0);
}
