# Phase 31 — Android and iOS Ports

**Depends on:** Phase 4 (Vulkan RHI) for Android — reuses the Vulkan
backend directly; Phase 21 (the Metal backend) for iOS. Neither needs the
DX12 work from Phase 13.
**Goal:** the engine running on Android and on iOS.
**Definition of done:** the Vulkan RHI backend runs unmodified on Android,
and the Metal RHI backend from Phase 21 runs on iOS; new platform layers
handle each OS's app lifecycle and touch input.

## Key deliverables (to be expanded into day-by-day tasks when started)

- Android NDK platform layer: native activity lifecycle, `ANativeWindow`
  surface for Vulkan, touch input events.
- APK packaging via the NDK's native-activity glue, which assumes Bionic
  (Android's libc) is present — no longer a concern to route around, since
  linking the OS/distro's own libc is in scope (see root `PLAN.md`
  assumption #2). Foundation-layer code still avoids relying on it for its
  own allocators/containers/etc., same as on Linux desktop.

- iOS platform layer: UIKit window, touch input, app lifecycle (moved here
  from the old macOS/iOS phase on 2026-11-05).
- iOS build config for Phase 27's LuaJIT backend: JIT mode is disallowed
  on iOS App Store builds, so this is where the interpreter-only fallback
  (root `PLAN.md` assumption #13) actually gets wired up.
- Audio output backends for the Phase 26 mixer: AAudio on Android and
  CoreAudio on iOS (shared with the macOS backend where the API matches),
  plus audio session handling (interruptions, route changes, pausing in the
  background). The mixer itself is unchanged.
- Apple Silicon / tile-based-GPU behavior in the Metal backend
  (memoryless attachments, shared storage) that the Intel Mac used for
  Phase 21 could not verify; `AddressMode::ClampToBorder` is unavailable
  on iOS GPU families (portability review).

## Notes / open questions

- Full day-by-day breakdown deferred until reached.

- **Debugging facilities backend (from Phase 7; designed in, no Phase 7 work):** sentry-native's `inproc` backend is the Android default and writes no minidump; Breakpad on Android and iOS is untested upstream, and iOS is not in the supported list: decide here. Death tests skip where a process cannot be spawned; post-mortem dumps report `Unsupported` where there is none. See `docs/guide/porting-debugging.md`.

- **Settings stores (from Phase 11 Day 139):** `engine/foundation/settings/BACKEND.md` lists the Android store (`SharedPreferences`; `apply()` versus `commit()` maps to staged versus per-key writes) and the iOS store (`NSUserDefaults`); both run the conformance suite in `tests/settings/`. Per-application sandbox paths replace the desktop standard locations; command line and environment layers are absent on these targets and the layer stack simply has no such layer.
