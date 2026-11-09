# Phase 16 / Day 208 — `lang`: Traits, Move, Limits, Ordering, `new` Helpers

**Phase:** [Phase 16 — Standard-Library Wrappers](../phase-16-std-wrappers.md)
**Depends on:** Day 207.

A layer-0 module `engine/foundation/lang/` wrapping the language-support headers: `<utility>`, `<type_traits>`, `<limits>`, `<initializer_list>`, `<compare>`, `<new>`, `<concepts>`, `<cstddef>`, `<climits>`.

## Tasks

- [ ] `lang/utility.h`: `Move`, `Forward`, `Swap`, `Exchange`, `AsConst`, `Declval`, `Pair` stays in containers (Phase 10 decision) — check, and port nothing that already has an engine type.
- [ ] `lang/traits.h`: `IsSame`, `IsConvertible`, `IsTriviallyCopyable`, `IsIntegral`, `IsFloatingPoint`, `EnableIf`, `Conditional`, `RemoveReference`, `RemoveConst`, `Decay`, `IsBaseOf`, plus the `_v` forms as `constexpr bool` variables; the list is exactly what Day 207's inventory found, not the whole header.
- [ ] `lang/limits.h`: `Limits<T>` with `Min()`, `Max()`, `Lowest()`, `Epsilon()`, `Infinity()`, `QuietNaN()`, `kDigits`; `lang/ordering.h`: `Ordering` wrapping `std::strong_ordering` so `operator<=>` is spelled once; `lang/initializer_list.h`: `InitializerList<T>`; `lang/new.h`: `AlignVal`, `NoThrow`, placement `new` helper.
- [ ] Every header carries `///` comments per `docs/DOC-CONVENTIONS.md`; the module is added to `docs/reference/modules.txt`, `ARCHITECTURE.md` (row, layer 0, no dependencies) and `tools/module_deps_check/modules.txt`.
- [ ] Tests in `tests/lang/` (phase label 16 in `tests/phases.txt`): each trait against the `std` original as an oracle (`static_assert`s), `Limits` against `numeric_limits` for every arithmetic type, `Move`/`Forward` value-category checks, `Ordering` comparisons, `InitializerList` constructing a `Vector`-like type; a zero-cost check (the same code generation for `Move` as `std::move` is not testable; instead `static_assert` that every wrapper is `constexpr`/`noexcept` as the original).
- [ ] Port `engine/foundation` only as far as it is needed to prove the module (`types.h` aliases keep their `<cstdint>` include; this is the one file allowed to keep it); the bulk port happens in Days 210-218.
- [ ] **Day review (CLAUDE.md, required):** scoped tests (`scripts/test-phase.sh --affected`, plain, then asan and tsan where the day touched concurrency or memory code), then `/code-review high` on the day's diff; verify, fix or decline each finding with a reason and record the count and outcome under "Post-implementation code review"; `STATUS.md` and `KNOWN-ISSUES.md` updated; commit and push.

**Done:** the `lang` module builds in unity and non-unity, with GCC and Clang, passes its tests, is documented (`docgen lint` zero, `docs` target run, guide section in `docs/guide/foundation-types.md`), and `module_deps_check` passes.
