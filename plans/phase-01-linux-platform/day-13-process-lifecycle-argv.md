# Phase 1 / Day 13 — Process Lifecycle & Argv

**Phase:** [Phase 1 — Linux Platform Layer](../phase-01-linux-platform.md)
**Depends on:** Phase 0 complete (links libc normally — see `PLAN.md`
assumption #2).

**Resolved (previously flagged in Phase 0's notes):** this uses libc's
standard `main()` entry rather than a hand-rolled `_start`. Since Phase 0
already links libc normally, skipping libc's own CRT startup would risk
skipping initialization later libc calls implicitly depend on (TLS,
stdio buffering, etc.) for no benefit — argc/argv/envp arrive at `main()`
already parsed correctly per the System V ABI, no reason to re-parse the
raw stack ourselves.

## Tasks

- [ ] Confirm the engine's entry point is a standard `int main(int argc,
  char** argv)` (Linux/glibc also exposes `envp` via `extern char**
  environ` if needed later — not required for this day).
- [ ] Implement logging of argc and each argv element via Day 8's
  formatter and Day 2's `os::Write`.
- [ ] Build and run with varied inputs: zero extra args, several args,
  and an arg containing a space (shell-quoted) — confirm each is logged
  correctly and individually (not split on internal spaces).
- [ ] Commit.

**Done:** engine prints its received command-line arguments correctly
for zero, one, and multiple arguments, including one containing a space.
