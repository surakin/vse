#pragma once

#include <csignal>

//! Signal handlers (Phase 16): the engine's spelling of `std::signal`. The `SIG*` constants and the POSIX `sigaction` family arrive
//! through this header.
namespace engine_placeholder::os {

/// A function the system calls with the signal number.
using SignalHandler = void (*)(int);

/// Installs `handler` for `signal` and returns the previous one, or `SIG_ERR` when it could not be installed (`std::signal`).
///
/// Thread-safety: not-thread-safe. Handlers are process-wide.
inline SignalHandler SetSignalHandler(int signal, SignalHandler handler) noexcept { return std::signal(signal, handler); }

} // namespace engine_placeholder::os
