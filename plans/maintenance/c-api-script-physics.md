# Maintenance: the physics and character script bindings in a C API world

Not a numbered phase. A host's request (`plans/maintenance/host-script-physics-bindings.md`, 2026-11-20). **Design:
[docs/superpowers/specs/2026-11-20-c-api-script-physics-design.md](../../docs/superpowers/specs/2026-11-20-c-api-script-physics-design.md),
approved by the user on 2026-11-20** (one character set per world: the user's choice). One day, at the user's request.

## Day

1. [Everything, on Linux](c-api-script-physics/01-bindings-and-one-character-set.md)

The PC and the Mac build and run it afterwards (recorded in `KNOWN-ISSUES.md`), as for every series.

**Definition of done:** a Lua behavior in a C API world makes a character, walks it, jumps it and teleports it with the documented calls; the
C API's character functions drive the same characters; the walked distance does not depend on the frame rate; the tests pass plain, asan and
tsan on Linux.
