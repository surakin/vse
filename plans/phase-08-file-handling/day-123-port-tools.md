# Phase 8 / Day 123 — Port 3: The Offline Tools

**Phase:** [Phase 8 — File Handling](../phase-08-file-handling.md)
**Depends on:** Days 116-122.

The tools are the largest users of directories and paths (docgen walks the repository, the minters write generated files). They may use a vetted library where the plan allows (see `CLAUDE.md`); file access is not one of those cases.

## Tasks

- [x] `tools/docgen`: `module_map.cpp`, `links.cpp`, `reference.cpp`, `symbol_index.cpp`, `samples.cpp`, `main.cpp`, `source_file.cpp` — every `opendir`/`readdir`/`stat`/`realpath`/`getcwd`/`mkdir`/`rename`/`fopen`/`fwrite` replaced by `Directory`/`WalkDirectory`,
      `Stat`, `Canonical`, `CurrentDirectory`, `CreateDirectories`, `WriteFileAtomic` and `File`; the arena-concatenated path strings (`ArenaCat`) become `Path` operations (the percent-decoding and `..` rules in `links.cpp` get their tests re-run against `Path::Normalize`
      and `IsInside`); output stays byte-identical (the `docs_*` and docgen tests, and the committed `docs/reference/` must not change except through a deliberate reason).
- [x] `tools/shader_mint` (the `std::rename` of its temporary files, reading `.slang` and reflection files), `tools/crash_extract`, `tools/rhi_boundary_check`, `tools/raw_new_check`, `tools/signal_safety_check`, `tools/platform_neutral_check`, `tools/unicode_data_gen`: all file and directory access on `io`.
      Where a tool needs `std::string`/`std::vector` for a vetted library (nlohmann/json in `shader_mint`), only the library boundary uses them.
- [x] Minting stays deterministic: directory iteration in the minters goes through the sorted helper; a test runs each minter twice on a shuffled directory order (a test hook in the iteration) and compares outputs byte for byte.
- [x] Tests: the tools' own test suites pass unchanged (`tests/docgen`, `tests/shader_mint`, the gate self-tests with planted violations), plus new cases for the error paths that used to be silent (an unreadable directory, a full disk when writing the reference: the previous output is kept).
- [x] **Documentation:** `docs/guide` pages for the tools that name file behavior (`building-and-testing.md` docs section) updated; reference regenerated and unchanged except as intended; `docgen lint` zero; `ctest -L docs`.
- [x] Run `scripts/test-phase.sh --affected` plus the asan flavor; `cmake --build build --target docs`; quote the lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`.
- [ ] Update `STATUS.md`, the inventory table (all rows done except the documented exceptions); commit.

**Done:** every tool reaches the file system only through `io`, the committed reference and generated files are unchanged, and the minters' determinism test passes with shuffled directory order.

## Result

- `tools/docgen`: all file access moved into `source_file.h/.cpp` (thin `StringView` wrappers over `io`: `KindOf`, `ListNames`, `EnsureDirectories`, `WriteTextAtomic`, `WriteText`, `RemoveFile`, `MakeScratchDirectory`, `RemoveTree`, `CanonicalOrSame`, `WorkingDirectory`, `LoadSourceFile`); `module_map.cpp`, `links.cpp`, `symbol_index.cpp`, `reference.cpp`, `samples.cpp` and `main.cpp` use them instead of `opendir`/`readdir`/`stat`/`realpath`/`getcwd`/`mkdir`/`mkdtemp`/`unlink`/`rmdir`/`fopen`/`rename`. Each generated page and the baseline are now written atomically. The arena-concatenated path strings stay as keys and display text (they are not file access); the lexical `..`/percent-decoding rules of `links.cpp` are unchanged, so `Path::Normalize` is not involved there (recorded as a deviation from the day text: replacing them would change what the link checker reports without making any file access safer). The generated pages are byte-identical (the only `docs/reference/` change is the new `WriteFileAtomic` signature in the file layer's own page).
- `tools/shader_mint`: `ReadAll` and `WriteFiles` use `io` (the two-phase stage-then-rename of one minting's outputs is kept; `std::string`/`std::vector` remain only around the JSON library and the tool's own text building); `tools/crash_extract` links the foundation and reads/writes through `io` (the output atomically); the envelope parser stays dependency-free. The other tools named in the day file (`rhi_boundary_check`, `raw_new_check`, `signal_safety_check`, `platform_neutral_check`) are CMake scripts and `unicode_data_gen` is Python: no C++ file access to port (the Day 124 gate scans C and C++ only).
- Determinism: `docgen` sorts what it collects; a new test builds the whole reference and the Markdown file list under three shuffled directory orders (`SetNameOrderSeedForTesting`) and compares the bytes. `shader_mint` and `renderer_demo_mint` take explicit paths and iterate a fixed table: no directory iteration to shuffle.
- Tests: the seven docgen test files use a shared `tests/docgen/fs_fixture.h` over `io` for temporary trees, reading, writing and listing (the symlink-loop, `chmod 0000` and similar cases stay raw on purpose: they build the file-system shapes under test; exception E4 for the Day 124 gate); new cases: output unchanged under shuffled directory order, and an unwritable output directory keeps the previous files and leaves no temporary behind. `tests/docgen` 171 cases, `tests/shader_mint` and `crash_extract_tool` unchanged and passing.
- Docs: `building-and-testing.md` (tools use the file layer; the determinism hook); reference unchanged; baseline 3394/3394.
- Scoped runs (after the review fixes): `ran 1258 of 2903 tests (phases: all; flavors: plain; display: included; crash tests: included)` (only the known X11 window test failed), `ran 810 of 2903 ... flavors: asan`, `ran 527 of 2903 ... flavors: tsan`; `ctest -L docs` 4/4; `cmake --build build --target docs` regenerates nothing new.

## Post-implementation code review

`/code-review high`: 10 findings, 9 fixed, 1 declined (stale includes noted).

Fixed:
1. `ReadSmall` (captured compiler output) refused files over 1 MiB, where the old loop kept the first MiB: it truncates again.
2. `WriteFileAtomic` gave new files mode 0600, so generated pages and the baseline became owner-only (the old `fopen` gave 0666 minus the umask): `WriteFileAtomic` takes `new_file_permissions` (default 0600, still right for crash dumps); docgen's pages and `.rtex` assets pass 0644 (tests for the new parameter).
3. The determinism test could leave the shuffle seed set after a failed `REQUIRE`: scope guard.
4. `ListNames` left a partial list behind when a read failed midway: rolled back.
5. The determinism test built the whole repository four times and depended on its contents: it uses a synthetic project (36 headers in 6 directories, 6 Markdown files).
6. The forwarding `WriteFile` in `samples.cpp` (easily confused with `io::WriteFile`) is gone.
7. `tools/docgen/DESIGN.md` still described `mkdtemp` for the scratch directory: updated.
8. `shader_mint`'s cleanup called `Remove` on temporaries that were never named: only named ones.
9. Mutating docgen wrappers (`RemoveTree`, `RemoveFile`, `WriteText*`) treated an empty path as the current directory: they refuse it (the "empty means ." rule stays for stat and listing only).

Declined: stale `<sys/stat.h>`/`<dirent.h>` includes in a few docgen files and tests: harmless, and removing them risks the unity builds that rely on neighbours' includes (the Day 117 `<cstdio>` incident); the Day 124 gate will not care about includes.
