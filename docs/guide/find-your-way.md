# I want to...

Status: written. A way into the documentation by what you want to do. Each line names the page to read and, when there is one, the tutorial that builds a working program.

## Get started

| I want to | Read |
|---|---|
| build the engine and run something | [Getting started](getting-started.md) |
| understand the words used here | [Glossary](glossary.md) |
| know why there is no `std::vector` | [Standard types](foundation-types.md) |
| run the tests that matter for my change | [Building and testing](building-and-testing.md) |
| build on Windows or macOS | [Windows](windows.md), [macOS](macos.md) |

## Put something on the screen

| I want to | Read |
|---|---|
| open a window | [Open a window](../tutorials/hello-window.md), [Windowing and input](platform-windowing-and-input.md) |
| react to the keyboard and mouse | [Handle keyboard and mouse input](../tutorials/handling-input.md) |
| clear the screen with the graphics layer | [Clear a window with the RHI](../tutorials/hello-rhi.md), [RHI](rhi.md) |
| draw a lit 3D scene | [From a scene to a lit frame](../tutorials/hello-renderer.md), [A 3D scene in your window](../tutorials/a-3d-scene-in-your-window.md), [Renderer](renderer.md) |
| write a shader | [Shaders](rhi-shaders.md) |
| draw text or build a user interface | [Text](text.md), [UI](ui.md) |
| let the user change quality settings while running | [Render settings](render-settings.md) |

## Make a world that moves

| I want to | Read |
|---|---|
| store game objects | [ECS](ecs.md) |
| ask what is near something, or cast a ray | [Ask what is near something](../tutorials/spatial-queries.md), [Spatial](spatial.md) |
| have bodies fall, collide and stack | [Physics in your window](../tutorials/physics-in-your-window.md), [Physics](physics.md) |
| play sound, including positional sound | [Sound in your window](../tutorials/audio-in-your-window.md), [Audio](audio.md) |
| play skeletal animation | [Animation](animation.md) |
| load models, textures and levels | [Assets](assets.md) |

## Work with data and the outside world

| I want to | Read |
|---|---|
| read or write files | [I/O](io.md), [Read a file asynchronously](../tutorials/async-file-io.md) |
| save and load a world | [Save a world without stalling the frame](../tutorials/saving-a-world.md), [Storage](storage.md) |
| use a database | [Database](database.md) |
| talk over a network | [Talk over the network](../tutorials/talking-over-the-network.md), [Net](net.md) |
| make something configurable | [Settings](settings.md) |

## Run work in parallel

| I want to | Read |
|---|---|
| run tasks on all cores | [Run work in parallel with jobs](../tutorials/parallel-jobs.md), [Jobs](jobs.md) |
| use threads, atomics or locks directly | [Sync](foundation-sync.md) |

## Use the engine from another program

| I want to | Read |
|---|---|
| load the engine as a shared library and call its C API | [Load the engine and clear a window](../tutorials/loading-the-engine.md), [Engine as a library](engine-library.md) |

## Find out why something broke

| I want to | Read |
|---|---|
| read a crash dump or a stack trace | [Debugging](foundation-debugging.md) |
| understand an assertion or a log line | [Logging and assertions](foundation-logging-and-assert.md) |
| see what is a known problem | `KNOWN-ISSUES.md` in the repository root |
| see why a check rejected my code | [Building and testing](building-and-testing.md), the section on gates |

## Change the engine

| I want to | Read |
|---|---|
| add a module or a dependency | `ARCHITECTURE.md`, then [Building and testing](building-and-testing.md) |
| port to a new platform | [Porting debugging](porting-debugging.md), `engine/foundation/io/BACKEND.md` and the other `BACKEND.md` files |
| follow the rules of the house | `CLAUDE.md` |
