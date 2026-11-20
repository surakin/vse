#include "debug/thread_info.h"

namespace vse::debug
{

namespace
{

// Constant-initialized and trivially destructible, so reading it from a signal handler needs no thread-local
// construction and no destructor runs at thread exit.
thread_local char t_crash_thread_name[kMaxThreadNameBytes] = {};

}  // namespace

void SetCrashThreadName(const char* name)
{
    usize n = 0;
    if (name != nullptr)
    {
        for (; n + 1 < kMaxThreadNameBytes && name[n] != '\0'; ++n)
        {
            t_crash_thread_name[n] = name[n];
        }
    }
    t_crash_thread_name[n] = '\0';
}

const char* CrashThreadName()
{
    return t_crash_thread_name;
}

}  // namespace vse::debug
