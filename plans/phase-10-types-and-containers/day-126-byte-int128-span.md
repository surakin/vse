# Phase 10 / Day 126 — `byte`, `i128`/`u128`, `Span`

**Phase:** [Phase 10 — Standard Types and Container Hygiene](../phase-10-types-and-containers.md)
**Depends on:** Day 125 (the spec).

The vocabulary itself. `engine/foundation/types.h` gains `byte`; new headers `engine/foundation/int128.h/.cpp` and `engine/foundation/containers/span.h`. A change to a header under `engine/foundation` makes `--affected` run the whole suite; that is why this day is
alone.

## Tasks

- [x] Phase setup that waited for this day: `flavors 10 plain,asan,tsan` in `tests/phases.txt`, `tests/types/` (own executables per flavor, like `tests/io`) with its line, `engine/foundation/int128.h` and `containers/span.h` covered by the `foundation/types`/`containers` modules in
      `docs/reference/modules.txt`, `tests/types` in `tests/module_map.txt`.
- [x] `types.h`: `using byte = u8;`, `isize` (`std::ptrdiff_t`) and `uptr`/`iptr` if the spec adds them; `static_assert`s of every width and of two's complement; a comment block that is the single statement of the vocabulary (widths, signedness, which are aliases and which are classes).
- [x] `u128` and `i128` (`int128.h`): two `u64` halves, trivially copyable, standard layout, 16 bytes with `alignof` 16 where the compiler allows (a static_assert records the answer); construction from every smaller integer (implicit widening only), explicit narrowing and conversion to `f64`; `+ - * / % << >> & | ^ ~ unary-`, compound
      assignments, increments, all comparisons, `<=>`; (`<=>` is not provided: <compare> is missing from the Windows/macOS shim; all six comparison operators are; recorded in KNOWN-ISSUES) `Hash`, `CountLeadingZeros`/`CountTrailingZeros`/`PopCount`, `ByteSwap`, `Min`/`Max` constants, `ToText`/`FromText` in base 2, 10 and 16; division by zero `Aborts:`; implemented with `unsigned __int128` when `ENGINE_HAS_NATIVE_INT128` (set by the build for GCC and Clang) and
      with the Spike A algorithms otherwise; `ENGINE_FORCE_PORTABLE_INT128` builds the portable path on any compiler.
- [x] `Span<T>` (`containers/span.h`): pointer and length, `Data`, `Size`, `Empty`, `operator[]` with the checked/unchecked rule from the spec, `begin`/`end` pointers for range-for, `First(n)`, `Last(n)`, `Subspan(offset, count)`, implicit construction from `Vector<T>`, `Array<T, N>`, `T[N]` and `Span<U>` where `U` is `T` with const added; `ByteSpan`,
      `MutableByteSpan`, `AsBytes(const T&)` for trivially copyable values and `AsByteSpan(StringView)`/`ToStringView(ByteSpan)`; no ownership, no allocation.
- [x] Tests (phase 10, `tests/types/`): **u128 against `__int128`**: 10 million random operations per operator (add, sub, mul, div, mod, shifts, compare, bit operations) with a fixed seed, plus the edge table (0, 1, 2^63, 2^64-1, 2^64, max, signed min and -1, shift counts 0, 1, 63, 64, 65, 127, 128) for `u128` and `i128`; the same suite built with `ENGINE_FORCE_PORTABLE_INT128` (a second
      executable); text round trips in three bases; `Hash` distribution sanity; layout `static_assert`s; `Span` conversions, bounds, subspans, const rules, range-for, the `AsBytes` of a struct, a `Vector<byte>` as `ByteSpan`; TSan/ASan flavors run the same cases.
- [x] Cross-target syntax: the new headers are added to the neutral-header translation units of the Windows and macOS checks (`io_cross_syntax_*` and the Phase 7/8 ones include `types.h` already) and a new `types_cross_syntax_windows|macos` compiles `int128.h` and `span.h` with the portable path forced (the shim has no `__int128`).
- [x] **Documentation:** `///` comments with labels (`Aborts:` for division by zero and checked indexing); the guide gets "Standard types" (a table of every type, its width, signedness and when to use it; bytes; 128-bit; spans) with tagged samples; reference regenerated; `docgen lint` at zero; `ctest -L docs`.
- [x] Run `scripts/test-phase.sh --affected` (a foundation header changed, so this is the whole suite) and the asan and tsan flavors; quote the lines.
- [x] **End-of-day review (CLAUDE.md):** `/code-review high`; record it. Extra scrutiny: the division algorithm and signed shifts and conversions (undefined behavior in the native path too).
- [x] Update `STATUS.md`; commit.

**Done:** `byte`, `i128`, `u128` and `Span`/`ByteSpan` exist with the native and the portable 128-bit paths agreeing with `__int128` on millions of operations, they compile for the Windows and macOS targets, and the guide documents the vocabulary.


## Result

- Added `byte`, `isize`, `u128`/`i128` (native and portable, inline namespaces per mode) and `Span`/`ByteSpan`; 4 test executables (plain, asan, tsan, portable), 232 assertions in 15 cases, 10M random ops vs `unsigned __int128`.
- Spec change: `Span::operator[]` is always `ENGINE_ASSERT`ed, because `platform_neutral_check` forbids `ENGINE_CHECK_SLOW` in a header.
- Docs: `docgen lint` 3505/3505; `ctest -L docs` passes; guide `docs/guide/foundation-types.md`.
- Runs: `ran 1136 of 2976 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (only the known X11 window test failed); asan `ran 825 of 2976 ...`; tsan `ran 542 of 2976 ...`.

## Post-implementation code review

`/code-review high`: 10 findings.
- Fixed: guide said `Span::operator[]` is unchecked (now says always checked); `types.h` claimed the gate exists (now "will, Day 129"); `module_map.txt` comment moved to the wrong line; `u128`->`f64` double-rounded (now sticky-bit single rounding; regression test fails on the old formula for 2623 of 200000 values); guide overstated implicit conversions; `Span(T*, usize)` now asserts null needs size 0; `PortableDivMod` asserts a nonzero divisor; `ToText` does one division per digit.
- Declined: always-checked `operator[]` costs a compare (a header cannot use `ENGINE_CHECK_SLOW`; safety on byte buffers preferred); rvalue Vector/Array binding to a Span stays allowed (passing a temporary as an argument is legitimate; hazard noted); bit-count loops left (constexpr-safe, not hot).
- `<=>` not implemented (see task); STATUS.md updated in this commit.
