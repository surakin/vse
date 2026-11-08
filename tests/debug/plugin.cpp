// A shared library the stack-trace tests load at run time (see stack_trace_test.cpp): it calls back into the test so the
// trace contains a frame inside a library the program did not start with.
#include "debug/compiler.h"
#include "build_defs.h"

#if ENGINE_PLATFORM_WINDOWS
#define DEBUG_TEST_PLUGIN_EXPORT __declspec(dllexport) // a DLL exports only what it names (Phase 13 Day 154)
#else
#define DEBUG_TEST_PLUGIN_EXPORT
#endif

extern "C" {

DEBUG_TEST_PLUGIN_EXPORT ENGINE_NOINLINE void DebugTestPluginCall(void (*callback)()) {
    callback();
    ENGINE_COMPILER_BARRIER(); // keep the call from being a tail call, so this frame stays on the stack
}

}
