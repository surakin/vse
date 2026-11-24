# Glossary

Status: written. Terms are in alphabetical order; each points to the page that explains it.

**Allocator.** An object that hands out memory; the engine's containers take one as an argument instead of using a global heap. See [Memory](foundation-memory.md).

**Archetype.** The set of component types an entity has. Entities with the same archetype are stored together in chunks. See [ECS](ecs.md).

**Asan, tsan.** The AddressSanitizer and ThreadSanitizer *flavors* of the build: the same code compiled to catch memory errors and data races. See [Building and testing](building-and-testing.md).

**Asset.** Game data (a mesh, a texture, a material, a clip) with a stable identity, stored as a *blob* and loaded by the *resource manager*. See [Assets](assets.md).

**Backend.** The part of an interface that talks to one platform or technology: a graphics backend (Vulkan, Direct3D 12, Metal), a window backend (Wayland, X11, Win32, Cocoa), an audio backend. Code above the interface never names one. See [RHI](rhi.md).

**Behavior.** A named table of lifecycle callbacks (`on_create`, `on_update`, ...) a script registers; an entity runs one through the script component. See [Scripting](scripting.md).

**Blob.** The bytes of one minted asset in its on-disk layout, read in place without conversion. See [Assets](assets.md).

**BVH (bounding-volume hierarchy).** A tree of boxes used to find what is near something quickly. See [Spatial](spatial.md).

**C API.** The plain-C interface of the engine as a shared library: one exported function and a table of functions, with versioned structs. See [Engine as a library](engine-library.md).

**Chunk.** A block of memory that holds the components of several entities of one archetype, one column per component type. See [ECS](ecs.md).

**Chunk (instances).** A block of up to 256 nearby instances of an instance set with one box around them: a cull accepts or rejects it with one test. See [Instancing](instancing.md).

**Command buffer.** Where a script's writes are recorded during a tick instead of being applied; the sync point applies the buffers in a fixed order. See [Scripting](scripting.md), "The world view and the merge".

**Component.** A plain data struct attached to an entity. A component with no fields is a *tag*. See [ECS](ecs.md).

**Cube map.** A texture of six square faces seen from the middle of a box, looked up by a direction; the sky and its reflections use one. See [Renderer](renderer.md), "The sky".

**Day, phase.** The unit of the plan. A *phase* is a body of work with a goal (physics, audio, ...); a *day* is one file of specific tasks inside it. Phases are whole numbers. See `PLAN.md`.

**Determinism.** Producing the same result from the same input. The physics and audio modules state exactly what is repeatable (bit for bit, or within a tolerance) and check it with a *state hash* or *block hash*. See [Physics](physics.md) and [Audio](audio.md).

**Device (graphics).** The object that owns the GPU connection. It can be reset at run time; every owner of GPU objects keeps its source data so it can rebuild them. See [Renderer](renderer.md), "Surviving a reset".

**ECS (entity-component system).** The way game objects are stored: entities are ids, components are data, systems are functions over entities that have certain components. See [ECS](ecs.md).

**Engine library.** The engine built as one shared library with hidden symbols and a C API, loaded by a host program. See [Engine as a library](engine-library.md).

**Entity.** An id (an index and a generation) that names one game object. A stale id is detected, not reused silently. See [ECS](ecs.md).

**Fixed step.** Simulating in equal time slices (60 per second by default) however long the frame took, so results do not depend on the frame rate. See [Physics](physics.md).

**Flavor.** One of the three builds of every library and test (plain, asan, tsan). See [Building and testing](building-and-testing.md).

**Gate.** A check that fails the build or a test when a rule of the house is broken (`std_usage_check`, `module_deps_check`, ...). Its allow list names the documented exceptions, and a stale entry fails. See [Building and testing](building-and-testing.md).

**Geomorph.** Moving a terrain tile's vertices toward the coarser tile's heights as the tile nears its switch distance, so a change of detail does not pop. See [Terrain](terrain.md).

**Height map.** A grid of 16-bit height samples over a range in metres; the terrain's drawn surface and its collider are both read from one. See [Terrain](terrain.md).

**Host.** A program that loads the engine as a library and uses it through the C API. See [Engine as a library](engine-library.md).

**Instance run.** A stretch of consecutive visible instances (or nodes of one mesh, slot and level) that a cull returns, drawn as one instanced draw. See [Instancing](instancing.md).

**Job.** A piece of work run by the job scheduler; jobs are C++20 coroutines, not threads. See [Jobs](jobs.md).

**Latent wait.** A script call (`wait`, `wait_frames`, `wait_event`, `wait_task`) that parks the callback as a coroutine and resumes it in a later tick without blocking a thread. See [Scripting](scripting.md).

**Layer (collision).** A number from 0 to 31 on a collider; the layer matrix says which layers touch. See [Physics](physics.md).

**Layer (module).** The level of a module in `ARCHITECTURE.md`. A module uses only modules below it. See `ARCHITECTURE.md`.

**Level of detail (LOD).** A coarser mesh or tile used beyond a distance, and for an instance node a stand-in or a cull distance as well. See [Instancing](instancing.md) and [Terrain](terrain.md).

**Mint.** To process source data (a glTF file, a shader, a font) offline into the engine's own format. The tools are *minters*, the outputs are *minted*, and the same inputs give byte-identical outputs. We do not say "cook". See [Assets](assets.md).

**Module.** One directory under `engine/` with its own target and namespace. See `ARCHITECTURE.md`.

**Pair tracker.** The spatial module's feed of which pairs of items began or stopped overlapping each frame; physics builds on it. See [Spatial](spatial.md).

**Partition.** One of `P` groups of entities that share one script state (`script.partitions`, default 64); partitions run at the same time, the entities of one in id order. See [Scripting](scripting.md).

**Phase labels.** The tag on every test (`tests/phases.txt`) that says which phase it belongs to, so a run can select tests by phase. See [Building and testing](building-and-testing.md).

**Render graph.** The list of passes of one frame, each declaring the images it reads and writes; the renderer orders them and drops the unused. See [Renderer](renderer.md).

**Resource manager.** The object that loads assets by id, with their dependencies, whole or not at all, and shares them. See [Assets](assets.md).

**RHI (render hardware interface).** The graphics abstraction every renderer call goes through; nothing above it calls Vulkan, Direct3D or Metal directly. See [RHI](rhi.md).

**Setting.** Anything a user can configure, declared once with a name, a flag and an environment variable. Do not read the environment or parse flags yourself. See [Settings](settings.md).

**Shared pointer alias.** For a shared class the engine spells its pointer `TypePtr` (`GpuMeshPtr`), never `SharedPtr<Type>`. See `CLAUDE.md`.

**Side phase.** A phase run beside the main line on another machine (the macOS port was one). See `CLAUDE.md`.

**Slang.** The shading language of the engine; shaders are compiled offline to SPIR-V and the other formats. See [Shaders](rhi-shaders.md).

**Spatial set.** The collection of items with bounds that answers "what is near this?" See [Spatial](spatial.md).

**Splat map.** A texture that says which terrain layer shows where (a weight for each of up to four layers). See [Terrain](terrain.md).

**State hash.** A hash of every body's state in the physics world, used to prove two runs (or two machines) agree. See [Physics](physics.md).

**Subtree.** A node of an instance hierarchy and all its descendants, which sit in one contiguous range of the packed array, so a cull can skip or accept them with one jump. See [Instancing](instancing.md).

**Surface.** In the C API, a render target that a host binds to its own window or texture. See [Engine as a library](engine-library.md).

**Sync point.** A place where structural changes are applied, between the parallel parts of a frame. See [ECS](ecs.md).

**Sync point.** The moment between script phases when the command buffers are merged, events are moved and finished tasks resume their waiters, all in an order that does not depend on the thread count. See [Why your script runs on many threads](scripting-and-threads.md).

**System.** A function that the ECS schedule runs over entities, declaring which components it reads and writes so the scheduler can overlap it with others. See [ECS](ecs.md).

**Task (script).** A pure function run over indices on every core (`task.run`, `task.map`, `task.parallel_for`); it sees no world, only its arguments. See [Scripting](scripting.md), "Script tasks".

**Tile.** One square piece of the terrain's quadtree, drawn with a fixed 32 by 32 grid whatever its level. See [Terrain](terrain.md).

**Unity build.** Merging several source files into one translation unit to compile faster. Both unity and non-unity builds must pass. See [Building and testing](building-and-testing.md).

**Wrapped types.** The engine's own replacements for the standard library and the C library (`Vector`, `String`, `Move`, `MemCopy`, `out::Print`, ...). Code does not include a standard header or write `std::`. See [Standard types](foundation-types.md).

