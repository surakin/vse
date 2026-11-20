// A fault inside a job: the crash report names the worker thread and the trace reaches the job's function.
#include <catch2/catch_test_macros.hpp>

#include "debug/crash_handler.h"
#include "debug/death_test.h"
#include "debug/program_setup.h"
#include "jobs/scheduler.h"
#include "log/assert.h"
#include "os/os.h"
#include "support/death_test_skip.h"

#include "sync/atomic.h"
#include "catch_engine_types.h"
#include "support/test_support_posix.h"

using namespace vse;
using namespace vse::debug;

namespace
{

jobs::Job AssertingJob(Atomic<int>* started)
{
    started->Store(1, MemoryOrder::Release);
    ENGINE_ASSERT(false, "assert inside a job");
    co_return;
}

}  // namespace

ENGINE_DEATH_TEST_FN(dt_job_assert)
{
    CrashHandlerOptions options;
    options.dump_directory = argument;
    InstallCrashHandler(options);
    jobs::Scheduler::Config config;
    config.participants = 2;
    jobs::Scheduler scheduler(config);
    Atomic<int> started{0};
    scheduler.Submit(AssertingJob(&started));
    // The main thread only waits; the job runs on a worker, which aborts.
    for (int i = 0; i < 10000 && started.Load() == 0; ++i)
    {
        os::SleepNanos(1000 * 1000);
    }
    os::SleepNanos(5ull * 1000 * 1000 * 1000);
}

TEST_CASE("An assert inside a job reports the worker's name and a trace that includes the job",
          "[crash][jobs][death][debug][linux]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    char directory[] = "/tmp/engine-job-XXXXXX";
    REQUIRE(mkdtemp(directory) != nullptr);
    ChildOptions options;
    options.timeout_ms = 60000;
    const ChildResult r = RunInChild("dt_job_assert", directory, options);
    const StringView out = debug_test::StdoutView(r);
    const StringView err = debug_test::StderrView(r);
    INFO(debug_test::Cat(out, err).CStr());
    CHECK(r.reason == CrashReason::Abort);
    CHECK(out.Contains("ASSERT FAILED: "));
    CHECK(out.Contains("AssertingJob"));  // the assert's own trace
    CHECK(err.Contains("=== CRASH REPORT ==="));
    CHECK(err.Contains("\"worker-"));     // the crash report names the thread
    CHECK(err.Contains("AssertingJob"));  // and its trace includes the job
    CHECK(debug_test::RunShell(debug_test::Cat("rm -rf '", directory, "'").View()).ExitCode() == 0);
}
