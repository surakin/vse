// The macOS module list (debug/module_list.h; Phase 21 Day 290): the images dyld has loaded, with their slide and their
// __TEXT segment.
#include "debug/module_list.h"

#include "cstr/cstr.h"
#include "os/os.h"
#include "sync/atomic.h"
#include "sync/mutex.h"

#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>

namespace engine_placeholder::debug
{

namespace
{

// An immutable snapshot published by an atomic pointer swap, as on Linux: a reader (possibly in a crash handler) never
// sees a half-written table, and old tables are never freed (a reader may still hold one). The entries follow the
// header in the same block.
struct ModuleTable
{
    u32 count;
    u32 capacity;
    LoadedModule* Entries() { return reinterpret_cast<LoadedModule*>(this + 1); }
    const LoadedModule* Entries() const { return reinterpret_cast<const LoadedModule*>(this + 1); }
};

Atomic<const ModuleTable*> g_module_table{nullptr};
Mutex g_module_refresh_mutex;

// The image's __TEXT segment in memory (vmaddr + slide): the code a stack frame can point into. Not the span of all
// segments: in the dyld shared cache an image's segments are not adjacent and every image shares one __LINKEDIT, so
// that span covers its neighbours and the first image listed would claim their frames (seen on Day 290:
// libsystem_pthread's frames named libobjc).
bool TextExtent(const mach_header_64* header, u64 slide, u64* low, u64* high)
{
    const byte* command = reinterpret_cast<const byte*>(header + 1);
    for (u32 i = 0; i < header->ncmds; ++i)
    {
        const load_command* lc = reinterpret_cast<const load_command*>(command);
        if (lc->cmd == LC_SEGMENT_64)
        {
            const segment_command_64* seg = reinterpret_cast<const segment_command_64*>(lc);
            if (seg->vmsize != 0 && CStrCompareN(seg->segname, SEG_TEXT, sizeof(seg->segname)) == 0)
            {
                *low = seg->vmaddr + slide;
                *high = *low + seg->vmsize;
                return true;
            }
        }
        command += lc->cmdsize;
    }
    return false;
}

// The images dyld reports through its add-image and remove-image callbacks, kept under g_registry_mutex. Each callback
// hands over a header and a slide that belong together, and the remove callback runs before the image is unmapped and
// waits for the mutex, so a refresh copying the registry never reads the load commands of an image that is going away
// (walking dyld's list by index would: indices shift under dlclose).
struct RegisteredImage
{
    const mach_header_64* header;
    u64 slide;
    char path[kMaxModulePathBytes];
};

Mutex g_registry_mutex;
RegisteredImage* g_registry = nullptr;  // pages of our own (os::ReserveMemory), grown by doubling
u32 g_registry_count = 0;
u32 g_registry_capacity = 0;
bool g_callbacks_registered = false;  // under g_module_refresh_mutex

void OnAddImage(const mach_header* header, intptr_t slide)
{
    if (header == nullptr || header->magic != MH_MAGIC_64)
    {
        return;
    }
    Dl_info info{};
    const bool named = ::dladdr(header, &info) != 0 && info.dli_fname != nullptr;
    LockGuard<Mutex> lock(g_registry_mutex);
    if (g_registry_count == g_registry_capacity)
    {
        const u32 capacity = g_registry_capacity == 0 ? 256 : g_registry_capacity * 2;
        auto* grown = static_cast<RegisteredImage*>(
            os::ReserveMemory(static_cast<usize>(capacity) * sizeof(RegisteredImage)));
        if (grown == nullptr)
        {
            return;  // the image stays unnamed in traces
        }
        for (u32 i = 0; i < g_registry_count; ++i)
        {
            grown[i] = g_registry[i];
        }
        if (g_registry != nullptr)
        {
            os::ReleaseMemory(g_registry, static_cast<usize>(g_registry_capacity) * sizeof(RegisteredImage));
        }
        g_registry = grown;
        g_registry_capacity = capacity;
    }
    RegisteredImage& r = g_registry[g_registry_count++];
    r.header = reinterpret_cast<const mach_header_64*>(header);
    r.slide = static_cast<u64>(slide);
    r.path[0] = '\0';
    if (named)
    {
        CStrCopyN(r.path, info.dli_fname, kMaxModulePathBytes - 1);
        r.path[kMaxModulePathBytes - 1] = '\0';
    }
}

void OnRemoveImage(const mach_header* header, intptr_t)
{
    LockGuard<Mutex> lock(g_registry_mutex);
    for (u32 i = 0; i < g_registry_count; ++i)
    {
        if (g_registry[i].header == reinterpret_cast<const mach_header_64*>(header))
        {
            g_registry[i] = g_registry[--g_registry_count];
            return;
        }
    }
}

}  // namespace

void RefreshLoadedModules()
{
    LockGuard<Mutex> lock(g_module_refresh_mutex);
    if (!g_callbacks_registered)
    {
        // Registering calls OnAddImage at once for every image already loaded; later ones arrive as they load.
        // Registered once, for good (dyld has no way to unregister).
        g_callbacks_registered = true;
        ::_dyld_register_func_for_add_image(&OnAddImage);
        ::_dyld_register_func_for_remove_image(&OnRemoveImage);
    }
    LockGuard<Mutex> registry(g_registry_mutex);
    const u32 images = g_registry_count;
    const usize bytes = sizeof(ModuleTable) + static_cast<usize>(images == 0 ? 1 : images) * sizeof(LoadedModule);
    ModuleTable* table = static_cast<ModuleTable*>(
        os::ReserveMemory(bytes));  // zeroed, from the OS: never freed, so no allocator owns it
    if (table == nullptr)
    {
        return;  // keep the previous list
    }
    table->count = 0;
    table->capacity = images;
    for (u32 i = 0; i < images; ++i)
    {
        const RegisteredImage& r = g_registry[i];
        LoadedModule& m = table->Entries()[table->count];
        if (!TextExtent(r.header, r.slide, &m.begin, &m.end))
        {
            continue;
        }
        m.load_bias = r.slide;
        CStrCopyN(m.path, r.path, kMaxModulePathBytes - 1);
        m.path[kMaxModulePathBytes - 1] = '\0';
        ++table->count;
    }
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
