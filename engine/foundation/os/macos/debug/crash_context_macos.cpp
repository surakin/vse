// The macOS half of the crash context (debug/crash_context_platform.h; Phase 26 Day 306) and the thread id. Called
// lazily from the assert path, where the heap may be inconsistent: nothing here allocates (sysctl into fixed buffers,
// the main image's load commands in memory).
#include "debug/crash_context_platform.h"
#include "debug/thread_info.h"
#include "os/macos/debug/thread_id_macos.h"

#include "cstr/cstr.h"
#include "cstr/mem_ops.h"
#include "os/os.h"

#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <pthread.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>

namespace engine_placeholder::debug
{

u64 CurrentThreadId()
{
    // The same number the job system prints for a thread (jobs/macos/thread_platform.cpp): pthread_threadid_np's low 31
    // bits, never 0, so a worker reads the same in a crash report and in the job logs. The full id counts up from boot
    // and stays below 2^31 for years of uptime.
    u64 id = 0;
    if (::pthread_threadid_np(nullptr, &id) != 0)
    {
        return 1;
    }
    return macos::ThreadNumber(id);
}

namespace detail
{

namespace
{

void CopyText(char* dest, usize capacity, const char* source)
{
    usize n = 0;
    for (; source != nullptr && n + 1 < capacity && source[n] != '\0'; ++n)
    {
        dest[n] = source[n];
    }
    dest[n] = '\0';
}

// A string sysctl into `out`; false when the name does not exist (machdep.cpu.brand_string on Apple silicon).
bool SysctlText(const char* name, char* out, usize capacity)
{
    size_t size = capacity;
    if (::sysctlbyname(name, out, &size, nullptr, 0) != 0 || size == 0)
    {
        out[0] = '\0';
        return false;
    }
    out[capacity - 1] = '\0';
    return out[0] != '\0';
}

template <typename T> bool SysctlNumber(const char* name, T* out)
{
    size_t size = sizeof(T);
    return ::sysctlbyname(name, out, &size, nullptr, 0) == 0 && size == sizeof(T);
}

// The main image's LC_UUID as lowercase hex without dashes (dsymutil and the symbol store name dSYMs by it).
void BuildId(char* out)
{
    out[0] = '\0';
    const mach_header* header = ::_dyld_get_image_header(0);  // image 0 is the main executable
    if (header == nullptr || header->magic != MH_MAGIC_64)
    {
        return;
    }
    const byte* command = reinterpret_cast<const byte*>(reinterpret_cast<const mach_header_64*>(header) + 1);
    for (u32 i = 0; i < header->ncmds; ++i)
    {
        const load_command* lc = reinterpret_cast<const load_command*>(command);
        if (lc->cmd == LC_UUID)
        {
            const uuid_command* uc = reinterpret_cast<const uuid_command*>(lc);
            usize digits = 0;
            for (u32 b = 0; b < sizeof(uc->uuid) && digits + 2 < kMaxBuildIdText; ++b)
            {
                out[digits++] = "0123456789abcdef"[uc->uuid[b] >> 4];
                out[digits++] = "0123456789abcdef"[uc->uuid[b] & 0xf];
            }
            out[digits] = '\0';
            return;
        }
        command += lc->cmdsize;
    }
}

}  // namespace

void FillPlatformCrashContext(CrashContext& context)
{
    BuildId(context.build_id);
    // The path the program was started by (no realpath here: it may allocate, and this runs on the assert path).
    u32 size = sizeof(context.executable);
    if (::_NSGetExecutablePath(context.executable, &size) != 0)
    {
        context.executable[0] = '\0';
    }
    CopyText(context.os_name, sizeof(context.os_name), "macOS");
    SysctlText("kern.osproductversion", context.os_version, sizeof(context.os_version));
    struct utsname name;
    if (::uname(&name) == 0)
    {
        CopyText(context.architecture, sizeof(context.architecture), name.machine);
    }
    SysctlText("machdep.cpu.brand_string", context.cpu_brand,
               sizeof(context.cpu_brand));  // "Apple M1" on Apple silicon; empty if absent
    int cpus = 0;
    context.cpu_count = SysctlNumber("hw.logicalcpu", &cpus) && cpus > 0 ? static_cast<u32>(cpus) : 0;
    u64 memory = 0;
    context.total_memory_bytes = SysctlNumber("hw.memsize", &memory) ? memory : 0;
    context.start_time_unix = os::WallClockUnixMillis() / 1000;
}

}  // namespace detail

}  // namespace engine_placeholder::debug
