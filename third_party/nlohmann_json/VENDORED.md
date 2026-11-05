# Vendored nlohmann/json

Upstream: https://github.com/nlohmann/json
Pinned release: **v3.12.0** (tag `v3.12.0`)
License: `LICENSE.MIT` (MIT), unmodified.
`nlohmann/json.hpp`: the upstream `single_include/nlohmann/json.hpp`, unmodified
(sha256 `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`).

Per `PLAN.md` assumption #19 this is used by offline tools only (`tools/shader_cook`,
to read `slangc -reflection-json`), never linked into the engine, a game, the renderer
or a test target. Source vendor, not a submodule or `FetchContent`, so builds stay
offline and reproducible (same pattern as Catch2, assumption #16).

To update the pin: replace the two files from the new release tag and update the
version and checksum above.
