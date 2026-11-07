// The DbgHelp half of resolve.h (Phase 13 Day 156): names a crash report's frames from the PDBs in a symbol store. The tool's one
// per-platform file (a platform-dir in tools/module_deps_check/modules.txt; ARCHITECTURE.md section 4).
#include "../resolve.h"

#include "io/file_system.h"
#include "io/path.h"

#include <cstdio>
#include <cstring>

#include "os/windows/win32.h"

#include "unity/windows_enable.h"
#include <dbghelp.h>
#include "unity/windows_disable.h"

namespace crash_extract {

namespace io = engine_placeholder::io;

namespace {

void ToWide(const char* utf8, wchar_t* out, int capacity) {
    if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, capacity) == 0) out[0] = 0;
}

void ToUtf8(const wchar_t* wide, char* out, int capacity) {
    if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, capacity, nullptr, nullptr) == 0) out[0] = '\0';
}

} // namespace

int ResolveReport(const char* report, std::size_t report_size, const DumpModule* modules, std::size_t module_count, const char* store) {
    const HANDLE session = GetCurrentProcess(); // a handle that names this DbgHelp session; nothing of this process is read (no invade)
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
    wchar_t search[2048];
    ToWide(store, search, 2048);
    // An explicit search path: _NT_SYMBOL_PATH, which may name a symbol server, is never read (no network path).
    if (!SymInitializeW(session, search, FALSE)) return -1;
    bool loaded[512] = {};
    int named = 0;
    std::size_t pos = 0;
    while (pos < report_size) {
        std::size_t end = pos;
        while (end < report_size && report[end] != '\n') ++end;
        const char* line = report + pos;
        std::size_t length = end - pos;
        if (length > 0 && line[length - 1] == '\r') --length;
        unsigned number = 0;
        char module_text[kMaxDumpText];
        u64 offset = 0;
        bool printed = false;
        if (ParseFrame(line, length, &number, module_text, sizeof(module_text), &offset)) {
            const DumpModule* module = FindModule(module_text, modules, module_count);
            char pdb_path[2048];
            if (module != nullptr && StorePdbPath(store, *module, pdb_path, sizeof(pdb_path))) {
                const std::size_t index = static_cast<std::size_t>(module - modules);
                wchar_t pdb_wide[2048];
                ToWide(pdb_path, pdb_wide, 2048);
                io::Path pdb_file;
                io::FileInfo pdb_info;
                const bool present = io::Path::Parse(engine_placeholder::StringView(pdb_path), io::PathStyle::Windows, &pdb_file) &&
                                     io::Stat(pdb_file, &pdb_info).Ok();
                if (index < 512 && !loaded[index] && present) {
                    loaded[index] = SymLoadModuleExW(session, nullptr, pdb_wide, nullptr, module->base, module->size, nullptr, 0) != 0;
                }
                if (index < 512 && loaded[index]) {
                    // A frame past the first is a return address: the byte before it is the call (as the engine's own resolver does).
                    const u64 address = module->base + offset - (number > 0 && offset > 0 ? 1 : 0);
                    alignas(SYMBOL_INFOW) engine_placeholder::byte storage[sizeof(SYMBOL_INFOW) + 1024 * sizeof(wchar_t)];
                    SYMBOL_INFOW* symbol = reinterpret_cast<SYMBOL_INFOW*>(storage);
                    std::memset(symbol, 0, sizeof(SYMBOL_INFOW));
                    symbol->SizeOfStruct = sizeof(SYMBOL_INFOW);
                    symbol->MaxNameLen = 1023;
                    DWORD64 displacement = 0;
                    if (SymFromAddrW(session, address, &displacement, symbol)) {
                        char function[3072];
                        ToUtf8(symbol->Name, function, sizeof(function));
                        IMAGEHLP_LINEW64 source{};
                        source.SizeOfStruct = sizeof(source);
                        DWORD line_displacement = 0;
                        char file[2048] = {};
                        if (SymGetLineFromAddrW64(session, address, &line_displacement, &source) && source.FileName != nullptr) {
                            ToUtf8(source.FileName, file, sizeof(file));
                            std::printf("#%u  %s  %s:%lu  (%s+0x%llx)\n", number, function, file, static_cast<unsigned long>(source.LineNumber),
                                        BaseName(module_text), static_cast<unsigned long long>(offset));
                        } else {
                            std::printf("#%u  %s  (%s+0x%llx)\n", number, function, BaseName(module_text), static_cast<unsigned long long>(offset));
                        }
                        printed = true;
                        ++named;
                    }
                }
            }
        }
        if (!printed) std::printf("%.*s\n", static_cast<int>(length), line);
        pos = end + 1;
    }
    SymCleanup(session);
    return named;
}

} // namespace crash_extract
