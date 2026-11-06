# Phase 8 / Day 123 — Port 3: The Offline Tools

**Phase:** [Phase 8 — File Handling](../phase-08-file-handling.md)
**Depends on:** Days 116-122.

The tools are the largest users of directories and paths (docgen walks the repository, the minters write generated files). They may use a vetted library where the plan allows (see `CLAUDE.md`); file access is not one of those cases.

## Tasks

- [ ] `tools/docgen`: `module_map.cpp`, `links.cpp`, `reference.cpp`, `symbol_index.cpp`, `samples.cpp`, `main.cpp`, `source_file.cpp` — every `opendir`/`readdir`/`stat`/`realpath`/`getcwd`/`mkdir`/`rename`/`fopen`/`fwrite` replaced by `Directory`/`WalkDirectory`,
      `Stat`, `Canonical`, `CurrentDirectory`, `CreateDirectories`, `WriteFileAtomic` and `File`; the arena-concatenated path strings (`ArenaCat`) become `Path` operations (the percent-decoding and `..` rules in `links.cpp` get their tests re-run against `Path::Normalize`
      and `IsInside`); output stays byte-identical (the `docs_*` and docgen tests, and the committed `docs/reference/` must not change except through a deliberate reason).
- [ ] `tools/shader_mint` (the `std::rename` of its temporary files, reading `.slang` and reflection files), `tools/crash_extract`, `tools/rhi_boundary_check`, `tools/raw_new_check`, `tools/signal_safety_check`, `tools/platform_neutral_check`, `tools/unicode_data_gen`: all file and directory access on `io`.
      Where a tool needs `std::string`/`std::vector` for a vetted library (nlohmann/json in `shader_mint`), only the library boundary uses them.
- [ ] Minting stays deterministic: directory iteration in the minters goes through the sorted helper; a test runs each minter twice on a shuffled directory order (a test hook in the iteration) and compares outputs byte for byte.
- [ ] Tests: the tools' own test suites pass unchanged (`tests/docgen`, `tests/shader_mint`, the gate self-tests with planted violations), plus new cases for the error paths that used to be silent (an unreadable directory, a full disk when writing the reference: the previous output is kept).
- [ ] **Documentation:** `docs/guide` pages for the tools that name file behavior (`building-and-testing.md` docs section) updated; reference regenerated and unchanged except as intended; `docgen lint` zero; `ctest -L docs`.
- [ ] Run `scripts/test-phase.sh --affected` plus the asan flavor; `cmake --build build --target docs`; quote the lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`.
- [ ] Update `STATUS.md`, the inventory table (all rows done except the documented exceptions); commit.

**Done:** every tool reaches the file system only through `io`, the committed reference and generated files are unchanged, and the minters' determinism test passes with shuffled directory order.
