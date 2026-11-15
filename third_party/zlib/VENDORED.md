# Vendored zlib

Upstream: https://zlib.net (https://github.com/madler/zlib). Pinned release: **1.3.1**, tarball `zlib-1.3.1.tar.gz`
(sha256 `9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23`), fetched 2026-11-15.
License: `LICENSE` (the zlib license), unmodified (sha256 `845efc77857d485d91fb3e0b884aaa929368c717ae8186b66fe1ed2495753243`).
The sources are the upstream files, unmodified: `adler32.c compress.c crc32.c deflate.c inffast.c inflate.c inftrees.c trees.c uncompr.c zutil.c`
and their headers. Not vendored: the `gz*` files (file access: `file_access_check`), the examples, tests and every platform directory.

Per `PLAN.md` assumptions #17 and #19 (amended 2026-11-14) zlib exists for libpng, which decodes PNG for `engine/asset_import` (the glTF importer, Phase 22
Day 338; its input may be hostile) and then for FreeType's color bitmap strikes (Phase 27 Day 369). Source vendor, not a system library, so builds are
offline and the same on Windows, Linux and macOS. Nothing but libpng includes `zlib.h`.

Built by `CMakeLists.txt` as one static C library per flavor (`zlib`, `zlib_asan`, `zlib_tsan`), warnings off for the target (third-party C), with `Z_SOLO`
(no `gz*` files, no default allocator: libpng gives zlib its own allocation functions). `compress.c` and `uncompr.c` are in the directory but not built.
