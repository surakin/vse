#pragma once
//! What the files of the module `engine_c` share (Phase 23 Day 349): the failure text of the calling thread and the way
//! from a handle to the engine's runtime. Internal to the module.
#include "build_defs.h"
#include "engine/engine_c.h"

namespace engine_placeholder::engine_c
{

class Runtime;

/// Keeps the text of a failure for `last_error` (printf style, cut to the buffer).
///
/// - Thread-safety: thread-safe; acts on the calling thread's own text.
/// - Fails: never.
void SetLastError(const char* format, ...) ENGINE_PRINTF_FORMAT(1, 2);

/// Keeps `StatusName(status): what` as the calling thread's failure text.
///
/// - Returns: `status`.
/// - Thread-safety: thread-safe.
/// - Fails: never.
engine_status Fail(engine_status status, const char* what);

/// Finds the runtime of the engine a handle names.
///
/// - Param out: receives the runtime, or null on failure.
/// - Returns: OK, or INVALID_ARGUMENT / STALE_HANDLE with the failure text set.
/// - Thread-safety: thread-safe. The runtime stays valid until the engine is destroyed, which the host does not do
/// while it calls the engine.
engine_status ResolveRuntime(engine_handle engine, Runtime** out);

/// The runtime of the engine that exists.
///
/// - Returns: it, or null when no engine exists.
/// - Thread-safety: thread-safe.
Runtime* CurrentRuntime();

/// The runtime a handle names, for the host's audio thread: no lock, no allocation, no failure text.
///
/// - Param engine: the handle `audio_render` was given.
/// - Returns: the runtime, or null when the handle is zero, was never issued or names an engine that is gone.
/// - Thread-safety: thread-safe and lock-free; the engine must not be destroyed while the call runs (the host stops
/// its audio thread first, as `audio_close` asks).
Runtime* AudioRenderRuntime(engine_handle engine);

}  // namespace engine_placeholder::engine_c
