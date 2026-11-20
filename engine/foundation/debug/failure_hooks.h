#pragma once

#include "types.h"

//! The debugging module's half of the hooks declared in `log/diagnostics_hooks.h`.

namespace vse::debug
{

/// Installs the assert and thread hooks: a stack trace and crash context for a failed assert, crash-handler
/// registration for new threads.
///
/// Called by [[debug::InitDebugFacilities]], so a program that calls [[debug::StartProgramDebugging]] needs nothing
/// else. Before it runs, an assert prints its message line and aborts (an ensure prints and continues) and threads
/// start unregistered. Calling it again is harmless.
///
/// - Fails: never.
/// - Thread-safety: thread-safe. Each hook is one atomic store, so a failure on another thread during installation sees
/// the old or the new
///   behavior, each complete.
void InstallDiagnosticsHooks();

}  // namespace vse::debug
