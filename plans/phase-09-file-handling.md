# Phase 9 — File Handling: `Path`, `File`, `Directory`

**Depends on:** Phase 0 (foundation: strings, containers, allocators, `os/`), Phase 2 (the job system, for the async side), and Phases 6-8
(test scoping, debugging facilities, RHI reset — the debugging facilities keep their signal-safe descriptor layer, see below). Must
complete before Phase 11 (the settings system, which reads and writes config files through this layer) and before the Windows and macOS
ports (Phases 13 and 22): the interfaces here are part of what those ports implement.
**Goal:** one file and path layer for the whole engine, in place of the scattered `fopen`/`open`/`opendir`/`stat`/`mkdir`/`rename` calls and
string-pasted paths that exist today. Three classes: **`io::Path`** (a value type for manipulating paths: join, split, normalize,
relative, extension, comparison), **`io::File`** (an *interface* class: synchronous and asynchronous read and write, so a file can be backed by
the network later; only the local-file implementation is built now), and **`io::Directory`** (create, remove, list, special folders). Every
piece of existing file-access code is ported to them, and a gate keeps new code from bypassing them.
**Definition of done:** `Path`, `File` (interface plus `LocalFile`), `Directory` and the file-system helpers exist with sync and async
operations on local files; a test double that behaves like a slow network file passes the same `File` conformance suite, so the interface
is proven not to assume a local descriptor; every listed access site is ported (the list is in Day 115's inventory and the gate); a
`file_access_check` gate fails the build on a new direct libc/POSIX file call outside the documented exceptions (signal-safe code, the
backend itself, third-party code); the Windows and macOS checklists (`BACKEND.md`) and a cross-target syntax check exist; the guide
`docs/guide/io.md` is complete; the full suite passes in every flavor.

Every day of this phase has its own file under `plans/phase-09-file-handling/` (see `CLAUDE.md`, planning policy).
**Status: COMPLETE (Days 115-124, 2026-11-06 to 2026-11-07).** Closing run: `ran 2914 of 2914 tests (phases: all; flavors: all; display: included; crash tests: included)`, all passing. Days 115-124 are written; `CURRENT_PHASE` changes to 8 in the commit that completes
Day 115's first task. Day numbers continue from Phase 8 (which ended at Day 114). What was built: `io::Path`/`PathBuilder`, `io::Error`/`Status`, the file-system operations and special folders, `io::File` with `LocalFile`, the scheme registry, asynchronous operations over an `IoExecutor` seam with `co_await` adapters, `io::Directory`/`WalkDirectory`, the `file_access_check` gate and `engine/foundation/io/BACKEND.md`; every file-access site ported except the documented exceptions. Design and numbers: `docs/superpowers/specs/2026-11-06-file-layer-design.md`; guide: `docs/guide/io.md`.

## Days

115. [Design Spec, Inventory & Phase Setup](phase-09-file-handling/day-115-design-spec-and-inventory.md)
116. [`io::Path`](phase-09-file-handling/day-116-path.md)
117. [Errors, `FileInfo` and File-System Operations](phase-09-file-handling/day-117-errors-and-file-system.md)
118. [`io::File` and `LocalFile`: the Synchronous Side](phase-09-file-handling/day-118-file-and-local-file.md)
119. [Asynchronous Operations and the Network-Like Test Double](phase-09-file-handling/day-119-async-and-network-double.md)
120. [`io::Directory`](phase-09-file-handling/day-120-directory.md)
121. [Port 1: Foundation, Jobs and Debugging Startup Code](phase-09-file-handling/day-121-port-foundation-and-jobs.md)
122. [Port 2: RHI Pipeline Cache, Textures, Demos and Test Helpers](phase-09-file-handling/day-122-port-rhi-game-and-tests.md)
123. [Port 3: The Offline Tools](phase-09-file-handling/day-123-port-tools.md)
124. [The Access Gate, Backend Checklists, Docs, Closing Full Run & Wrap-up](phase-09-file-handling/day-124-gate-docs-and-closing-run.md)

Order reasoning: Day 115 settles the architecture and takes the inventory of what must be ported, because the layering question (the job
system is above the foundation, but async I/O needs an executor) decides the async design. `Path` (116) is pure and comes first; the
error model and file-system operations (117) are what `File` and `Directory` build on; the synchronous file (118) precedes the async side
(119), which is proven with a network-like double before anything is ported; `Directory` (120) closes the new API. The three port days
(121-123) go from the lowest layers upward so every caller is ported onto an API that already has its own tests. Day 124 adds the gate
that keeps the port from rotting, the platform checklists, and closes the phase.

## Design decisions made while planning (2026-11-06; Day 115 confirms or amends them in the spec)

1. **Where:** `engine/foundation/io/` (headers `path.h`, `file.h`, `directory.h`, `file_system.h`, `error.h`), namespace
   `engine_placeholder::io`, with the Linux implementation in `engine/foundation/io/linux/`. Foundation-level, so everything above can use it.
   The existing `os/` keeps what is not about files (time, memory, libraries, console descriptors).
2. **`Path` is a value type with no syscalls.** UTF-8, `/` as the canonical separator (Windows converts at the boundary), root names and
   drive letters understood by a `PathStyle` (so Windows parsing is tested on Linux), lexical operations only (`Join`, `Parent`, `FileName`,
   `Stem`, `Extension`, `WithExtension`, `Normalize`, `RelativeTo`, `IsAbsolute`, components). Anything that touches the disk (absolute,
   canonical, exists) lives in the file-system helpers, not in `Path`.
3. **`File` is an abstract interface.** A backend implements `Read`, `Write` (at an offset or sequential), `Size`, `Flush`, `Close` and the
   same operations asynchronously, and reports its **capabilities** (seekable, size known, writable, natively asynchronous). `File::Open(path or
   URL, mode)` picks the backend by scheme; only `file` (a plain path) exists now, any other scheme fails with `NotSupported`. Nothing in the
   interface exposes a descriptor, a `FILE*` or a `stat` structure.
4. **Async without a layering inversion.** The job system (coroutines, the scheduler) sits above the foundation, so the foundation defines
   completion-callback operations and an `IoExecutor` interface; `jobs::IoThread` implements the executor and `jobs` provides the
   `co_await` adapters. A backend that is natively asynchronous (a network file) completes on its own thread; a local file runs on the executor.
5. **Errors are values.** An `io::Error` enum (`NotFound`, `PermissionDenied`, `AlreadyExists`, `NotADirectory`, `IsADirectory`, `NoSpace`,
   `Interrupted`, `Unsupported`, `Io`, ...) with the OS code kept for logs; no exceptions, no `errno` leaking out.
6. **The signal-safe layer stays raw.** Code between `// SIGNAL-SAFE-BEGIN` and `// SIGNAL-SAFE-END` (the crash report writer) cannot allocate,
   lock or format; it keeps `os::Write` on descriptors, as do console output and `/proc` reads that must stay async-signal-safe. These are
   the *documented exceptions* of the access gate; everything else is ported.
7. **Atomic replace is a first-class operation** (`WriteFileAtomic`: write a temporary file next to the target, flush, rename): the pipeline cache,
   the minters and, soon, config files all need it, and each re-implements it today.
8. **Special folders are part of the layer:** temp, current, executable, user config, user cache, user data (XDG on Linux; the Windows and
   macOS locations are in the checklists). The pipeline cache and the future settings system find their directories there instead of
   building `$HOME/.cache/...` strings themselves.
9. **Platform designs ride along.** Windows (UTF-16 at the boundary, drive letters and UNC, overlapped I/O, long paths), macOS (case-insensitive
   volumes, `dispatch_io`), Android/iOS (app-private directories, asset bundles as read-only archives), consoles (title storage): the interfaces
   must not preclude any of them; each gets a section in `engine/foundation/io/BACKEND.md` on Day 124, kept in step by a test.
10. **Phase 20's resource system builds on this.** Its pluggable storage backends (pak, filesystem, network; `PLAN.md` assumption #12) are
    `io::File` implementations plus a container reader; nothing in this phase builds the pak or network formats.

## Key deliverables (the day files hold the sub-tasks)

- **`io::Path`**, **`io::File`/`LocalFile`**, **`io::Directory`**, error and info types, file-system helpers, special folders.
- **Sync and async I/O** on local files; the `IoExecutor` seam; coroutine adapters in `jobs`; a network-like test double and a conformance
  suite any `File` backend must pass.
- **The port** of every file-access site (inventory on Day 115) and the **access gate** with its planted-violation tests.
- **Platform checklists** (`BACKEND.md`) with a list test and a cross-target syntax check, as Phases 7 and 8 did.
- **Docs:** `docs/guide/io.md`, `///` comments with contract labels, the reference regenerated.

## Notes / open questions

- **Memory-mapped files** (for the asset system's large read-only archives) are not in this phase; `File` leaves room (a `Map` capability).
- **File watching** (hot reload of config and shaders) is not in this phase; the settings system (Phase 11) may ask for it.
- **Locks and sharing modes** (Windows share flags, `flock`): minimal in this phase (exclusive create, atomic replace); revisit with Phase 13.
- **Text encodings and line endings** are the caller's concern (the layer moves bytes); the config classes in Phase 11 define their own text rules.
- **Large directory trees** (docgen walks the repository): the iteration API must not require loading a whole directory into memory.
- **Open-question dispositions (Day 115):** memory mapping deferred to Phase 20 planning; file watching decided by Phase 11's design; locks/sharing modes Phase 13; `log::LoadConfig` (an existing config reader) is for Phase 11 to absorb or replace. See the spec, section 7.
