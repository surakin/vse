# Phase 9 / Day 118 — `io::File` and `LocalFile`: the Synchronous Side

**Phase:** [Phase 9 — File Handling](../phase-09-file-handling.md)
**Depends on:** Day 117.

The interface class and its only implementation for now. New files `io/file.h`, `io/linux/local_file.cpp`, tests in `tests/foundation`.

## Tasks

- [x] `io::File` as an abstract class (virtual destructor, no copy), owned through `UniquePtr<File>`: `OpenMode` (`Read`, `Write` truncating, `Append`, `ReadWrite`; flags `Create`, `CreateNew`, `Truncate`), `Capabilities()` (seekable, size known,
      writable, natively asynchronous, supports atomic replace), `Size`, `Read(buffer, length, offset)` (returns bytes read, 0 at the end; a short read is not an error), `Write(data, length, offset)`, `Append`, a stream position
      (`Seek`, `Tell`, `Read(buffer, length)`, `Write(data, length)`), `Flush` (to the OS), `Sync` (to the device), `Truncate`, `Close` (idempotent; the destructor closes and logs a failed close).
- [x] The helpers that sit on the interface and work for any backend: `ReadAll` into an arena (the port of `ReadFileToArena`), into a `Vector<u8>`, `ReadExact`, `WriteAll`, `ReadText` (a `String`), and the free function `WriteFileAtomic(path, data, length)`
      (temporary file in the same directory, `Flush`+`Sync`, `Rename`; the target is never seen half-written; the temporary is removed on every failure path).
- [x] `File::Open(const Path&, OpenMode, UniquePtr<File>* out)` returning `Status`; the scheme registry: a path with no scheme is a local file, `file:` is the same, `RegisterFileScheme(scheme, factory)` for backends added later,
      an unknown scheme fails with `Unsupported` (tested with a registered fake scheme); a test that `Open` of a directory fails with `IsADirectory`.
- [x] `LocalFile` (Linux): `open`/`pread`/`pwrite`/`fsync`/`ftruncate`/`close` with `O_CLOEXEC`, `EINTR` retried, partial writes completed, offsets 64-bit (a test over 4 GiB on a sparse file, skipped on file systems without sparse files with the reason printed),
      the descriptor never exposed (no `NativeHandle`), a `ReadOnly` open of an unreadable file failing with `PermissionDenied`.
- [x] A **conformance suite** `FileConformance(Factory)` (tests/foundation): sequential and offset reads and writes, short reads at the end, appends, truncate, close twice, use after close (`Aborts:` or `Fails:` as the spec says), reading a
      zero-length file, large buffers, a failing write on a full device (a tmpfs with a size limit when available; skipped with a reason otherwise); `LocalFile` runs it now, the Day 119 double runs it too.
- [x] Tests (phase 9): the conformance suite on `LocalFile`; `WriteFileAtomic` with an injected failure at each step (write, flush, rename: a test hook in the Linux backend) leaves the old content intact and no temporary behind; two threads reading
      different offsets of one file concurrently (TSan flavor); the helpers on a file larger than the buffer.
- [x] **Documentation:** labels (`Fails:` for every status-returning function, `Aborts:` for use after close if that is the decision); the guide gets "Reading and writing files" and "Replacing a file atomically" with tagged samples that run; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [x] Run `scripts/test-phase.sh --affected`, then the asan and tsan flavors (the day adds concurrency and lifetime code); quote the lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`; record it. Extra scrutiny: error paths that leak descriptors, the atomic replace on failure.
- [ ] Update `STATUS.md`; commit.

**Done:** a file can be opened, read, written, appended, truncated and replaced atomically through the interface with the conformance suite passing on `LocalFile`, no descriptor visible in the API, and the guide sections committed.

## Result

- `io/file.h` (`File`, `OpenMode`, `OpenFlag`, `FileCapability`, scheme registry, `ReadExact`/`ReadAll`/`ReadFile`/`ReadTextFile`/`WriteFile`/`WriteFileAtomic`), `io/file.cpp` (stream operations, registry, helpers, atomic write with a failure-injection hook in `io::detail`), `io/linux/local_file.cpp` (`open`/`pread`/`pwrite`/`fsync`/`ftruncate`/`close`, `O_CLOEXEC`, `EINTR` retried, transfers chunked at 1 GiB, no descriptor in the API).
- Decisions: offset reads return a count through `usize*` (`Status` carries the error); `Write` is complete-or-error; `Append` is a separate operation allowed only on `OpenMode::Append` files (`kCapAppendOnly`), and the stream `Write` on such a file appends; use after `Close` is `Fails: Invalid` (not an abort); `Error::EndOfFile` added for `ReadExact`; `File::Open` takes a `Path` (a canonicalized path cannot carry `scheme://`), `File::OpenLocation` takes text and dispatches to the registry; `WriteFileAtomic` gives new files mode 0600 and does not carry permissions over (documented).
- Tests: `tests/io/file_conformance.h` (the `FileMaker`-based read and write suites, reused by Day 119's double) and `tests/io/file_test.cpp`; the io executables now run 32 cases / 17271 assertions in plain, ASan and TSan. Covered: both suites on `LocalFile`, open modes and flags and every documented error, append, schemes and the registry, a 5 GiB sparse offset, `/proc` files (size 0), `/dev/full` for `NoSpace`, four threads reading one file, helper edge cases, `WriteFileAtomic` with a failure injected at each of its four steps (old content intact, no temporary left).
- Docs: guide sections "Reading and writing files" and "Replacing a file atomically" with two run samples; baseline 3296/3296; `docs/samples-prelude.h` includes `io/file.h`.
- Scoped runs: `ran 1069 of 2834 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (only the known X11 window test failed), `ran 786 of 2834 ... flavors: asan`, `ran 506 of 2834 ... flavors: tsan`, all pass; `ctest -L docs` 4/4.

## Post-implementation code review

`/code-review high`: 10 findings, 8 fixed, 2 declined.

Fixed (with tests where practical):
1. The `/dev/full` test also called `WriteFileAtomic` on the real device path, which as root could replace the device node: removed; the test only writes to the device.
2. `OpenMode::Read` with a create or truncate flag created or truncated a file: now `Invalid` (test).
3. `Seek` after `Close` succeeded: `File` gained `IsOpen()` and `Seek` returns `Invalid` when closed (test).
4. `file://` URLs were not percent-decoded and a Windows drive form would break: decoded (bad escapes are `Invalid`), `file:///C:/x` handled for the Windows style (tests for the first).
5. `WriteFileAtomic` did not sync the directory, replaced a symlink instead of its target and reset permissions: it now syncs the directory (new `io::SyncDirectory`), follows a link at the target, and keeps a replaced file's permission bits (new `io::SetPermissions`); tests for each.
6. The arena `ReadAll` copied through a temporary vector: it now reads straight into an arena block sized from `Size()` (the size stays a hint: a probe read decides whether the general path is needed; test with a `/proc` file).
7. A zero return from `write`/`pwrite` made the write loop spin: now `Io`.
8. The failure hook was an unsynchronized `int`: now `std::atomic<int>`. Declined moving it out of production code: the hook is a few lines in a `detail` namespace and a wrapper `File` cannot reach the temporary-file and rename steps.
9. `open` without `O_NOCTTY`: added; FIFO blocking documented.

Declined: `snprintf` in the close-failure log line: the foundation's log and RHI code format with `snprintf` already, the rule is about `printf` output, not formatting into a buffer. Bookkeeping (STATUS.md, this section): this commit.
