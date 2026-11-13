# Phase 22 — Asset Pipeline & Resource System

**Depends on:** Phase 17 (ECS), Phase 5 (Renderer core), Phase 19 (storage: the component serialization registry and entity keys that the level format reuses), Phase 20 (networking: the network backend is a user of the transport layer), Phase 9 (file layer). Placed before physics (22) and audio (23) so both load minted assets.
**Goal:** a resource system with pluggable storage backends, versioned
per-type binary formats, offline tooling to produce them, and a runtime
loader — the foundation Phase 29 (Unreal import) targets when converting
external content into engine-native form. See root `PLAN.md` assumption
#12 for the settled design decisions this phase implements.
**Definition of done:** an offline mint converts source assets (in our
own custom formats) into engine-ready binary blobs, loadable at runtime
through at least the pak-file and filesystem backends, with no
editor/tool code linked into the game binary. Minting is deterministic
(same inputs and tool version give byte-identical outputs, tested; see
`PLAN.md`, guiding principles), which Unreal's cook famously is not.

## Key deliverables (to be expanded into day-by-day tasks when started)

- **Resource backend abstraction.** One interface (open/read/list/watch-
  for-changes as relevant) with pluggable implementations:
  - **Filesystem backend** — loose files on disk, for fast dev iteration
    without a repack step.
  - **Pak backend** — resources packed into one or more archive files by
    the offline mint/packer tool; the shipping default.
  - **Network backend** — resources streamed from a remote source. Scope
    (auth, caching, retry/offline behavior) is deliberately left to be
    decided when this phase starts — it's the least-used path early on
    and shouldn't gate the other two.
  Upper layers request a resource by ID/path and never know which backend
  served it, same pattern already used for the RHI (assumption's Vulkan/
  DX12/Metal) and the Linux window backends (Wayland/X11).
- **Per-type versioned binary formats.** Each resource type (mesh,
  texture, material, level/scene, ...) owns its own binary format with an
  explicit version header. The runtime loader (or a per-type upgrade
  path) supports old versions rather than breaking previously-minted
  content when a format changes — this is the resolution to the
  versioning/migration question this phase previously flagged as open;
  the concrete migration mechanism (in-place upgrade on load vs. an
  offline re-mint step vs. both) is a day-level design decision when this
  phase starts.
  - Custom mesh format (vertex/index buffers, LODs, material slot
    references) — also the target format Phase 29's Unreal mesh importer
    writes into.
  - Custom texture format (wraps GPU-native compressed payloads —
    BCn/ASTC — per assumption #4).
  - Custom material format (shader reference + parameters).
  - Custom level/scene format (entity/component data, referencing the
    above).
- **Optional text format, per type, where it earns its keep.** Binary is
  the default and the only format that ships; a parallel text
  representation is opt-in for specific types where hand-editing or
  VCS-friendly diff/merge matters most (levels/scenes are the obvious
  first candidate — designers and merge conflicts both live there).
  Meshes/textures are unlikely to ever want one. The offline mint
  converts text → binary; the engine's runtime loader only ever reads
  binary.
- Offline mint tool(s) under `/tools` that produce these formats
  (and pack them for the pak backend).
- Runtime loader with (at least) synchronous loading; async/streaming is a
  later refinement.

## Notes / open questions

- Full day-by-day breakdown deferred until Phase 17 is complete.
- Network backend scope (auth, caching, offline/retry behavior) is
  intentionally undecided until this phase starts.
- Exact version-migration mechanism (loader-side upgrade vs. offline
  re-mint vs. both) is a day-level decision, not yet made.
- Which resource types get an optional text format, beyond the obvious
  levels/scenes case, is undecided — revisit per-type as each format is
  designed.

- **File layer (from Phase 9, planned 2026-11-06):** the storage backends of the resource system (pak, filesystem, network; `PLAN.md` assumption #12) are `io::File` implementations plus a container reader; Phase 9 builds the interface, the local file and a network-like test double, not the pak or network formats.