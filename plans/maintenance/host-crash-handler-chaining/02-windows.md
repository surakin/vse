# Crash handler chaining / Day 2: Windows: the previous unhandled-exception filter (the Windows PC)

**Series:** [the crash handler should chain to the handler installed before it](../host-crash-handler-chaining.md)
**Depends on:** Day 1 (the shape and the tests); Phase 13 Day 155 (`os/windows/debug/crash_hook.cpp`, the crash gate).

## Tasks

- [ ] **Read first:** what `sentry_init`'s Breakpad backend does with the filter it replaces (`SetUnhandledExceptionFilter`'s previous value),
  whether it calls it after the dump, and where our gate (`g_filter_before_gate`) sits; record the chain in the notes.
- [ ] **Note the host's filter** before `sentry_init` (the value `SetUnhandledExceptionFilter` returns, or a read of it), never the gate or the
  library's own.
- [ ] **Call it after the dump:** from `on_crash` (`OnLibraryCrash`, after the report) or from the gate once the library has written the dump,
  with the original `EXCEPTION_POINTERS`, once; its return value is ignored for the decision (the process ends with the original exception
  code as now).
- [ ] **Tests:** the Day 1 cases with a filter set before (`SetUnhandledExceptionFilter`) instead of a signal handler: it runs once after the
  report and dump, an access violation and an `abort()` each reach it, install/uninstall/install does not call it twice, and `crash_host.c`'s
  Windows host sees its marker; plain and asan, MSVC and clang-cl.
- [ ] **The guide:** the Windows order and caveats (the filter runs on the faulting thread or on Breakpad's handler thread, as found).
- [ ] **Standing rows:** as Day 1, on the PC.

**Done when:** on Windows, a host filter set before `crash_handler_install` runs once after the report and dump with the original exception,
and the process still ends with the original code.
