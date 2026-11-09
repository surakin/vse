# Phase 15 / Day 187 — Platform: the OS Clipboard (Wayland, X11)

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Days 185, 186.

Copy and paste with other programs: text first, then file lists and a private format for the engine's own data. The Win32 backend follows on Day 205 behind the same interface.

## Tasks

- [ ] **API on `os::Window`** (defaults report unsupported so Win32 and macOS compile): `Clipboard` operations `SetClipboardText(StringView)`, `RequestClipboardText()` (asynchronous: the answer arrives as a `ClipboardData` event on a later `PumpEvents`, because Wayland and X11 transfer through the other client), `ClipboardHasFormat(Format)`, formats `Text` (UTF-8), `FileList` (a list of `io::Path`), `Custom(mime/atom name)` with bytes; `SupportsClipboard()`; selection kinds `Clipboard` and `Primary` (the X11/Wayland middle-click selection; Win32 has none and reports unsupported).
- [ ] **Events:** `ClipboardData{request_id, format, bytes or failure}` and `ClipboardChanged` (the owner changed; used to enable/disable Paste) added to `InputEvent` (Day 185); a request that gets no answer in 2 s (the owner is gone or stuck) completes with a `Timeout` failure, so a paste never hangs the UI.
- [ ] **Wayland:** bind `wl_data_device_manager`; offering data = `wl_data_source` with the MIME types `text/plain;charset=utf-8`, `text/plain`, `UTF8_STRING`-equivalents, `text/uri-list` for files; serving `send` requests by writing to the passed file descriptor without blocking the pump (non-blocking writes continued across `PumpEvents`, a size limit and a timeout); receiving = `wl_data_offer` selection, `receive(mime, fd)` and non-blocking reads accumulated across pumps; selection serial from the last key or pointer event; the source is cancelled and freed on `cancelled`; primary selection through `zwp_primary_selection_device_manager_v1` when the compositor has it (generated bindings committed, `VENDORED.md` updated).
- [ ] **X11:** `CLIPBOARD` and `PRIMARY` selections: own with `XSetSelectionOwner`, answer `SelectionRequest` for `TARGETS`, `UTF8_STRING`, `STRING` (Latin-1 conversion where lossless), `text/uri-list`, `TIMESTAMP`; request with `XConvertSelection` and read the property on `SelectionNotify`; **INCR** transfers in both directions for data larger than the server's maximum request (receive: accumulate chunks; send: chunk on `PropertyNotify`), a 64 MiB cap; selection ownership lost (`SelectionClear`) reported as `ClipboardChanged`. No clipboard manager persistence is implemented (documented: contents vanish with the process, as for any client that does not hand them to a manager).
- [ ] **Safety:** data from other programs is untrusted: text is validated as UTF-8 (invalid sequences become U+FFFD, counted), the length is capped (default 16 MiB, a setting-free constant documented), file-list entries are parsed as `file://` URIs with percent-decoding and rejected if they are not absolute local paths; no network URIs are followed.
- [ ] **Fake-protocol tests:** the offer/receive state machines for both protocols with a scripted peer: round trip of ASCII, accents, a combining mark, a 4-byte emoji, an empty string, a 5 MiB text through INCR and through a pipe that stalls (continued across pumps), a peer that never answers (timeout), a peer that closes the pipe early, ownership lost during a transfer, two requests in flight, a request for a missing format, invalid UTF-8 from the peer, a hostile file list (`..`, relative, `http:`), oversize data.
- [ ] **Real-server tests** (skip with a printed reason without a display): X11 between two connections of the same test process (set on one, request on the other), Wayland through the test compositor seam the existing Wayland tests use; the primary selection where available.
- [ ] **Probe program:** `game/input_probe` (Day 186) gets keys to copy a string, paste and print what arrived, and show the formats on offer; tried by eye against another program (a terminal, a text editor) on both backends in both directions, recorded in this file.
- [ ] **BACKEND checklist:** `engine/platform/BACKEND.md` lists the clipboard operations, events and format rules; Win32 (`OpenClipboard`, `CF_UNICODETEXT`, `CF_HDROP`, `WM_CLIPBOARDUPDATE`) and macOS (`NSPasteboard`) sections say "not implemented yet" and name Day 205 / Phase 22.
- [ ] **Tests are written with the code:** every public behavior above has a test; GPU tests carry the `gpu` tag, use `FOR_EACH_GPU_BACKEND` and skip a backend that has no shaders (Vulkan only until Day 202 mints DXIL); every new test file has its line in `tests/phases.txt` (phase 15).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux; then the asan and tsan flavors when the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the phase closes (Day 202); a day done on one machine files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/ui.md` gains the day's section (tagged samples compile and run); new modules are in `docs/reference/modules.txt`.
- [ ] **Module rules:** `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` updated in the same commit when a module, an edge or a prerequisite changed; `module_deps_check`, `platform_neutral_check`, `std_container_check`, `file_access_check` and `config_access_check` pass.
- [ ] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a program can put text, file lists and custom data on the clipboard and read what other programs put there, asynchronously and with timeouts, on Wayland and X11, tested against scripted peers and by eye against real programs.

## Notes

(filled in as the day goes)

## Post-implementation code review

(filled in at the end of the day)
