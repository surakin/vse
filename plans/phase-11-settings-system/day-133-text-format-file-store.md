# Phase 11 / Day 133 — The Text Format and `FileConfigStore`

**Phase:** [Phase 11 — Settings System](../phase-11-settings-system.md)
**Depends on:** Day 132; Phase 9 (`io::File`, `WriteFileAtomic`).

The one store this phase ships for real: a human-edited text file that keeps what the human wrote. New files `settings/config_text.h/.cpp` (parser and writer, no I/O), `settings/file_store.h/.cpp`; tests in `tests/settings/`.

## Tasks

- [x] The grammar from Spike A in `config_text`: `[section]` headers (dotted sections allowed), `key = value`, `#` and `;` comments on their own line and after a value, blank lines, `version = N` as the first key, quoted text with the documented escapes, lists if the spec has them; every error carries a 1-based line and column and a message, and parsing continues to report up to 20 errors.
- [x] The **document model**: a line-based list (each line kept with its comment, spacing and quoting) so a change rewrites only the lines it touches; `Set` of an existing key edits in place, a new key goes at the end of its section (the section is created if needed), `Remove` deletes the line and the comment block directly above it only if marked as belonging to it (rule from the spec); a repeated key is an error on read and the last-wins/first-wins rule is the spec's.
- [x] `FileConfigStore(io::Path)`: loads through `io::File` (size-capped, UTF-8 validated, a UTF-8 BOM accepted, `\r\n` preserved), `Commit` writes with `io::WriteFileAtomic` (default mode 0600 for user files, 0644 for project files — a constructor option) after keeping the previous good file as `<name>.bak`; a missing file is an empty store, not an error; the file's line endings are kept.
- [x] **A failed read never loses the user's file:** a parse with errors yields the entries that were readable plus the error list, marks the store `damaged`, and `Commit` on a damaged store refuses (`DamagedSource`) unless the caller passes the explicit `ReplaceDamaged` option (then `.bak` holds the damaged text). Test: damaged file, set a value, commit refused, file unchanged byte for byte.
- [x] Read-only and missing-directory behavior: a read-only file or directory gives `Commit` the status `PermissionDenied`/`ReadOnly` and leaves everything as it was; a missing parent directory is created on `Commit` (with `io::CreateDirectories`) only when the option says so.
- [x] Tests (phase 11): the format table (80 inputs: each construct, each error with its line and column); **round-trip property** over 1000 random documents (parse, write unchanged, bytes equal; change one value, only that line differs); comments and order preserved; BOM and CRLF; a 1 MiB file; the conformance suite from Day 132 on `FileConfigStore`; the damaged-file protections; `.bak` creation and rotation; failure injected into the file layer (`io::detail::SetAtomicWriteFailureStep`) at each step leaves the old file; two stores on one file (last commit wins, no torn file).
- [x] **Documentation:** the grammar written out in `docs/guide/settings.md` ("The file format", with a sample file and the error messages); labels on the headers; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [x] Run `scripts/test-phase.sh --affected` and the asan and tsan flavors; quote the lines.
- [x] **End-of-day review (CLAUDE.md):** `/code-review high`. Extra scrutiny: the parser on hostile input (run it under ASan on the 1000 random documents and on a mutation sweep that flips every byte of the sample files), and the damaged-file path.
- [x] Update `STATUS.md`; commit.

**Done:** a settings file is read, edited and saved by the program without disturbing what a person wrote, a damaged or read-only file is never overwritten by accident, and the file store passes the same conformance suite as the memory store.


## Result

- New: `settings/config_text.h/.cpp` (`ConfigDocument`, `ConfigReport`: parser, in-place editing, serializer; no I/O), `settings/file_store.h/.cpp` (`FileConfigStore`, `FileStoreOptions`); tests `config_text_test.cpp` (grammar table of 39 value and 24 error rows with line and column, 1000 random documents that round-trip byte for byte and change exactly one line, a byte-flip sweep over a sample file under ASan) and `file_store_test.cpp` (the Day 132 conformance suite on the file store, damaged-file protection, `.bak`, BOM and CRLF, a 1 MiB file, injected failures at all four steps of `WriteFileAtomic`, permissions, missing directories, two stores on one file); guide section "The file format".
- Deviations from the plan: `Remove` never deletes comments (only a person writes them, so no marker rule is needed); a repeated key is a warning, not an error (the Day 130 spec); `FileConfigStore` has no `kCapTypedValues` (text store); the failure injection covers `WriteFileAtomic`'s steps, not a partial `Commit` (a file commit is atomic).
- Not done: no death tests for the new `Aborts:` (none added: the stores return errors); the `.bak` path failing to parse (a path within 4 bytes of the limit) returns `TooLong` and is not tested.
- Runs: plain `ran 1222 of 3194 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (only the known X11 test fails); asan `ran 891 of 3194`; tsan `ran 608 of 3194`; settings executables 29947 assertions in 54 cases; docs lint 3788/3788, `ctest -L docs` passes.

## Post-implementation code review

`/code-review high`: 10 findings, all fixed with a regression test except the last: a new key could land under the wrong header (a top-level dotted key is not a place for it; now a section is created); setting an empty value that precedes a comment glued them (a blank is added); a failed `Load` dropped the staged-change count (Load now keeps the state and `Commit` refuses with the load error until a `Load` succeeds, so an unreadable file is not overwritten); an unreadable section header attributed the entries below it to the previous section (they are now errors); `replace_damaged` mutated the document before the write (now only after it succeeds); parse and `Keys` were quadratic (hashed); an in-place edit could make a line over 4096 bytes (refused); a `.bak` path that does not parse stopped the backup silently (now `TooLong`); a new top-level key could split a comment from its header (kept together, except a block that opens the file); the two byte-by-byte copies are `memcpy`. Declined: none.
