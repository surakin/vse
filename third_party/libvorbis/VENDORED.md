# Vendored libvorbis

Upstream: https://xiph.org/vorbis/ (https://gitlab.xiph.org/xiph/vorbis). Pinned release: **1.3.7**, tarball `libvorbis-1.3.7.tar.xz`
(sha256 `b33cc4934322bcbf6efcbacf49e3ca01aadbea4114ec9589d1b1e9d20f72954b`), fetched for the Day 414 spike (2026-11-17) and checked again on Day 432.
License: `LICENSE` (the Xiph.Org BSD licence), unmodified (sha256 `ec1815db59fcd302846df949d7424876cb2e2dc5ed1606c5fb0b36787b1cf43a`).
The sources are the upstream files, unmodified: `lib/{mdct,smallft,block,envelope,window,lsp,lpc,analysis,synthesis,psy,info,floor1,floor0,res0,mapping0,
registry,codebook,sharedbook,lookup,bitrate}.c`, their private headers and `include/vorbis/codec.h`. Not vendored: `vorbisenc.c` and its `books/` and `modes/`
tables (the encoder), `vorbisfile.c` (file access: `file_access_check`; the facade drives the sync and decode state itself), the tools, examples and docs.

Per `PLAN.md` assumption #30 (the user, 2026-11-17) libogg and libvorbis decode Ogg Vorbis for the audio module. Source vendor, not a system library, so
builds are offline and the same on Windows, Linux and macOS. Nothing but `engine/audio/vorbis.cpp` includes `vorbis/codec.h`.

Built by `CMakeLists.txt` as one static C library per flavor (`vorbis`, `vorbis_asan`, `vorbis_tsan`) over `ogg`, warnings off for the target. The sanitized
flavors keep AddressSanitizer and leave UBSan out: the Day 414 spike saw UBSan reports on valid streams in `sharedbook.c` (left shifts of signed values,
benign on two's-complement machines; `KNOWN-ISSUES.md`). Decoded output may differ in the last bits across machines (libm and the compiler); the audio
module compares across machines within a tolerance.
