#pragma once

#include "debug/death_test.h"

// The per-platform half of the death-test helper (death_test.h). Each platform backend implements these; the neutral
// code in debug/death_test.cpp calls them. Not part of the public API.
namespace engine_placeholder::debug::detail {

// True if the platform can start child processes.
bool PlatformCanSpawn();

// Starts the running program as a child for the death test `name` and watches it (see RunInChild).
ChildResult PlatformRunChild(const char* name, const char* argument, const ChildOptions& options);

// If this process was started as a death-test child, stores the requested name and argument (pointers into the
// environment) and returns true.
bool PlatformChildRequest(const char** name, const char** argument);

// Applies the parent's requests that the child must carry out itself (for example turning core dumps off).
void PlatformPrepareChild();

} // namespace engine_placeholder::debug::detail
