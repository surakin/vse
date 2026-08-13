# Phase 1 / Day 22 — GPU Driver Library Loading

**Phase:** [Phase 1 — Linux Platform Layer](../phase-01-linux-platform.md)
**Depends on:** Day 2 (OS interaction wrappers).

## Tasks

- [ ] Load `libvulkan.so` via the OS dynamic loader (`dlopen`, libc).
- [ ] Resolve `vkGetInstanceProcAddr` via `dlsym`.
- [ ] Use the resolved `vkGetInstanceProcAddr` to fetch
  `vkCreateInstance`.
- [ ] Log success/failure at each step (missing driver should fail
  clearly, not crash).
- [ ] Commit.

**Done:** `vkGetInstanceProcAddr` resolves and successfully fetches
`vkCreateInstance`.
