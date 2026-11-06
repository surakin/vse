// A fault inside a job: the crash report names the worker thread and the trace reaches the job's function.
#include <catch2/catch_test_macros.hpp>

#include "debug/crash_handler.h"
#include "debug/death_test.h"
#include "debug/program_setup.h"
#include "jobs/scheduler.h"
#include "log/assert.h"
#include "os/os.h"
#include "support/death_test_skip.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace engine_placeholder;
using namespace engine_placeholder::debug;

namespace {

jobs::Job AssertingJob(std::atomic<int>* started) {
    started->store(1, std::memory_order_release);
    ENGINE_ASSERT(false, "assert inside a job");
    co_return;
}

} // namespace

ENGINE_DEATH_TEST_FN(dt_job_assert) {
    CrashHandlerOptions options;
    options.dump_directory = argument;
    InstallCrashHandler(options);
    jobs::Scheduler::Config config;
    config.participants = 2;
    jobs::Scheduler scheduler(config);
    std::atomic<int> started{0};
    scheduler.Submit(AssertingJob(&started));
    // The main thread only waits; the job runs on a worker, which aborts.
    for (int i = 0; i < 10000 && started.load() == 0; ++i) {
        os::SleepNanos(1000 * 1000);
    }
    os::SleepNanos(5ull * 1000 * 1000 * 1000);
}

TEST_CASE("An assert inside a job reports the worker's name and a trace that includes the job", "[crash][jobs][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    char directory[] = "/tmp/engine-job-XXXXXX";
    REQUIRE(mkdtemp(directory) != nullptr);
    ChildOptions options;
    options.timeout_ms = 60000;
    const ChildResult r = RunInChild("dt_job_assert", directory, options);
    const std::string out(r.stdout_text, r.stdout_length);
    const std::string err(r.stderr_text, r.stderr_length);
    INFO(out << err);
    CHECK(r.reason == CrashReason::Abort);
    CHECK(out.find("ASSERT FAILED: ") != std::string::npos);
    CHECK(out.find("AssertingJob") != std::string::npos); // the assert's own trace
    CHECK(err.find("=== CRASH REPORT ===") != std::string::npos);
    CHECK(err.find("\"worker-") != std::string::npos);     // the crash report names the thread
    CHECK(err.find("AssertingJob") != std::string::npos);  // and its trace includes the job
    std::string cleanup = std::string("rm -rf '") + directory + "'";
    CHECK(std::system(cleanup.c_str()) == 0);
}
