# Phase 8 / Day 124 — The Access Gate, Backend Checklists, Docs, Closing Full Run & Wrap-up

**Phase:** [Phase 8 — File Handling](../phase-08-file-handling.md)
**Depends on:** Days 115-123.

Keep the port from rotting, make the layer something Windows and macOS can implement without redesign, finish the documentation, run everything, close the phase.

## Tasks

- [ ] **The access gate** `tools/file_access_check` (a ctest `file_access_check`, like `rhi_boundary_check` and `raw_new_check`): fails on a direct file or directory call (`fopen`, `open(`, `openat`, `opendir`, `readdir`, `stat`, `lstat`, `mkdir`, `rename`, `unlink`, `realpath`, `getcwd`, `std::ifstream`/`ofstream`, `fread`, `fwrite`,
      `access(`) in `engine/`, `game/`, `tools/` and `tests/` outside an allow list with a **reason per entry**: the io backend itself (`engine/foundation/io/linux`), the descriptor primitives of `os/`, code between `SIGNAL-SAFE` markers, the Wayland `memfd`, the tests of the gate, `third_party`. A stale allow-list
      entry (no longer matching anything) also fails. Self-tests with planted violations and a planted stale entry, as the other gates have.
- [ ] **Backend checklists:** `engine/foundation/io/BACKEND.md` — what Windows (UTF-8 to UTF-16 at the boundary, drive letters and UNC, long paths, `CreateFileW`, overlapped I/O, `ReplaceFile`/`MoveFileEx` for atomic replace, known folders), macOS (case-insensitive volumes, `dispatch_io`, `NSFileManager` locations), Android and iOS
      (app-private directories, the asset bundle as a read-only archive) and consoles (title storage) each implement, one `###` section per function that carries a `Backend:` label; a ctest `io_backend_lists` keeps the list in step with the headers (as `debug_backend_lists` does) and
      `io_cross_syntax_windows|macos` compile the neutral headers for those targets with clang's `-fsyntax-only` (extending the shim as Day 114 of Phase 7 did).
- [ ] Add the Windows and macOS file-layer items to the Phase 10 and 11 plan files (the backend, the `File` conformance suite run, the special folders), and a note to Phase 17 (the resource system's storage backends are `io::File` implementations) and Phase 13 (the UI's file dialogs will need `Path`).
- [ ] **Guide pass:** read `docs/guide/io.md` end to end as a new developer (Path, Errors, Looking at the disk, Special folders, Reading and writing, Replacing atomically, Async, Writing a File backend, Directories); a "Which API do I use for a file?" table at the top; every tagged sample compiles and runs;
      `docs/guide/README.md` and `platform-os.md` current; `docgen lint` zero; reference regenerated and committed.
- [ ] Update `CLAUDE.md`: a hard architectural constraint "All file and directory access goes through `io::File`, `io::Path` and `io::Directory`" with its exceptions (signal-safe descriptors, console, third-party) and a pointer to the gate; `PLAN.md` assumption added for the file layer; `STATUS.md` build and test commands.
- [ ] Known-issues sweep: every deferral filed (memory mapping, file watching, share modes and locks, the Windows and macOS backends, network backends not built, non-ASCII case folding on Windows), entries this phase fixed removed.
- [ ] **Closing full run (CLAUDE.md, required for a phase's last day):** `scripts/test-phase.sh --all --flavor all`, `ctest -L docs`, both demos 1000 frames on Wayland and X11 with `--validation`; record the final `ran X of Y tests (phases ...)` line in this file; nothing left out.
- [ ] **End-of-phase review:** `/code-review high` over the whole phase's diff (Days 115-124 as committed); fix or decline each finding with a reason; record it under "Post-implementation code review".
- [ ] Update `plans/phase-08-file-handling.md` (status complete, days marked done), `PLAN.md` (roadmap row, progress-log entry), `STATUS.md`; `CURRENT_PHASE` stays 8 until Phase 9's day files exist and it starts (policy); commit.

**Done:** the gate and the checklist tests pass, the guide is complete, `CLAUDE.md` states the rule, the closing line `ran X of Y tests (phases: all; flavors: all)` is recorded with every test passing, the demos are clean on both display backends, and Phase 8 is marked complete.
