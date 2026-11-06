# Phase 10 / Day 133 — The Text Format and `FileConfigStore`

**Phase:** [Phase 10 — Settings System](../phase-10-settings-system.md)
**Depends on:** Day 132; Phase 8 (`io::File`, `WriteFileAtomic`).

The one store this phase ships for real: a human-edited text file that keeps what the human wrote. New files `settings/config_text.h/.cpp` (parser and writer, no I/O), `settings/file_store.h/.cpp`; tests in `tests/settings/`.

## Tasks

- [ ] The grammar from Spike A in `config_text`: `[section]` headers (dotted sections allowed), `key = value`, `#` and `;` comments on their own line and after a value, blank lines, `version = N` as the first key, quoted text with the documented escapes, lists if the spec has them; every error carries a 1-based line and column and a message, and parsing continues to report up to 20 errors.
- [ ] The **document model**: a line-based list (each line kept with its comment, spacing and quoting) so a change rewrites only the lines it touches; `Set` of an existing key edits in place, a new key goes at the end of its section (the section is created if needed), `Remove` deletes the line and the comment block directly above it only if marked as belonging to it (rule from the spec); a repeated key is an error on read and the last-wins/first-wins rule is the spec's.
- [ ] `FileConfigStore(io::Path)`: loads through `io::File` (size-capped, UTF-8 validated, a UTF-8 BOM accepted, `\r\n` preserved), `Commit` writes with `io::WriteFileAtomic` (default mode 0600 for user files, 0644 for project files — a constructor option) after keeping the previous good file as `<name>.bak`; a missing file is an empty store, not an error; the file's line endings are kept.
- [ ] **A failed read never loses the user's file:** a parse with errors yields the entries that were readable plus the error list, marks the store `damaged`, and `Commit` on a damaged store refuses (`DamagedSource`) unless the caller passes the explicit `ReplaceDamaged` option (then `.bak` holds the damaged text). Test: damaged file, set a value, commit refused, file unchanged byte for byte.
- [ ] Read-only and missing-directory behavior: a read-only file or directory gives `Commit` the status `PermissionDenied`/`ReadOnly` and leaves everything as it was; a missing parent directory is created on `Commit` (with `io::CreateDirectories`) only when the option says so.
- [ ] Tests (phase 10): the format table (80 inputs: each construct, each error with its line and column); **round-trip property** over 1000 random documents (parse, write unchanged, bytes equal; change one value, only that line differs); comments and order preserved; BOM and CRLF; a 1 MiB file; the conformance suite from Day 132 on `FileConfigStore`; the damaged-file protections; `.bak` creation and rotation; failure injected into the file layer (`io::detail::SetAtomicWriteFailureStep`) at each step leaves the old file; two stores on one file (last commit wins, no torn file).
- [ ] **Documentation:** the grammar written out in `docs/guide/settings.md` ("The file format", with a sample file and the error messages); labels on the headers; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [ ] Run `scripts/test-phase.sh --affected` and the asan and tsan flavors; quote the lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`. Extra scrutiny: the parser on hostile input (run it under ASan on the 1000 random documents and on a mutation sweep that flips every byte of the sample files), and the damaged-file path.
- [ ] Update `STATUS.md`; commit.

**Done:** a settings file is read, edited and saved by the program without disturbing what a person wrote, a damaged or read-only file is never overwritten by accident, and the file store passes the same conformance suite as the memory store.
