# Vendored LuaJIT

Upstream: https://luajit.org/ (https://github.com/LuaJIT/LuaJIT). LuaJIT has no release tags: its `v2.1` branch is the release and the version string is
`2.1.<commit time>`. **Pinned commit `63c8f1a9b479ba5518cf6a170ff7729178bc3cfd`** (2026-11-11, version `2.1.1794386141`, `luajit_relver.txt`), taken from the GitHub
archive `https://github.com/LuaJIT/LuaJIT/archive/63c8f1a9b479ba5518cf6a170ff7729178bc3cfd.tar.gz` (1,104,417 bytes, **sha256
`8cad76f444b76001b7123dc626c5d0bfc24f1f790333737443345f7e6b41bb5f`**), chosen on Phase 27 Day 444 (the user, 2026-11-18). Licence: MIT, `LICENSE` is upstream's
`COPYRIGHT` (sha256 `6cda3076a8c09b31bb1f90da614ea89e7ee194589626eeac613e522849604e47`), unmodified.

Contents: upstream's `src/` and `dynasm/` unmodified, minus what the build here does not use: `src/Makefile`, `src/Makefile.dep`, the platform build scripts
(`*build.bat`), `src/jit/` (the Lua modules of `jit.dump` and friends, which scripts never see), `src/host/README`, the documentation (`doc/`, `etc/`). **No local
changes.** `luajit_relver.txt` is ours: the commit time upstream's Makefile reads from git, which the archive does not carry.

Built by `CMakeLists.txt` as the static library `luajit`, without `make`: the host tools `minilua`, DynASM and `buildvm` run at build time and write their output
(`lj_vm.S`, `lj_bcdef.h`, ..., `luajit.h`) to the build tree. The build uses 64-bit GC objects (`LJ_GC64`, LuaJIT's default on x86-64 and arm64), so
`lua_newstate` accepts the engine's allocator. `ENGINE_LUAJIT_JIT=OFF` builds with `LUAJIT_DISABLE_JIT`. Warnings are off for the target (third-party C). Only
`engine/script/luajit/` includes its headers (gate `luajit_boundary_check`). Linux x86-64, and macOS x86-64 and arm64 (Day 474; arm64 compile-only); Windows: Phase 27 Day 473.
