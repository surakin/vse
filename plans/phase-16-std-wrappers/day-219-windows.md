# Phase 16 / Day 219 — The Windows PC: Windows-Only Files and Both Compilers

**Phase:** [Phase 16 — Standard-Library Wrappers](../phase-16-std-wrappers.md)
**Depends on:** Days 208-218 pushed.

The files Linux cannot build (`engine/platform/windows`, `engine/rhi/dx12`, `engine/foundation/os/windows`, `win32_*`, `tests/windows`, `tests/rhi/dx12`) are ported on the Windows PC; shared code changed on Days 208-218 is built there for the first time.

## Tasks

- [ ] `git pull` on the Windows PC; build `windows-msvc` (plain and asan) and `windows-clang-cl` (plain) in unity and `-DENGINE_UNITY_BUILD=OFF`; fix what Days 208-218 broke on MSVC (coroutine traits specialization, `constexpr` differences, `/W4 /WX` findings in the new headers); no warning suppressions.
- [ ] Port the Windows-only files (list from Day 207): Win32 and COM calls stay (they are the OS boundary, not the standard library); only `std::` and standard includes move.
- [ ] MSVC specifics: `Atomic<T>` for 64-bit types, `Mutex` over SRWLOCK versus `std::mutex` (keep the standard type inside `sync`, record the choice), coroutine frame allocation behavior, `swprintf` signature differences inside `cstr/format.h` (the wrapper hides them).
- [ ] Run `scripts/test-phase.sh --all` on Windows (plain and asan) and the Direct3D 12 and Vulkan demos with validation.
- [ ] Move the entries "Windows files still hold std (Phase 16)" out of `KNOWN-ISSUES.md`; record anything that still fails there with the observed fact.
- [ ] **Day review (CLAUDE.md, required):** scoped tests (`scripts/test-phase.sh --affected`, plain, then asan and tsan where the day touched concurrency or memory code), then `/code-review high` on the day's diff; verify, fix or decline each finding with a reason and record the count and outcome under "Post-implementation code review"; `STATUS.md` and `KNOWN-ISSUES.md` updated; commit and push.

**Done:** the Windows-only files contain no `std::` or standard includes, both Windows compilers build clean in both unity modes, the Windows test run's final line is recorded, and `KNOWN-ISSUES.md` lists only what was observed to fail.
