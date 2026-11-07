// A windowed program written as the rule wants: the macro, no main of its own (a comment saying int main( is ignored).
#include "platform/entry_point.h"
#include "platform/window_host.h"

int EngineMain(int, char**) { return 0; }

ENGINE_WINDOWED_PROGRAM(EngineMain)
