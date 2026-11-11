// The loaded-module table on Windows (Phase 13 Day 154): EnumProcessModulesEx, GetModuleInformation and
// GetModuleFileNameExW, published as an immutable snapshot like the Linux one (os/linux/debug/module_list.cpp), so a
// reader never sees a half-written table.
#include "debug/module_list.h"

#include "os/os.h"
#include "os/windows/utf16.h"
#include "os/windows/win32.h"

#include "sync/atomic.h"
#include "sync/mutex.h"

#include "unity/windows_enable.h"
#include <psapi.h>
#include "unity/windows_disable.h"

namespace engine_placeholder::debug
{

namespace
{

// An immutable snapshot; old tables are never freed (a reader may still hold one). The entries follow the header in one
// block.
struct ModuleTable
{
    u32 count;
    u32 capacity;
    LoadedModule* Entries() { return reinterpret_cast<LoadedModule*>(this + 1); }
    const LoadedModule* Entries() const { return reinterpret_cast<const LoadedModule*>(this + 1); }
};

Atomic<const ModuleTable*> g_module_table{nullptr};
Mutex g_module_refresh_mutex;

}  // namespace

void RefreshLoadedModules()
{
    LockGuard<Mutex> lock(g_module_refresh_mutex);
    const HANDLE process = GetCurrentProcess();
    // Ask how many there are, then list them; a module loaded in between is left for the next refresh.
    DWORD needed = 0;
    if (!EnumProcessModulesEx(process, nullptr, 0, &needed, LIST_MODULES_ALL) || needed == 0)
    {
        return;
    }
    const u32 room = static_cast<u32>(needed / sizeof(HMODULE)) + 8;
    // Zero-initialized memory straight from the OS: the table and the handle list are never freed into any allocator.
    const usize bytes = sizeof(ModuleTable) + static_cast<usize>(room) * sizeof(LoadedModule);
    ModuleTable* table = static_cast<ModuleTable*>(os::ReserveMemory(bytes));
    HMODULE* handles = static_cast<HMODULE*>(os::ReserveMemory(room * sizeof(HMODULE)));
    if (table == nullptr || handles == nullptr)
    {
        if (handles != nullptr)
        {
            os::ReleaseMemory(handles, room * sizeof(HMODULE));
        }
        if (table != nullptr)
        {
            os::ReleaseMemory(table, bytes);
        }
        return;  // keep the previous list
    }
    table->count = 0;
    table->capacity = room;
    DWORD listed = 0;
    if (EnumProcessModulesEx(process, handles, room * sizeof(HMODULE), &listed, LIST_MODULES_ALL))
    {
        const u32 n = static_cast<u32>(listed / sizeof(HMODULE)) < room ? static_cast<u32>(listed / sizeof(HMODULE))
                                                                        : room;
        for (u32 i = 0; i < n; ++i)
        {
            MODULEINFO info{};
            if (!GetModuleInformation(process, handles[i], &info, sizeof(info)))
            {
                continue;  // unloaded meanwhile
            }
            LoadedModule& m = table->Entries()[table->count];
            m.begin = reinterpret_cast<u64>(info.lpBaseOfDll);
            m.end = m.begin + info.SizeOfImage;
            m.load_bias = m.begin;  // module offsets are relative to the image base, as a debugger and the symbol files
                                    // expect
            wchar_t path[MAX_PATH * 4];
            const DWORD length = GetModuleFileNameExW(process, handles[i], path,
                                                      static_cast<DWORD>(sizeof(path) / sizeof(path[0])));
            if (length > 0)
            {
                const usize written = os::windows::Utf16ToUtf8(path, length, m.path, kMaxModulePathBytes);
                if (written >= kMaxModulePathBytes)
                {
                    m.path[kMaxModulePathBytes - 1] = '\0';  // cut, as on Linux
                }
            }
            ++table->count;
        }
    }
    os::ReleaseMemory(handles, room * sizeof(HMODULE));
    g_module_table.Store(table, MemoryOrder::Release);
}

bool FindLoadedModule(u64 address, LoadedModule* out)
{
    const ModuleTable* table = g_module_table.Load(MemoryOrder::Acquire);
    if (table == nullptr)
    {
        return false;
    }
    for (u32 i = 0; i < table->count; ++i)
    {
        const LoadedModule& m = table->Entries()[i];
        if (address >= m.begin && address < m.end)
        {
            *out = m;
            return true;
        }
    }
    return false;
}

}  // namespace engine_placeholder::debug
