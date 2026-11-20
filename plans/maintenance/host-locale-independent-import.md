# Maintenance: glTF import fails under a decimal-comma locale

Not a numbered phase. Bug report from the host project (2026-11-18), against the engine at `6d42892a`. Done 2026-11-18 on the Windows PC (see Result).

## Symptom

On a user's machine with the latest the host: `[world] 3D scene failed (4): unsupported: materials[0]: a float value is a finite number` (the host's 4
is `ImportFailed`). The same `robot.glb` and `green_field.glb` import on machines with the C or US locale.

## Cause

Qt and GTK programs set the process locale from the environment at startup, and printf's `%g` follows `LC_NUMERIC`: under `nb_NO`, `de_DE` or `fr_FR`
`snprintf("%.9g", 0.5)` writes `0,5`. The importer writes the material source text with `FormatTo(... "%.9g" ...)` (`gltf_material.cpp`,
`material_source.cpp`'s `MsAppendFloat`) and reads it back with `ParseDoubleExact`, which accepts only `.`, so `materials[0]` is refused. The engine itself never
changes the locale; a host does.

## Tasks

- [x] A locale-independent writer: `FormatGeneral(buffer, capacity, value, precision)` in `cstr/number.h` (`std::to_chars`, `chars_format::general`), byte for
  byte `%.<precision>g` of the C locale, so already-minted blobs and stored summaries do not change.
- [x] Every number the engine formats and parses back, or stores, written with it (the audit below).
- [x] A reader that followed the locale: `RenderSettings::SetFromString` used `strtof`; now `ParseDoubleExact`.
- [x] `os::SetLocale` (`os/locale.h`), the wrapper the tests need; `setlocale` and `localeconv` join the banned C names of `std_usage_check`.
- [x] Tests under a decimal-comma locale (`test::CommaLocale`, `tests/support/comma_locale.h`: `nb_NO.utf8` first, then `de_DE` and `fr_FR` forms, POSIX and
  Windows; skipped when none is installed), each shown failing without the fix.
- [x] Docs: `docs/guide/foundation-cstr.md` (a rule and a common mistake), `docs/guide/platform-os.md` ("Locale"), the reference regenerated.

## The audit

Every `%g`, `%f`, `%e` (and `%a`) conversion in `engine/`, `tools/` and `game/`, and every `strtod`/`strtof`/`atof`/`ScanFrom` reader:

| Where | What it writes | Fixed? |
|---|---|---|
| `engine/asset_import/gltf_material.cpp` | the material source text, minted at once (the bug) | yes |
| `engine/asset_import/material_source.cpp` `MsAppendFloat` | `FormatMaterialBlob`'s text, minted back | yes |
| `engine/asset_import/gltf_summary.cpp` `AppendNumber` | the import `summary` JSON that the host stores in its cache | yes |
| `engine/asset_import/gltf_clip.cpp` | the cubic-spline resampling note in `ImportReport::notes` (returned to the host as a warning) | yes |
| `engine/renderer/render_settings.cpp` `ValueToString`, `SetFromString` | the render settings text format, both ways (`strtof` followed the locale too) | yes |
| `engine/foundation/settings/macos/user_defaults_store.cpp` | a real number from user defaults as text for the settings parser (`%.17g`) | yes (not built here: Mac) |
| `tools/asset_mint/level_source.cpp` | the level source text (`f32` `%.9g`, `f64` `%.17g`), minted back | yes |
| `engine/foundation/settings/desc.cpp` | the settings file's numbers | already `FormatShortest` / `ParseDoubleExact` |
| the import manifest, `engine_c/import.cpp` | ids, kinds, indices and names: no floating-point text | nothing to do |
| `engine/script/luajit/luajit_backend.cpp` (`fmt('%g')` in Lua) | Lua's `string.format`: LuaJIT 2.1 formats numbers itself (`lj_strfmt_num.c`), not with printf | nothing to do |
| logs and display text: `vk_memory.cpp`, `vulkan_device.cpp`, `debug_overlay.cpp`, `asset_mint/main.cpp` messages, `net_probe` | messages for people, never parsed | left (messages only, as the guide now says) |
| `game/ecs_bench/bench.cpp` CSV (`%.9g`, `%.17g`, read with `CStrToDouble`), `game/ui_demo` arguments, the benches' and demos' prints | programs of their own that never change the locale, so they always run in the C locale | left |

## Result

Done 2026-11-18 on the Windows PC. **Engine commit: `1636ecec`** (the fix, its tests and docs), with the reference index and this note in the commit after it. The importer's bytes do not change in the C locale (`FormatGeneral` is
`%.9g` byte for byte), so `kGltfImporterOutputVersion` and `import_format_version()` stay as they were: a cache entry made before the fix stays valid, and only
an entry the host made under a decimal-comma locale (which failed to import, so none exists) would have differed.

- **The fix:** the eight writers of the audit table, through `FormatGeneral`; `RenderSettings::SetFromString` reads with `ParseDoubleExact` and refuses a
  value beyond `f32`. From the review (below), both float parsers also refuse `+-1`.
- **Tests, each shown failing without the fix** (the import one with the host's own message, `materials[0]: a float value is a finite number`), passing with
  it under `nb_NO.utf8`, which this PC has: `tests/asset/gltf_locale_test.cpp` (the import's blobs, summary and notes are byte-identical under C and
  `nb_NO.utf8`; `FormatMaterialBlob` prints the same text); `level_test.cpp` (level text prints and mints back the same); `gltf_skin_test.cpp` (the
  resampling note); `render_settings_test.cpp` (the settings text both ways); `cstr_test.cpp` (`FormatGeneral` equals `snprintf("%.*g")` of the C locale for
  20 values at precisions 1, 6, 9 and 17, nine digits read back as the same `f32`, a short buffer).
- **Runs (ctest directly, without the phase 8 reset-loop test, the user's rule):** build-msvc plain: 3659 of 3661 passed in 268 s; build-msvc asan: 3122 of 3123 passed in 280 s; build-clang-cl plain: 3689 of 3691 passed in 337 s. The failures: the
  voice-cost bound (Debug, already in `KNOWN-ISSUES.md`), `FramePool does not commit slab memory until blocks are used [asan]` (filed flaky) and
  `test_phase_labels` (new, not this change: the Phase 27 tests build on Windows and `tests/phases.txt` lists no Windows flavor for phase 27;
  `KNOWN-ISSUES.md`). After the review's fix, phases 3, 5, 8, 11, 16 and 22 again: 1009 of 1009 in `build-msvc` and in `build-clang-cl`, the touched asan
  tests 142 of 142, `ctest -L docs` 7 of 7. All six Windows trees (MSVC and clang-cl, unity and not, shared) build clean.
- **Not run here:** Linux (GCC and Clang, tsan) and the Mac (`user_defaults_store.cpp` is macOS-only and was not compiled here; Apple's libc++ must have
  `to_chars` with a precision, as it has the shortest form `FormatShortest` uses). Recorded in `KNOWN-ISSUES.md`.

### Post-implementation code review

`/code-review high` over the diff, done inline: 3 findings. **Fixed (1):** skipping a leading `+` and then calling `ParseDoubleExact` accepted `+-1` as -1, in the new
`RenderSettings::SetFromString` and in the settings parser it was modeled on (`settings/desc.cpp`, from Phase 11); both refuse it now, with a case in each test
(`value_test.cpp`'s fails without the fix). **Declined (2):** `SetFromString` rounds the text to `f64` and then to `f32`, where `strtof` rounded once, so a typed
value with more than nine digits may differ in the last bit (the importer's material reader does the same, and text the engine writes reads back exactly); the
benches' CSV and the demos' arguments keep the locale-dependent calls (programs of their own that never change the locale).
