#include "debug/crash_context_platform.h"
#include "debug/thread_info.h"

#include "os/os.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <link.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <unistd.h>

namespace engine_placeholder::debug {

u64 CurrentThreadId() {
    return static_cast<u64>(::syscall(SYS_gettid));
}

namespace detail {

namespace {

constexpr u32 kGnuBuildIdNote = 3; // NT_GNU_BUILD_ID

void CopyText(char* dest, usize capacity, const char* source) {
    usize n = 0;
    for (; source != nullptr && n + 1 < capacity && source[n] != '\0'; ++n) {
        dest[n] = source[n];
    }
    dest[n] = '\0';
}

// Looks for the GNU build-id note in the notes of the first object the loader lists (the executable) and writes it as hex.
int FindBuildId(struct dl_phdr_info* info, size_t, void* data) {
    char* out = static_cast<char*>(data);
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr)& ph = info->dlpi_phdr[i];
        if (ph.p_type != PT_NOTE) {
            continue;
        }
        const byte* begin = reinterpret_cast<const byte*>(info->dlpi_addr + ph.p_vaddr);
        const byte* end = begin + ph.p_memsz;
        const byte* p = begin;
        while (p + sizeof(ElfW(Nhdr)) <= end) {
            const ElfW(Nhdr)* note = reinterpret_cast<const ElfW(Nhdr)*>(p);
            const byte* name = p + sizeof(ElfW(Nhdr));
            const usize name_size = (static_cast<usize>(note->n_namesz) + 3u) & ~static_cast<usize>(3);
            const usize desc_size = (static_cast<usize>(note->n_descsz) + 3u) & ~static_cast<usize>(3);
            const usize room = static_cast<usize>(end - name);
            if (name_size > room || note->n_descsz > room - name_size) {
                break; // truncated or corrupt note
            }
            const byte* desc = name + name_size;
            if (note->n_type == kGnuBuildIdNote && note->n_namesz == 4 && name_size >= 4 && std::memcmp(name, "GNU", 4) == 0) {
                usize digits = 0;
                for (u32 b = 0; b < note->n_descsz && digits + 2 < kMaxBuildIdText; ++b) {
                    out[digits++] = "0123456789abcdef"[desc[b] >> 4];
                    out[digits++] = "0123456789abcdef"[desc[b] & 0xf];
                }
                out[digits] = '\0';
                return 1; // found; stop
            }
            if (desc_size > static_cast<usize>(end - desc)) {
                break;
            }
            p = desc + desc_size;
        }
    }
    return 1; // only the first object (the executable) is examined
}

// Reads "key: value" from a /proc text file; copies the value (trimmed at the line end) into out. Returns false if absent.
bool ReadProcValue(const char* path, const char* key, char* out, usize capacity) {
    const int fd = os::Open(path, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    char text[16384];
    usize used = 0;
    for (;;) {
        const long n = os::Read(fd, text + used, sizeof(text) - 1 - used);
        if (n <= 0) {
            break;
        }
        used += static_cast<usize>(n);
        if (used + 1 >= sizeof(text)) {
            break;
        }
    }
    os::Close(fd);
    text[used] = '\0';
    const usize key_length = std::strlen(key);
    for (const char* line = text; *line != '\0';) {
        const char* next = std::strchr(line, '\n');
        const char* after_key = line + key_length;
        while (std::strncmp(line, key, key_length) == 0 && (*after_key == ' ' || *after_key == '\t')) {
            ++after_key; // /proc/cpuinfo writes "model name<TAB>: value"
        }
        if (std::strncmp(line, key, key_length) == 0 && *after_key == ':') {
            const char* value = after_key + 1;
            while (*value == ' ' || *value == '\t') {
                ++value;
            }
            const usize length = next != nullptr ? static_cast<usize>(next - value) : std::strlen(value);
            usize n = length < capacity - 1 ? length : capacity - 1;
            std::memcpy(out, value, n);
            out[n] = '\0';
            return true;
        }
        if (next == nullptr) {
            break;
        }
        line = next + 1;
    }
    return false;
}

} // namespace

void FillPlatformCrashContext(CrashContext& context) {
    ::dl_iterate_phdr(&FindBuildId, context.build_id);

    const ssize_t n = ::readlink("/proc/self/exe", context.executable, sizeof(context.executable) - 1);
    context.executable[n > 0 ? n : 0] = '\0';

    struct utsname name;
    if (::uname(&name) == 0) {
        CopyText(context.os_name, sizeof(context.os_name), name.sysname);
        CopyText(context.os_version, sizeof(context.os_version), name.release);
        CopyText(context.architecture, sizeof(context.architecture), name.machine);
    }
    ReadProcValue("/proc/cpuinfo", "model name", context.cpu_brand, sizeof(context.cpu_brand));
    const long cpus = ::sysconf(_SC_NPROCESSORS_ONLN);
    context.cpu_count = cpus > 0 ? static_cast<u32>(cpus) : 0;
    char memory[64];
    if (ReadProcValue("/proc/meminfo", "MemTotal", memory, sizeof(memory))) {
        // "16384000 kB"
        u64 kilobytes = 0;
        for (const char* p = memory; *p >= '0' && *p <= '9'; ++p) {
            kilobytes = kilobytes * 10 + static_cast<u64>(*p - '0');
        }
        context.total_memory_bytes = kilobytes * 1024ull;
    }
    context.start_time_unix = static_cast<u64>(::time(nullptr));
}

} // namespace detail

} // namespace engine_placeholder::debug
