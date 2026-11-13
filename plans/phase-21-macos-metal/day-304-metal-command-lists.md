# Phase 21 / Day 304 — Metal V: Command Lists, Encoders, Barriers, Mips

**Phase:** [Phase 21 — macOS Port + Metal Backend](../phase-21-macos-metal.md)
**Depends on:** Day 303.

Command recording on Metal: one `MTLCommandBuffer` per command list, encoders opened and split around render passes, copies and dispatches, the
render graph's barriers mapped to what Metal needs, mip generation, and recording from several jobs. Modelled on DX12 Day 168.

## Tasks

- [x] **Command lists** (`metal_command_list.mm`): `Begin` takes a command buffer from the queue (labelled), `End` commits it with a completion handler
      feeding the fence; recording from several jobs in parallel (one command buffer each, committed in submission order through `enqueue`).
- [x] **Encoders:** `BeginRenderPass` opens an `MTLRenderCommandEncoder` from an `MTLRenderPassDescriptor` (load and store actions, clear values,
      resolve); copies open a blit encoder, dispatches a compute encoder; the open encoder is ended when the kind changes; state that does not survive an
      encoder (bound slots, pipeline, viewport, scissor) is re-applied (the binding cache of Day 302 resets).
- [x] **Barriers:** with hazard tracking on (decision 9) a `Barrier` inside an encoder is a no-op, between encoders the order is the driver's; a
      read-after-write inside one compute encoder uses `memoryBarrierWithScope:`; the render graph's barrier tests check what was emitted per case.
- [x] **Draws and dispatches:** indexed and non-indexed draws with `baseInstance` (the object index stream, `first_instance = i`), triangle strips with
      primitive restart (always on for strips on Metal: check the RHI contract and document), `dispatchThreadgroups:` with the reflected size.
- [x] **Mips:** `GenerateMips` through a blit encoder's `generateMipmapsForTexture:` (no compute shader needed); filter matches the Vulkan path within the
      golden tolerance.
- [x] **Debug markers:** `pushDebugGroup:`/`popDebugGroup` from the RHI's markers.
- [x] **Tests:** (the Metal file; `compute_test` and the shared command tests join through the GPU fixture on Day 306) `compute_test`, the command tests through Metal, `metal_commands_test.mm` (encoder splits, re-applied state after a split, parallel
      recording order, strips with restart, mips).
- [x] **Documentation:** `rhi.md` Metal section (encoders, barriers, restart); `docgen lint` zero.
- [x] **Runs:** `ran 6110 of 6490 tests (phases: all; flavors: all; display: included; crash tests: excluded) in 1479 s` ([the Day 309 run](day-309-metal-lifecycle.md#the-run-of-days-315-325)). Originally: `scripts/test-phase.sh --affected --flavor all` (quote). **Deferred to Day 309 (the user's choice).**
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: state re-application after every split, submission order across parallel recording,
      command-buffer lifetime until completion.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** command lists record render passes, copies, dispatches and mips on Metal from several jobs with validation silent, the compute tests and the
command tests pass in three flavors, and the barrier mapping is tested per case.

## Implementation notes

- **`metal_command_list.mm`:**
  - **Recording:** each list records into a retained `MTLCommandBuffer` taken at `Begin`, with autorelease pools around every Metal call that
    makes objects (lists are recorded on long-lived job threads).
  - **Encoders:** a rendering pass is one render encoder. Copies and mips share a blit encoder and dispatches a compute encoder, opened lazily
    and ended when the kind changes.
  - **Bound state:** the pipeline, bind groups and push constants are applied at draw or dispatch time through the Day 302 binder, which a new
    encoder resets. A graphics pipeline ends with its pass; a compute pipeline bound before a pass stays.
  - **Fixed-function state:** viewport and scissor are set on the render encoder, the scissor clipped to the attachments.
  - **Barriers:** a memory barrier after a writing dispatch inside a compute encoder, else nothing; both counted for the tests.
  - **Draws and dispatches:** draws use `baseInstance` and `baseVertex`; strips restart at the all-ones index (always, on Metal); dispatches use
    the minted threadgroup size.
  - **Mips and labels:** `generateMipmapsForTexture:` for mips; debug labels are encoder groups, closed and reopened across splits, with an
    overflow count past 16.
  - **Failure:** a failure is logged and marks the list, the pass and label bookkeeping still pairs, and `Submit` refuses the batch.
- **`metal_device_commands.mm`:**
  - **Pools:** lists come from a pool per frame slot.
  - **Submit:** it checks each list (one listed twice is caught), commits the lists in order, and signals the timeline once after them.
  - **Queue:** it allows 512 outstanding command buffers, since open lists hold theirs.
- **Contract note:** `CopyBuffer` on Metal needs offsets and a size that are multiples of 4 (asserted; `docs/guide/rhi.md`).
- **Tests added** (run on Day 309): `tests/rhi/macos/metal_commands_test.mm` covers:
  - a cleared pass and a strip with a restart, by pixel;
  - dispatches split by a copy, with the state re-applied and the barrier counts;
  - two threads' lists in submission order;
  - mips and labels across splits;
  - a failed list refused, while still pairing its pass and labels;
  - a compute pipeline surviving a pass;
  - seventeen nested labels.
- **Built:** both unity modes on macOS, every flavor, no warning. **Runs: deferred to Day 309.**

## Post-implementation code review

`/code-review high` on the day's diff: 10 findings, 9 fixed, 1 left for the Day 309 run.

1. **After a recording failure, `BeginRendering` and the label calls skipped their bookkeeping**, so `EndRendering` or `End` aborted. Fixed: the
   pass and the label depth are kept even on a failed list; tested.
2. **A pass dropped a bound compute pipeline.** Fixed: only a graphics binding ends with a pass; tested.
3. **A list listed twice in one batch passed the check** and would be committed twice (an exception inside Metal). Fixed: each list is marked as
   it is checked.
4. **No autorelease pools on the recording paths.** Fixed.
5. **Labels past 16 unbalanced the pops.** Fixed: an overflow count consumed first; tested with seventeen.
6. **The queue's default 64 command buffers could block `Begin`.** Fixed: 512.
7. **UInt16 indices at an odd `first_index` give an index-buffer offset that is not a multiple of 4** (lower confidence). Not changed: the Day 309
   run with validation decides it (`KNOWN-ISSUES.md`).
8. **The current-encoder choice was copied four times.** Fixed: `CurrentEncoder()`.
9. **Two lookups per texture command.** Fixed: `TextureObject(handle, &format)`. The two copy functions still share some extent and pitch code.
10. **A dead ternary and repeated dirty-flag resets.** Fixed.
