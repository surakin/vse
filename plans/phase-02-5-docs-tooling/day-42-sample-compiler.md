# Phase 2.5 / Day 42 — Sample Compiler

**Phase:** [Phase 2.5 — Documentation Tooling](../phase-02-5-docs-tooling.md)
**Depends on:** Day 41.

Guarantees that code samples in the documentation compile (and, where tagged, produce the stated output).

## Tasks

- [x] Extract fenced code blocks tagged `cpp compile` (and `cpp compile run`, optionally followed by an `output` block) from `docs/**/*.md`, remembering file and line.
- [x] Wrap each sample in a generated translation unit with a documented prelude (standard engine includes, `using namespace`), or use it verbatim if tagged `cpp compile standalone`.
- [x] Compile with the project's real flags (production options, include roots) by invoking the configured compiler (`posix_spawn`, no shell), in a scratch directory under the build tree; collect diagnostics mapped back to the Markdown file/line.
- [x] `run` samples: link against the engine libraries, run with a timeout, compare stdout to the `output` block.
- [x] Parallel compilation with a job count option; failures reported together.
- [x] `docgen samples` command; tests with passing, failing-to-compile, wrong-output and timeout samples.
- [x] Security/hygiene: scratch dir cleaned up; no execution of untagged blocks.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [x] Update `STATUS.md`; commit.

**Done:** tagged samples in a test docs tree compile (and run) in CI-style invocation, and each failure mode (compile error, wrong output, timeout) is reported against the Markdown line.

## Result

New: `tools/docgen/src/samples.{h,cpp}` (`ExtractSamples`, `RunSamples`,
`RequestStop`), the `docgen samples` command, `CollectMarkdownFiles` (shared
with `links`), `MdFence::end_line`, `tests/docgen/samples_test.cpp` (18 cases;
suite is 152 cases, plain and under ASan/UBSan). Verified by hand against the
real engine headers and `libfoundation.a`: a `cpp compile run` sample using
`StringBuilder` compiles, links, runs and matches its `output` block in
~0.1 s. The tag semantics are specified in `docs/DOC-CONVENTIONS.md`, the
mechanism in DESIGN.md section 11.

Decisions: non-standalone samples are wrapped in a function (`compile`) or
`main` (`run`) so statement-style snippets work and free functions need
`standalone`; `#line` carries Markdown positions so no diagnostic remapping
exists; scheduling is one thread polling `waitpid` over N child processes;
compile uses `-c` (not `-fsyntax-only`) so code generation errors surface.
The prelude `docs/samples-prelude.h` and the real flags/libraries are wired
by Day 43 (CMake); no engine doc contains a tagged sample yet.

## Post-implementation code review (2026-11-04, `/code-review high`)

Fixed, each with a `[review]` test where practical:
- A misspelled tag (`cpp complie`) skipped the block silently → any unknown
  word after `cpp` is an error.
- `waitpid` returning -1 was read as a clean exit → the sample fails ("lost
  track of the child").
- Children chdir'd into the scratch directory, breaking relative compiler/
  flag paths → only programs run there; compilers keep the caller's directory;
  a `--compiler` containing `/` is made absolute.
- `jobs == 0` looped forever → clamped to 1 in the library.
- Ctrl-C/SIGTERM left child process groups and the scratch directory behind →
  handlers set a flag, children are killed, scratch removed, "interrupted"
  error (tested through `RequestStop()`, which the handler also calls).
- `BlankBetween` rescanned the file per sample → line starts computed once.
- Collected Markdown/tag errors were dropped when the build setup failed →
  printed first.
- `indent` was documented as columns but counts characters → documented as
  characters (a tab is one, consistently on both sides).
