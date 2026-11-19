# Maintenance: the engine's crash handler should chain to the handler that was installed before it

Not a numbered phase. Request from the the host project (2026-11-18). Planned 2026-11-19 (below, "The plan"); the user asked for it to be addressed.

## Why

the host has its own fault handlers (SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS through backward-cpp on POSIX; an unhandled-exception filter on Windows) plus a
Rust panic hook. They write the host's crash log (build, account-free metadata, the stderr capture). the host installs them at startup, before any engine exists.

The engine exists in the host only while the 3D world view is shown, and the host wants the engine's minidump when the engine crashes. It would call
`crash_handler_install` after `engine_create`. But the engine's handler does not call the handler that was there before it, so the host's log is lost for
every crash that happens while the engine's handler is installed. the host wants both: the engine's dump and its own log.

## What happens now (measured on Linux, 2026-11-18, `libengine.so.1` from master)

A child process installs its own SIGSEGV handler (it writes a marker file, then restores SIG_DFL and re-raises), creates an engine, optionally calls
`crash_handler_install`, then dereferences null.

| | the earlier handler's marker | engine output |
|---|---|---|
| no `crash_handler_install` | written | none |
| `crash_handler_install` after the earlier handler | **not written** | `crash-report.txt`, `.envelope`, `.crash` |

The process still dies with signal 11. The guide ("If your program has its own signal handler") says the same: a handler installed before is replaced and never runs.

## Wanted

1. After the engine's handler has written its report and dump, it calls the handler that was installed when `crash_handler_install` ran, so a host's earlier
   handler still runs: on POSIX the `sigaction` that was replaced, per signal, invoked with the original `siginfo` and context (honouring `SA_SIGINFO`, and the
   default or ignore dispositions: default means re-raise as now); on Windows the previous unhandled-exception filter; on macOS whatever the equivalent of the
   previous Mach exception port or `SIGABRT` handler is. If there was none, behave as now.
2. A handler that is not the engine's must not be called twice, and must not be called if it is the engine's own (install, uninstall, install again).
3. `crash_handler_uninstall` and `engine_destroy` keep restoring that previous handler, as the guide says today.
4. State in the guide what order the report, the dump and the previous handler run in (the host suggests report, dump, previous handler, then the original fault), and that
   the previous handler runs in signal context on the faulting thread.
5. A way for the host to learn the dump location without the user's directory default: `crash.dump_directory` exists; say in the guide how to set it before
   `crash_handler_install` through the C API.

## Not wanted

- Chaining through sentry's own transport, uploads or a second process. The host decides what to do with the dump.
- Any change when no handler was installed before.

## Tests this needs

- The probe below as a test: the earlier handler's marker is written, and the engine's envelope exists.
- Two earlier handlers on different signals (SIGSEGV and SIGABRT): each chains for its own signal only.
- An earlier handler with `SA_SIGINFO` receives a non-null `siginfo` whose `si_addr` is the fault address.
- Uninstall puts the earlier handler back (exists today); installing twice (`ALREADY_EXISTS`) does not chain to itself.
- Same on Windows (an unhandled-exception filter set before) and on macOS.

## Repro

```cpp
#include <engine/engine.h>
#include <engine/engine_abi_version.h>
#include <engine/engine_loader.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

static const char* g_marker;
static void prev_handler(int sig)
{
    int fd = open(g_marker, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) { write(fd, "previous handler ran\n", 21); close(fd); }
    signal(sig, SIG_DFL);
    raise(sig);
}

int main(int argc, char** argv)
{
    const char* lib = argv[1];
    const char* home = argv[2];
    bool install_engine = argc > 3 && !strcmp(argv[3], "engine");
    pid_t pid = fork();
    if (pid == 0)
    {
        setenv("HOME", home, 1);
        setenv("XDG_DATA_HOME", home, 1);
        setenv("XDG_STATE_HOME", home, 1);
        setenv("XDG_CACHE_HOME", home, 1);
        static char marker[512];
        snprintf(marker, sizeof marker, "%s/prev-ran.txt", home);
        g_marker = marker;
        struct sigaction sa{};
        sa.sa_handler = prev_handler;
        sigaction(SIGSEGV, &sa, nullptr);
        engine_loader loader{};
        if (engine_load(lib, ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, &loader) != ENGINE_OK)
        { fprintf(stderr, "load: %s\n", loader.message); _exit(90); }
        namespace eh = engine_host;
        auto info = eh::Init<engine_create_info>();
        info.flags = ENGINE_CREATE_LOG_SUPPRESS_CONSOLE;
        eh::Engine engine;
        if (!eh::Engine::Create(loader.api, info, &engine).Ok()) { fprintf(stderr, "create failed\n"); _exit(91); }
        if (install_engine)
        {
            auto st = loader.api->crash_handler_install(engine.Get());
            fprintf(stderr, "crash_handler_install status=%d\n", (int)st);
        }
        fflush(stderr);
        volatile int* p = nullptr;
        *p = 1;
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status)) printf("child killed by signal %d\n", WTERMSIG(status));
    else printf("child exited %d\n", WEXITSTATUS(status));
    return 0;
}
```

Build: `clang++ -std=c++20 -I<engine-sdk>/include probe.cpp -o probe -ldl`; run: `./probe <engine-sdk>/lib/libengine.so.1 <temp dir> engine` (and `plain`).

## The plan (2026-11-19)

### Why the earlier handler is lost today (read, Linux)

The chain at a fault is: our recorder (`os/linux/debug/crash_signal.cpp`, installed last, in front) writes the report, then calls the handler it
replaced, which is Breakpad's (`sentry_init` installed it, and Breakpad itself saved the host's handler as its "old" one). Breakpad writes the
dump and then, **because the dump succeeded, installs the default action** (`InstallDefaultHandler`,
`third_party/sentry_native/external/breakpad/src/client/linux/handler/exception_handler.cc:388`); it puts its saved handlers back only when the
dump fails. Our recorder returns, the faulting instruction runs again and the default action ends the process. Nobody calls the host's handler.
`tests/debug/crash_end_to_end_linux_test.cpp` pins exactly that ("A handler installed before the crash handler is replaced by the library's
and does not run"), and the guide documents it.

On Windows the library's filter (Breakpad's) replaces the host's unhandled-exception filter, and our crash gate sits on top; on macOS Breakpad
takes hardware faults as Mach exceptions on its own thread, so a host's POSIX signal handler never sees them, and aborts through a `SIGABRT`
handler. Each platform needs its own route; the Linux one is designed below, the other two are their days' first task.

### The approach (Linux; the shape the other platforms follow)

- **Note the host's handlers before the library installs its own.** `InstallCrashHandler` reads, for each fatal signal, the disposition in
  place before `sentry_init` (`sigaction(sig, nullptr, &host)`), and keeps it per signal (the "host's handler"). An entry that is the engine's
  own recorder or Breakpad's (an install after an uninstall that left them, or a second install) is treated as none.
- **Call it after the dump, in signal context, on the faulting thread:** the recorder calls the library (report, then dump), and when the
  library returns, calls the host's handler for that signal with the original `signo`, `siginfo` and context (`sa_sigaction` when it has
  `SA_SIGINFO`, else `sa_handler`); `SIG_DFL` or `SIG_IGN` means nothing to call (the current behavior). Then the recorder returns as now: the
  fault runs again under the default action Breakpad left, or a raised signal is re-raised. Order: **report, dump, the host's handler, the
  original fault**.
- **Once per crash:** a flag set before the call, so a handler that faults, or a fault on another thread, does not call it twice; the engine's
  own recorder and Breakpad's handler are never treated as the host's.
- **Uninstall** puts the host's handler back (the library's `sentry_close` restores what it saved; the recorder's removal does the rest), and a
  later install notes it again.
- **The dump directory through the C API:** `crash.dump_directory` is a setting given at creation (`engine_create_info.settings`); the guide
  says how to set it before `crash_handler_install`, and where the dump and report land.

### Days

1. [Linux: chain to the host's handler; tests; the guide](host-crash-handler-chaining/01-linux.md)
2. [Windows: the previous unhandled-exception filter (the Windows PC)](host-crash-handler-chaining/02-windows.md)
3. [macOS: the host's signal handlers after a Mach exception (the Mac)](host-crash-handler-chaining/03-macos.md)

**Definition of done:** on each platform, a host handler installed before `crash_handler_install` runs after the engine's report and dump, once,
with the original fault's details, and the process still dies of the original fault; nothing changes when there was no earlier handler;
uninstall restores it; the tests below pass on the platform's machine; the guide documents the order and the dump directory; the request is
answered in the the host notes (`plans/maintenance/`).

**Rules for every day:** the usual day rules (scoped runs in the platform's flavors, `/code-review high`, `docgen lint` at zero, regenerated
`docs/reference`, `STATUS.md` and `KNOWN-ISSUES.md`, commit and push). The fault path is signal-safe code (`// SIGNAL-SAFE-BEGIN`/`END`,
`signal_safety_check`): no allocation, no lock, no C-library formatting in it.

## Done on Linux (2026-11-19, Day 1)

**What changed:**
- **Linux, `crash_handler_install` and `InstallCrashHandler`:** a handler installed before them for a fatal signal (`SIGSEGV`, `SIGBUS`,
  `SIGILL`, `SIGFPE`, `SIGABRT`, `SIGTRAP`) now runs at a crash, **after the report and the dump, once, in signal context on the faulting
  thread**, with the fault's `siginfo` and context. Then the original fault ends the process, which still dies of the original signal.
- **No earlier handler:** nothing changes. Uninstalling still puts it back.
- **the host's own repro** (above, run against `libengine.so.1` built from this commit): `prev-ran.txt` is written **and** the engine's
  `crash-report.txt`, `.envelope` and `.crash` are there; the child dies of signal 11.

**What the host does:**
- Nothing new. Keep installing its handlers at startup, create the engine, then call `crash_handler_install`.
- To choose the dump directory, give `{"crash.dump_directory", "<dir>"}` in `engine_create_info.settings`. The report is
  `<dir>/crash-report.txt`; the envelope and `.crash` go in a `<run id>.run` directory below it.
- The handler must be async-signal-safe, as it already is.
- Under AddressSanitizer with `handle_segv=1`, ASan's handler is an earlier handler too, and its report follows the engine's dump.

**Still open:** Windows (Day 2, the PC) and macOS (Day 3, the Mac). There the earlier handler is still replaced (`KNOWN-ISSUES.md`).
