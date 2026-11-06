# Phase 8.5 / Day 124a — Design Spec, Inventory & Phase Setup

**Phase:** [Phase 8.5 — Standard Types and Container Hygiene](../phase-08-5-types-and-containers.md)
**Depends on:** Phase 8 complete.

No product code except the phase setup. The definitions the gate and the ports rely on are written down, the usage is inventoried exactly, and two spikes check the types.

## Tasks

- [ ] **Phase setup first:** `CURRENT_PHASE` becomes 8.5; `tests/phases.txt` phase list comment names "8.5 standard types and containers" and the label list gains `phase8_5` (the file already supports `2.5`); `docs/guide/building-and-testing.md` table gets the row;
      check that `scripts/test-phase.sh` accepts 8.5 and that a phase with no tests runs nothing and says so. The `flavors 8.5 ...` line, `tests/types/` and the module-map line arrive on Day 124b with the first test.
- [ ] **The exact definition of a banned `std::` use** (the list in the phase file, decision 3), each name with its engine replacement (`Vector`, `String`, `StringBuilder`, `Array`, `HashMap`, `UniquePtr`/`MakeUnique`, `SharedPtr`, a function pointer plus user pointer or a small `Function` if the inventory shows the need,
      `Optional` if it does) and a list of what is deliberately allowed and why. Record which replacements do not exist yet and must be built before the ports (the inventory decides: for example `Span`, a hash set, `Function`, `Optional`).
- [ ] **Inventory of every banned use** in `engine/`, `game/`, `tools/` and `tests/`, as a table in the spec (file, count, what for, replacement, port day or the exception): `game/common/textures.*`, `game/rhi_demo`, `game/rhi_bench`, `game/renderer_demo`;
      `tools/shader_mint` (split into the JSON boundary and the rest); `tests/io`, `tests/io/slow_file.h`, `tests/docgen/fs_fixture.h`, `tests/jobs/async_file_test` and the other Phase 8 additions; the old tests (counts per directory, deferred). Find them with
      `grep` for `std::(string|vector|array|map|unordered_|set|deque|list|queue|stack|unique_ptr|shared_ptr|function|optional|variant|span|pair|tuple|sort|find)` and `#include <(string|vector|...)>`, and record every hit.
- [ ] **Inventory of byte buffers and raw integer spellings** in the same directories: `unsigned char`, `uint8_t`, `char*`/`const char*` used as bytes, `void*` plus length, `unsigned long`, `long long`, `uint32_t` and so on; for each group: replace, or keep with the reason (an API type). Decide whether the raw-spelling rule is
      gated (with an allow list) or only ported.
- [ ] **Spike A — `u128`/`i128`.** A throwaway program: a portable two-halves implementation of add, sub, mul, shifts, compare, unsigned and signed division with remainder (Knuth D or a shift-subtract loop), and text conversion; check 10 million random operations and the edge cases (0, 1, max, min,
      signed overflow behavior, shifts by 0, 64, 127, 128) against `__int128`; time the portable path against the native one. Decide the layout, the overflow contract (wrap for unsigned; signed wrap defined or an `ENGINE_CHECK_SLOW`), and the division algorithm. Record numbers in the spec.
- [ ] **Spike B — `Span` and `ByteSpan` ergonomics.** A throwaway port of four real sites (`game/common/textures.cpp` `WriteRtex`, `io::WriteFile`'s callers, `ReadAll` into a `Vector<byte>`, a `crash_extract` envelope parse) to `ByteSpan`/`MutableByteSpan`; check implicit conversions from `Vector`, `Array`, C arrays, `StringView`, and the
      explicit ones; decide const rules, bounds checking (`ENGINE_CHECK_SLOW` or always), `Subspan`/`First`/`Last`, and whether the file layer's `(const void*, usize)` pairs get `ByteSpan` overloads.
- [ ] **The spec** `docs/superpowers/specs/2026-11-NN-types-and-containers-design.md`: confirms or amends the five decisions with the evidence; class sketches (`byte`, `u128`, `i128`, `Span<T>`); the operation lists with `Aborts:` contracts; the portable-versus-native switch; the inventory tables; the gate definition; what is deferred.
- [ ] **Documentation:** the spec is linked from `docs/README.md`; the phase file's day list is current; `ctest -L docs` passes.
- [ ] Run `scripts/test-phase.sh --affected` (the new phase has no tests yet; quote the line, which says so).
- [ ] No code review (no product code beyond the setup). Update `STATUS.md`; commit.

**Done:** `CURRENT_PHASE` is 8.5, the spec with the exact definitions and inventories is committed, both spikes are written up with their numbers, and the five decisions are confirmed or changed in the spec.
