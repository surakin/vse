// A shared library the stack-trace tests load at run time (see stack_trace_test.cpp): it calls back into the test so the
// trace contains a frame inside a library the program did not start with.
#include "debug/compiler.h"

extern "C" {

ENGINE_NOINLINE void DebugTestPluginCall(void (*callback)()) {
    callback();
    ENGINE_COMPILER_BARRIER(); // keep the call from being a tail call, so this frame stays on the stack
}

}
