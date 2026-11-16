# Physics: rigid bodies that agree to the bit

Status: in progress (Phase 25). Built: the determinism foundation (Day 374). Next: shapes, mass and the components (Day 375).

**Audience:** engine programmers who write or review physics code, and hosts that compare simulations across machines. Read [Math](foundation-math.md) and
[Spatial partitioning](spatial.md) first. Design and the experiments behind it: `docs/superpowers/specs/2026-11-16-physics-design.md`.

The physics module (`engine/physics`, namespace `physics`) simulates rigid bodies with a fixed step. Its requirement beyond being correct is that the
same scene gives the **same bits** on Linux, Windows and macOS (x86-64), whatever the compiler, the optimisation level and the number of threads, so a
replay, a rollback and a networked peer can trust it. This page states the rules that make that true; every later day of the phase obeys them.

## The rules of the step

1. **The step uses `+ - * / Sqrt` on `f32` and nothing else that rounds.** IEEE 754 defines exactly these (and comparisons, min, max, abs, negation,
   conversions within range): every conforming machine gives the same bits. `Sin`, `Cos`, `Tan`, `Asin`, `Acos`, `Atan`, `Atan2`, `Exp`, `Log`, `Pow` are
   not exact and differ between glibc, the Microsoft C library and Apple's; the hardware estimates `rsqrt` and `rcp` differ between CPUs. None is used in
   the step. The quaternion update is first order with a normalising `Sqrt`; the soft-constraint coefficients are closed forms in `+ - * /`.
2. **Where a transcendental cannot be avoided it is at setup, not in the step**, it goes through `lang/scalar.h` (libm is used as is; nothing is
   reimplemented), and it is listed here: converting an authored angle to its sine and cosine (a hinge limit, a motor target, a slope limit), building
   a quaternion from an axis and an angle, reporting an angle to the host. The reference scenes use literal constants for these; a scene that does not
   is hashed separately and a disagreement between machines is recorded in `KNOWN-ISSUES.md`, not worked around.
3. **No contraction, no fast-math.** `-ffp-contract=off -fno-fast-math` on GCC and Clang, `/fp:precise` on cl, `/fp:precise` and
   `/clang:-ffp-contract=off` on clang-cl (`ENGINE_FP_COMPILE_OPTIONS`, `cmake/EngineCompilerOptions.cmake`), applied to every target of ours. A fused
   multiply-add rounds once where a multiply and an add round twice, so a build for a machine with FMA (`-march=native`, and arm64 by default) would give
   other bits, and GCC and Clang would give others still (Day 373 spike 2). The configure step stops when a fast-math flag, or contraction, is set
   anywhere (`cmake/EngineFpCheck.cmake`; a fast-math flag at link time also sets flush-to-zero for the whole process).
4. **Denormals are kept.** The engine never sets flush-to-zero or denormals-are-zero; `physics::DenormalsPreserved()` says whether the process has them
   off, and the step will check it (Day 378). A host that sets the mode for its own sake must restore it around the step.
5. **Fixed order everywhere.** A fixed time step; pairs in the order the pair tracker gives (sorted by key); islands by their lowest body id; a parallel
   job writes only its own output and the outputs are merged in chunk order; no float reduction in thread order, no atomic float add, no iteration order of
   a hash map, no sort by a float without an id tie-break. Day 373 spike 2: an unordered merge gave nine different hashes in nine runs; the ordered merge
   gave one hash for 1, 2, 4, 16 and 32 threads.

## Checking the build

`physics::ProbeHash()` evaluates a fixed set of expressions that a different rounding, contraction or flush mode would change and hashes the results;
`fp_determinism_probe` prints it with the mode the build has:

```
fp_determinism_probe: hash 50e1be9251ea408f contraction=off denormals=kept
```

The hash is the same under GCC and Clang, `-O0` to `-O3`, unity and non-unity builds, and with `-march=native -ffp-contract=off`; with contraction on it is
`60498f6f6e05389d` under both compilers. The ctest `physics_fp_determinism_probe` and the test `Determinism: the probe's hash is the committed constant`
compare it with the committed value; the Windows PC and the Mac compare it on Days 398 and 399 (arm64 can only be compiled, not run). A control probe
compiled with contraction allowed shows that a fused multiply-add would be seen (`Determinism: the control probe does fuse`; skipped where the machine has no
FMA instruction).

## The state hash

`physics::StateHasher` is FNV-1a 64: bytes in the order fed, words least significant byte first, floats by bit pattern with **-0.0 as +0.0**, and
`Valid()` false once a NaN or an infinity was fed (a simulation that produced one has failed). Nothing but the fed words is hashed: no padding, no
pointers. The reference scenes hash every body in id order: position, orientation, linear and angular velocity, then a sleep byte.
