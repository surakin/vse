// The Windows half of the job system's platform interface (jobs/detail/thread_platform.h; Phase 13 Day 153).
//
// CPU ids are `group * 64 + bit`: Windows numbers logical processors within processor groups of at most 64, and a machine with more than 64
// has several groups. Since Windows 11 a process may run on every group by default; before, on its primary group only, which
// GetProcessGroupAffinity reports either way.
#include "jobs/detail/thread_platform.h"

#include "cstr/mem_ops.h"

#include "containers/vector.h"

#include "jobs/windows/topology_windows.h"
#include "os/windows/utf16.h"
#include "os/windows/win32.h"

namespace engine_placeholder::jobs::detail {

usize CpusFromGroupRecords(const void* buffer, usize size, const u16* process_groups, usize group_count, u64 primary_mask, u32* out,
                           usize capacity) {
    usize n = 0;
    const byte* at = static_cast<const byte*>(buffer);
    usize offset = 0;
    constexpr usize kHeader = offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Group);
    while (offset + kHeader <= size) {
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX head;
        MemCopy(&head, at + offset, kHeader);
        if (head.Size < kHeader || offset + head.Size > size) break; // malformed: stop rather than read past the buffer
        const usize infos = offsetof(GROUP_RELATIONSHIP, GroupInfo);
        // The group header (its counts) must lie inside the record before it is read (found by the Day 153 review).
        if (head.Relationship == RelationGroup && head.Size >= kHeader + infos) {
            const byte* group = at + offset + kHeader;
            WORD active_groups = 0;
            MemCopy(&active_groups, group + offsetof(GROUP_RELATIONSHIP, ActiveGroupCount), sizeof(active_groups));
            for (WORD g = 0; g < active_groups; ++g) {
                const usize info_at = kHeader + infos + g * sizeof(PROCESSOR_GROUP_INFO);
                if (info_at + sizeof(PROCESSOR_GROUP_INFO) > head.Size) break;
                PROCESSOR_GROUP_INFO info;
                MemCopy(&info, at + offset + info_at, sizeof(info));
                bool mine = false;
                for (usize i = 0; i < group_count; ++i) mine = mine || process_groups[i] == g;
                if (!mine) continue;
                u64 mask = static_cast<u64>(info.ActiveProcessorMask);
                if (group_count == 1) mask &= primary_mask;
                for (u32 bit = 0; bit < 64 && n < capacity; ++bit) {
                    if ((mask >> bit) & 1u) out[n++] = static_cast<u32>(g) * 64 + bit;
                }
            }
        }
        offset += head.Size;
    }
    return n;
}

usize QueryAllowedCpus(u32* out, usize capacity) {
    DWORD length = 0;
    GetLogicalProcessorInformationEx(RelationGroup, nullptr, &length);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || length == 0) return 0;
    Vector<u64> storage; // u64 for the records' alignment
    if (!storage.TryResize((length + sizeof(u64) - 1) / sizeof(u64))) return 0;
    void* buffer = storage.Data();
    if (!GetLogicalProcessorInformationEx(RelationGroup, static_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer), &length)) return 0;
    USHORT groups[64];
    USHORT group_count = 64;
    if (!GetProcessGroupAffinity(GetCurrentProcess(), &group_count, groups)) return 0;
    DWORD_PTR process_mask = 0;
    DWORD_PTR system_mask = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask)) process_mask = ~DWORD_PTR{0};
    u16 process_groups[64];
    for (USHORT i = 0; i < group_count; ++i) process_groups[i] = groups[i];
    return CpusFromGroupRecords(buffer, length, process_groups, group_count, static_cast<u64>(process_mask), out, capacity);
}

i32 CurrentThreadId() { return static_cast<i32>(GetCurrentThreadId()); }

bool CanPinThreads() { return true; }

bool PinCurrentThread(u32 cpu) {
    GROUP_AFFINITY affinity{};
    affinity.Group = static_cast<WORD>(cpu / 64);
    affinity.Mask = KAFFINITY{1} << (cpu % 64);
    return SetThreadGroupAffinity(GetCurrentThread(), &affinity, nullptr) != FALSE;
}

void NameCurrentThread(const char* name) {
    if (name == nullptr) return;
    wchar_t wide[64];
    os::windows::Utf8ToUtf16(StringView(name), wide, sizeof(wide) / sizeof(wide[0]));
    SetThreadDescription(GetCurrentThread(), wide); // shown by debuggers, ETW and crash dumps; failure is harmless
}

} // namespace engine_placeholder::jobs::detail
