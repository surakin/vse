#ifndef ENGINE_ABI_VERSION_H
#define ENGINE_ABI_VERSION_H

/* The version of the engine's C ABI (Phase 23 Day 344). Valid C. A major change breaks hosts, a minor change only adds;
 * abi/engine.exports carries the same numbers on its `version` line and tools/abi_check fails when they differ, or when
 * the list of exports changes without the bump the change needs. */
#define ENGINE_ABI_VERSION_MAJOR 1
#define ENGINE_ABI_VERSION_MINOR 0

#endif
