# Phase 9 / Day 124 — The Access Gate, Backend Checklists, Docs, Closing Full Run & Wrap-up

**Phase:** [Phase 9 — File Handling](../phase-09-file-handling.md)
**Depends on:** Days 115-123.

Keep the port from rotting, make the layer something Windows and macOS can implement without redesign, finish the documentation, run everything, close the phase.

## Tasks

- [x] **The access gate** `tools/file_access_check` (a ctest `file_access_check`, like `rhi_boundary_check` and `raw_new_check`): fails on a direct file or directory call (`fopen`, `open(`, `openat`, `opendir`, `readdir`, `stat`, `lstat`, `mkdir`, `rename`, `unlink`, `realpath`, `getcwd`, `std::ifstream`/`ofstream`, `fread`, `fwrite`,
      `access(`) in `engine/`, `game/`, `tools/` and `tests/` outside an allow list with a **reason per entry**: the io backend itself (`engine/foundation/io/linux`), the descriptor primitives of `os/`, code between `SIGNAL-SAFE` markers, the Wayland `memfd`, the tests of the gate, `third_party`. A stale allow-list
      entry (no longer matching anything) also fails. Self-tests with planted violations and a planted stale entry, as the other gates have.
- [x] **Backend checklists:** `engine/foundation/io/BACKEND.md` — what Windows (UTF-8 to UTF-16 at the boundary, drive letters and UNC, long paths, `CreateFileW`, overlapped I/O, `ReplaceFile`/`MoveFileEx` for atomic replace, known folders), macOS (case-insensitive volumes, `dispatch_io`, `NSFileManager` locations), Android and iOS
      (app-private directories, the asset bundle as a read-only archive) and consoles (title storage) each implement, one `###` section per function that carries a `Backend:` label; a ctest `io_backend_lists` keeps the list in step with the headers (as `debug_backend_lists` does) and
      `io_cross_syntax_windows|macos` compile the neutral headers for those targets with clang's `-fsyntax-only` (extending the shim as Day 114 of Phase 8 did).
- [x] Add the Windows and macOS file-layer items to the Phase 13 and 14 plan files (the backend, the `File` conformance suite run, the special folders), and a note to Phase 20 (the resource system's storage backends are `io::File` implementations) and Phase 16 (the UI's file dialogs will need `Path`).
- [x] **Guide pass:** read `docs/guide/io.md` end to end as a new developer (Path, Errors, Looking at the disk, Special folders, Reading and writing, Replacing atomically, Async, Writing a File backend, Directories); a "Which API do I use for a file?" table at the top; every tagged sample compiles and runs;
      `docs/guide/README.md` and `platform-os.md` current; `docgen lint` zero; reference regenerated and committed.
- [x] Update `CLAUDE.md`: a hard architectural constraint "All file and directory access goes through `io::File`, `io::Path` and `io::Directory`" with its exceptions (signal-safe descriptors, console, third-party) and a pointer to the gate; `PLAN.md` assumption added for the file layer; `STATUS.md` build and test commands.
- [x] Known-issues sweep: every deferral filed (memory mapping, file watching, share modes and locks, the Windows and macOS backends, network backends not built, non-ASCII case folding on Windows), entries this phase fixed removed.
- [x] **Closing full run (CLAUDE.md, required for a phase's last day):** `scripts/test-phase.sh --all --flavor all`, `ctest -L docs`, both demos 1000 frames on Wayland and X11 with `--validation`; record the final `ran X of Y tests (phases ...)` line in this file; nothing left out.
- [x] **End-of-phase review:** `/code-review high` over the whole phase's diff (Days 115-124 as committed); fix or decline each finding with a reason; record it under "Post-implementation code review".
- [x] Update `plans/phase-09-file-handling.md` (status complete, days marked done), `PLAN.md` (roadmap row, progress-log entry), `STATUS.md`; `CURRENT_PHASE` stays 8 until Phase 11's day files exist and it starts (policy); commit.

**Done:** the gate and the checklist tests pass, the guide is complete, `CLAUDE.md` states the rule, the closing line `ran X of Y tests (phases: all; flavors: all)` is recorded with every test passing, the demos are clean on both display backends, and Phase 9 is marked complete.

## Result

- **Gate:** `tools/file_access_check` (`check.cmake`, `allowlist.txt`, fixtures, ctest `file_access_check` plus planted, count, clean, stale, stale-message and no-reason cases): scans 420 C/C++ files under `engine/`, `game/`, `tools/`, `tests/` for calls that open or name a path (`fopen`, `open`, `opendir`, `readdir`, `stat`, `mkdir`, `rename`, `unlink`, `realpath`, `getcwd`, `access`, `chmod`, `mkstemp`, `memfd_create`, `std::ifstream`/`ofstream`/`filesystem`, ...); calls on something already open (`read`, `write`, `fstat`, `fsync`, `fwrite` to stderr) are not path access. Exempt: `SIGNAL-SAFE` regions, member calls, comments and strings, and 11 allow-list entries with reasons (the Linux backend, `os.cpp`'s two descriptor primitives, `module_list.cpp`, two `memfd` users, and the tests that plant raw shapes on purpose: `tests/io`, `tests/debug`, three docgen and jobs files, the pipeline-cache damage test). A stale entry or one without a reason fails. Porting the last `/proc` readers (`/proc/self/statm`, `/proc/self/fd`, `Cpus_allowed_list`, the executable path in `InitCrashContext`) onto `io` was part of making the tree clean.
- **Backend checklists:** `engine/foundation/io/BACKEND.md` (32 `###` sections, one per function with a `Backend:` label in the io headers, plus the rules every backend follows, platform notes and the `File` interface); ctest `io_backend_lists` (and a planted bad copy that must fail), `io_cross_syntax_windows` and `io_cross_syntax_macos` (the neutral headers compile for those targets with clang's `-fsyntax-only` and the existing shim; the check was shown to reject a `<unistd.h>` leak). The Phase 7 list test now skips the io headers (they have their own list) and `CrashDumpDirectory` moved from the Windows/macOS debug checklists to the io one.
- **Plans:** Phase 13, 14 and 20 already carried their file-layer items; Phase 16 got its note (file dialogs use `Path`/`Directory`).
- **Guide pass:** `docs/guide/io.md` read end to end; a "Which API do I use?" table at the top; every tagged sample compiles and runs (`docs_samples`); `docs/guide/README.md`, `platform-os.md`, `jobs.md`, `rhi.md`, `building-and-testing.md` current; reference regenerated, `docgen lint` at 3394/3394.
- **CLAUDE.md** states the rule and the exception list with a pointer to the gate; `PLAN.md` assumption #21 describes what was built.
- **Known issues:** the deferrals are filed in `KNOWN-ISSUES.md` (memory mapping, watching, share modes and locks, the Windows/macOS/mobile/console backends, network and pak backends, Windows non-ASCII case folding, platform-neutral code still under `io/linux/`, the remaining `os::Open` users, parents not checked, `/tmp/re_io_test.*` leftovers).
- **Closing run** (on the final code, after the review fixes below): `ran 2914 of 2914 tests (phases: all; flavors: all; display: included; crash tests: included)` in 36 s, **all 2914 passed**; nothing left out. `ctest -L docs` 4/4. `rhi_demo` and `renderer_demo --frames 1000 --validation` ran to the end on Wayland and on X11 with no validation messages and flat memory (rhi_demo 61776 to 61796 KiB Wayland, 58700 to 58720 KiB X11; renderer_demo 68712 to 68724 and 65648 to 65660 KiB, 18 open descriptors, 0 resets). An earlier full run the same day (before the review fixes) had one failure, `X11Window::Create opens a mapped window on the real X server`, the test filed in `KNOWN-ISSUES.md` since Phase 6 Day 85, which fails or passes depending on the session's Xwayland (it failed alone twice right after that run and passed in the final one); it is not related to the file layer.

## Post-implementation code review

End-of-phase `/code-review high` over `git diff 5129ab4` (Days 115-124): 10 findings, 8 fixed, 2 declined.

Fixed:
1. **`InitCrashContext` allocated on the assert path.** Day 121 had ported its `/proc` reads and the executable path onto `io`; the assert handler calls it lazily, possibly while the heap is inconsistent. Restored to the raw primitives (as before Phase 9), recorded as exception E2 in the allow list, the spec and `KNOWN-ISSUES.md`; the Day 121 file carries an amendment.
2. `ListDirectory` reserved one slot per entry (quadratic): geometric growth.
3. `Copy` read an uninitialized `stat` result when `fstat` of the destination failed: the failure is checked first.
4. `RemoveTree` could fail with `NotEmpty` on file systems that skip entries while a directory is being emptied: it sweeps again, up to three times.
5. The awaitable of `ReadAllAsync` carried a 64 KiB buffer in the awaiting coroutine's frame: allocated when the read starts.
6. A redundant `ClearDefaultExecutorIf` in `~IoThread`: only `Shutdown` withdraws it.
7. `file://...%00...` was reported as `TooLong`: `Invalid` (test).
8. `WriteFileAtomic`'s side effects (the writing user owns the replacement; hard links, ACLs and extended attributes are not carried over) are documented, and the pipeline cache passes 0644 as before.
9. `WalkDirectory` ignored an out-of-memory while recording the root for loop detection: returns `OutOfMemory`.
10. A stray `<dirent.h>` in two files after the port: removed.

Declined: none beyond folding item 8's second part into documentation (the layer deliberately replaces by rename; an in-place write would not be atomic).
