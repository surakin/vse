# Log devices / Day 3: the file device with rotation

**Series:** [log devices](../log-devices.md)
**Depends on:** Day 1.

## Tasks

- [ ] **Tests first** (`tests/io/file_log_device_test.cpp`, tag `[log][file]`; `tests/support` temp directory; line in `tests/phases.txt`):
  - lines are appended in order, one per `Write`, and survive a reopen (append mode);
  - a line at `Error` level is on disk without a `Flush()`; lower levels are on disk after `Flush()` or destruction;
  - rotation: when the file would pass `max_bytes`, `name` becomes `name.1`, `name.1` becomes `name.2`, and so on up to `keep` files, the oldest
    removed; no line is split or lost across a rotation;
  - open failure (a directory path, no permission) is an `io::Status` from `Open`, and the device then drops writes without aborting;
  - 4 threads writing 1000 lines each give 4000 whole lines (tsan clean);
  - a device removed from the registry then destroyed has flushed its file.
- [ ] **`io/file_log_device.{h,cpp}`** (the io layer, `io::File` only; `file_access_check` clean): `FileLogDevice::Open(path, options, UniquePtr<FileLogDevice>*)`
  returning `io::Status`; `FileLogOptions` (`max_bytes`, `keep`, `flush_level`); a `Mutex`-guarded buffer; `Flush()`; rotation through
  `io::Path` and `io::Directory`.
- [ ] **`modules.txt` and `ARCHITECTURE.md`**: `module_deps_check` maps the new files to the io layer; `log` includes nothing new.
- [ ] **Docs**: header `///` comments (`Fails:` labels), `docgen lint` at zero, `docs/reference/` regenerated, `docs/guide/io.md` reference paragraph.
- [ ] **Standing rows** (series rules).

**Done:** a file device appends, flushes on errors, rotates by size and fails by value; tests pass plain, asan and tsan; the review is recorded below.

## Post-implementation code review

(to be written)
