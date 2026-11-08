// The platform fields of the crash context on Windows (Phase 13 Day 154; os/windows/debug/BACKEND.md, InitCrashContext), the thread id
// and the debugger check.
#include "debug/crash_context_platform.h"
#include "debug/debugger.h"
#include "debug/thread_info.h"
#include "os/windows/utf16.h"
#include "os/windows/win32.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <intrin.h>
#include "build_defs.h"

namespace engine_placeholder::debug {

u64 CurrentThreadId() { return static_cast<u64>(GetCurrentThreadId()); }

bool IsDebuggerAttached() { return IsDebuggerPresent() != FALSE; }

namespace detail {

namespace {

void CopyText(char* dest, usize capacity, const char* source) {
    usize n = 0;
    for (; source != nullptr && n + 1 < capacity && source[n] != '\0'; ++n) dest[n] = source[n];
    dest[n] = '\0';
}

void AppendDecimal(char* dest, usize capacity, u64 value) {
    char digits[24];
    int n = 0;
    do {
        digits[n++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value > 0);
    usize at = std::strlen(dest);
    while (n > 0 && at + 1 < capacity) dest[at++] = digits[--n];
    dest[at] = '\0';
}

// The executable's build id: the CodeView (RSDS) record of its PE debug directory, GUID then age, as lowercase hex. It is the key a symbol
// store files the PDB under (Day 156), without the dashes the GUID is usually written with.
void BuildIdFromCodeView(char* out, usize capacity) {
    out[0] = '\0';
    const byte* base = reinterpret_cast<const byte*>(GetModuleHandleW(nullptr));
    if (base == nullptr) return;
    IMAGE_DOS_HEADER dos;
    std::memcpy(&dos, base, sizeof(dos));
    if (dos.e_magic != IMAGE_DOS_SIGNATURE) return;
    IMAGE_NT_HEADERS nt;
    std::memcpy(&nt, base + dos.e_lfanew, sizeof(nt));
    if (nt.Signature != IMAGE_NT_SIGNATURE) return;
    const IMAGE_DATA_DIRECTORY& directory = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    const usize entries = directory.Size / sizeof(IMAGE_DEBUG_DIRECTORY);
    for (usize i = 0; i < entries; ++i) {
        IMAGE_DEBUG_DIRECTORY entry;
        std::memcpy(&entry, base + directory.VirtualAddress + i * sizeof(IMAGE_DEBUG_DIRECTORY), sizeof(entry));
        if (entry.Type != IMAGE_DEBUG_TYPE_CODEVIEW || entry.AddressOfRawData == 0 || entry.SizeOfData < 24) continue;
        const byte* record = base + entry.AddressOfRawData;
        if (std::memcmp(record, "RSDS", 4) != 0) continue;
        // GUID (16 bytes: Data1 u32, Data2 u16, Data3 u16, Data4 u8[8], the first three little-endian) and age (u32).
        GUID guid;
        DWORD age = 0;
        std::memcpy(&guid, record + 4, sizeof(guid));
        std::memcpy(&age, record + 20, sizeof(age));
        char text[48];
        std::snprintf(text, sizeof(text), "%08lx%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x%lx", static_cast<unsigned long>(guid.Data1),
                      guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5],
                      guid.Data4[6], guid.Data4[7], static_cast<unsigned long>(age));
        CopyText(out, capacity, text);
        return;
    }
}

} // namespace

void FillPlatformCrashContext(CrashContext& context) {
    BuildIdFromCodeView(context.build_id, sizeof(context.build_id));
    wchar_t path[MAX_PATH * 4];
    const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(sizeof(path) / sizeof(path[0])));
    if (length > 0 && length < sizeof(path) / sizeof(path[0])) {
        const usize n = os::windows::Utf16ToUtf8(path, length, context.executable, sizeof(context.executable));
        if (n >= sizeof(context.executable)) context.executable[sizeof(context.executable) - 1] = '\0';
    }
    // RtlGetVersion tells the truth; GetVersionEx answers what the manifest claims to support.
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto rtl_get_version = ntdll != nullptr ? reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")))
                                                  : nullptr;
    CopyText(context.os_name, sizeof(context.os_name), "Windows");
    if (rtl_get_version != nullptr && rtl_get_version(&version) == 0) {
        context.os_version[0] = '\0';
        AppendDecimal(context.os_version, sizeof(context.os_version), version.dwMajorVersion);
        CopyText(context.os_version + std::strlen(context.os_version), sizeof(context.os_version) - std::strlen(context.os_version), ".");
        AppendDecimal(context.os_version, sizeof(context.os_version), version.dwMinorVersion);
        CopyText(context.os_version + std::strlen(context.os_version), sizeof(context.os_version) - std::strlen(context.os_version), ".");
        AppendDecimal(context.os_version, sizeof(context.os_version), version.dwBuildNumber);
    }
    SYSTEM_INFO system{};
    GetNativeSystemInfo(&system);
    switch (system.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64: CopyText(context.architecture, sizeof(context.architecture), "x86_64"); break;
    case PROCESSOR_ARCHITECTURE_ARM64: CopyText(context.architecture, sizeof(context.architecture), "aarch64"); break;
    case PROCESSOR_ARCHITECTURE_INTEL: CopyText(context.architecture, sizeof(context.architecture), "x86"); break;
    default: CopyText(context.architecture, sizeof(context.architecture), "unknown"); break;
    }
#if ENGINE_ARCH_X64
    // The processor brand string: CPUID leaves 0x80000002-0x80000004, 16 bytes each.
    int registers[4] = {};
    __cpuid(registers, static_cast<int>(0x80000000u));
    if (static_cast<unsigned>(registers[0]) >= 0x80000004u) {
        char brand[49] = {};
        for (unsigned leaf = 0; leaf < 3; ++leaf) {
            __cpuid(registers, static_cast<int>(0x80000002u + leaf));
            std::memcpy(brand + leaf * 16, registers, 16);
        }
        const char* start = brand;
        while (*start == ' ') ++start;
        CopyText(context.cpu_brand, sizeof(context.cpu_brand), start);
    }
#endif
    context.cpu_count = static_cast<u32>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) context.total_memory_bytes = static_cast<u64>(memory.ullTotalPhys);
    context.start_time_unix = static_cast<u64>(::time(nullptr));
}

} // namespace detail

} // namespace engine_placeholder::debug
