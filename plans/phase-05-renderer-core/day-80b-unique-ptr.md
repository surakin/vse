# Phase 5 / Day 80b — `UniquePtr` & no raw `new`/`delete`

**Phase:** [Phase 5 — Renderer Core](../phase-05-renderer-core.md)
**Depends on:** Day 80a. Inserted before Day 81.

Day 80a removed raw array allocation. The same hand-paired ownership remains for single objects: ~60 `new T` / `delete p` sites in `jobs`, `rhi`, `rhi_vulkan` and `renderer` (a `delete` on every error path and in every destructor). Policy (user, 2026-11-05): **ban every raw `new`/`delete`** in `engine/` and `game/`; the foundation allocators and placement `new (ptr) T` into storage a container owns are the only exceptions.

## Design (user answers 2026-11-05: deleter parameter yes; standalone Day 80b; ban all raw new/delete)

- `UniquePtr<T, Deleter = DefaultDelete<T>>` in `engine/foundation/containers/unique_ptr.h`: owns one object, move-only, `operator->`, `operator*`, `Get()`, `Release()`, `Reset(p)`, `explicit operator bool`, converting move from `UniquePtr<Derived>` to `UniquePtr<Base>` (needed for `Device`/`NullDevice`, command lists).
- `DefaultDelete<T>` destroys and frees through `HeapAllocator`; a custom deleter is any callable `void(T*)` (Vulkan handles, `munmap`, an arena that only runs the destructor). Empty deleters cost nothing (`[[no_unique_address]]`).
- `MakeUnique<T>(args...)` allocates from the heap and constructs; returns an empty pointer on allocation failure (the engine is exception-free).

## Sites to convert (from a scan of `engine/` and `game/`)

- **Owned members, straightforward:** `Scheduler::pool_`/`io_`; `VulkanDevice::memory_`, `memory_backend_`, `staging_ring_`; `vk_memory` blocks and free lists; command-list pools (`vulkan_device_commands.cpp`, `null_device.cpp`); `NullDevice`'s `State`; `VulkanDevice::swapchain_`; the `ShaderReflection` temporary in `material.cpp` (becomes a plain local or `UniquePtr`).
- **Decisions (user, 2026-11-05):**
  1. *Public API that hands out raw owning pointers:* **(a)** — `rhi::CreateDevice`, `rhi::CreateNullDevice` and `FramePool::Create` return `UniquePtr`; `VulkanSwapchain::Create` likewise. `rhi::DestroyDevice` is removed: the pointer type's deleter performs the idle wait and teardown (`DevicePtr = UniquePtr<Device, DeviceDeleter>`; the deleter parameter exists for this). Every caller (tests, demos, tools) changes.
  2. *`FallbackSlots` (`vulkan_device_commands.cpp`):* the leak is not kept. It exists because a thread's exit handler (the `thread_local` `FallbackSlotHolder` destructor) can run after static destructors, and a destroyed `std::mutex` must not be touched then. Replace it with constant-initialized, trivially destructible storage: an array of `std::atomic<u64>` bitmasks claimed by compare-and-swap (`kThreadSlots` bits), so there is no `new`, no mutex and no destruction order problem.
  3. *`jobs/async_io.cpp`'s `ReadState`:* ownership is single (awaitable, then the result object; the I/O thread only borrows the pointer until its last `Decrement`), so `UniquePtr` moved from the awaitable into the result is enough. The user wants a `SharedPtr<T>` with optional thread safety as well: **open question — add it this day (no site needs it yet) or when the first shared-ownership site appears?**
  4. *Command-list pools:* yes — pool storage becomes `Vector<UniquePtr<...>>`.

## Tasks

- [x] Settle decisions 1-4 above with the user and record the answers here (3: build `SharedPtr` this day, user 2026-11-05; a `UniquePtr` with a custom deleter always gets its own `using` alias, e.g. `DevicePtr`).
- [x] (If decided for this day) `SharedPtr<T>` with a thread-safety policy parameter (atomic or plain reference count), `MakeShared`, `WeakPtr` only if a site needs it; tests including a TSan stress run for the atomic policy.
- [x] `UniquePtr`, `DefaultDelete`, `MakeUnique` with `///` docs; foundation tests (destroys once, move leaves the source empty, `Release`/`Reset`, converting move to a base, custom deleter called with the pointer, empty deleter adds no size, `MakeUnique` failure on an absurd size, ASan leak-free).
- [x] Convert the owned-member sites listed above; each keeps its existing behavior and error paths.
- [x] Apply the answers to decisions 1-4.
- [x] Extend the gate: `raw_array_check` becomes `raw_new_check` (rename the tool, CMake and ctest names, fixtures) and also bans `new T` / `new T(...)` / `new (std::nothrow) T` and `delete p`; placement `new (ptr) T` and `operator new`/`operator delete` definitions stay allowed; `engine/foundation/mem/` stays exempt; any allowlisted exception carries a reason, as in `rhi_boundary_check`. Fixtures for each form and for the allowed placement `new`.
- [x] Update PLAN.md's guiding principle and `docs/guide/foundation-containers.md` (UniquePtr section with a compiled sample); `docgen lint` at zero, `docs/reference/` regenerated, `ctest -L docs` passes.
- [x] **End-of-day review (CLAUDE.md):** `/code-review high` (extra scrutiny: ownership and lifetime, destructor order in `VulkanDevice`, the async-read state), record under "Post-implementation code review", mention in `STATUS.md`.
- [x] Update `STATUS.md` and `KNOWN-ISSUES.md` (remove the Day 80a entries this day resolves); commit and push.

**Done:** `grep` finds no raw `new`/`delete` in `engine/` or `game/` outside the allowlist, `raw_new_check` enforces it with planted-violation fixtures, the full ctest (plain, ASan, TSan) passes, and the docs pages are named in the commit.

## Result

- `UniquePtr<T, Deleter = DefaultDelete<T>>` (move-only, converting move to a base, `MakeUnique`, `== nullptr`), `SharedPtr<T, Count = AtomicRefCount>` (+ `PlainRefCount`, `LocalSharedPtr`, `MakeShared`, one allocation for object and block, no weak pointers). Aliases for custom-deleter pointers: `rhi::DevicePtr`, `rhi::NullDevicePtr`, `rhi::vulkan::VulkanDevicePtr`.
- `rhi::CreateDevice` / `CreateNullDevice` return pointers; `rhi::DestroyDevice` is gone (the deleter waits for the GPU, then destroys). About 35 test/demo/tool files and 2 guide pages changed with it.
- Converted: `Scheduler::pool_`/`io_`, `VulkanDevice::memory_`/`memory_backend_`/`staging_ring_`/`swapchain_`, GPU memory block records, per-thread command pools and lists (`owned_pools_` owns, the atomic slots only publish), `NullDevice` state/swapchain/command lists, `async_io` request state (moved awaitable to result), the `ShaderReflection` temporary.
- The two process-lifetime singletons no longer leak: `FallbackSlots` became a constant-initialized `std::atomic<u64>` bitmask table (no mutex, no `new`, no destruction-order hazard); `FramePool::Global()` is constructed with placement `new` into static storage and is never destroyed (a thread's exit-time cache flush needs it alive).
- Gate: `raw_array_check` became `raw_new_check` (`ctest -L arrays`): bans `new T`, `new T[n]`, `new (std::nothrow) T`, `delete p`, `delete[] p`, with planted-violation and clean fixtures. `engine/` and `game/` are clean.
- `SharedPtr` threading: a standalone TSan program (8 threads x 20000 copy/release rounds) is clean with `AtomicRefCount` and reports data races with `PlainRefCount`; the foundation tests have no TSan flavor, so this was run by hand and is recorded in `KNOWN-ISSUES.md`.
- Full ctest (plain, ASan, TSan): 1754 pass. Docs 100%, guide section "UniquePtr and SharedPtr: single objects".

## Post-implementation code review

`/code-review high`: 10 findings.

- **Fixed:** `NullDevice`'s constructor did not check that its state allocated (`MakeUnique` returns empty on exhaustion); it now asserts, and the header says it aborts rather than returning empty.
- **Fixed:** `DefaultDelete<Base>` freed derived objects with `Base`'s alignment and ran only `~Base` unless it was virtual. The converting constructor now `static_assert`s a virtual destructor on `Base` and `alignof(Derived) <= 16`.
- **Fixed:** on the out-of-host-memory path for a block record, `objects_made_` stayed incremented (the statistic drifts by one after fault injection); it is rolled back with `live_objects_`.
- **Fixed:** the `VulkanDevice::Create` doc still said "or null" and "the caller owns the result".
- **Fixed:** the gate missed `new(std::nothrow) T`, `new ::T` and `delete(p)`, and its `operator new` exemption covered the whole line. The exemption now strips only the `operator new/delete` text; fixtures for the no-space forms were added. (A first attempt matched `new_capacity`; the final pattern requires a type start after `new`.)
- **Fixed:** `frame_pool.cpp` relied on a transitive `<new>`.
- **Fixed:** three test fixtures had a destructor that only called `device.Reset()`, destroying the device before later-declared members; removed so reverse declaration order does it.
- **Fixed:** `SharedPtr`'s public adopting constructor could pair any pointer with any block; it now needs a `detail::SharedAdopt` passkey that only `MakeShared` makes.
- **Declined:** `owned_pools_` (UniquePtr) and `thread_pools_` (atomic pointers) duplicate state. An atomic cannot hold a `UniquePtr`; the atomic table is a lock-free view of what `owned_pools_` owns, both are cleared in `ShutdownCommands`, and the comment on the members says so.
- **Declined:** deferring `SharedPtr` until it has a call site; the user asked for it to be built now.
