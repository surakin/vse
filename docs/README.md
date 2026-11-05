# Engine Documentation

End-user documentation for the engine. **Status: skeleton.** Every page
below exists as an outline with pointers to its source of truth and the
facts already known; none is written yet. See [DOC-CONVENTIONS.md](DOC-CONVENTIONS.md) for how
pages are structured and when a day's work counts as documented.

Internal design history is for contributors, not engine users: the specs in
`docs/superpowers/specs/` —
[job system](superpowers/specs/2026-08-13-job-system-design.md),
[Unicode string support](superpowers/specs/2026-08-14-unicode-string-support-design.md),
[command-line wrapper](superpowers/specs/2026-08-16-command-line-wrapper-design.md),
[RHI design](superpowers/specs/2026-11-04-rhi-design.md),
[RHI portability review](superpowers/specs/2026-11-04-rhi-portability-review.md),
[renderer design](superpowers/specs/2026-11-04-renderer-design.md),
[renderer portability review](superpowers/specs/2026-11-04-renderer-portability-review.md) —
and the plans in `plans/`.

## Map

| Section | What it is | Pages |
|---|---|---|
| [guide/](guide/README.md) | Concept pages, one per subsystem — *how it works and how to use it* | 11 |
| [reference/](reference/README.md) | API reference, generated from header comments — *what every symbol does* | index + per-module stubs |
| [tutorials/](tutorials/README.md) | Task-oriented walkthroughs — *do this, get that* | 4 |

## Coverage by phase

| Phase | Status | Documented in |
|---|---|---|
| 0 — Foundation | implemented, docs skeleton | guide: memory, containers, strings, math, logging |
| 1 — Linux platform | implemented, docs skeleton | guide: platform-os, windowing-input |
| 2 — Job system | implemented, docs skeleton | guide: jobs; tutorials: parallel-jobs, async-file-io |
| 3 — Vulkan RHI | implemented, guide drafted | guide: rhi, rhi-shaders; tutorial: hello-rhi |
| 4 — Renderer core | implemented, guide drafted | guide: renderer, rhi-shaders; tutorial: hello-renderer; reviews: RHI and renderer portability |
| 5+ | not started | sections are added when each phase ships |
