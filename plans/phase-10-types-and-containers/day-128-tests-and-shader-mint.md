# Phase 10 / Day 128 — The Phase 9 Test Helpers and `tools/shader_mint`

**Phase:** [Phase 10 — Standard Types and Container Hygiene](../phase-10-types-and-containers.md)
**Depends on:** Day 127.

The two remaining places: the test code Phase 9 added (it should have used the engine's types) and the one tool with a legitimate exception, narrowed to its exact boundary.

## Tasks

- [x] **Catch2 adapters** in a small shared header (`tests/support/catch_engine_types.h`): `Catch::StringMaker` specializations for `StringView`, `String`, `io::Path`, `io::Status`, `Error`, `u128`/`i128` and `Span`, so assertions on engine types print readable values without converting to `std::string`; `REQUIRE(a == b)` on these types works and
      is tested (a failing assertion's message is checked once, in a test that expects the failure).
- [x] Port the Phase 9 additions: `tests/io/*` (`temp_dir.h`, `file_conformance.h`, `file_system_test`, `file_test`, `directory_test`, `async_file_test`, `slow_file.h`, `path_test`), `tests/docgen/fs_fixture.h` and the docgen tests' fixture helpers, `tests/rhi/image_compare.*` and the pipeline-cache test additions, `tests/jobs`'s `/proc` readers: `std::string`/`std::vector`/`std::unique_ptr`/`std::function`/`std::set` become `String`/`Vector`/`UniquePtr`/function pointers/
      sorted `Vector`s or `HashMap`; `std::thread` and `std::atomic` stay (allowed). Where Catch2 itself requires `std::` (`GENERATE` ranges, `INFO` streams, matchers), the use is local and commented.
- [x] `tools/shader_mint`: define the **JSON boundary** (`reflect_json.cpp`, the conversion from `nlohmann::json` to the engine's reflection structures, the text it needs from the library) and move everything else (argument parsing, file reading and writing, text generation for the `.spv.inc`/`.refl.inc`/`.perm.inc` files, the permutation manifest, the compiler invocation) to `String`, `StringBuilder`, `Vector` and `HashMap`; the boundary
      translation unit may use `std::string`/`std::vector` and says so in its header comment; generated outputs stay byte-identical (the shaders regenerate to the same files, `shaders_check` passes, the committed `*.inc` files do not change).
- [x] Tests: the tests ported above pass unchanged in expectation; `tests/shader_mint` passes; `cmake --build build --target shaders` (where `slangc` exists) reproduces the committed files; new cases for the Catch2 adapters.
- [x] The remaining old tests: the exact per-file counts are recorded in `KNOWN-ISSUES.md` as a deferral with the rule ("convert a test file when a day touches it") and the adapters from this day as the way to do it.
- [x] **Documentation:** `docs/guide/building-and-testing.md` ("Writing tests": use the engine's types, the adapters, when `std::` is acceptable in a test); `tools/shader_mint` README/design comment names the boundary; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [x] Run `scripts/test-phase.sh --affected` and the asan and tsan flavors; quote the lines.
- [x] **End-of-day review (CLAUDE.md):** `/code-review high`.
- [x] Update `STATUS.md`, the spec's inventory table; commit.

**Done:** the tests written in Phase 9 and `tools/shader_mint` outside its JSON boundary use the engine's types, the generated files did not change, and the old tests' remaining `std::` use is a counted, filed deferral with a way to convert them.


## Result

- `tests/support/catch_engine_types.h` (Catch2 printers) and `tests/support/test_text.h` (`Text`, `TextList`, `TextSet`, builders). `String` gained `operator==`/`!=` with a view and an lvalue-only conversion to `StringView`; `Vector` gained `Empty()` and `Insert(index, T&&)`.
- Ported to the engine's types: all of `tests/io` (helpers `temp_dir.h`, `file_conformance.h`, `slow_file.h` and the five test files), `tests/shader_mint/permutations_test.cpp`, the pipeline-cache test's temp-dir use, and `tools/shader_mint` outside `reflect_json.cpp` (`text.h`, `permutations.*`, `main.cpp`). `reflect_json.h` now takes a `StringView` and returns a `Text`; `reflect_json.cpp` is the only file with `std::string`/`std::vector`. `tests/shader_mint/reflect_json_test.cpp` builds its JSON text with `std::string` (the boundary).
- Not ported (filed in KNOWN-ISSUES): `tests/docgen/fs_fixture.h` (14) and `tests/rhi/image_compare.*` (14) because 7 docgen and 8 rhi test files call them with `std::string`/`std::vector<unsigned char>`; they convert with those directories.
- Not verified: `slangc` is not installed here, so the `shaders` target did not regenerate the committed `*.inc` files; the permutation emitter is covered by `permutations_test` and the unchanged text builders by the same strings.
- Runs: plain `ran 1155 of 3015 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (only the known X11 test failed); asan `ran 835 of 3015`; tsan `ran 552 of 3015`. Docs: lint 3520/3520, `ctest -L docs` passes.

## Post-implementation code review

`/code-review high`: 10 findings. Fixed: guide claimed the gate before it exists (reworded); a temporary `String` converted to a dangling `StringView` (now deleted for rvalues; three call sites fixed); `Vector::Insert` bounds assert and doc; `TextSet::SameAs` accepted repeated expectations; `Has` is a binary search; one `VAppendf` replaces two copies; the reflection wrappers' implementations are `static`; module vectors are moved, not copied; STATUS written. Declined: avoiding the extra JSON/error copies in the dev-time reflection wrappers and the `StringView` to `Text` copies in `EmitPermutationInc` (dev tool, small inputs).
