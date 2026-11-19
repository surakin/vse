# glTF coverage / Day 15: closing: the guides, the model viewer, the full runs

**Series:** [glTF coverage](../gltf-coverage.md)
**Depends on:** Days 1 to 14.

## Tasks

- [ ] **Guides:** `docs/guide/assets.md` ("Importing glTF": what is imported now, the units of lights and their settings, the morph limits,
  the colour encoding), `docs/guide/renderer.md` (the G-buffer's four targets, the material slots, spot lights, morph targets), a short
  walkthrough of importing a file with every feature and seeing it in the model viewer, with compiling samples where code is shown.
- [ ] **The gap list:** the glTF features still not imported (the "Not wanted" list of the series plus anything found) recorded in the
  guide, so the next ask starts from it.
- [ ] **One sample file** with every feature (built by a test or a tool of ours, not downloaded) used by a model-viewer screenshot ctest.
- [ ] **Full runs** on each machine (`scripts/test-phase.sh --all --flavor all` on Linux; the PC and Mac halves as their rules say) and
  `ctest -L docs`.
- [ ] **Review:** `/code-review high --max-findings all` over the series' diff (every day), each finding fixed or declined with its reason.
- [ ] **Standing rows.**

## Notes

(written during the day)

**Done when:** the guides describe what is imported and how, the sample file shows every feature on every backend, and the full runs and
the uncapped review are recorded.
