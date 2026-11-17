# Vendored libogg

Upstream: https://xiph.org/ogg/ (https://gitlab.xiph.org/xiph/ogg). Pinned release: **1.3.6**, tarball `libogg-1.3.6.tar.xz`
(sha256 `5c8253428e181840cd20d41f3ca16557a9cc04bad4a3d04cce84808677fa1061`), fetched for the Day 414 spike (2026-11-17) and checked again on Day 432.
License: `LICENSE` (the Xiph.Org BSD licence), unmodified (sha256 `d2ab5758336489da61c12cc5bb757da5339c4ae9001f9bb0562b4370249af814`).
The sources are the upstream files, unmodified: `src/bitwise.c`, `src/framing.c`, `src/crctable.h` and `include/ogg/ogg.h`, `include/ogg/os_types.h`.
The one file that is ours is `ogg/config_types.h`: upstream's `config_types.h.in` with the `<stdint.h>` branch selected (every platform we build has it).
Not vendored: the autotools and CMake files, the documentation and the tests.

Per `PLAN.md` assumption #30 (the user, 2026-11-17) libogg and libvorbis decode Ogg Vorbis for the audio module instead of a hand-written decoder. Source
vendor, so builds are offline and the same on Windows, Linux and macOS. Nothing but `engine/audio/vorbis.cpp` includes `ogg/ogg.h`.

Built by `CMakeLists.txt` as one static C library per flavor (`ogg`, `ogg_asan`, `ogg_tsan`), warnings off for the target (third-party C).
