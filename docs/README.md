# Engine Documentation

End-user documentation for the engine. **Status: skeleton.** Every page
below exists as an outline with pointers to its source of truth and the
facts already known; none is written yet. See `DOC-CONVENTIONS.md` for how
pages are structured and when a day's work counts as documented.

Internal design history lives in `docs/superpowers/specs/` and the plans in
`plans/` — those are for contributors, not engine users.

## Map

| Section | What it is | Pages |
|---|---|---|
| [guide/](guide/README.md) | Concept pages, one per subsystem — *how it works and how to use it* | 10 |
| [reference/](reference/README.md) | API reference, generated from header comments — *what every symbol does* | index + per-module stubs |
| [tutorials/](tutorials/README.md) | Task-oriented walkthroughs — *do this, get that* | 4 |

## Coverage by phase

| Phase | Status | Documented in |
|---|---|---|
| 0 — Foundation | implemented, docs skeleton | guide: memory, containers, strings, math, logging |
| 1 — Linux platform | implemented, docs skeleton | guide: platform-os, windowing-input |
| 2 — Job system | implemented, docs skeleton | guide: jobs; tutorials: parallel-jobs, async-file-io |
| 3+ | not started | sections are added when each phase ships |
