# Phase 11 / Day 139 — Platform Checklists, Guide, Closing Full Run & Wrap-up

**Phase:** [Phase 11 — Settings System](../phase-11-settings-system.md)
**Depends on:** Days 130-138.

Make the store interface something Windows, macOS and the mobile and console targets can implement without redesign, finish the documentation, run everything, close the phase.

## Tasks

- [x] **Backend checklist** `engine/foundation/settings/BACKEND.md`: what each platform store implements — **Windows registry** (`HKCU\Software\<app>`: sections as subkeys, typed values `REG_DWORD`/`REG_QWORD`/`REG_SZ`, lists as `REG_MULTI_SZ`, no comments or order, per-key writes, `RegNotifyChangeKeyValue` for the change capability, 32/64-bit view, group policy keys as a read-only `System` layer), **macOS** (`NSUserDefaults` suites or a plist file through the file store, `NSUserDefaultsDidChangeNotification`), **Android** (`SharedPreferences`, `apply()` versus `commit()`), **iOS** (`NSUserDefaults`), **consoles** (save-data APIs behind the same store, size limits and commit latency); one `###` section per virtual function of `ConfigStore` and per `Backend:`-labeled function; a ctest `settings_backend_lists` keeps it in step with the headers (as `io_backend_lists` does) and `settings_cross_syntax_windows|macos` compile the neutral headers for those targets with clang's `-fsyntax-only`.
- [x] A **registry-shaped store sketch** in `tests/settings/` (`NoOrderConfigStore` from Day 132 extended to typed per-key writes, no comments, notifications from another thread) passes the full conformance suite, so the Windows port starts from a passing model; record in the checklist which conformance cases a registry store needs capability flags for.
- [x] Add the Phase 13 and 21 items to their plan files (the registry store and the `NSUserDefaults` store, run the conformance suite, the standard locations), a note to Phase 15 (the UI's settings screens bind to the registry metadata: labels, groups, ranges, `restart_required`; `Settings` listeners drive live updates) and to Phase 27 (scripting reads and sets settings by key).
- [x] **Guide pass:** read `docs/guide/settings.md` end to end as a new developer (Keys and values, Declaring a setting, Reading, Where values live, The file format, Layers, Environment and command line, Loading/changing/saving, Reacting to a change, Changing a setting between versions, Writing a store backend, Settings in an application); a "Which API do I use?" table at the top; every tagged sample compiles and runs; `docs/guide/README.md` current; `docgen lint` zero; reference regenerated and committed.
- [x] Update `CLAUDE.md`: a hard architectural constraint "Anything a user can configure is a setting registered with `settings::Registry` and read through `Setting<T>` or `Settings`; no new ad hoc `getenv` or flag parsing for configuration, no config file formats of its own" with its exceptions (pre-`main` bootstrap variables listed in the spec, third-party code, tools' own CLI) and a pointer to the check below; `PLAN.md` assumption added; `STATUS.md` commands.
- [x] **A configuration check** (`tools/config_access_check`, ctest, like the Phase 9 gate): fails on a new `getenv` or `--flag` parse in `engine/` and `game/` outside an allow list with a reason per entry (the bootstrap variables, the demos' launch-only options, the settings system itself); stale entries fail; planted-violation self-tests.
- [x] Known-issues sweep: every deferral filed (remote and live-reloaded config, a UI binding layer, secrets and per-machine state, the Windows registry and other platform stores, autosave policies beyond the first, list-valued settings if cut, migration tooling), entries this phase fixed removed.
- [x] **Closing full run (CLAUDE.md, required for a phase's last day):** `scripts/test-phase.sh --all --flavor all`, `ctest -L docs`, both demos 1000 frames on Wayland and X11 with `--validation`, `--list-settings` and `--save-settings` round trip; record the final `ran X of Y tests (phases ...)` line in this file; nothing left out.
- [x] **End-of-phase review (CLAUDE.md, uncapped):** `/code-review high --max-findings all` over the whole phase's diff (Days 130-139 as committed: from the commit before Day 130 to the working tree); verify, fix or decline each finding with a reason; record the count and outcome under "Post-implementation code review".
- [x] Update `plans/phase-11-settings-system.md` (status complete, days marked done), `PLAN.md` (roadmap row, progress-log entry), `STATUS.md`; `CURRENT_PHASE` stays 9 until Phase 13 has its day files and starts (policy); commit.

**Done:** the checklist and its tests pass, the configuration check passes, the guide is complete, `CLAUDE.md` states the rule, the closing line `ran X of Y tests (phases: all; flavors: all)` is recorded with every test passing, the demos behave on both display backends, and Phase 11 is marked complete.

## Result

Backend checklist (`engine/foundation/settings/BACKEND.md`, `settings_backend_lists`, `settings_cross_syntax_windows|macos`), `RegistryShapedConfigStore` passing the conformance suite, plan notes in Phases 13, 21, 15, 27, 29 and 30, the guide's "Which API do I use?" table and "Keeping configuration in one place", `CLAUDE.md` rule, `PLAN.md` assumption #22, and the `config_access_check` gate (allow list with a reason per entry, planted, clean, stale and no-reason self-tests) are done. `debug_backend_lists` now skips `engine/foundation/settings/` as it skips `io/`.

Closing run: `ran 3441 of 3441 tests (phases: all; flavors: all; display: included; crash tests: included) in 73 s`; the only failure is the known `X11Window::Create` test (KNOWN-ISSUES). `ctest -L docs` passes; docgen lint at zero. Both demos ran 1000 frames with `--validation` on Wayland and X11 (exit 0); the `--save-settings`/`--settings` round trip is the ctest `*_settings_save_reproduce`.

## Post-implementation code review

End-of-phase review `/code-review high --max-findings all` over Days 130-139: **18 findings**.

Fixed, each with a regression test in `tests/settings/review_test.cpp` (or the demo CLI checks / gate fixtures); the cases were written with the fixes and were not each re-run against the unfixed code:
- Legacy demo flags lost to any file layer: now they yield only to an explicit `--set` (`*_settings_flag_beats_user_file`).
- `LayerStack::Set` (minimal) and `Unset` left the deprecated name behind: both remove it.
- Text: invalid UTF-8 accepted and a stored form over 511 bytes failed only at save: both rejected at entry (`ParseValue`/`ValidateValue`).
- `last_save_status_` race: written under `mutex_`.
- Renderer demo mislogged `ApplyFailed`: it logs the renderer's own result.
- Listener removed by an earlier listener still called (use after free): checked before each call.
- `--settings` with an unparsable path or a missing file: reported as a problem.
- `Save` with `include_command_line` wrote secrets: skipped.
- Rollback left a generated section header and blank line: generated sections are removed when emptied.
- Reserved keys accepted as deprecated names, and keys containing `__` sharing an environment variable: refused by `Register`.
- `MapValue` never matched quoted text: matches both forms.
- `SaveSettingsTo` downgraded a newer file's stamps: returns `TooNew`.
- `.bak` replaced on every save: kept from the first replacement of a load.
- `RestArgs()` not null-terminated: now is.
- Gate ignored trailing `//` comments: handled outside string literals (fixtures added).

Declined or deferred, with the reason, filed in `KNOWN-ISSUES.md` (settings entry items 11-14): the platform-specific `environ` in `env_source.cpp` (Phase 13 moves it behind `os::`); whole-snapshot rebuild per change (not measured); the 511-byte limit on the stored text form (needs a larger transport than `Value`); provenance listing for a legacy flag shadowed by a higher layer.
