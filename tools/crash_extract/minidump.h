#pragma once

#include <cstddef>

#include "types.h"

// Reading a minidump's module list and exception, and a Windows executable's build id (Phase 13 Day 156).
//
// Pure functions over a buffer, like the envelope parser: they read nothing outside it, whatever the bytes are. A module's build id has the
// form the crash context prints (debug/crash_context.h): for a CodeView "RSDS" record (Windows) the GUID's fields and the age in lowercase
// hex, for Breakpad's "BpEL" record (Linux) the ELF build id's bytes in hex.
namespace crash_extract {

using engine_placeholder::u32;
using engine_placeholder::u64;

constexpr std::size_t kMaxDumpText = 780; // a module path (260 UTF-16 units, 3 UTF-8 bytes each) and its terminator

struct DumpModule {
    u64 base = 0;              // where the module was loaded
    u32 size = 0;              // SizeOfImage
    u32 timestamp = 0;         // TimeDateStamp of the image
    char name[kMaxDumpText] = {};       // the module's path as the dump records it, UTF-8
    char build_id[80] = {};             // empty if the dump has no CodeView record for it
    char debug_file[kMaxDumpText] = {}; // the PDB path the image names (RSDS only), as recorded; empty otherwise
};

struct DumpException {
    bool present = false;
    u32 thread_id = 0;
    u32 code = 0;
    u64 address = 0;
};

enum class DumpResult {
    Ok,
    Malformed // not a minidump, or a stream that does not fit in the data
};

// Reads the module list: up to `capacity` modules into `out`; `*count` is how many the dump lists (it may be more than `capacity`).
DumpResult ReadDumpModules(const char* data, std::size_t size, DumpModule* out, std::size_t capacity, std::size_t* count);

// Reads the exception stream; `out->present` is false for a dump without one.
DumpResult ReadDumpException(const char* data, std::size_t size, DumpException* out);

// What a symbol store files a Windows image and its PDB under.
struct ImageInfo {
    char build_id[80] = {};             // the CodeView record's GUID and age, as for a module
    char debug_file[kMaxDumpText] = {}; // the PDB path the image names
    u32 timestamp = 0;                  // COFF TimeDateStamp
    u32 size_of_image = 0;              // the optional header's SizeOfImage
};

// Reads a PE file (an .exe or .dll). False if it is not one or has no CodeView record.
bool ReadImageInfo(const char* data, std::size_t size, ImageInfo* out);

// The last component of a path (after the last '/' or '\'), for a file name taken from a dump: never a path to follow.
const char* BaseName(const char* path);

} // namespace crash_extract
