# Phase 2.5 / Day 42 — Sample Compiler

**Phase:** [Phase 2.5 — Documentation Tooling](../phase-02-5-docs-tooling.md)
**Depends on:** Day 41.

Guarantees that code samples in the documentation compile (and, where tagged, produce the stated output).

## Tasks

- [ ] Extract fenced code blocks tagged `cpp compile` (and `cpp compile run`, optionally followed by an `output` block) from `docs/**/*.md`, remembering file and line.
- [ ] Wrap each sample in a generated translation unit with a documented prelude (standard engine includes, `using namespace`), or use it verbatim if tagged `cpp compile standalone`.
- [ ] Compile with the project's real flags (production options, include roots) by invoking the configured compiler (`posix_spawn`, no shell), in a scratch directory under the build tree; collect diagnostics mapped back to the Markdown file/line.
- [ ] `run` samples: link against the engine libraries, run with a timeout, compare stdout to the `output` block.
- [ ] Parallel compilation with a job count option; failures reported together.
- [ ] `docgen samples` command; tests with passing, failing-to-compile, wrong-output and timeout samples.
- [ ] Security/hygiene: scratch dir cleaned up; no execution of untagged blocks.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit.

**Done:** tagged samples in a test docs tree compile (and run) in CI-style invocation, and each failure mode (compile error, wrong output, timeout) is reported against the Markdown line.
