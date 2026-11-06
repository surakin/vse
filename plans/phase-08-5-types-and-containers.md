# Phase 8.5 — Standard Types and Container Hygiene

**Depends on:** Phase 0 (foundation types and containers), Phase 8 (the file layer and its tests, which are among the code to port). Inserted between Phase 8 and Phase 9 so the settings system
(Phase 9) and everything after it is written against the finished vocabulary.
**Goal:** one standard vocabulary of types, used everywhere, and a rule about containers that is enforced rather than hoped for. Two parts. **Types:** a standard `byte`, fixed-width signed and unsigned integers
of 8, 16, 32, 64 and **128** bits (`i8`..`i64`, `u8`..`u64` exist; `i128` and `u128` do not), and non-owning views (`Span<T>`, `ByteSpan`). **Containers:** the engine's own `Vector`, `String`, `Array`, `HashMap`,
`UniquePtr` and `SharedPtr` instead of `std::vector`, `std::string`, `std::unique_ptr` and the like in `engine/`, `game/` and `tools/`; Phase 8 left a number of `std::` uses in game code, in the test helpers it added and
in `tools/shader_mint`, and nothing stops more from arriving.
**Definition of done:** `byte`, `i128`/`u128` (portable: the same behavior on every compiler, including ones without `__int128`), `Span<T>`/`ByteSpan`/`MutableByteSpan` exist in the foundation with tests (the 128-bit
arithmetic checked against `__int128` as an oracle where the compiler has one, and the portable implementation forced on and checked the same way) and a guide section; byte buffers in `engine/`, `game/` and `tools/` are spelled
`byte`/`u8` (not `unsigned char`/`uint8_t`/`char*`) except at documented API boundaries; `game/` and the Phase 8 additions to `tests/` (and `tools/` except the documented JSON boundary of `shader_mint`) use the engine's
containers; a `std_container_check` gate fails on a new `std::` container, string, smart pointer or algorithm in `engine/`, `game/` and `tools/` outside an allow list with a reason per entry (the old tests that Catch2 drives
are listed as a deferral, not allowed silently); `CLAUDE.md` states the rule; the full suite passes in every flavor.

Every day of this phase has its own file under `plans/phase-08-5-types-and-containers/` (see `CLAUDE.md`, planning policy). **Status: planned (2026-11-07), not started.** Days are numbered 124a-124e (the way Phase 4
numbered 80a and 80b) so Phase 9 keeps Days 125-134. `CURRENT_PHASE` changes to 8.5 in the commit that completes Day 124a's first task.

## Days

- 124a. [Design Spec, Inventory & Phase Setup](phase-08-5-types-and-containers/day-124a-design-spec-and-inventory.md)
- 124b. [`byte`, `i128`/`u128`, `Span`](phase-08-5-types-and-containers/day-124b-byte-int128-span.md)
- 124c. [Byte Buffers and the `game/` Port](phase-08-5-types-and-containers/day-124c-byte-buffers-and-game-port.md)
- 124d. [The Phase 8 Test Helpers and `tools/shader_mint`](phase-08-5-types-and-containers/day-124d-tests-and-shader-mint.md)
- 124e. [The Container Gate, Docs, Closing Full Run & Wrap-up](phase-08-5-types-and-containers/day-124e-gate-docs-and-closing-run.md)

Order reasoning: the spec (124a) fixes what "std container" and "byte buffer" mean and takes an exact inventory, because the gate and the ports both depend on those definitions. The types come first (124b) because
the ports (124c, 124d) use them; the foundation header change reruns the whole suite, so it is isolated in its own day. The ports go from the engine and game code (124c) to the test helpers and the one tool with a
legitimate exception (124d). The gate (124e) comes last so it can start at zero violations and the phase can close.

## Design decisions made while planning (2026-11-07; Day 124a confirms or amends them in the spec)

1. **`byte` is an alias of `u8`** (decided with the user), not a distinct enum: raw bytes, `u8` values and buffers interchange freely. `ByteSpan = Span<const byte>` and `MutableByteSpan = Span<byte>` are pointer-and-length views like
   `StringView`; `StringView` converts to a `ByteSpan` and back through named functions.
2. **`u128`/`i128` are portable structs** (decided with the user), not aliases of `__int128`, because MSVC (Phase 10) has none: two 64-bit halves (little-endian layout: low half first), operators for the full set of integer
   arithmetic, shifts, bit operations and comparisons, division and remainder, `Hash`, `ToText`/`FromText`, `CountLeadingZeros`; implemented with `__int128` inside where `ENGINE_HAS_NATIVE_INT128` is set, and a forced-portable
   build (`ENGINE_FORCE_PORTABLE_INT128`) tested against the native one on Linux.
3. **What "std container" means for the gate** (Day 124a lists it exactly): `std::string`, `std::vector`, `std::array`, `std::map`/`set` and their unordered forms, `std::deque`, `std::list`, `std::queue`, `std::stack`,
   `std::unique_ptr`, `std::shared_ptr`, `std::function`, `std::optional`, `std::variant`, `std::span`, `std::pair`/`std::tuple` in new code, and `<algorithm>` calls (`std::sort`, `std::find`, ...). Allowed as before:
   the concurrency primitives (`std::thread`, `std::mutex`, `std::atomic`, `std::condition_variable`, coroutines), `<cmath>`, `<cstring>`, `<new>`, `<type_traits>`, `<utility>` (`std::move`/`forward`) and similar language-support headers
   (`PLAN.md` assumption #2).
4. **Scope** (decided with the user): enforced in `engine/`, `game/` and `tools/`; `tools/shader_mint` may use `std::string`/`std::vector` only inside its JSON boundary (nlohmann/json requires them, `PLAN.md` assumption #19);
   `tests/` use the engine's types for everything written in Phase 8 and later, and wherever Catch2 does not force `std::`; the older test files (about 1000 uses in `tests/debug`, `docgen`, `rhi`, `renderer`, `jobs`) are filed as a
   deferral and converted when a day touches them.
5. **Raw integer spellings**: `unsigned char`, `std::uint8_t`..`std::uint64_t`, `unsigned long`, `long long` and friends are replaced by the standard names in code the project owns; the exceptions (POSIX and Vulkan API types such as
   `mode_t` and `VkDeviceSize`, `printf` argument casts, `types.h` itself) are listed. Whether the second part is gated or only ported is Day 124a's call.

## Key deliverables (the day files hold the sub-tasks)

- **Types:** `byte`, `i128`, `u128`, `Span<T>`, `ByteSpan`, `MutableByteSpan`, with tests against an oracle and a forced-portable build; guide and reference.
- **Ports:** byte buffers and fixed-width spellings; `game/`, the Phase 8 test helpers and `tools/shader_mint` onto the engine's containers.
- **The gate** `std_container_check` with planted-violation self-tests and an allow list with a reason per entry; the rule in `CLAUDE.md` and `PLAN.md`.

## Notes / open questions

- **Old tests** (about 1000 `std::` uses in tests written before Phase 8): deferred; converting them is mechanical but large and Catch2 stringification of engine types needs a small adapter (`StringMaker` specializations for `StringView`, `String`, `Path`, `u128`) that Day 124d writes first.
- **`std::string_view`/`std::span` at third-party boundaries** (Catch2, nlohmann/json): converted at the boundary, never stored.
- **Floating point**: `f32`/`f64` exist; `f16` and `f128` are not planned (GPU formats keep their own conversion helpers).
