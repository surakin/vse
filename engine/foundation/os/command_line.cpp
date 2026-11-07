#include "os/command_line.h"

namespace engine_placeholder::os {

namespace {

int g_argc = 0;
char** g_argv = nullptr;

} // namespace

void StashNativeCommandLine(int argc, char** argv) {
    g_argc = argc;
    g_argv = argv;
}

void Detail_NativeCommandLineArgs(int* out_argc, char*** out_argv) {
    ENGINE_ASSERT(g_argv != nullptr, "Detail_NativeCommandLineArgs: StashNativeCommandLine was never called");
    *out_argc = g_argc;
    *out_argv = g_argv;
}

} // namespace engine_placeholder::os
