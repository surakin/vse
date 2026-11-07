#pragma once

#include <cstddef>

#include "minidump.h"

// Naming the frames of a crash report against a symbol store (Phase 13 Day 156; scripts/store-symbols.ps1 fills the store). The DbgHelp part is windows/resolve.cpp.
//
// A Windows crash report prints its frames as `#N  0x<address>  <module path>+0x<offset>` (no DbgHelp while the process crashes). This finds
// each frame's module in the dump's module list (by file name), the module's PDB in the store under the build id the dump records
// (`<store>/<pdb>/<GUID+age>/<pdb>`, the layout WinDbg reads), and prints the frame with its function and line. Other lines pass through.
// Names taken from the dump (the PDB's file name, the build id) are checked before they become a path: a file name only, hex only.
namespace crash_extract {

// Parses a report line "#N  0x<address>  <module>+0x<offset>" into the frame number, the module text and the offset; false for any other.
bool ParseFrame(const char* line, std::size_t length, unsigned* number, char* module, std::size_t module_capacity, u64* offset);

// The dump's module whose file name matches the last component of `module_text` (case ignored, as Windows file names); null if none.
const DumpModule* FindModule(const char* module_text, const DumpModule* modules, std::size_t count);

// The store path of a module's PDB, `<store>\<pdb>\<ID>\<pdb>` with the ID in upper case; false if the module has no usable record.
bool StorePdbPath(const char* store, const DumpModule& module, char* out, std::size_t capacity);

// True if `name` can be used as one path component: not empty, not "." or "..", no separator, drive colon or control character.
bool SafeFileName(const char* name);

// Writes the report with its frames named. Returns the number of frames named; -1 where symbols cannot be read (not Windows).
int ResolveReport(const char* report, std::size_t report_size, const DumpModule* modules, std::size_t module_count, const char* store);

} // namespace crash_extract
