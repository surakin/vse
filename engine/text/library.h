#pragma once

#include "types.h"

//! The text module's view of the FreeType library: its version, and the allocation counts of a library session.
//!
//! FreeType (vendored under `third_party/freetype/`, `PLAN.md` assumption #17) is named in exactly one file of the engine, `engine/text/font_freetype.cpp`
//! (the `platform_neutral_check` library boundary); everything else uses the types of this module. Every block FreeType allocates comes from the
//! engine's heap through a counting `FT_Memory`, so the engine's allocation accounting stays true and a failed allocation is testable.
namespace engine_placeholder::text {

/// The version of the FreeType library the engine was built with.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct LibraryVersion {
    /// The major number (2 in 2.15.0).
    u32 major = 0;
    /// The minor number (15 in 2.15.0).
    u32 minor = 0;
    /// The patch number (0 in 2.15.0).
    u32 patch = 0;
};

/// The heap traffic of one FreeType library session, as counted by the engine's allocator hook.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct LibraryAllocationStats {
    /// Blocks allocated (a reallocation counts as a free and an allocation).
    u64 allocations = 0;
    /// Blocks freed.
    u64 frees = 0;
    /// The largest number of bytes live at one time, headers excluded.
    u64 peak_bytes = 0;
    /// Bytes still live when the session ended; zero means the library freed everything it allocated.
    u64 live_bytes_at_end = 0;
};

/// Starts a FreeType library with the engine's allocator and its module list, reads the version and shuts it down.
///
/// - Param version: receives the library's version; may be null.
/// - Param stats: receives the session's allocation counts; may be null.
/// - Returns: true when the library started and shut down cleanly; false when FreeType refused to start or reported an error while shutting down.
/// - Thread-safety: thread-safe; every call is its own library instance.
/// - Ownership: nothing is retained; the library is destroyed before the call returns.
/// - Fails: returns false, leaving `version` and `stats` unchanged, when FreeType reports an error at startup (out of memory is the realistic cause); an error while shutting down also returns false, with both outputs filled in.
bool ProbeLibrary(LibraryVersion* version, LibraryAllocationStats* stats);

} // namespace engine_placeholder::text
