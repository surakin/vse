#include "jobs/topology.h"

#include "os/console.h"

#include "jobs/detail/thread_platform.h"
#include "jobs/jobs_log.h"
#include "cstr/number.h"

namespace engine_placeholder::jobs
{

LogChannel LogJobs("LogJobs", LogVerbosity::Warn);

usize GetAllowedCpus(u32* out, usize capacity)
{
    usize n = detail::QueryAllowedCpus(out, capacity);  // the platform's answer (jobs/<platform>/thread_platform.cpp)
    if (n == 0 && capacity > 0)
    {
        out[0] = 0;
        n = 1;
    }
    return n;
}

usize ParseParticipantOverride(const char* text, usize fallback, bool* warned)
{
    if (warned != nullptr)
    {
        *warned = false;
    }
    if (text == nullptr)
    {
        return fallback;
    }
    char* end = nullptr;
    long long v = CStrToLongLong(text, &end, 10);
    bool valid = end != text && *end == '\0' && v >= 1;
    if (!valid)
    {
        if (warned != nullptr)
        {
            *warned = true;
        }
        return fallback;
    }
    if (static_cast<unsigned long long>(v) > kMaxParticipants)
    {
        if (warned != nullptr)
        {
            *warned = true;
        }
        return kMaxParticipants;
    }
    return static_cast<usize>(v);
}

usize DefaultParticipantCount()
{
    u32 cpus[kMaxParticipants];
    usize allowed = GetAllowedCpus(cpus, kMaxParticipants);
    const char* env = os::GetEnvironment("ENGINE_JOB_THREADS");
    bool warned = false;
    usize n = ParseParticipantOverride(env, allowed, &warned);
    if (warned)
    {
        JobsLog(LogVerbosity::Warn, "ENGINE_JOB_THREADS=\"%s\" is invalid or out of range [1, %zu]; using %zu", env,
                static_cast<size_t>(kMaxParticipants), static_cast<size_t>(n));
    }
    return n;
}

}  // namespace engine_placeholder::jobs
