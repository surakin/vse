#pragma once

//! The engine's own settings (the ones that used to be environment variables or per-program options) and the log channels as settings.

#include "settings/settings.h"

namespace engine_placeholder::settings {

/// Declares the engine-level settings in `registry`:
///
/// | key | type | environment variable |
/// |---|---|---|
/// | `platform.window_backend` | enum `auto`, `wayland`, `x11`, `win32` | `ENGINE_WINDOW_BACKEND` (flag `--backend`) |
/// | `rhi.pipeline_cache` | bool | |
/// | `rhi.pipeline_cache_dir` | text | `ENGINE_RHI_CACHE_DIR` |
///
/// Only settings something reads are declared. `platform.window_backend` is read by the programs (which pass it to the window host as before), and
/// `rhi.pipeline_cache` and `rhi.pipeline_cache_dir` fill the device creation. The variables `ENGINE_JOB_THREADS`, `ENGINE_CRASH_HANDLER` and
/// `ENGINE_RHI_VALIDATION_BEST_PRACTICES` are read directly by the job topology, the crash handler setup and the Vulkan instance because they run
/// before any [[settings::Settings]] can exist; they are not settings (the configuration check lists them as exceptions).
///
/// Param registry: an unfrozen registry.
/// Aborts: the registry is frozen or already holds one of the keys.
/// Ownership: the registry borrows static strings.
/// Thread-safety: not-thread-safe; call at start-up.
void RegisterEngineSettings(Registry* registry);

/// Declares `log.<channel>.verbosity` (enum `error`, `warn`, `info`, `debug`, `trace`) and `log.<channel>.enabled` (bool) for every log channel that
/// exists now, with the channel's current values as defaults. Channels whose lower-case name is not a valid key segment are skipped.
///
/// Param registry: an unfrozen registry.
/// Aborts: the registry is frozen.
/// Ownership: the registry borrows static strings.
/// Thread-safety: not-thread-safe; call at start-up, after the static channels exist.
void RegisterLogSettings(Registry* registry);

/// Applies the `log.*` settings to the channels now and whenever they change (a listener).
///
/// The setters of a log channel are unsynchronized, so change `log.*` settings from the thread that owns logging configuration.
///
/// Param settings: the service; it must outlive the program's logging.
/// Thread-safety: not-thread-safe; call after `Load`.
void AttachLogSettings(Settings* settings);

} // namespace engine_placeholder::settings
