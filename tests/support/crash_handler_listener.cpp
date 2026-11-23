// Installs the crash handler before the first test runs, so a test binary that faults or calls abort() leaves a report
// on standard error and a minidump instead of waiting for a click: on Windows the C runtime's "abort() has been called"
// dialog and Windows Error Reporting's would otherwise hold the run (and ctest) until someone closes them. A binary opts
// in by compiling this file in, as it does tests/support/register_backends.cpp. The dumps go to the directory the build
// names (ENGINE_TEST_CRASH_DIR, under the build tree's test-tmp), never the user's crash directory.
#include <catch2/catch_test_case_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "debug/crash_context.h"
#include "debug/crash_handler.h"
#include "debug/stack_trace.h"
#include "debug/thread_info.h"

#ifndef ENGINE_TEST_CRASH_DIR
#error "ENGINE_TEST_CRASH_DIR must name the directory the test binary's crash dumps go to"
#endif

namespace
{

class CrashHandlerListener : public Catch::EventListenerBase
{
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(const Catch::TestRunInfo&) override
    {
        vse::debug::InitDebugFacilities();
        vse::debug::InitCrashContext(0, nullptr);
        vse::debug::CrashHandlerOptions options;
        options.dump_directory = ENGINE_TEST_CRASH_DIR;
        vse::debug::InstallCrashHandler(options);  // a failure is logged; the run goes on without a handler
        vse::debug::SetCrashThreadName("test-main");
    }
};

}  // namespace

CATCH_REGISTER_LISTENER(CrashHandlerListener)
