# Phase 19 / Day 256 — Component Registry I: Identities, Fields & the Canonical Encoding

**Phase:** [Phase 19 — Storage](../phase-19-storage.md)
**Depends on:** Phase 17 (components, `ComponentOf<T>`); Phase 10 (`String`, `Array`); Phase 16 (wrappers).

Describe how each component type is stored, and write the byte encoding. Outside `ecs`, without touching its hot paths.

## Tasks

- [x] **New module `engine/storage`** (the registry and codec; CMake target `storage` with flavors, `ARCHITECTURE.md` row, `modules.txt`, `docs/reference/modules.txt`): depends on `foundation`, `ecs` and `jobs`; the registry part has no `database` dependency.
- [x] **`ByteWriter` and `ByteReader` live in `foundation` (`foundation/bytes/`, a new module row with its layer above `cstr` and `types`, in `modules.txt`), not in `storage`** (Day 250 plan check: Phase 20's `NetWriter`/`NetReader` need the same bounds-checked reader and writer and extend these with quantized floats instead of duplicating them): canonical little-endian encoding, fixed-width integers, `f32`/`f64` as their bit patterns (a NaN payload, `-0` and denormals survive), `bool` as one byte validated on read, lengths as bounded varints; the reader is bounds-checked and returns a `Status` on truncation or an over-limit length, never reading past the span.
- [x] **`storage::ComponentRegistry`:** `Register<T>(ComponentDesc)` with a stable numeric id written in code, a unique name, a version and a field list; registration errors (duplicate id, duplicate name, an id or name changed against a recorded manifest) are `Status` values; lookup by id, name and `ComponentOf<T>`.
- [x] **Field descriptions** by pointer to member: `Field("x", &T::x)` for `u8`..`u64`, `i8`..`i64`, `f32`, `f64`, `bool`, `Vec2`/`Vec3`/`Vec4`, `Quat`, `Mat4`, enums (stored as their underlying integer, validated against a declared range), `Array<T, N>` of any stored type, `String` (bounded length, valid UTF-8 checked) and `EntityRef` (the type is declared here and resolved on Day 258); a struct with a field the registry cannot describe fails to compile (no silent skip).
- [x] **Encode and decode of one component value** from and into the component's memory (`Encode(const void*, ByteWriter&)`, `Decode(ByteReader&, void*)`), with a `Derived` flag (never stored) and a `Skip` flag set explicitly.
- [x] **Registered-or-flagged check:** `UnregisteredWithData(const ecs::World&)` lists component types present in a world that are neither registered nor marked `Derived`/`Skip`, so a new component cannot be forgotten silently; the engine's own components (`LocalTransform`, `Parent`, `SpatialMembership`, `SpatialBounds`, `SpatialLayer`, ...) are registered in `storage/builtin_components.cpp` and the derived ones flagged.
- [x] **Tests** (`tests/storage/registry_test.cpp`, `encoding_test.cpp`): every field kind round trips bit-exact including NaN with payload, `-0`, denormals and extreme integers; golden byte strings for a fixed component set (a change to the encoding fails the test and must bump the format version); every registration error; truncated and over-long inputs return `Status` at every possible cut point; the unregistered-component list on a world with an extra component.
- [x] **Guide** `docs/guide/storage.md` started: the registry and the encoding.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (most days of this phase do); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [x] **Two machines (CLAUDE.md "Windows is a supported platform"):** the code is portable; it is built and tested on the Windows PC no later than Day 265, and until then the day is listed as "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page named in the day is updated or created and its tagged samples compile and run.
- [x] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 19).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a component type is described once and encodes to and decodes from stable bytes, bit-exact, with every bad input an error value.

## Notes

- **`Field<&T::member>("name")`, not `Field("name", &T::member)`:** the member is a template argument, so the encode and decode functions are plain function pointers with the member baked in (no offset arithmetic, no allocation). Enum fields take `FieldOptions::Enum(min, max)`; a missing range is an `Invalid` registration.
- Added the field kind `Aabb` (two `Vec3`): `spatial::SpatialBounds` holds a box and a field is one direct member. Nested user structs are not supported yet; compose them from the kinds above.
- `foundation/bytes/` is header-only (layer 5). Its reader returns `bytes::ByteError`, not a `Status` (the foundation cannot depend on `storage`); `storage` maps `Truncated`/`Invalid` to `Corrupt` and `TooLong` to `LimitExceeded`. A varint is accepted only in its shortest form, so every value has one encoding.
- `storage::Status` is minimal (code plus a static message); Day 259 adds the record-store codes. `CodecLimits` carries the string and array bounds with defaults; Day 264 wires the settings.
- `ecs::Parent` is registered `kComponentSkip` until Day 258 stores it as an `EntityRef`. There is no `SpatialLayer` component (the plan's list was loose): the layer is a member of `SpatialMembership`.
- C++ has no reflection: a member left out of a field list is not detected. The check available is the golden-bytes test and `UnregisteredWithData` for whole types.
- `Register<T>` calls `ecs::ComponentOf<T>()`, which uses one of the 128 slots of the ECS registry, as any component does.
- Fixed on the way: `containers/hash_map.h` (Mac Day 301) had a doc comment inside a constrained `Hash<K>` specialization that made `docgen` fail on Linux; it is a plain comment now. The docgen lint baseline was refreshed (other modules had grown).
- Runs (Linux, Clang): plain `ran 2363 of 6180 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (the two known X11Window failures; `docs_check` failed once mid-day on a stale docs state and passes now: `ctest -L docs` 7 of 7); asan `ran 1819 of 6180 tests`; tsan `ran 1501 of 6180 tests` (the Day 235 scene test only). After the last edits `storage_tests` (plain, asan, tsan) and `foundation_base_tests [bytes]` were rerun and pass; Clang non-unity and GCC build clean and pass.

## Post-implementation code review

Inline review of the day's diff (not the `/code-review` skill): 3 findings, all fixed. (1) The integer decode reinterpret-cast the member to a fixed-width pointer, wrong for `long` versus `long long`; it now reads into the fixed-width type and converts. (2) `WriteBytes` grew the buffer one byte at a time; it reserves first. (3) The golden test's string length cut a literal short; corrected. Declined: nothing.

## Follow-up (same day, at the user's request)

- `ENGINE_TRY(expr)` (`lang/status_try.h`): returns a failed status from the enclosing function in one line; `RegisterBuiltinComponents` and the test setup use it instead of `if (s.Ok())` chains.
- `database::Status`, `storage::Status` and `bytes::ByteError` are `[[nodiscard]]`: an ignored result is a build error. Fixed 9 sites: destructors that cannot report (`Transaction`, `SqliteConnection`) and the fault-injection write path discard on purpose with `static_cast<void>` and a comment; setup steps in the crash test now `REQUIRE` their result.
- Runs: plain `ran 2363 of 6180 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (the known X11Window failures; the timing benchmark of `KNOWN-ISSUES.md` failed once and passed alone), asan `ran 1819 of 6180`, tsan `ran 1501 of 6180`; Clang non-unity and GCC build clean; docs 7 of 7.
