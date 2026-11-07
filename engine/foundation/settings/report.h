#pragma once

//! Text the command line shows about settings: `--list-settings`, the generated `--help` section, and `--save-settings`.

#include "io/path.h"
#include "settings/settings.h"

namespace engine_placeholder::settings {

/// Writes one line for every setting, sorted by key: the key, the current value, the default, where the value came from, and the description
/// (a `kSecret` setting shows `<secret>`; a setting marked `kHidden` is left out).
///
/// ```
/// renderer.width                      1920     default 0        user file (line 3)    the window width in pixels (0: the display's)
/// ```
///
/// Param settings: the service; its layers are read for the provenance.
/// Param out: receives the text.
/// Thread-safety: thread-safe.
void FormatSettingsListing(Settings& settings, String<HeapAllocator>* out);

/// Writes the options a program built on this settings system understands, generated from the registry: `--set`, `--settings`, `--save-settings`,
/// `--list-settings`, each setting's flag alias with its description, and the environment variables (the standard name and the declared `env`).
///
/// Param registry: the settings.
/// Param out: receives the text.
/// Thread-safety: thread-safe.
void FormatSettingsHelp(const Registry& registry, String<HeapAllocator>* out);

/// Writes a new settings file at `path` holding every setting whose current value is not its default, so that loading it reproduces the session
/// (`--settings FILE`). Creates missing directories, writes atomically, stamps the format and schema versions.
///
/// A `kSecret` setting is not written. The file is private to the user (mode 0600); an existing file that cannot be read is left alone and the error returned;
/// a damaged one is replaced and its text kept in `<path>.bak`.
///
/// Param settings: the service.
/// Param path: the file to write; replaced if it exists.
/// Returns: success, or why not.
/// Fails: `PermissionDenied`, `Io`, `ReadOnly`, `TooLong`.
/// Thread-safety: thread-safe.
Status SaveSettingsTo(Settings& settings, const io::Path& path);

} // namespace engine_placeholder::settings
