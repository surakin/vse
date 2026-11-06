# Phase 8 / Day 118 — `io::File` and `LocalFile`: the Synchronous Side

**Phase:** [Phase 8 — File Handling](../phase-08-file-handling.md)
**Depends on:** Day 117.

The interface class and its only implementation for now. New files `io/file.h`, `io/linux/local_file.cpp`, tests in `tests/foundation`.

## Tasks

- [ ] `io::File` as an abstract class (virtual destructor, no copy), owned through `UniquePtr<File>`: `OpenMode` (`Read`, `Write` truncating, `Append`, `ReadWrite`; flags `Create`, `CreateNew`, `Truncate`), `Capabilities()` (seekable, size known,
      writable, natively asynchronous, supports atomic replace), `Size`, `Read(buffer, length, offset)` (returns bytes read, 0 at the end; a short read is not an error), `Write(data, length, offset)`, `Append`, a stream position
      (`Seek`, `Tell`, `Read(buffer, length)`, `Write(data, length)`), `Flush` (to the OS), `Sync` (to the device), `Truncate`, `Close` (idempotent; the destructor closes and logs a failed close).
- [ ] The helpers that sit on the interface and work for any backend: `ReadAll` into an arena (the port of `ReadFileToArena`), into a `Vector<u8>`, `ReadExact`, `WriteAll`, `ReadText` (a `String`), and the free function `WriteFileAtomic(path, data, length)`
      (temporary file in the same directory, `Flush`+`Sync`, `Rename`; the target is never seen half-written; the temporary is removed on every failure path).
- [ ] `File::Open(const Path&, OpenMode, UniquePtr<File>* out)` returning `Status`; the scheme registry: a path with no scheme is a local file, `file:` is the same, `RegisterFileScheme(scheme, factory)` for backends added later,
      an unknown scheme fails with `Unsupported` (tested with a registered fake scheme); a test that `Open` of a directory fails with `IsADirectory`.
- [ ] `LocalFile` (Linux): `open`/`pread`/`pwrite`/`fsync`/`ftruncate`/`close` with `O_CLOEXEC`, `EINTR` retried, partial writes completed, offsets 64-bit (a test over 4 GiB on a sparse file, skipped on file systems without sparse files with the reason printed),
      the descriptor never exposed (no `NativeHandle`), a `ReadOnly` open of an unreadable file failing with `PermissionDenied`.
- [ ] A **conformance suite** `FileConformance(Factory)` (tests/foundation): sequential and offset reads and writes, short reads at the end, appends, truncate, close twice, use after close (`Aborts:` or `Fails:` as the spec says), reading a
      zero-length file, large buffers, a failing write on a full device (a tmpfs with a size limit when available; skipped with a reason otherwise); `LocalFile` runs it now, the Day 119 double runs it too.
- [ ] Tests (phase 8): the conformance suite on `LocalFile`; `WriteFileAtomic` with an injected failure at each step (write, flush, rename: a test hook in the Linux backend) leaves the old content intact and no temporary behind; two threads reading
      different offsets of one file concurrently (TSan flavor); the helpers on a file larger than the buffer.
- [ ] **Documentation:** labels (`Fails:` for every status-returning function, `Aborts:` for use after close if that is the decision); the guide gets "Reading and writing files" and "Replacing a file atomically" with tagged samples that run; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [ ] Run `scripts/test-phase.sh --affected`, then the asan and tsan flavors (the day adds concurrency and lifetime code); quote the lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`; record it. Extra scrutiny: error paths that leak descriptors, the atomic replace on failure.
- [ ] Update `STATUS.md`; commit.

**Done:** a file can be opened, read, written, appended, truncated and replaced atomically through the interface with the conformance suite passing on `LocalFile`, no descriptor visible in the API, and the guide sections committed.
