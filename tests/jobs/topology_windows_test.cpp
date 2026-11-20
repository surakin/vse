// The Windows CPU list (Phase 13 Day 153): the parser of GetLogicalProcessorInformationEx(RelationGroup) fed recorded
// buffers, so machines with more than 64 logical processors (several processor groups) are tested on a machine with one
// group; and the live answer of this machine.
#include <catch2/catch_test_macros.hpp>

#include "cstr/mem_ops.h"
#include "cstr/cstr.h"
#include "sync/thread.h"
#include "lang/initializer_list.h"

#include "containers/vector.h"
#include "jobs/detail/thread_platform.h"
#include "jobs/topology.h"
#include "jobs/windows/topology_windows.h"
#include "os/windows/win32.h"

using namespace vse;
using namespace vse::jobs;

namespace
{

// A RelationGroup record for groups with the given active processor masks, as the OS lays it out.
Vector<u64> GroupRecord(InitializerList<u64> masks)
{
    const usize header = offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Group);
    const usize infos = offsetof(GROUP_RELATIONSHIP, GroupInfo);
    const usize size = header + infos + masks.size() * sizeof(PROCESSOR_GROUP_INFO);
    Vector<u64> storage;
    storage.Resize((size + 7) / 8);
    byte* at = reinterpret_cast<byte*>(storage.Data());
    MemSet(at, 0, storage.Size() * 8);
    const LOGICAL_PROCESSOR_RELATIONSHIP relation = RelationGroup;
    const DWORD record_size = static_cast<DWORD>(size);
    MemCopy(at + offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Relationship), &relation, sizeof(relation));
    MemCopy(at + offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Size), &record_size, sizeof(record_size));
    const WORD count = static_cast<WORD>(masks.size());
    MemCopy(at + header + offsetof(GROUP_RELATIONSHIP, MaximumGroupCount), &count, sizeof(count));
    MemCopy(at + header + offsetof(GROUP_RELATIONSHIP, ActiveGroupCount), &count, sizeof(count));
    usize g = 0;
    for (u64 mask : masks)
    {
        PROCESSOR_GROUP_INFO info{};
        info.ActiveProcessorMask = static_cast<KAFFINITY>(mask);
        u32 bits = 0;
        for (u64 m = mask; m != 0; m &= m - 1)
        {
            ++bits;
        }
        info.ActiveProcessorCount = static_cast<BYTE>(bits);
        info.MaximumProcessorCount = info.ActiveProcessorCount;
        MemCopy(at + header + infos + g * sizeof(PROCESSOR_GROUP_INFO), &info, sizeof(info));
        ++g;
    }
    return storage;
}

usize Size(InitializerList<u64> masks)
{
    return offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Group) + offsetof(GROUP_RELATIONSHIP, GroupInfo) +
           masks.size() * sizeof(PROCESSOR_GROUP_INFO);
}

}  // namespace

TEST_CASE("Windows topology: two processor groups give ids group * 64 + bit", "[jobs][topology][windows]")
{
    const Vector<u64> record = GroupRecord({~u64{0}, 0xF});
    const u16 groups[] = {0, 1};
    u32 cpus[kMaxParticipants];
    const usize n = jobs::detail::CpusFromGroupRecords(record.Data(), Size({~u64{0}, 0xF}), groups, 2, ~u64{0}, cpus,
                                                       kMaxParticipants);
    REQUIRE(n == 68);
    for (u32 i = 0; i < 64; ++i)
    {
        CHECK(cpus[i] == i);
    }
    for (u32 i = 0; i < 4; ++i)
    {
        CHECK(cpus[64 + i] == 64 + i);
    }
}

TEST_CASE("Windows topology: a process in one group honours its affinity mask", "[jobs][topology][windows]")
{
    const Vector<u64> record = GroupRecord({0xFFFF, 0xFFFF});
    u32 cpus[kMaxParticipants];
    // The process's only group is 1, restricted to CPUs 1 and 3 of it (`start /affinity 0xA` on that group).
    const u16 second[] = {1};
    usize n = jobs::detail::CpusFromGroupRecords(record.Data(), Size({0xFFFF, 0xFFFF}), second, 1, 0b1010, cpus,
                                                 kMaxParticipants);
    REQUIRE(n == 2);
    CHECK(cpus[0] == 65);
    CHECK(cpus[1] == 67);
    // Several groups: the primary mask does not apply (it describes one group only).
    const u16 both[] = {0, 1};
    n = jobs::detail::CpusFromGroupRecords(record.Data(), Size({0xFFFF, 0xFFFF}), both, 2, 0b1, cpus, kMaxParticipants);
    CHECK(n == 32);
    // An inactive processor (a hole in the mask) is not listed.
    const Vector<u64> holes = GroupRecord({0b1011});
    const u16 first[] = {0};
    n = jobs::detail::CpusFromGroupRecords(holes.Data(), Size({0b1011}), first, 1, ~u64{0}, cpus, kMaxParticipants);
    REQUIRE(n == 3);
    CHECK((cpus[0] == 0 && cpus[1] == 1 && cpus[2] == 3));
}

TEST_CASE("Windows topology: capacity and malformed records are respected", "[jobs][topology][windows]")
{
    const Vector<u64> record = GroupRecord({~u64{0}});
    const u16 groups[] = {0};
    u32 cpus[8];
    CHECK(jobs::detail::CpusFromGroupRecords(record.Data(), Size({~u64{0}}), groups, 1, ~u64{0}, cpus, 8) == 8);
    // A size shorter than the record, or a record that claims more than the buffer holds, ends the parse without
    // reading past it.
    CHECK(jobs::detail::CpusFromGroupRecords(record.Data(), 4, groups, 1, ~u64{0}, cpus, 8) == 0);
    Vector<u64> lying = GroupRecord({~u64{0}});
    const DWORD too_big = 1 << 20;
    MemCopy(reinterpret_cast<byte*>(lying.Data()) + offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Size), &too_big,
            sizeof(too_big));
    CHECK(jobs::detail::CpusFromGroupRecords(lying.Data(), Size({~u64{0}}), groups, 1, ~u64{0}, cpus, 8) == 0);
}

TEST_CASE("Windows topology: this machine's CPUs, thread ids, pinning and names", "[jobs][topology][windows]")
{
    u32 cpus[kMaxParticipants];
    const usize n = GetAllowedCpus(cpus, kMaxParticipants);
    // The PC is one group; the answer must agree with the process affinity mask.
    DWORD_PTR process_mask = 0;
    DWORD_PTR system_mask = 0;
    REQUIRE(GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask));
    usize bits = 0;
    for (DWORD_PTR m = process_mask; m != 0; m &= m - 1)
    {
        ++bits;
    }
    CHECK(n == bits);
    for (usize i = 1; i < n; ++i)
    {
        CHECK(cpus[i - 1] < cpus[i]);
    }
    CHECK(jobs::detail::CurrentThreadId() == static_cast<i32>(GetCurrentThreadId()));
    // Pinning a fresh thread to the last allowed CPU, and its name, are what the OS reports back.
    bool pinned = false;
    bool named = false;
    Thread t(
        [&]
        {
            pinned = jobs::detail::PinCurrentThread(cpus[n - 1]);
            GROUP_AFFINITY affinity{};
            pinned = pinned && GetThreadGroupAffinity(GetCurrentThread(), &affinity) &&
                     affinity.Mask == (KAFFINITY{1} << (cpus[n - 1] % 64));
            jobs::detail::NameCurrentThread("worker-test");
            PWSTR description = nullptr;
            if (SUCCEEDED(GetThreadDescription(GetCurrentThread(), &description)) && description != nullptr)
            {
                named = WStrCompare(description, L"worker-test") == 0;
                LocalFree(description);
            }
        });
    t.Join();
    CHECK(pinned);
    CHECK(named);
}

TEST_CASE("Windows topology: a record too short for its group header is not read", "[jobs][topology][windows]")
{
    // A record whose Size covers only the common header: the group count that would follow lies outside it (and outside
    // the buffer).
    const usize header = offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Group);
    Vector<u64> storage;
    storage.Resize((header + 7) / 8);
    byte* at = reinterpret_cast<byte*>(storage.Data());
    MemSet(at, 0, storage.Size() * 8);
    const LOGICAL_PROCESSOR_RELATIONSHIP relation = RelationGroup;
    const DWORD record_size = static_cast<DWORD>(header);
    MemCopy(at + offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Relationship), &relation, sizeof(relation));
    MemCopy(at + offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Size), &record_size, sizeof(record_size));
    // A heap block of exactly the record's size, so ASan sees any read past it.
    Vector<byte> exact;
    exact.Resize(header);
    MemCopy(exact.Data(), at, header);
    const u16 groups[] = {0};
    u32 cpus[8];
    CHECK(jobs::detail::CpusFromGroupRecords(exact.Data(), header, groups, 1, ~u64{0}, cpus, 8) == 0);
}
