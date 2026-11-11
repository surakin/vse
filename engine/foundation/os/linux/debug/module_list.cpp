#include "debug/module_list.h"

#include "os/os.h"

#include "sync/atomic.h"
#include "cstr/cstr.h"
#include <link.h>
#include "sync/mutex.h"
#include <unistd.h>

namespace engine_placeholder::debug
{

namespace
{

// An immutable snapshot, published by an atomic pointer swap, so a reader (possibly in a signal handler) never sees a
// half-written table. Old tables are never freed: a reader may still hold one. The entries follow the header in the
// same block of memory.
struct ModuleTable
{
    u32 count;
    u32 capacity;
    LoadedModule* Entries() { return reinterpret_cast<LoadedModule*>(this + 1); }
    const LoadedModule* Entries() const { return reinterpret_cast<const LoadedModule*>(this + 1); }
};

Atomic<const ModuleTable*> g_module_table{nullptr};
Mutex g_module_refresh_mutex;

int CountModule(struct dl_phdr_info*, size_t, void* data)
{
    ++*static_cast<u32*>(data);
    return 0;
}

int CollectModule(struct dl_phdr_info* info, size_t, void* data)
{
    ModuleTable* table = static_cast<ModuleTable*>(data);
    if (table->count >= table->capacity)
    {
        return 1;  // a library was loaded between counting and collecting: leave it for the next refresh
    }
    u64 low = ~0ull;
    u64 high = 0;
    for (int i = 0; i < info->dlpi_phnum; ++i)
    {
        const ElfW(Phdr) & ph = info->dlpi_phdr[i];
        if (ph.p_type != PT_LOAD)
        {
            continue;
        }
        const u64 begin = static_cast<u64>(info->dlpi_addr) + ph.p_vaddr;
        const u64 end = begin + ph.p_memsz;
        low = begin < low ? begin : low;
        high = end > high ? end : high;
    }
    if (low >= high)
    {
        return 0;  // no loadable segment (nothing to name)
    }
    LoadedModule& m = table->Entries()[table->count];
    m.load_bias = static_cast<u64>(info->dlpi_addr);
    m.begin = low;
    m.end = high;
    const char* name = info->dlpi_name;
    if (name == nullptr || name[0] == '\0')
    {
        // The main program has no name here; ask for the executable's own path.
        const ssize_t n = ::readlink("/proc/self/exe", m.path, kMaxModulePathBytes - 1);
        m.path[n > 0 ? n : 0] = '\0';
    }
    else
    {
        CStrCopyN(m.path, name, kMaxModulePathBytes - 1);
        m.path[kMaxModulePathBytes - 1] = '\0';
    }
    ++table->count;
    return 0;
}

}  // namespace

void RefreshLoadedModules()
{
    LockGuard<Mutex> lock(g_module_refresh_mutex);
    // Count first so the table holds every loaded object, however many there are (a graphics driver pulls in dozens).
    u32 objects = 0;
    ::dl_iterate_phdr(&CountModule, &objects);
    objects += 8;  // room for libraries loaded between the two passes
    const usize bytes = sizeof(ModuleTable) + static_cast<usize>(objects) * sizeof(LoadedModule);
    // Zero-initialized memory straight from the OS: the table is never freed, so it does not belong to any allocator.
    ModuleTable* table = static_cast<ModuleTable*>(os::ReserveMemory(bytes));
    if (table == nullptr)
    {
        return;  // keep the previous list
    }
    table->count = 0;
    table->capacity = objects;
    ::dl_iterate_phdr(&CollectModule, table);
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
