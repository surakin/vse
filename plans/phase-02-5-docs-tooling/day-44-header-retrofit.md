# Phase 2.5 / Day 44 — Retrofit Existing Headers & Adopt the Rule

**Phase:** [Phase 2.5 — Documentation Tooling](../phase-02-5-docs-tooling.md)
**Depends on:** Day 43.

Brings Phases 0-2 up to the new convention so the tool starts from a clean, fully documented baseline.

## Tasks

- [x] Foundation memory, containers, types: convert existing comments to `///`, add missing summaries and contract labels.
- [x] Foundation strings/Unicode and math.
- [x] Foundation log/assert and the OS layer (`os.h`, file util, command line, driver probe, window, input, Linux Wayland/X11/xkb headers).
- [x] Jobs: `job.h`, `scheduler.h`, `counter.h`, `deque.h`, `worker_pool.h`, `topology.h`, `frame_pool.h`, `parallel_for.h`, `async_io.h` — move the existing contract prose (ownership, lifetime, shutdown, capacity) into labeled fields.
- [x] Fix the module map and lint config so strict modules are strict; raise the baseline to the achieved coverage (target 100% of public symbols).
- [x] Regenerate and commit `docs/reference/`; fill in the `docs/guide/` pages' API-link placeholders using `[[refs]]`.
- [x] **Propose** adding the "a day that changes public API is documented before it is done" rule to `CLAUDE.md` and `PLAN.md` and wait for the user's approval before editing them.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [x] Update `STATUS.md`; commit.

**Done:** coverage for Phases 0-2 is 100% (or each exception is listed with a reason), all four docs checks are green, and the user has decided on the CLAUDE.md rule.

## Result

All 41 public headers of Phases 0-2 now carry `///` comments (file overviews
as `//!`), done by four parallel retrofit passes (foundation core; OS and
window; Linux platform; jobs) that converted the existing `//` prose into
summaries and labeled fields (`Param`, `Returns`, `Fails`, `Aborts`,
`Thread-safety`, `Ownership`, `Internal`). Lock-free invariants that used to
sit in header prose (Counter's state word, the deque's slot-reuse argument)
stay as plain comments next to the code they constrain. Coverage is
**667/667 (100%)** in every module; the lint baseline has **0** known findings;
`docs/reference/` is regenerated and committed; all four docs checks pass; the
guide pages gained an "API reference" section with `[[refs]]` (40 references
resolve). `= delete`/`= default` members are exempt and render as
`_Deleted._`/`_Defaulted._`. `Detail_*` helpers that have to be public are
marked `Internal:` and left out of the reference.

Verification: the diff against HEAD was compared with comments stripped for
all 41 headers: identical code except reformatting two enums over several
lines to hold per-enumerator comments (`LogVerbosity`, `os::Key`) and
`WindowBackend` (plus a trailing comma). Every header still parses; the full
suite passes except `X11Window::Create opens a mapped window on the real X
server`, which fails the same way on an unmodified HEAD in this Wayland
session (environmental, unrelated).

## Code observations from the retrofit (reported by the passes; not fixed here)

- `log.cpp` `ParseVerbosityName`: an unrecognized level in a config line sets
  the channel to `Warn` instead of skipping the line (documented as is).
- `Log` truncates over-long lines to 512 bytes and loses the trailing newline.
- `Array::Insert/Remove/operator[]` and `Vec*::operator[]` have comment-only
  preconditions; `Push(const T&)`/`Insert` break if the argument aliases an
  element and the array grows.
- A moved-from `String` has no storage: `CStr()`/`Length()` are invalid on it.
- `StringBuilder::AppendFloat` is undefined for huge values, NaN/inf and
  `precision` > 18; `StringView(const char*)` crashes on null.
- `os::ReleaseMemory` ignores the `munmap` result; `os::GetSymbol` does not
  guard a null handle; `os::Exit` runs atexit handlers/static destructors;
  `ReadFileToArena` cannot distinguish an empty file from failure when the
  arena returns null for a zero-size allocation.
- `Scheduler::ResumesBy`/`StealsBy` do not bounds-check the participant index;
  `FramePool::~FramePool` does not check for outstanding blocks;
  `Scheduler::Shutdown` uses a plain `bool`, so concurrent calls race (it is
  owner-thread-only); `ParseParticipantOverride` sets `*warned` for an empty
  string.
- `XkbKeyboard::UpdateMask` old comment claimed a bool return (it is `void`).
These are candidates for the next phase's cleanup, not part of this day.

## Post-implementation code review (2026-11-04, `/code-review high`)

Fixed:
- `DecodeUtf8At`'s `out_valid` doc said it is untouched for valid input; it
  is always written when non-null.
- `LoadConfig` doc omitted that an unrecognized verbosity sets `Warn`.
- `Quaternion::FromMat3` doc claimed it divides by the largest component; it
  is the usual four-branch selection and the chosen one is not always the
  largest.
- `AppendFloat` doc omitted that `precision` above 18 overflows.
- The rewrite had dropped the Counter state-word and deque slot-reuse
  invariants → restored as comments at the members / in the overview.
- `docgen samples` reported failures in completion order, which varies with
  `--jobs` (found by the parallel sample test flaking under load) → reported in
  document order (the existing parallel-vs-serial test is the regression).
- No test covered `_Defaulted._` → added.
- (STATUS.md and this file were updated in this commit.)

## Rule proposal for CLAUDE.md / PLAN.md — awaiting the user's decision

Not applied. Proposed addition to CLAUDE.md under "End-of-day code review":

> **Public API is documented before a day is done.** A day that adds or
> changes public API (anything in a header named by `docs/reference/modules.txt`)
> is not complete until the header carries `///` comments that pass
> `docgen lint` (the baseline stays at zero known findings), `cmake --build
> build --target docs` has been run and its output committed, and the
> relevant guide page under `docs/guide/` is updated or created. The day
> file's Done condition lists the pages. `ctest -L docs` must pass.

and a matching one-paragraph note in PLAN.md's principles/process section.
