# Tutorial: Using Every Core with Scripts

**Goal:** Run 1,000 scripted entities across several partitions, see that the world ends in the same state at 1, 2, 4 and 8 partitions, and use events and tasks for what a behavior cannot do alone.

**You need:** [Your First Script](your-first-script.md) and [Why your script runs on many threads](../guide/scripting-and-threads.md). The program of this page is `tests/engine_library/script_sample.c` (`cmake --build build/linux --target script_sample`; `./build/linux/tests/engine_library/script_sample`; no display needed). A bigger game is `game/script_demo` (`script_demo --threads 16 --entities 2000 --frames 600`, in a window; `--headless` prints the state hash instead). Reference: [Scripting](../guide/scripting.md), "Partitions", "Events", "Script tasks", "Cost".

## Steps

1. **Make many entities with one behavior.** The sample makes 1,000 entities with a `mover` behavior that moves its entity a little every update with `transform.translate`. Each entity lands in a partition by its id; partitions run at the same time.
2. **Choose the number of partitions** with the setting `script.partitions` (default 64). Use at least four times the thread count so the work balances; more than 256 buys nothing. The sample runs the whole scenario at 1, 2, 4 and 8 and prints one line per run (`partitions 1: state hash ...`) with the **state hash** of each: they are equal. That is the guarantee: the number of partitions and threads changes the speed, never the result.
3. **Send an event from the host** with `script_event_emit`; scripts hear it at the start of the next tick (`event.on('name', function(me, payload, sender) ... end)` in a behavior). Events are how entities talk: one emits, any number of listeners react, and none sees the other's state.
4. **Hear the scripts' events** with `script_event_subscribe`: the callback runs on the frame's thread at the start of the next tick, in the order the events were emitted, whatever the thread count.
5. **Run a task.** `task.parallel_for(count, source, args, chunk)` (the source is the text of a function over `first` and `last`) inside a script, or `script_task_run` from the host, runs a pure function over indices on every core. A task sees no world, only the values you hand it; the result comes back as a value. In a behavior, `local r = wait_task(task.parallel_for(...))` (the task runs at the end of the tick that asked for it) parks the call and resumes it the tick after the task finishes.
6. **Call from other threads.** The sample has three host threads calling `script_call` and `script_task_run` on their own worlds while another thread runs frames: each world's runtime is used by one caller at a time (a busy world answers `ENGINE_ERROR_NOT_READY`).
7. **Measure.** `script_bench --scene tick` prints the cost per entity and the speedup per thread count on your machine.

## What to look at

- The four `state hash` lines, equal, and the last line `script_sample: ok`.
- `script_stats`: the counts of calls, errors, instances and events.

## Where to go next

- [Scripting](../guide/scripting.md), "Cost" and "Limits": what a call, a wait and an event cost, and what is capped.
- [Physics in Your Window](physics-in-your-window.md): scripts and physics together are what `game/script_demo` does.
