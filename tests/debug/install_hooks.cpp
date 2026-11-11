// The tests in this directory check assert and error reports with their stack traces and crash context. A program gets
// those from debug::InstallDiagnosticsHooks (which debug::InitDebugFacilities and debug::StartProgramDebugging call); a
// test executable has no main of its own, so it installs them before any test runs.
#include "debug/failure_hooks.h"

namespace
{
const int g_hooks_installed = (engine_placeholder::debug::InstallDiagnosticsHooks(), 0);
}  // namespace
