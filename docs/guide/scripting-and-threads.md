# Why your script runs on many threads, and what that means for you

**Audience:** gameplay programmers who come from an engine where scripts run on one thread (Blueprints, Unity's `MonoBehaviour`, Godot's `_process`). Everything here is the model of [Scripting](scripting.md) in plain terms; the reference
sections of that page give the numbers and the names.

## The short version

Your behavior's `on_update` is called for every entity that has it, once per tick, and **the engine runs many of those calls at the same time** on all the cores. You write each callback as if it were the only one running. Three rules make that
safe, and the engine checks all of them for you:

1. **You change the world through the engine's calls, not by reaching into it.** `component.set`, `transform.set_position`, `entity.spawn`, `physics.add_force` do not change anything at that moment: they record what you asked for.
2. **What you read is the world as it was when the tick started.** Your own writes are visible to *you* in the same callback (you read back what you set), but not to any other entity until the next tick.
3. **Nothing you own is visible to another script.** Your `self` table, your locals and your upvalues live in one state that no other entity's script can touch.

At the end of the tick, at a point called the **sync point**, the engine applies everything that was recorded, in a fixed order that does not depend on how many threads ran or which finished first. The result is the same at 1 thread and at 32.

## What this costs you, and what it buys

You give up three things you may be used to:

| In a single-threaded engine | Here |
|---|---|
| Entity A sets `health = 0`; entity B, running right after, sees it | B sees the value of the tick start. If B must react, A emits an event (`event.emit`); B hears it at the start of the next tick. |
| A global table that every script reads and writes (`Game.score`) | Scripts share no tables. Keep the score in a component on an entity, or count with events that one listener handles. |
| `local ok = other:take_damage(5)` and use the answer on the next line | A call into another entity's script does not exist. Send an event, or read the component the other entity writes. |

In return:

- **Scripts scale with the cores.** The same behaviors that take 796 ms for 100,000 entities on one thread take 125 ms on 16 (`Scripting`, "Cost").
- **There are no data races to debug**, because no two scripts touch the same memory; no locks to take, no `volatile`.
- **A run repeats exactly.** With the same seed and inputs the world after tick N is identical at any thread count, on every run. That is what makes replay, rollback, and a bug report that says "frame 4,312 of this recording" possible.

## One tick, as your script sees it

```
tick start      the world is frozen as the "tick-start view"
  per partition (many at once, one thread each):
    for each entity in id order:  on_start / on_fixed_update / on_update
       reads  -> the tick-start view (plus this entity's own writes)
       writes -> recorded in the partition's command buffer
sync point      the buffers are applied in partition order, then in entity order
                events emitted this tick are delivered; finished tasks resume their waiters
tick end        the next tick sees the changed world
```

A **partition** is a group of entities that share one script state (default 64 partitions; `script.partitions`). An entity is always in the same partition (a fixed function of its id), so its `self` and its waiting coroutines stay in one state.

## Writing scripts that scale

- **Do the work of one entity in that entity's callback.** A loop over a thousand neighbors is fine (it reads the tick-start view); a loop that changes a thousand neighbors is not possible, and that is the point: send each one an event.
- **Read, compute, write once.** Several `component.set` calls for one component in one callback cost more than one with a table.
- **Use `wait` instead of polling.** `wait(0.5)` or `wait_event('switch')` parks the call and costs nothing until it is due; a flag checked every tick costs a call per entity per tick.
- **Put big pure computations in tasks.** `task.parallel_for(...)` and `task.map(...)` run a chunked function over many indices on every core and give the result back to a waiting call (`wait_task`). A task sees no world: it gets values in and returns values out, so it cannot race either ([Using every core](../tutorials/using-every-core.md)).
- **Do not count on the order between entities within a tick.** The merge order is fixed, so the result repeats, but "entity 5 ran before entity 6" is not something you can use: neither saw the other's writes.
- **Do not let `pairs` order reach the world.** The order of `pairs` over a table with string keys differs from run to run; collect the keys, `table.sort` them, and loop over the sorted list when what you do depends on the order (`ipairs` over an array is fine).
- **Random numbers** come from `random.*`, a stream of the entity, the world seed and the tick: the same at any thread count. `math.random` is not available.

## When two scripts write the same thing

If two entities (in different partitions) write the same component of the same target in one tick, both writes are valid and one has to win. By default the higher partition's write applies, and the count of such conflicts is in the stats; `script.conflicts = error` refuses the later one instead. Design so that one entity owns each piece of state, and tell the others by event.

## What the engine does *not* do

It does not run one script's code on two threads at once, does not move a running callback between threads, and does not let a script block a thread: a script cannot sleep, wait on a lock, or start a thread. A callback that runs too long hits its instruction budget and ends with an error (`script.on_error` says whether the behavior is then disabled or retried); its neighbors are not affected.

## If you need to know more

- The model in full: [Scripting](scripting.md), sections "Partitions", "The world view and the merge" and "The parallel script system".
- Doing your first script: [Your first script](../tutorials/your-first-script.md); spreading work over the cores: [Using every core](../tutorials/using-every-core.md).
- Why events and tasks, and what each costs: [Scripting](scripting.md), "Events" and "Script tasks".
