# Crash handler chaining / Day 3: macOS: the host's signal handlers after a Mach exception (the Mac)

**Series:** [the crash handler should chain to the handler installed before it](../host-crash-handler-chaining.md)
**Depends on:** Day 1; Phase 21 Day 291 (`os/macos/debug/crash_hook.cpp`: Breakpad takes faults as Mach exceptions on its own thread and
aborts through a `SIGABRT` handler).

## Tasks

- [ ] **Read first:** with Breakpad holding the task's exception ports, a hardware fault never becomes a signal while the handler is in, so a
  host's `SIGSEGV` handler is never reached; find what Breakpad does after the dump (forward to the previous exception ports, or let the
  thread die) and record it. Decide between: (a) after the dump, restore the host's signal handlers and let the fault be delivered as a
  signal (the thread resumed with the exception unhandled, so the kernel turns it into the BSD signal), or (b) call the host's handler from
  Breakpad's thread (wrong thread: rejected unless (a) cannot work). Record the decision.
- [ ] **Note the host's handlers** (signals, and the previous exception ports if (a) needs them) before `sentry_init`.
- [ ] **Chain:** per the decision; `abort()` (the `SIGABRT` path, on the crashing thread) calls the host's `SIGABRT` handler as on Linux.
- [ ] **Tests:** the Day 1 cases on macOS (x86-64 runs; arm64 compiles), and `crash_host.c`'s macOS host.
- [ ] **The guide:** the macOS order and what differs (the thread the handler runs on, if it does).
- [ ] **Standing rows:** as Day 1, on the Mac (plain only, the Mac's rule).

**Done when:** on macOS, a host handler installed before `crash_handler_install` runs once after the report and dump for a hardware fault and
for an abort, and the process still dies of the original signal; or, if (a) proves impossible, the limit is documented and recorded.
