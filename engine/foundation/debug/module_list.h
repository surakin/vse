#pragma once

#include "types.h"

//! The executable and shared libraries loaded into the running process, for naming the module an address belongs to.
namespace engine_placeholder::debug
{

/// Bytes kept of a module's path.
constexpr usize kMaxModulePathBytes = 256;

/// One loaded executable or shared library.
///
/// - Thread-safety: thread-compatible. A plain value.
struct LoadedModule
{
    /// The difference between the addresses in the file and the addresses in memory; an address minus this is the
    /// offset a debugger or `addr2line` expects.
    u64 load_bias = 0;
    /// First address of the module in memory.
    u64 begin = 0;
    /// One past the last address of the module in memory.
    u64 end = 0;
    /// The file the module was loaded from, null-terminated and cut at [[debug::kMaxModulePathBytes]] - 1 bytes.
    char path[kMaxModulePathBytes] = {};
};

/// Reads the list of loaded modules from the operating system and replaces the one in use.
///
/// Called by [[debug::InitDebugFacilities]]; call it again after loading a library at run time if its addresses are
/// to be named. Each refresh keeps one small table for the life of the program (a reader may still be using the old
/// one).
///
/// - Thread-safety: thread-safe. Not async-signal-safe: it allocates and walks the loader's list.
void RefreshLoadedModules();

/// Finds the module that contains an address.
///
/// - Param address: an address in the process.
/// - Param out: receives the module when found; borrowed for the call.
/// - Returns: true if a module in the last refreshed list contains `address`.
/// - Fails: returns false for an address in no module or before the first refresh.
/// - Ownership: `out` stays the caller's; the path is copied into it.
/// - Thread-safety: thread-safe and async-signal-safe: it reads an immutable table.
bool FindLoadedModule(u64 address, LoadedModule* out);

}  // namespace engine_placeholder::debug
