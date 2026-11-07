# Vendored sentry-native

Upstream: https://github.com/getsentry/sentry-native
Pinned release: **0.17.6** (2026-11-04), taken from the release asset `sentry-native.zip` (submodules included, so the
build needs no network), SHA-256 `40b4b70cbada5bfae5b73f8267831dd023ec676d9676fb0d890878f31d2b5055`.
Licenses: `LICENSE` (MIT, sentry-native), `external/breakpad/LICENSE` (BSD-3-Clause, Google Breakpad),
`external/third_party/lss/LICENSE` (BSD-3-Clause, Linux Syscall Support), `vendor/libunwind/COPYING` (MIT, libunwind).
All unmodified.

Per `PLAN.md` assumption #20 this is a source vendor (not a submodule, not `FetchContent`). It is used **only** for crash
capture and minidumps, through the facade `engine/foundation/debug/crash_handler.h`; the one file that includes `sentry.h`
is `engine/foundation/debug/crash_handler_sentry.cpp` (enforced by `tools/platform_neutral_check`). It is configured with
the **Breakpad** backend and **no transport** (`SENTRY_TRANSPORT=none`): no network code is compiled in and our options
have no URL or DSN, so nothing can be uploaded (a ctest checks that no built test binary links a network library). At run
time the facade gives the library a transport that only copies each envelope into `<dump directory>/saved/`: with the null
transport the library deletes the envelope of a crash on the next start, losing the dump (found in the Day 95 review). The
Crashpad backend was rejected on Phase 6 Day 91: on Linux it needs libcurl at build time and its handler executable needs
libcurl and about 25 shared libraries (`docs/superpowers/specs/2026-11-06-debugging-design.md`).

## Kept from the release asset

- `CMakeLists.txt`, `cmake/`, `sentry-config.cmake.in`, `sentry.rc.in`, `LICENSE`, `README.md`, `CHANGELOG.md`
- `include/` (the public C header `sentry.h`) and `src/` (all of it, including the backends of other platforms)
- `vendor/`: the single-file helpers (`mpack`, `jsmn`, `stb_sprintf`) and `vendor/libunwind/` without its tests, docs and
  autotools files
- `external/CMakeLists.txt`, `external/breakpad/LICENSE`, and of Breakpad only the source files that sentry's
  `external/CMakeLists.txt` names (for every platform) plus the headers the Linux build includes, and
  `external/third_party/lss/`

Dropped: `tests/`, `examples/`, `fuzzing-examples/`, `ndk/`, `toolchains/`, `external/crashpad/` (16 MB), `external/benchmark/`,
`external/libunwindstack-ndk/`, `vendor/acutest.h`, and the rest of Breakpad (its processor, tools, docs and tests).
**Phase 13 Day 154 (Windows):** the two private headers the Windows build includes were copied from the same release asset (its
SHA-256 checked against the one above), unmodified: `external/breakpad/src/client/windows/common/ipc_protocol.h` and
`external/breakpad/src/common/windows/string_utils-inl.h` (found by building the `sentry` target until no include was missing).
**Phase 14 (macOS):** the macOS private headers are still to re-vendor the same way.

## Added by this repository

Nothing in this directory. The configuration is set by the root `CMakeLists.txt` before `add_subdirectory`:

```
SENTRY_BACKEND=breakpad  SENTRY_TRANSPORT=none  SENTRY_BUILD_SHARED_LIBS=OFF  SENTRY_BUILD_TESTS=OFF
SENTRY_BUILD_EXAMPLES=OFF  SENTRY_BUILD_BENCHMARKS=OFF  SENTRY_ENABLE_INSTALL=OFF
```

It is added before the project turns on `CMAKE_UNITY_BUILD`, so its files are not merged into unity batches. One
uninstrumented build is linked into the plain, ASan and TSan flavors.

## Updating the pin

Download the new release's `sentry-native.zip` into an empty directory, repeat the pruning above (keep the same
directories; for Breakpad take the files named in the new `external/CMakeLists.txt` plus the headers a build includes: build
once with `ninja -t deps` to list them), update the version and checksum here, and run the Phase 6 tests.
