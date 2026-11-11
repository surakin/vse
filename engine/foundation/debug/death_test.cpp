#include "debug/death_test.h"

#include "debug/death_test_platform.h"
#include "os/os.h"
#include "string/string.h"

#include "sync/atomic.h"

namespace engine_placeholder::debug
{

namespace
{

// Head of the intrusive list of registered death tests; constant-initialized so registration works from any static
// initializer.
DeathTestEntry* g_death_test_head = nullptr;
Atomic<bool> g_death_tests_supported_override{true};

bool CaptureContains(const char* haystack, usize length, const char* needle)
{
    return StringView(haystack, length).Contains(StringView(needle));
}

}  // namespace

DeathTestRegistrar::DeathTestRegistrar(DeathTestEntry& entry)
{
    entry.next = g_death_test_head;
    g_death_test_head = &entry;
}

const DeathTestEntry* FindDeathTest(const char* name)
{
    const StringView wanted(name);
    for (const DeathTestEntry* e = g_death_test_head; e != nullptr; e = e->next)
    {
        if (StringView(e->name) == wanted)
        {
            return e;
        }
    }
    return nullptr;
}

bool ChildResult::StderrContains(const char* text) const
{
    return CaptureContains(stderr_text, stderr_length, text);
}

bool ChildResult::StdoutContains(const char* text) const
{
    return CaptureContains(stdout_text, stdout_length, text);
}

bool DeathTestsSupported()
{
    return g_death_tests_supported_override && detail::PlatformCanSpawn();
}

void SetDeathTestSupportForTesting(bool supported)
{
    g_death_tests_supported_override = supported;
}

ChildResult RunInChild(const char* name, const char* argument, const ChildOptions& options)
{
    ENGINE_ASSERT(name != nullptr && argument != nullptr, "RunInChild: name and argument must not be null");
    if (!DeathTestsSupported())
    {
        return ChildResult();
    }
    return detail::PlatformRunChild(name, argument, options);
}

bool RunDeathTestChildIfRequested()
{
    const char* name = nullptr;
    const char* argument = nullptr;
    if (!detail::PlatformChildRequest(&name, &argument))
    {
        return false;
    }
    detail::PlatformPrepareChild();
    const DeathTestEntry* entry = FindDeathTest(name);
    if (entry == nullptr)
    {
        static const char kMessage[] = "death test child: no death test is registered under that name\n";
        os::Write(2, kMessage, sizeof(kMessage) - 1);
        ExitChildProcess(kUnknownDeathTestExitCode);
    }
    entry->fn(argument);
    ExitChildProcess(0);
}

}  // namespace engine_placeholder::debug
