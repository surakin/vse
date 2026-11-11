#include "debug/crash_state.h"

#include "debug/crash_report.h"

#include "cstr/cstr.h"
#include "sync/mutex.h"
#include "sync/atomic.h"

namespace engine_placeholder::debug
{

namespace
{

constexpr usize kMaxEntries = 8;

struct Entry
{
    char name[32] = {};
    bool is_counter = false;
    Atomic<u64> counter{0};
    char text[kCrashStateEntryTextBytes] = {};
};

Entry g_entries[kMaxEntries];
usize g_entry_count = 0;  // guarded by g_mutex
Mutex g_mutex;

// SIGNAL-SAFE-BEGIN
// Runs inside a crashing process: plain reads only, no formatting library.
void ProvideEntry(void* context, char* out, usize capacity)
{
    const Entry& entry = *static_cast<const Entry*>(context);
    if (capacity == 0)
    {
        return;
    }
    if (entry.is_counter)
    {
        char digits[24];
        usize count = 0;
        u64 value = entry.counter.Load(MemoryOrder::Relaxed);
        do
        {
            digits[count++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0 && count < sizeof(digits));
        usize n = 0;
        while (count > 0 && n + 1 < capacity)
        {
            out[n++] = digits[--count];
        }
        out[n] = '\0';
        return;
    }
    usize n = 0;
    while (n + 1 < capacity && n + 1 < kCrashStateEntryTextBytes && entry.text[n] != '\0')
    {
        out[n] = entry.text[n];
        ++n;
    }
    out[n] = '\0';
}
// SIGNAL-SAFE-END

Entry* FindOrCreate(const char* name, bool counter)
{
    if (name == nullptr)
    {
        return nullptr;
    }
    LockGuard<Mutex> lock(g_mutex);
    for (usize i = 0; i < g_entry_count; ++i)
    {
        if (CStrCompare(g_entries[i].name, name) == 0)
        {
            return g_entries[i].is_counter == counter ? &g_entries[i] : nullptr;
        }
    }
    if (g_entry_count == kMaxEntries)
    {
        return nullptr;
    }
    Entry& entry = g_entries[g_entry_count];
    CStrCopyN(entry.name, name, sizeof(entry.name) - 1);  // copied: the caller's string need not outlive the call
    entry.name[sizeof(entry.name) - 1] = '\0';
    entry.is_counter = counter;
    if (!RegisterCrashStateProvider(entry.name, &ProvideEntry, &entry))
    {
        entry.name[0] = '\0';  // the provider table is full: leave the slot unused
        entry.is_counter = false;
        return nullptr;
    }
    ++g_entry_count;
    return &entry;
}

}  // namespace

void ResetCrashStateForTesting()
{
    LockGuard<Mutex> lock(g_mutex);
    ResetCrashStateProvidersForTesting();
    for (usize i = 0; i < g_entry_count; ++i)
    {
        g_entries[i].name[0] = '\0';
        g_entries[i].text[0] = '\0';
        g_entries[i].counter.Store(0, MemoryOrder::Relaxed);
    }
    g_entry_count = 0;
}

bool SetCrashStateText(const char* name, const char* text)
{
    Entry* entry = FindOrCreate(name, false);
    if (entry == nullptr)
    {
        return false;
    }
    LockGuard<Mutex> lock(g_mutex);
    usize n = 0;
    for (; text != nullptr && n + 1 < kCrashStateEntryTextBytes && text[n] != '\0'; ++n)
    {
        entry->text[n] = text[n];
    }
    entry->text[n] = '\0';
    return true;
}

Atomic<u64>* CrashStateCounter(const char* name)
{
    Entry* entry = FindOrCreate(name, true);
    return entry != nullptr ? &entry->counter : nullptr;
}

}  // namespace engine_placeholder::debug
