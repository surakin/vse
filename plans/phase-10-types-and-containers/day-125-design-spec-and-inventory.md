# Phase 10 / Day 125 — Design Spec, Inventory & Phase Setup

**Phase:** [Phase 10 — Standard Types and Container Hygiene](../phase-10-types-and-containers.md)
**Depends on:** Phase 9 complete.

No product code except the phase setup. The definitions the gate and the ports rely on are written down, the usage is inventoried exactly, and two spikes check the types.

## Tasks

- [x] **Phase setup first:** `CURRENT_PHASE` becomes 10; `tests/phases.txt` phase list comment names "10 standard types and containers" and the label list gains `phase10` (the file already supports `2.5`); `docs/guide/building-and-testing.md` table gets the row;
      check that `scripts/test-phase.sh` accepts 10 and that a phase with no tests runs nothing and says so. The `flavors 10 ...` line, `tests/types/` and the module-map line arrive on Day 126 with the first test.
- [x] **The exact definition of a banned `std::` use** (the list in the phase file, decision 3), each name with its engine replacement (`Vector`, `String`, `StringBuilder`, `Array`, `HashMap`, `UniquePtr`/`MakeUnique`, `SharedPtr`, a function pointer plus user pointer or a small `Function` if the inventory shows the need,
      `Optional` if it does) and a list of what is deliberately allowed and why. Record which replacements do not exist yet and must be built before the ports (the inventory decides: for example `Span`, a hash set, `Function`, `Optional`).
- [x] **Inventory of every banned use** in `engine/`, `game/`, `tools/` and `tests/`, as a table in the spec (file, count, what for, replacement, port day or the exception): `game/common/textures.*`, `game/rhi_demo`, `game/rhi_bench`, `game/renderer_demo`;
      `tools/shader_mint` (split into the JSON boundary and the rest); `tests/io`, `tests/io/slow_file.h`, `tests/docgen/fs_fixture.h`, `tests/jobs/async_file_test` and the other Phase 9 additions; the old tests (counts per directory, deferred). Find them with
      `grep` for `std::(string|vector|array|map|unordered_|set|deque|list|queue|stack|unique_ptr|shared_ptr|function|optional|variant|span|pair|tuple|sort|find)` and `#include <(string|vector|...)>`, and record every hit.
- [x] **Inventory of byte buffers and raw integer spellings** in the same directories: `unsigned char`, `uint8_t`, `char*`/`const char*` used as bytes, `void*` plus length, `unsigned long`, `long long`, `uint32_t` and so on; for each group: replace, or keep with the reason (an API type). Decide whether the raw-spelling rule is
      gated (with an allow list) or only ported.
- [x] **Spike A — `u128`/`i128`.** A throwaway program: a portable two-halves implementation of add, sub, mul, shifts, compare, unsigned and signed division with remainder (Knuth D or a shift-subtract loop), and text conversion; check 10 million random operations and the edge cases (0, 1, max, min,
      signed overflow behavior, shifts by 0, 64, 127, 128) against `__int128`; time the portable path against the native one. Decide the layout, the overflow contract (wrap for unsigned; signed wrap defined or an `ENGINE_CHECK_SLOW`), and the division algorithm. Record numbers in the spec.
- [x] **Spike B — `Span` and `ByteSpan` ergonomics.** A throwaway port of four real sites (`game/common/textures.cpp` `WriteRtex`, `io::WriteFile`'s callers, `ReadAll` into a `Vector<byte>`, a `crash_extract` envelope parse) to `ByteSpan`/`MutableByteSpan`; check implicit conversions from `Vector`, `Array`, C arrays, `StringView`, and the
      explicit ones; decide const rules, bounds checking (`ENGINE_CHECK_SLOW` or always), `Subspan`/`First`/`Last`, and whether the file layer's `(const void*, usize)` pairs get `ByteSpan` overloads.
- [x] **The spec** `docs/superpowers/specs/2026-11-NN-types-and-containers-design.md`: confirms or amends the five decisions with the evidence; class sketches (`byte`, `u128`, `i128`, `Span<T>`); the operation lists with `Aborts:` contracts; the portable-versus-native switch; the inventory tables; the gate definition; what is deferred.
- [x] **Documentation:** the spec is linked from `docs/README.md`; the phase file's day list is current; `ctest -L docs` passes.
- [x] Run `scripts/test-phase.sh --affected` (the new phase has no tests yet; quote the line, which says so).
- [x] No code review (no product code beyond the setup). Update `STATUS.md`; commit.

**Done:** `CURRENT_PHASE` is 10, the spec with the exact definitions and inventories is committed, both spikes are written up with their numbers, and the five decisions are confirmed or changed in the spec.

## Result

- Spec: `docs/superpowers/specs/2026-11-07-types-and-containers-design.md` (the exact definition of a banned `std::` use and of what stays allowed, the inventory, the six decisions confirmed or amended with evidence, class sketches, deferrals).
- Inventory: `engine/` has zero banned uses; `game/` 10; `tools/shader_mint` about 140 (53 in the JSON boundary `reflect_json.*`, 99 in files with no JSON that port); the Phase 9 test code 126 + 11 + 7; the older tests about 1090 (deferred, counted per directory); about 120 byte spellings in 30 files.
- Spike A (`u128`): a portable two-halves implementation with Knuth algorithm D matched `unsigned __int128` on 10 million random values per operator for add, sub, mul, div, mod, shl and shr (60 million checks, 0 mismatches); 16.3 ns against 4.3 ns native on a multiply-add-modulo loop.
- Spike B (`Span`): four real call patterns compile against the real `Vector`/`Array`; found that `Array` needs its own constructor and that const to mutable conversion is (correctly) rejected.
- Amendments to the plan: no `Function`/`Optional`/`Pair` (nothing needs them); `Sort`/`StableSort`/`Min`/`Max`/`IndexOf` are added on Day 127; `uptr`/`iptr` not added; the raw-spelling rule gates `unsigned char`/`signed char`/`(std::)u?int(8..64)_t` only.
- Phase setup: `CURRENT_PHASE` is 10; the `flavors 10` line, `tests/types/` and the module-map line arrive on Day 126 (their gates need a test and a directory to exist); the guide table has the phase 9 counts (`ran` lines below) and a phase 10 row.
