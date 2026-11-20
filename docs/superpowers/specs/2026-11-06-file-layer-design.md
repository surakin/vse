# File layer — design (Phase 9, Day 115)

Status: decided 2026-11-06 from the inventory and three spikes below. Scope: Linux implemented (`LocalFile`); the interface is shaped for
Windows, macOS, mobile, consoles and network-backed files. `plans/phase-09-file-handling.md` holds the day list; this spec holds the reasons.

## 1. Goal

One path/file/directory layer in `engine/foundation/io/` (namespace `vse::io`) replacing scattered `fopen`/`open`/`opendir`/
`stat`/`mkdir`/`rename` calls and string-pasted paths. Phase 11 (settings) is built on it; Phase 22's storage backends implement `io::File`.

## 2. Inventory (what exists today)

Production code (all hits of `fopen|fread|fwrite|opendir|readdir|mkdir|rename|stat|getenv|os::Open/Read/Write/...|ReadFileAsync|memfd_create`):

| Site | What it does | API today | Signal-safe? | Port day / disposition |
|---|---|---|---|---|
| `foundation/os/os.h`, `os/linux/os.cpp` | `Open/Read/Write/Close/Lseek`, `CreateFileForWriting`, `EnsureDirectory`, `EnsurePrivateDirectory`, `GetCrashDumpDirectory`, `ReadFileToArena` | raw POSIX | partly (descriptor primitives) | **Done Day 121:** `Lseek`, `EnsureDirectory`, `EnsurePrivateDirectory`, `GetCrashDumpDirectory`, `ReadFileToArena` (and `os/file_util.*`) removed; `Read/Write/Close/WriteAllWithTimeout` stay (E1); `Open` and `CreateFileForWriting` stay for the debugger check and the crash report (E1/E2) |
| `foundation/os/file_util.h` | helpers over the above | os | no | **Done Day 121** (removed) |
| `foundation/log/log.cpp` | `log::LoadConfig` reads a config file; default sink is `os::Write` (console) | `os::Open/Read/Close` | no | **Done Day 121** (reads through `io::File`; the console sink is E1). Note for Phase 11: `LoadConfig` is an existing config reader |
| `foundation/main.cpp` | console output only (`os::Write(1, ...)`) | `os::Write` | no | E1: console descriptor, stays |
| `foundation/debug/crash_handler_sentry.cpp` | report file, database directory | `os::` | partly | **Done Day 121:** directories and the stale-report reset use `io`; the SIGNAL-SAFE report file keeps `os::CreateFileForWriting` (E1) |
| `os/linux/debug/crash_signal.cpp` | crash-time writes | `os::` | yes | E1: stays raw |
| `os/linux/debug/crash_context.cpp` | `/proc/cpuinfo` and `/proc/meminfo` reads and the executable path in `InitCrashContext` | `os::Open/Read/Close`, `readlink` | no, but the assert-failure handler calls `InitCrashContext` lazily | **Stays raw (E2)**: ported on Day 121, reverted on Day 124 when the end-of-phase review found the assert path reaches it while the heap may be inconsistent |
| `os/linux/debug/debugger.cpp` | reads `/proc/self/status` (tracer detection) | `os::Open/Read/Close` | no, but reached from the assert-failure path | **Day 121 decision: stays raw (E2)**: the assert handler may run while the heap is inconsistent, and the file layer allocates |
| `os/linux/debug/core_dump.cpp` | reads `/proc/sys/kernel/core_pattern` | `os::Open/Read` | no | **Done Day 121** (`io::File`) |
| `foundation/debug/death_test.cpp` | writes a message to stderr in the child | `os::Write(2, ...)` | yes | E1: stays |
| `os/linux/wayland/wayland_window.cpp` | `memfd_create` shared memory | memfd | no | E3: not a path, stays |
| `jobs/async_io.{h,cpp}` | `ReadFileAsync`, `IoThread` | `os::`, `IoRequest` | no | **Done Days 119 (executor) and 121** (`ReadFileAsync` reads through `io::File`) |
| `rhi/vulkan/vulkan_device_pipelines.cpp` | pipeline cache: `getenv`, `mkdir`, `stat`, `fopen`, `rename` | libc | no | 122 (`UserCacheDirectory` + `WriteFileAtomic`) |
| `game/common/textures.cpp` | `.rtex` read/write | `fopen` | no | 122 |
| `game/renderer_demo/main.cpp` | PPM writer, asset dir listing (`opendir`/`readdir`), `getenv` | libc | no | 122 |
| `game/rhi_demo/main.cpp` | PPM writer, `getenv` | libc | no | 122 |
| `tools/docgen/src/*` (`main`, `reference`, `links`, `samples`, `module_map`, `source_file` via `os::ReadFileToArena`, `symbol_index`) | tree walk, read sources, write the reference | `opendir`/`readdir`/`stat`/`realpath`/`getcwd`/`mkdir`/`rename`/`fopen` | no | 123 |
| `tools/shader_mint/main.cpp` | read `.slang`/reflection, write `.spv.inc` atomically | `fopen`, `rename` | no | 123 |
| `tools/crash_extract/main.cpp` | read dumps, write reports | `fopen`, `rename` | no | 123 |

Tests: about 40 test files use raw file calls (largest: `tests/debug/crash_handler_linux_test.cpp` 25 hits, `tests/docgen/reference_test.cpp` 17,
`tests/rhi/vulkan_pipeline_test.cpp` 12, `tests/jobs/async_io_test.cpp` 12, `tests/debug/crash_end_to_end_linux_test.cpp` 10, ...). Test helpers
(`tests/rhi/image_compare.cpp`, golden paths, temporary files) port on Day 122; tests that exercise raw OS behavior on purpose (crash and core-dump
tests, planted gate violations) are the documented exception E4 and are listed in the gate's allowlist on Day 124.

## 3. Spikes

**Spike A — async layering** (throwaway C++, `-O1`, 1000 sequential 4 KiB round trips on one `IoThread`):

| Path | us/op |
|---|---|
| Direct `IoRequest` on `jobs::IoThread` (today's mechanism) | 2.28 |
| Draft `File::ReadAsync` through the foundation `IoExecutor` seam with a completion callback | 2.32 (+1.7%, noise) |
| `co_await` of a `Counter` completed by the callback, inside a job | 1.67 |

The draft's `IoWork` (16 bytes, `next` + `run`) has the same layout as `jobs::IoRequest`, so `JobsExecutor` is a cast and a call. The foundation header
needs only `types.h`; no include goes from the foundation up into `jobs`. Requests are caller-owned (`ReadRequest`, 72 bytes), so the async path allocates nothing.
Confirms decision 4.

**Spike B — network-like file** (a `File` whose reads complete on its own thread after 200 us, in chunks of at most 1000 bytes, size unknown until the first read hits the end):

- The draft (offset reads, capabilities, partial reads, no descriptor) expressed it with no escape hatch. `ReadAll` written against `Read` only (loop until 0) works; `Size()` returns -1 until known.
- Synchronous `Read` is implementable as "submit the async one and block", so a backend may implement only the async primitive.
- An injected failure at request 2 propagated as a negative status from `ReadAll`, with the 1000 bytes read before it still delivered.
- Destroying the file with 10 requests in flight completed each exactly once, with `Interrupted`.
- 100 concurrent offset reads all returned correct bytes (the double serializes them on one thread, hence 25 ms; a real backend may overlap).

Changes the draft needs: (1) a **short read is not an error and not end-of-file** — the contract says `Read` returns fewer bytes than asked only for a
reason, 0 only at the end; callers use `ReadExact`/`ReadAll` helpers, never a single `Read`; (2) **`Size` is `Status`-returning and may report `Unknown`**,
with a `SizeKnown` capability that can turn on after the first read; (3) **completion is delivered exactly once, including on destruction and shutdown**
(`Interrupted`), so an owner may always wait for its requests; (4) the request struct is caller-owned and must outlive its completion.

**Spike C — path edge cases:** a Python prototype of the draft operations over 60 inputs per style (POSIX and Windows), plus join and relative-to tables.
The generated tables are in Appendix A and become Day 116's test data. Decisions it fixed:

- `Normalize` is lexical: collapses repeated and trailing separators and `.`; resolves `..` against a preceding normal component; keeps leading `..` for relative paths; drops `..` at the root of an absolute path (`/..` -> `/`). The empty path normalizes to `.`.
- `Parent("a")` is empty (not `.`); `Parent("/")` is `/`; `Parent` and `FileName` are computed on the path as written (`a/..` has file name `..`).
- `Extension` is the part from the last dot of the file name, empty when the name starts with the dot and has no other (`.gitignore`), and empty for `.` and `..`. `Stem` is the name without it (`c.tar.gz` -> `c.tar`).
- Windows style: `\` and `/` both separate; the canonical form uses `/`; drive letters (`C:/x`) and UNC (`//server/share/x`) are root names; `C:foo` is drive-relative (not absolute, root name kept); `C:/..` stays `C:/`.
- Joining with an absolute right-hand side replaces the left; `RelativeTo` between different roots (or different drives) fails (empty result with a `false` status), never guesses.
- Component and path length limits are not enforced by `Path` (it is a value); the file-system operations report `NameTooLong` from the OS. A name of exactly `NAME_MAX` bytes round-trips through every lexical operation.

## 4. The ten decisions, confirmed or amended

1. **Location:** confirmed — `engine/foundation/io/`, Linux implementation in `io/linux/`. *Amendment:* the module entry in `docs/reference/modules.txt`, the `engine/foundation/io` line in `tests/module_map.txt` and the `flavors 8` line in `tests/phases.txt`
   arrive on Day 116 with the first header and test (the docs and phase-map gates require a header/test to exist); Day 115 only sets `CURRENT_PHASE`, the phase list comment, `tests/module_map.txt` and the guide table.
2. **`Path`:** confirmed (§3, Spike C). Allocator story: `Path` stores a `String` and takes the same `Allocator` template parameter as `String` with the default allocator, so a path on a job's arena works; `Path` is cheap to
   move, copy allocates. Views (`PathView`, non-owning) are the parameter type of every operation; a `Path` converts to one.
3. **`File`:** confirmed with the Spike B changes. Reads and writes take an explicit offset (`pread`-style); sequential helpers (`ReadAll`, `WriteAll`, a `FileReader` cursor) are built on them, so no stream position is shared.
   `Open(path-or-url, mode)` picks the backend by scheme through a small registry; only `file` exists.
4. **Async:** confirmed (Spike A). Caller-owned request structs, exactly-once completion, `IoExecutor::Submit` returns false after shutdown (the caller completes the request with `Interrupted`).
5. **Errors as values:** confirmed. `io::Error` adds `NameTooLong`, `NotEmpty`, `CrossDevice`, `InvalidPath`, `Unknown` to the planned list; `Status` carries the enum plus the OS code for logs. `Fails:` labels use the enum names.
6. **Signal-safe layer stays raw:** confirmed; exceptions E1 (SIGNAL-SAFE code and console descriptors), E2 (`/proc` reads inside signal-safe sections), E3 (`memfd`), E4 (tests that exercise raw OS behavior on purpose), E5 (third-party code, the backend itself).
7. **Atomic replace:** confirmed. `WriteFileAtomic(path, bytes)`: temporary sibling (same directory, so `rename` is atomic), write, flush and `fsync`, rename; a failure removes the temporary. Cross-device rename is not a case (same directory).
8. **Special folders:** confirmed: `TempDirectory`, `CurrentDirectory`, `ExecutableDirectory`, `UserConfigDirectory`, `UserCacheDirectory`, `UserDataDirectory` (XDG with `$HOME` fallbacks on Linux); a program name is passed by the caller, never guessed.
9. **Platform designs ride along:** confirmed; `BACKEND.md` and its list test on Day 124.
10. **Phase 22 builds on `io::File`:** confirmed; Spike B is the evidence a non-local backend fits.

## 5. Class sketches

```cpp
namespace vse::io {
enum class PathStyle : u8 { Posix, Windows };
class PathView;  class Path;                      // lexical only, no syscalls

enum class Error : u8 { None, NotFound, PermissionDenied, AlreadyExists, NotADirectory, IsADirectory, NotEmpty, NoSpace,
                        NameTooLong, InvalidPath, CrossDevice, Interrupted, Unsupported, Io, Unknown };
struct Status { Error error; i32 os_code; bool Ok() const; };

enum class FileKind : u8 { None, File, Directory, Symlink, Other };
struct FileInfo { FileKind kind; u64 size; i64 modified_unix_ns; };

class IoExecutor { virtual bool Submit(IoWork*) = 0; };           // foundation; jobs::IoThread implements it
struct ReadRequest  { IoWork work; File* file; void* buffer; usize length; u64 offset; Completion done; void* user; IoResult result; };
struct WriteRequest { /* same shape, const void* data */ };

class File {                                                      // thread-compatible; offset I/O may run concurrently
public:
    enum Capability : u32 { Seekable = 1, SizeKnown = 2, Writable = 4, NativelyAsync = 8 };
    virtual u32 Capabilities() const = 0;
    virtual IoResult Size() = 0;                                   // Unknown allowed
    virtual IoResult Read(void*, usize, u64 offset) = 0;           // short read allowed; 0 only at the end
    virtual IoResult Write(const void*, usize, u64 offset) = 0;
    virtual Status Flush() = 0;   virtual Status Close() = 0;
    virtual void ReadAsync(ReadRequest*) = 0;   virtual void WriteAsync(WriteRequest*) = 0;
    static Status Open(PathView path_or_url, OpenMode, UniquePtr<File>* out);
};
class Directory { /* Create, CreateAll, Remove, Iterate (streaming iterator, no full load), Exists */ };
}
```

**Operations and contracts.** Every operation that touches the disk reports failure with `Fails:` (a `Status` return); `Aborts:` is used only for programmer errors
(`ReadAsync` with a null request, a request reused before its completion, a `Path` operation given an invalid `PathStyle`). File-system helpers: `Stat`, `Exists`, `CreateDirectory`,
`CreateDirectories`, `Remove`, `RemoveAll`, `Rename`, `Copy`, `ReadAll`, `WriteAll`, `WriteFileAtomic`, `MakeAbsolute`, `Canonical`, the six special folders.

**Threading:** a `File` is thread-compatible; concurrent offset reads/writes are allowed on one file; there is no shared stream position; destroying a file with requests in flight completes each with `Interrupted` and
then returns. A completion callback runs on the executor/backend thread and must not block.

## 6. Platform outline (`BACKEND.md`, Day 124)

Windows: UTF-16 at the boundary, drive/UNC roots, `\\?\` long paths, overlapped I/O (`NativelyAsync`), share flags; macOS: case-insensitive volumes, `dispatch_io`, sandbox container directories; Android/iOS: app-private
directories, assets as read-only archives (`Writable` off); consoles: title storage and save-data APIs behind the same `File`.

## 7. Open questions (filed in the phase file)

Memory mapping (a `Map` capability): deferred to Phase 22 planning; file watching: Phase 11 decides whether it asks; locks/sharing modes: Phase 13; text encodings: caller's concern; `log::LoadConfig` is an existing config reader the
Phase 11 design must absorb or replace.

## Appendix A — Spike C tables

## posix  (60 inputs)
input | abs/rel | Normalize | Parent | FileName | Stem | Extension
`` | rel | `.` | `` | `` | `` | ``
`.` | rel | `.` | `` | `.` | `.` | ``
`..` | rel | `..` | `` | `..` | `..` | ``
`/` | abs | `/` | `/` | `` | `` | ``
`//` | abs | `/` | `/` | `` | `` | ``
`///` | abs | `/` | `/` | `` | `` | ``
`a` | rel | `a` | `` | `a` | `a` | ``
`a/` | rel | `a` | `` | `a` | `a` | ``
`a//b` | rel | `a/b` | `a` | `b` | `b` | ``
`/a` | abs | `/a` | `/` | `a` | `a` | ``
`/a/` | abs | `/a` | `/` | `a` | `a` | ``
`./a` | rel | `a` | `.` | `a` | `a` | ``
`a/./b` | rel | `a/b` | `a/.` | `b` | `b` | ``
`a/../b` | rel | `b` | `a/..` | `b` | `b` | ``
`a/..` | rel | `.` | `a` | `..` | `..` | ``
`a/b/..` | rel | `a` | `a/b` | `..` | `..` | ``
`a/b/../..` | rel | `.` | `a/b/..` | `..` | `..` | ``
`a/b/../../..` | rel | `..` | `a/b/../..` | `..` | `..` | ``
`../a` | rel | `../a` | `..` | `a` | `a` | ``
`../../a` | rel | `../../a` | `../..` | `a` | `a` | ``
`/..` | abs | `/` | `/` | `..` | `..` | ``
`/../a` | abs | `/a` | `/..` | `a` | `a` | ``
`/a/../..` | abs | `/` | `/a/..` | `..` | `..` | ``
`a/b/c` | rel | `a/b/c` | `a/b` | `c` | `c` | ``
`a/b/c.txt` | rel | `a/b/c.txt` | `a/b` | `c.txt` | `c` | `.txt`
`a/b/c.tar.gz` | rel | `a/b/c.tar.gz` | `a/b` | `c.tar.gz` | `c.tar` | `.gz`
`.gitignore` | rel | `.gitignore` | `` | `.gitignore` | `.gitignore` | ``
`a/.gitignore` | rel | `a/.gitignore` | `a` | `.gitignore` | `.gitignore` | ``
`a.` | rel | `a.` | `` | `a.` | `a` | `.`
`a/b.` | rel | `a/b.` | `a` | `b.` | `b` | `.`
`a/..hidden` | rel | `a/..hidden` | `a` | `..hidden` | `.` | `.hidden`
`...` | rel | `...` | `` | `...` | `..` | `.`
`dir.d/file` | rel | `dir.d/file` | `dir.d` | `file` | `file` | ``
`dir.d/` | rel | `dir.d` | `` | `dir.d` | `dir` | `.d`
`a b/c d` | rel | `a b/c d` | `a b` | `c d` | `c d` | ``
`ünï/çödé.txt` | rel | `ünï/çödé.txt` | `ünï` | `çödé.txt` | `çödé` | `.txt`
`日本語/テスト.md` | rel | `日本語/テスト.md` | `日本語` | `テスト.md` | `テスト` | `.md`
`C:` | rel | `C:` | `` | `C:` | `C:` | ``
`C:/` | rel | `C:` | `` | `C:` | `C:` | ``
`C:\` | rel | `C:\` | `` | `C:\` | `C:\` | ``
`C:a` | rel | `C:a` | `` | `C:a` | `C:a` | ``
`C:a\b` | rel | `C:a\b` | `` | `C:a\b` | `C:a\b` | ``
`C:\a\b` | rel | `C:\a\b` | `` | `C:\a\b` | `C:\a\b` | ``
`C:/a/../b` | rel | `C:/b` | `C:/a/..` | `b` | `b` | ``
`C:/..` | rel | `.` | `C:` | `..` | `..` | ``
`C:..` | rel | `C:..` | `` | `C:..` | `C:.` | `.`
`c:/A/b.TXT` | rel | `c:/A/b.TXT` | `c:/A` | `b.TXT` | `b` | `.TXT`
`\a\b` | rel | `\a\b` | `` | `\a\b` | `\a\b` | ``
`/a\b` | abs | `/a\b` | `/` | `a\b` | `a\b` | ``
`a\b/c` | rel | `a\b/c` | `a\b` | `c` | `c` | ``
`//server/share` | abs | `/server/share` | `/server` | `share` | `share` | ``
`//server/share/` | abs | `/server/share` | `/server` | `share` | `share` | ``
`//server/share/dir/file.txt` | abs | `/server/share/dir/file.txt` | `/server/share/dir` | `file.txt` | `file` | `.txt`
`\\server\share\dir` | rel | `\\server\share\dir` | `` | `\\server\share\dir` | `\\server\share\dir` | ``
`//server` | abs | `/server` | `/` | `server` | `server` | ``
`//server/share/..` | abs | `/server` | `/server/share` | `..` | `..` | ``
`//server/share/../x` | abs | `/server/x` | `/server/share/..` | `x` | `x` | ``
`C:/a/` | rel | `C:/a` | `C:` | `a` | `a` | ``
`xxxxxxxxxxxx...(255)` | rel | `xxxxxxxxxxxx...(255)` | `` | `xxxxxxxxxxxx...(255)` | `xxxxxxxxxxxx...(255)` | ``
`a/yyyyyyyyyy...(259)` | rel | `a/yyyyyyyyyy...(259)` | `a/yyyyyyyyyy...(257)` | `z` | `z` | ``

## windows  (60 inputs)
input | abs/rel | Normalize | Parent | FileName | Stem | Extension
`` | rel | `.` | `` | `` | `` | ``
`.` | rel | `.` | `` | `.` | `.` | ``
`..` | rel | `..` | `` | `..` | `..` | ``
`/` | rel | `/` | `/` | `` | `` | ``
`//` | rel | `/` | `/` | `` | `` | ``
`///` | rel | `/` | `/` | `` | `` | ``
`a` | rel | `a` | `` | `a` | `a` | ``
`a/` | rel | `a` | `` | `a` | `a` | ``
`a//b` | rel | `a/b` | `a` | `b` | `b` | ``
`/a` | rel | `/a` | `/` | `a` | `a` | ``
`/a/` | rel | `/a` | `/` | `a` | `a` | ``
`./a` | rel | `a` | `.` | `a` | `a` | ``
`a/./b` | rel | `a/b` | `a/.` | `b` | `b` | ``
`a/../b` | rel | `b` | `a/..` | `b` | `b` | ``
`a/..` | rel | `.` | `a` | `..` | `..` | ``
`a/b/..` | rel | `a` | `a/b` | `..` | `..` | ``
`a/b/../..` | rel | `.` | `a/b/..` | `..` | `..` | ``
`a/b/../../..` | rel | `..` | `a/b/../..` | `..` | `..` | ``
`../a` | rel | `../a` | `..` | `a` | `a` | ``
`../../a` | rel | `../../a` | `../..` | `a` | `a` | ``
`/..` | rel | `/` | `/` | `..` | `..` | ``
`/../a` | rel | `/a` | `/..` | `a` | `a` | ``
`/a/../..` | rel | `/` | `/a/..` | `..` | `..` | ``
`a/b/c` | rel | `a/b/c` | `a/b` | `c` | `c` | ``
`a/b/c.txt` | rel | `a/b/c.txt` | `a/b` | `c.txt` | `c` | `.txt`
`a/b/c.tar.gz` | rel | `a/b/c.tar.gz` | `a/b` | `c.tar.gz` | `c.tar` | `.gz`
`.gitignore` | rel | `.gitignore` | `` | `.gitignore` | `.gitignore` | ``
`a/.gitignore` | rel | `a/.gitignore` | `a` | `.gitignore` | `.gitignore` | ``
`a.` | rel | `a.` | `` | `a.` | `a` | `.`
`a/b.` | rel | `a/b.` | `a` | `b.` | `b` | `.`
`a/..hidden` | rel | `a/..hidden` | `a` | `..hidden` | `.` | `.hidden`
`...` | rel | `...` | `` | `...` | `..` | `.`
`dir.d/file` | rel | `dir.d/file` | `dir.d` | `file` | `file` | ``
`dir.d/` | rel | `dir.d` | `` | `dir.d` | `dir` | `.d`
`a b/c d` | rel | `a b/c d` | `a b` | `c d` | `c d` | ``
`ünï/çödé.txt` | rel | `ünï/çödé.txt` | `ünï` | `çödé.txt` | `çödé` | `.txt`
`日本語/テスト.md` | rel | `日本語/テスト.md` | `日本語` | `テスト.md` | `テスト` | `.md`
`C:` | rel | `C:` | `C:` | `` | `` | ``
`C:/` | abs | `C:/` | `C:/` | `` | `` | ``
`C:\` | abs | `C:/` | `C:/` | `` | `` | ``
`C:a` | rel | `C:a` | `C:` | `a` | `a` | ``
`C:a\b` | rel | `C:a/b` | `C:a` | `b` | `b` | ``
`C:\a\b` | abs | `C:/a/b` | `C:/a` | `b` | `b` | ``
`C:/a/../b` | abs | `C:/b` | `C:/a/..` | `b` | `b` | ``
`C:/..` | abs | `C:/` | `C:/` | `..` | `..` | ``
`C:..` | rel | `C:..` | `C:` | `..` | `..` | ``
`c:/A/b.TXT` | abs | `c:/A/b.TXT` | `c:/A` | `b.TXT` | `b` | `.TXT`
`\a\b` | rel | `/a/b` | `/a` | `b` | `b` | ``
`/a\b` | rel | `/a/b` | `/a` | `b` | `b` | ``
`a\b/c` | rel | `a/b/c` | `a/b` | `c` | `c` | ``
`//server/share` | abs | `//server/share/` | `//server/share/` | `` | `` | ``
`//server/share/` | abs | `//server/share/` | `//server/share/` | `` | `` | ``
`//server/share/dir/file.txt` | abs | `//server/share/dir/file.txt` | `//server/share/dir` | `file.txt` | `file` | `.txt`
`\\server\share\dir` | abs | `//server/share/dir` | `//server/share/` | `dir` | `dir` | ``
`//server` | rel | `/server` | `/` | `server` | `server` | ``
`//server/share/..` | abs | `//server/share/` | `//server/share/` | `..` | `..` | ``
`//server/share/../x` | abs | `//server/share/x` | `//server/share/..` | `x` | `x` | ``
`C:/a/` | abs | `C:/a` | `C:/` | `a` | `a` | ``
`xxxxxxxxxxxx...(255)` | rel | `xxxxxxxxxxxx...(255)` | `` | `xxxxxxxxxxxx...(255)` | `xxxxxxxxxxxx...(255)` | ``
`a/yyyyyyyyyy...(259)` | rel | `a/yyyyyyyyyy...(259)` | `a/yyyyyyyyyy...(257)` | `z` | `z` | ``

## posix Join(a, b)
`a` | `b` | `a/b`
`a/` | `b` | `a/b`
`a` | `/b` | `/b`
`/a` | `b` | `/a/b`
`/a` | `/b` | `/b`
`` | `b` | `b`
`a` | `` | `a`
`` | `` | ``
`a` | `../b` | `a/../b`
`.` | `b` | `./b`
`a/b` | `../../c` | `a/b/../../c`
`C:/a` | `b` | `C:/a/b`
`C:/a` | `D:/b` | `C:/a/D:/b`
`C:/a` | `D:b` | `C:/a/D:b`
`C:/a` | `/b` | `/b`
`C:a` | `b` | `C:a/b`
`C:` | `b` | `C:/b`
`C:a` | `/b` | `/b`
`//server/share/a` | `b` | `/server/share/a/b`
`//server/share` | `b` | `/server/share/b`
`a` | `C:/b` | `a/C:/b`
## posix RelativeTo(p, base)
`a/b/c` | `a` | `b/c`
`a/b` | `a/b` | `.`
`a` | `a/b` | `..`
`/a/b` | `/a/c` | `../b`
`/a` | `b` | (none)
`a/b` | `a/b/c/d` | `../..`
`../a` | `b` | `../../a`
`a` | `..` | (none)
`C:/a/b` | `C:/a` | `b`
`C:/a` | `D:/a` | `../../C:/a`
`c:/A/b` | `C:/a` | `../../c:/A/b`
`//server/share/a/b` | `//server/share/a` | `b`
`//server/s1/a` | `//server/s2/a` | `../../s1/a`
`.` | `a` | `..`
`a` | `.` | `a`
## windows Join(a, b)
`a` | `b` | `a/b`
`a/` | `b` | `a/b`
`a` | `/b` | `/b`
`/a` | `b` | `/a/b`
`/a` | `/b` | `/b`
`` | `b` | `b`
`a` | `` | `a`
`` | `` | ``
`a` | `../b` | `a/../b`
`.` | `b` | `./b`
`a/b` | `../../c` | `a/b/../../c`
`C:/a` | `b` | `C:/a/b`
`C:/a` | `D:/b` | `D:/b`
`C:/a` | `D:b` | `D:b`
`C:/a` | `/b` | `C:/b`
`C:a` | `b` | `C:a/b`
`C:` | `b` | `C:b`
`C:a` | `/b` | `C:/b`
`//server/share/a` | `b` | `//server/share/a/b`
`//server/share` | `b` | `//server/share/b`
`a` | `C:/b` | `C:/b`
## windows RelativeTo(p, base)
`a/b/c` | `a` | `b/c`
`a/b` | `a/b` | `.`
`a` | `a/b` | `..`
`/a/b` | `/a/c` | `../b`
`/a` | `b` | (none)
`a/b` | `a/b/c/d` | `../..`
`../a` | `b` | `../../a`
`a` | `..` | (none)
`C:/a/b` | `C:/a` | `b`
`C:/a` | `D:/a` | (none)
`c:/A/b` | `C:/a` | `b`
`//server/share/a/b` | `//server/share/a` | `b`
`//server/s1/a` | `//server/s2/a` | (none)
`.` | `a` | `..`
`a` | `.` | `a`
