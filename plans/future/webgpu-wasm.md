# Future project: a WebAssembly and WebGPU port

Not scheduled and not a numbered phase (whole-number phases only; when the user schedules it, it takes the next number and `PLAN.md` gets its roadmap row).
Nothing here is started, and no code is written until the plan below is expanded into day files (`CLAUDE.md`, planning policy). Evaluated on 2026-11-22
from the RHI headers, `BACKEND-LIFECYCLE.md` and a few probes on the Linux machine; **nothing was built for wasm**.

## Goal

The engine runs in a browser: the same sources compile to WebAssembly, draw through a WebGPU backend behind the existing RHI, and are driven by a JavaScript host.
The recommended scope is **the engine as a loadable module** (`engine_c`, Phase 23), with the host calling `frame()` from `requestAnimationFrame`, not a full
application with its own window layer.

## Verdict

Feasible, on the scale of the macOS/Metal port (about 30 days: foundation, platform pieces, RHI backend, demos), and the RHI backend is the smaller part. The risk is in
four places: the blocking RHI calls, threads, scripting and the missing platform services (below). Two spikes settle most of it before a phase is planned.

## What fits

- Shaders: `slangc` v2026.18.2 accepts `-target wgsl` (a trivial fragment compiled; real shaders and their reflection are untested).
- The RHI shape: bind groups, one serialized queue, handle epochs, device recreate and a lost-device path all map to WebGPU's single queue and `device.lost`.
- The C API is host-driven (`frame()` called by the host, tickets for readback), which is what a browser needs.
- `clang++` on the Linux machine lists the `wasm32` and `wasm64` targets. Emscripten is not installed.
- Wasm floating point is IEEE with no FMA contraction, which suits the bit-exact physics rule (libm would be Emscripten's, and the physics step uses only `+ - * / Sqrt`).

## Risks, most serious first

1. **Blocking RHI calls against asynchronous WebGPU.** `ReadbackBuffer`, `ReadbackTexture`, `WaitFence(timeout)` and the persistent `MapBuffer` block; WebGPU's
   `mapAsync`, `onSubmittedWorkDone` and device creation are promises. Options: JSPI (or Asyncify: slow, larger code) so the calls can block, or ticket-based paths
   (as `surface_read_pixels` already is). Either changes the RHI contract, not only adds a backend. Spike B measures it.
2. **Threads.** The engine rests on one work-stealing job scheduler. Wasm threads need `SharedArrayBuffer`: the page must be cross-origin isolated (COOP/COEP) and the
   main thread must not block. wasm32 has a 4 GB limit, wasm64 is not in every browser, and the engine uses 64-bit sizes. Spike A measures it.
3. **Scripting.** LuaJIT does not target wasm. The language-backend seam allows a plain Lua interpreter backend, but `engine/script/luajit/` and the gate
   `luajit_boundary_check` assume LuaJIT, and the determinism tests compare thread counts on it.
4. **Platform services with no browser equivalent:** windowing (a canvas, input and clipboard through JavaScript), `io::File` (IndexedDB or memory), `engine/net`
   (raw TCP and UDP do not exist: WebSocket or WebRTC), audio (an AudioWorklet), debugging (libbacktrace and sentry-native do not apply).
5. **WebGPU limits against the RHI:** no push constants (`SetPushConstants` goes through the uniform ring), no binary pipeline cache, 4 bind groups, limits on storage
   buffers per stage and on color attachments, no 64-bit atomics.

## Decisions for the user, before any planning

- Emscripten (a third-party toolchain) and `webgpu.h` (Dawn's `emdawnwebgpu` or the browser's own): proposed as the OS and driver boundary, like Cocoa and Win32. The plan's
  "zero third-party convenience dependencies" line and the `std_usage_check` and `file_access_check` gates would each need an explicit entry.
- Scope: the engine as a module driven by a JavaScript host (recommended), or a full application.
- Scripting: accept a plain Lua backend on wasm, or leave scripts out of the first wasm build.

## Spikes (planning inputs, not phase days)

- **Spike A (1 to 2 days):** build `foundation`, `ecs` and `jobs` with Emscripten pthreads; run the job-scheduler tests under Node and in a browser. Measures: the
  thread model, the 4 GB limit, whether the wrapper modules (`lang`, `sync`, `cstr`, `os`) port cleanly, and `build_defs.h` for a new platform and processor.
- **Spike B (2 to 3 days):** a stub WebGPU backend behind the RHI draws a triangle and reads it back, with JSPI. Measures: the cost of each blocking call, the WGSL output
  of real shaders from `slangc`, and what the RHI contract has to change.

## Outline of the phase (to be expanded into day files, not before)

1. Platform and processor in `build_defs.h`; the toolchain files and a `BUILD.md` section; the wasm preset.
2. Foundation: `os`, `sync`, `io` (memory and IndexedDB), `log` devices, time, settings store.
3. Jobs on wasm threads (or a documented single-thread fallback); the test runner under Node.
4. The WebGPU backend: device and adapter, resources, bind groups and the uniform ring for push constants, pipelines, command lists, swapchain on a canvas, readback and
   fence paths settled by Spike B, device loss, the lifecycle checklist and the RHI test suite.
5. Shaders: WGSL outputs minted by `shader_mint` (a fourth output beside SPIR-V, DXIL and Metal) and their gates.
6. The engine as a module: `engine_c` built for wasm, the JavaScript host glue, input, a canvas surface, audio worklet, network transport.
7. Scripting: the interpreter backend, or a documented exclusion.
8. Demos, the closing full-suite run per flavor that exists on the platform, the tutorial and the user-facing guide (the closing day, not before).

## Not wanted

A second graphics abstraction beside the RHI; WebGL as a fallback; any change of the native backends for the sake of the web.
