#pragma once

/// Reads a value from a handle.
///
/// Details paragraph: this explains the behaviour in more depth and may
/// span several lines.
///
/// - First bullet.
/// - Second bullet, which continues
///   on a second line.
///
/// - Param handle: the handle to read from; must be open.
/// - Param out: receives the value; may be null to only validate.
/// - Returns: true on success, false if the handle is closed.
/// - Fails: returns false and leaves `out` untouched on a closed handle.
/// - Aborts: `handle` is negative.
/// - Thread-safety: thread-compatible. Distinct handles may be read
///   concurrently.
/// - Ownership: the caller keeps ownership of `out`.
/// - Example:
/// ```cpp
/// int v;
/// Read(3, &v);
/// ```
/// - See: [[Read]], [[Write]]
bool Read(int handle, int* out);

/// Old entry point.
/// - Deprecated: use [[Read]] instead; removal in Phase 5.
bool OldRead(int handle);

/// A symbol visible only for technical reasons.
/// - Internal: exposed so the test fixture can reach it.
void ForTests();

/// Writes a value.
bool Write(int handle, int value);
