# Tutorial: Your First Script

**Goal:** Write a behavior in Lua, attach it to entities from a host program, run frames, call a script function from the host and answer a script's call back into the host.

**You need:** a built tree on Linux x86-64 (LuaJIT is built there; `ENGINE_CAP_SCRIPT` says whether the engine you loaded has scripts). Background: [Why your script runs on many threads](../guide/scripting-and-threads.md). The program of this page is `tests/engine_library/host_script.c`: `cmake --build build/linux --target host_script` builds it and `./build/linux/tests/engine_library/host_script` runs it (no display needed); its script is the string `kScript` near the top. The reference: [Scripting](../guide/scripting.md) and [The Engine as a Loadable Library](../guide/engine-library.md).

## Steps

1. **Write a behavior.** A behavior is a table of callbacks you register by name; the callbacks you leave out are skipped.

   ```lua
   behavior('counter', {
     on_create = function(self) self.n = 0 end,
     on_update = function(self, dt)
       self.n = self.n + 1
       host.note(self.n + self.params.start)
     end,
   })
   ```

   `self` is a table made for the entity (its fields last between callbacks); `self.entity` is its handle and `self.params` is a copy of what the host gave the component. `dt` is the tick's time in seconds.
2. **Load the script into a world.** `script_load` takes the source text (and a name for messages); the engine makes the world's script runtime on the first load. A script that fails to compile does not fail the call: the error is in the log and in `script_stats.errors`.
3. **Attach the behavior.** Set `ENGINE_COMPONENT_SCRIPT` on an entity with the behavior's name, `enabled` and the parameters (a map as an `engine_value_pool`). From the next `frame` the entity has an instance: `on_create`, then `on_start`, then `on_update` every frame.
4. **Run frames.** `frame` runs the script phase (in parallel when the engine has several threads), applies what the scripts recorded, then the rest of the frame. A script's calls into the world take effect at the sync point of that frame.
5. **Call a function.** `script_call` runs a global function with values in and out: `add3(1, 2, 3)` returns `6`. Values are numbers, booleans, strings, lists, maps and vectors; they are copied, so nothing of the script's memory is exposed.
6. **Let the script call you.** `script_register_function` adds a function the scripts see as `host.name`; register before the first `script_load`. Mark it `ENGINE_SCRIPT_FUNCTION_PURE` only when it touches nothing but its arguments (a pure function may be called from many threads at once); otherwise it is made for the frame's thread and the script must not call it from a partition job.
7. **Look at the limits.** `script_set_budget` bounds the instructions and memory of a call; `slow()` in the sample (an endless loop) ends with "instruction budget exceeded" and the world carries on. `script_stats` counts calls, errors and instances.

## What the program prints

It checks each step and prints `host_script: ok` at the end (a failed check prints its line instead).

## Where to go next

- [Using every core](using-every-core.md): the same ideas at 1,000 entities, with tasks and events.
- [Scripting](../guide/scripting.md): the lifecycle, events, latent waits, tasks, hot reload and debugging.
