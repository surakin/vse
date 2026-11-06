# Phase 8 / Day 115 — Design Spec, Inventory & Phase Setup

**Phase:** [Phase 8 — File Handling](../phase-08-file-handling.md)
**Depends on:** Phase 7 complete.

No product code except the phase setup. The decisions in the phase file are confirmed or amended by experiment, the inventory of access sites
is made exact, and the phase is wired into the test map.

## Tasks

- [ ] **Phase setup first:** `CURRENT_PHASE` becomes 8; `tests/phases.txt` gets `flavors 8 plain,asan,tsan` (the foundation tests have no sanitizer
      executables today; decide here which flavors the io tests get and record why) and the phase list comment names "8 file handling"; `docs/reference/modules.txt` gets the `io` module
      (`header engine/foundation/io/*.h`); `scripts/test-phase.sh` and `docs/guide/building-and-testing.md` list the new label. Check that a phase with no test files runs nothing and says so.
- [ ] **Inventory of every file-access site**, committed as a table in the spec (file, what it does, which API it uses, signal-safe or not, the port day or
      the documented exception). Start from: `os::Open/Read/Write/Close/Lseek/CreateFileForWriting/EnsureDirectory/EnsurePrivateDirectory/GetCrashDumpDirectory`
      and `os::ReadFileToArena` (`engine/foundation/os`), `jobs::ReadFileAsync` and `jobs::IoThread` (`engine/jobs/async_io.*`), `os/linux/debug/debugger.cpp` and `crash_context.cpp` (reading
      `/proc`), `debug/crash_handler_sentry.cpp` (the report file, partly signal-safe), `vulkan_device_pipelines.cpp` (pipeline cache: `mkdir`, `stat`, `fopen`, `rename`),
      `game/common/textures.cpp` (`.rtex`), `game/rhi_demo` and `game/renderer_demo` (PPM writers, asset and cache directories), `tools/docgen` (`opendir`, `readdir`, `stat`,
      `realpath`, `getcwd`, `mkdir`, `rename`, `fopen`), `tools/shader_mint`, `tools/crash_extract`, the tests' helpers (`tests/rhi/image_compare.cpp`, golden paths, temporary files), and the `memfd` shared
      memory of the Wayland window (not a path: stays). Find the rest with `grep` for `fopen|open\(|opendir|readdir|stat\(|mkdir|rename\(|unlink|realpath|getcwd|access\(|std::ifstream|std::ofstream|fread|fwrite` and record every hit.
- [ ] **Spike A — async layering.** A throwaway program: an `IoExecutor` interface in the foundation, `jobs::IoThread` implementing it, a callback-based `ReadAsync`,
      and a `co_await` adapter in `jobs`; measure the cost of a callback hop versus today's `ReadFileAsync` (it must not be slower by more than noise for 1000 small reads) and
      confirm no include goes from the foundation up into `jobs`.
- [ ] **Spike B — a network-like file.** A throwaway `File` implementation whose reads complete on another thread after a delay and whose size is unknown until the first read; confirm the draft interface
      (offset reads, capabilities, partial reads, no descriptor) can express it without escape hatches. Record what had to change in the draft.
- [ ] **Spike C — path edge cases.** A table of 60 inputs with the expected result for each `PathStyle` (POSIX, Windows): empty, `.`, `..`, trailing slashes, repeated slashes, `a/../..`, drive letters, UNC,
      `C:foo` (drive-relative), Unicode, a name that is exactly the maximum length; run the draft operations over it. The table becomes Day 116's test data.
- [ ] **The spec** `docs/superpowers/specs/2026-11-NN-file-layer-design.md`: confirms or amends each of the ten decisions in the phase file with the evidence of the spikes; the class sketches
      (`Path`, `File`, `Directory`, `FileInfo`, `Error`, `IoExecutor`); the allocator story (does `Path` take an `Allocator` template parameter like `String`?); the full list of operations with their `Fails:`/`Aborts:`
      contracts; the signal-safe exception list; the threading rules (a `File` is thread-compatible; concurrent reads at offsets are allowed, a stream position is not shared); the platform sections outline.
- [ ] **File the questions that stay open** (memory mapping, watching, locks) in the phase file's notes with the day on which each will be decided or the phase it is deferred to.
- [ ] **Documentation:** the spec is linked from `docs/guide/README.md` under "design"; the phase file's day list is current; `ctest -L docs` passes.
- [ ] Run `scripts/test-phase.sh --affected` (the new phase has no tests yet; quote the line, which says so).
- [ ] No code review (no product code beyond the setup). Update `STATUS.md`; commit.

**Done:** `CURRENT_PHASE` is 8, the spec and the exact inventory are committed, the three spikes are written up with their numbers, and the ten decisions are confirmed or changed in the spec.
