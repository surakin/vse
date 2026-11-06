#pragma once

#include "types.h"

//! Capturing the call stack and turning it into function names, files and lines.
//!
//! Built on libbacktrace behind this interface (PLAN.md assumption #20); nothing here names the library. Capturing and
//! resolving need no frame pointers. After [[debug::InitDebugFacilities]] has run once they never call `malloc` (the
//! library maps memory with `mmap` when it first reads the debug information of a part of the program), so a crash
//! handler can use them; they do take the library's internal lock, so a fault inside them on the same thread is not
//! survivable. File and line need debug information in the binary (`-g`); without it frames carry the function name from
//! the symbol table, or only the module and offset.
namespace engine_placeholder::debug {

/// Most frames a [[debug::StackTrace]] holds.
constexpr usize kMaxStackFrames = 64;

/// Bytes kept of a function name in a [[debug::FrameInfo]], including the null byte. A longer (mangled template) name is
/// cut, and a cut name cannot be demangled.
constexpr usize kMaxSymbolBytes = 512;
/// Bytes kept of a source file path in a [[debug::FrameInfo]], including the null byte.
constexpr usize kMaxSourcePathBytes = 256;
/// Bytes kept of a module path in a [[debug::FrameInfo]], including the null byte.
constexpr usize kMaxFrameModuleBytes = 128;

/// A captured call stack: return addresses, innermost first.
///
/// Thread-safety: thread-compatible. A plain value.
struct StackTrace {
    /// The addresses, innermost call first; `count` of them are valid.
    u64 frames[kMaxStackFrames] = {};
    /// Number of valid frames.
    u32 count = 0;
    /// True if the stack was deeper than [[debug::kMaxStackFrames]] and the outer frames were dropped.
    bool truncated = false;
};

/// What is known about one frame (one address, or one function inlined at that address).
///
/// Thread-safety: thread-compatible. A plain value.
struct FrameInfo {
    /// The address of the frame, as captured.
    u64 address = 0;
    /// The address minus the load bias of its module: the offset to give `addr2line` or a debugger. 0 if no module.
    u64 module_offset = 0;
    /// Line in `file`; 0 if unknown.
    int line = 0;
    /// True if this entry is a function that was inlined into the next entry at the same address.
    bool inlined = false;
    /// True if `module` and `module_offset` are valid.
    bool has_module = false;
    /// The function name as the symbol table has it (mangled for C++); empty if unknown.
    char function[kMaxSymbolBytes] = {};
    /// The source file; empty if the binary has no debug information for this address.
    char file[kMaxSourcePathBytes] = {};
    /// The path of the executable or library that contains the address; empty if unknown.
    char module[kMaxFrameModuleBytes] = {};
};

/// How [[debug::FormatStackTrace]] writes names.
///
/// Thread-safety: thread-compatible. A plain value.
struct FormatOptions {
    /// Turn mangled C++ names into readable ones. It allocates, so use it only where heap use is allowed (not in a signal
    /// handler).
    bool demangle = false;
};

/// Prepares the stack-trace facilities: reads the debug information of the executable and the list of loaded modules.
///
/// Calling it at program start means a later trace, including one taken in a signal handler, never needs `malloc`: it
/// also takes and resolves one trace, which primes the stack unwinder and the debug information of the code on that path.
/// Without it the first trace does the same work then. Safe to call more than once.
///
/// Thread-safety: thread-safe. Not async-signal-safe: it allocates and reads files.
void InitDebugFacilities();

/// Reads the debug information and the list of loaded modules again, to pick up libraries loaded since the last call.
///
/// Libraries loaded after [[debug::InitDebugFacilities]] are not symbolized until this runs. Each call keeps a small amount
/// of memory for the life of the program (the library cannot free what it read).
///
/// Thread-safety: thread-safe. Not async-signal-safe. A lookup that runs at the same time may mix the old and the new
///   information for that one call.
void ReloadDebugModules();

/// Captures the call stack of the current thread.
///
/// Param trace: receives the frames, innermost first; any previous content is replaced.
/// Param skip_frames: how many frames to leave out, counting from the caller of this function: 0 starts with the function
///   that called [[debug::CaptureStackTrace]]. A frame the compiler removed (an inlined function, a tail call) is not there
///   to skip, so keep the functions you skip out of line.
/// Returns: true if at least one frame was captured.
/// Fails: returns false if the stack could not be walked or `skip_frames` covers it all.
/// Thread-safety: thread-safe. Async-signal-safe once [[debug::InitDebugFacilities]] has run (it never calls `malloc`).
bool CaptureStackTrace(StackTrace& trace, u32 skip_frames = 0);

/// Looks up function, file, line and module for every frame of a trace.
///
/// A frame with inlined functions produces several entries, innermost first, all with the frame's address; every entry but
/// the last has `inlined` set. The result is truncated at `capacity`.
///
/// Param trace: the frames to look up.
/// Param out: receives the entries; at least `capacity` of them; borrowed for the call.
/// Param capacity: number of entries `out` holds.
/// Returns: the number of entries written (at least one per frame, unless `capacity` ran out).
/// Ownership: `out` stays the caller's; the strings inside the entries are copies, not pointers into the library.
/// Thread-safety: thread-safe. Async-signal-safe once [[debug::InitDebugFacilities]] has run (it never calls `malloc`).
usize ResolveStackTrace(const StackTrace& trace, FrameInfo* out, usize capacity);

/// Writes a trace as text, one line per frame, to a buffer.
///
/// The line layout is fixed so tools can read it: `#N  0xADDRESS  function  file:line`. `N` is the frame number (an inlined
/// function repeats the number of the frame it was inlined into and ends with ` [inlined]`); the address has 16 hex digits;
/// `file:line` is left out when unknown; when the function is unknown the module and offset take its place
/// (`/path/lib.so+0xOFFSET`), or `??` when even the module is unknown. A trace that was cut ends with `  ... more frames`.
///
/// Param trace: the trace to write.
/// Param out: the buffer; borrowed for the call.
/// Param capacity: size of `out`; the text is cut to fit and always ends with a null byte if `capacity` is not 0.
/// Param options: how to write names.
/// Returns: the number of bytes written, not counting the null byte.
/// Ownership: `out` stays the caller's; nothing is retained after the call.
/// Thread-safety: thread-safe. Async-signal-safe once [[debug::InitDebugFacilities]] has run (it never calls `malloc`),
///   unless `options.demangle`.
usize FormatStackTrace(const StackTrace& trace, char* out, usize capacity, const FormatOptions& options = FormatOptions());

/// Turns a mangled C++ name into a readable one.
///
/// Param mangled: a mangled name; borrowed for the call.
/// Param out: receives the readable name, null-terminated; borrowed for the call.
/// Param capacity: size of `out`.
/// Returns: true if `mangled` was a mangled name and the result fit.
/// Fails: returns false, leaving `out` empty, for a name that is not mangled or too long for `out`.
/// Ownership: `out` stays the caller's; nothing is retained after the call.
/// Thread-safety: thread-safe. Allocates; not async-signal-safe.
bool DemangleSymbol(const char* mangled, char* out, usize capacity);

} // namespace engine_placeholder::debug
