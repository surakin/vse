// Included by the asan test binaries' lsan_options.cpp. The Vulkan loader unloads the driver (ICD) when the instance is
// destroyed. A leak the driver left behind (process-lifetime state, caches) is then reported with frames that name no
// module, so no suppression can match it, while the same memory is reachable, not a leak, if the driver stays loaded.
// Mesa's radv needs this; NVIDIA's leaks are covered by name in lsan.supp. Runs at the earliest constructor priority,
// before any other static initializer or test can create an instance; a value already in the environment wins.
#pragma once

#include "build_defs.h"
#if ENGINE_SANITIZER_ADDRESS
#include "os/console.h" // declares setenv (the engine has no wrapper that changes the environment)

namespace {
__attribute__((constructor(101))) void KeepDriverLoadedForLeakChecks() {
    setenv("VK_LOADER_DISABLE_DYNAMIC_LIBRARY_UNLOADING", "1", 0);
}
}  // namespace
#endif
