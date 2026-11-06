# Phase 10 / Day 128 — The Phase 9 Test Helpers and `tools/shader_mint`

**Phase:** [Phase 10 — Standard Types and Container Hygiene](../phase-10-types-and-containers.md)
**Depends on:** Day 127.

The two remaining places: the test code Phase 9 added (it should have used the engine's types) and the one tool with a legitimate exception, narrowed to its exact boundary.

## Tasks

- [ ] **Catch2 adapters** in a small shared header (`tests/support/catch_engine_types.h`): `Catch::StringMaker` specializations for `StringView`, `String`, `io::Path`, `io::Status`, `Error`, `u128`/`i128` and `Span`, so assertions on engine types print readable values without converting to `std::string`; `REQUIRE(a == b)` on these types works and
      is tested (a failing assertion's message is checked once, in a test that expects the failure).
- [ ] Port the Phase 9 additions: `tests/io/*` (`temp_dir.h`, `file_conformance.h`, `file_system_test`, `file_test`, `directory_test`, `async_file_test`, `slow_file.h`, `path_test`), `tests/docgen/fs_fixture.h` and the docgen tests' fixture helpers, `tests/rhi/image_compare.*` and the pipeline-cache test additions, `tests/jobs`'s `/proc` readers: `std::string`/`std::vector`/`std::unique_ptr`/`std::function`/`std::set` become `String`/`Vector`/`UniquePtr`/function pointers/
      sorted `Vector`s or `HashMap`; `std::thread` and `std::atomic` stay (allowed). Where Catch2 itself requires `std::` (`GENERATE` ranges, `INFO` streams, matchers), the use is local and commented.
- [ ] `tools/shader_mint`: define the **JSON boundary** (`reflect_json.cpp`, the conversion from `nlohmann::json` to the engine's reflection structures, the text it needs from the library) and move everything else (argument parsing, file reading and writing, text generation for the `.spv.inc`/`.refl.inc`/`.perm.inc` files, the permutation manifest, the compiler invocation) to `String`, `StringBuilder`, `Vector` and `HashMap`; the boundary
      translation unit may use `std::string`/`std::vector` and says so in its header comment; generated outputs stay byte-identical (the shaders regenerate to the same files, `shaders_check` passes, the committed `*.inc` files do not change).
- [ ] Tests: the tests ported above pass unchanged in expectation; `tests/shader_mint` passes; `cmake --build build --target shaders` (where `slangc` exists) reproduces the committed files; new cases for the Catch2 adapters.
- [ ] The remaining old tests: the exact per-file counts are recorded in `KNOWN-ISSUES.md` as a deferral with the rule ("convert a test file when a day touches it") and the adapters from this day as the way to do it.
- [ ] **Documentation:** `docs/guide/building-and-testing.md` ("Writing tests": use the engine's types, the adapters, when `std::` is acceptable in a test); `tools/shader_mint` README/design comment names the boundary; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [ ] Run `scripts/test-phase.sh --affected` and the asan and tsan flavors; quote the lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`.
- [ ] Update `STATUS.md`, the spec's inventory table; commit.

**Done:** the tests written in Phase 9 and `tools/shader_mint` outside its JSON boundary use the engine's types, the generated files did not change, and the old tests' remaining `std::` use is a counted, filed deferral with a way to convert them.
