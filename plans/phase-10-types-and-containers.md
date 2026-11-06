# Phase 10 — Standard Types and Container Hygiene

**Depends on:** Phase 0 (foundation types and containers), Phase 9 (the file layer and its tests, which are among the code to port). Inserted after Phase 9 and before the settings system (Phase 11), so the settings system
and everything after it is written against the finished vocabulary.
**Goal:** one standard vocabulary of types, used everywhere, and a rule about containers that is enforced rather than hoped for. Two parts. **Types:** a standard `byte`, fixed-width signed and unsigned integers
of 8, 16, 32, 64 and **128** bits (`i8`..`i64`, `u8`..`u64` exist; `i128` and `u128` do not), and non-owning views (`Span<T>`, `ByteSpan`). **Containers:** the engine's own `Vector`, `String`, `Array`, `HashMap`,
`UniquePtr` and `SharedPtr` instead of `std::vector`, `std::string`, `std::unique_ptr` and the like in `engine/`, `game/` and `tools/`; Phase 9 left a number of `std::` uses in game code, in the test helpers it added and
in `tools/shader_mint`, and nothing stops more from arriving.
**Definition of done:** `byte`, `i128`/`u128` (portable: the same behavior on every compiler, including ones without `__int128`), `Span<T>`/`ByteSpan`/`MutableByteSpan` exist in the foundation with tests (the 128-bit
arithmetic checked against `__int128` as an oracle where the compiler has one, and the portable implementation forced on and checked the same way) and a guide section; byte buffers in `engine/`, `game/` and `tools/` are spelled
`byte`/`u8` (not `unsigned char`/`uint8_t`/`char*`) except at documented API boundaries; `game/` and the Phase 9 additions to `tests/` (and `tools/` except the documented JSON boundary of `shader_mint`) use the engine's
containers; a `std_container_check` gate fails on a new `std::` container, string, smart pointer or algorithm in `engine/`, `game/` and `tools/` outside an allow list with a reason per entry (the old tests that Catch2 drives
are listed as a deferral, not allowed silently); `CLAUDE.md` states the rule; the full suite passes in every flavor.

Every day of this phase has its own file under `plans/phase-10-types-and-containers/` (see `CLAUDE.md`, planning policy). **Status: started (2026-11-07); Day 125 done.** Days 125-129 (Phase 9 ended at Day 124; Phase 11, the settings system, follows with Days 130-139). `CURRENT_PHASE` changes to 10 in the commit that completes Day 125's first task.

## Days

125. [Design Spec, Inventory & Phase Setup](phase-10-types-and-containers/day-125-design-spec-and-inventory.md)
126. [`byte`, `i128`/`u128`, `Span`](phase-10-types-and-containers/day-126-byte-int128-span.md)
127. [Byte Buffers and the `game/` Port](phase-10-types-and-containers/day-127-byte-buffers-and-game-port.md)
128. [The Phase 9 Test Helpers and `tools/shader_mint`](phase-10-types-and-containers/day-128-tests-and-shader-mint.md)
129. [The Container Gate, Docs, Closing Full Run & Wrap-up](phase-10-types-and-containers/day-129-gate-docs-and-closing-run.md)

Order reasoning: the spec (125) fixes what "std container" and "byte buffer" mean and takes an exact inventory, because the gate and the ports both depend on those definitions. The types come first (126) because
the ports (127, 128) use them; the foundation header change reruns the whole suite, so it is isolated in its own day. The ports go from the engine and game code (127) to the test helpers and the one tool with a
legitimate exception (128). The gate (129) comes last so it can start at zero violations and the phase can close.

## Design decisions made while planning (2026-11-07; Day 125 confirms or amends them in the spec)

1. **`byte` is an alias of `u8`** (decided with the user), not a distinct enum: raw bytes, `u8` values and buffers interchange freely. `ByteSpan = Span<const byte>` and `MutableByteSpan = Span<byte>` are pointer-and-length views like
   `StringView`; `StringView` converts to a `ByteSpan` and back through named functions.
2. **`u128`/`i128` are portable structs** (decided with the user), not aliases of `__int128`, because MSVC (Phase 12) has none: two 64-bit halves (little-endian layout: low half first), operators for the full set of integer
   arithmetic, shifts, bit operations and comparisons, division and remainder, `Hash`, `ToText`/`FromText`, `CountLeadingZeros`; implemented with `__int128` inside where `ENGINE_HAS_NATIVE_INT128` is set, and a forced-portable
   build (`ENGINE_FORCE_PORTABLE_INT128`) tested against the native one on Linux.
3. **What "std container" means for the gate** (Day 125 lists it exactly): `std::string`, `std::vector`, `std::array`, `std::map`/`set` and their unordered forms, `std::deque`, `std::list`, `std::queue`, `std::stack`,
   `std::unique_ptr`, `std::shared_ptr`, `std::function`, `std::optional`, `std::variant`, `std::span`, `std::pair`/`std::tuple` in new code, and `<algorithm>` calls (`std::sort`, `std::find`, ...). Allowed as before:
   the concurrency primitives (`std::thread`, `std::mutex`, `std::atomic`, `std::condition_variable`, coroutines), `<cmath>`, `<cstring>`, `<new>`, `<type_traits>`, `<utility>` (`std::move`/`forward`) and similar language-support headers
   (`PLAN.md` assumption #2).
4. **Scope** (decided with the user): enforced in `engine/`, `game/` and `tools/`; `tools/shader_mint` may use `std::string`/`std::vector` only inside its JSON boundary (nlohmann/json requires them, `PLAN.md` assumption #19);
   `tests/` use the engine's types for everything written in Phase 9 and later, and wherever Catch2 does not force `std::`; the older test files (about 1000 uses in `tests/debug`, `docgen`, `rhi`, `renderer`, `jobs`) are filed as a
   deferral and converted when a day touches them.
5. **Raw integer spellings**: `unsigned char`, `std::uint8_t`..`std::uint64_t`, `unsigned long`, `long long` and friends are replaced by the standard names in code the project owns; the exceptions (POSIX and Vulkan API types such as
   `mode_t` and `VkDeviceSize`, `printf` argument casts, `types.h` itself) are listed. Whether the second part is gated or only ported is Day 125's call.

## Key deliverables (the day files hold the sub-tasks)

- **Types:** `byte`, `i128`, `u128`, `Span<T>`, `ByteSpan`, `MutableByteSpan`, with tests against an oracle and a forced-portable build; guide and reference.
- **Ports:** byte buffers and fixed-width spellings; `game/`, the Phase 9 test helpers and `tools/shader_mint` onto the engine's containers.
- **The gate** `std_container_check` with planted-violation self-tests and an allow list with a reason per entry; the rule in `CLAUDE.md` and `PLAN.md`.

## Notes / open questions

- **Old tests** (about 1000 `std::` uses in tests written before Phase 9): deferred; converting them is mechanical but large and Catch2 stringification of engine types needs a small adapter (`StringMaker` specializations for `StringView`, `String`, `Path`, `u128`) that Day 128 writes first.
- **`std::string_view`/`std::span` at third-party boundaries** (Catch2, nlohmann/json): converted at the boundary, never stored.
- **Floating point**: `f32`/`f64` exist; `f16` and `f128` are not planned (GPU formats keep their own conversion helpers).
