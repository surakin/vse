// Registers every RHI backend this build has before the first test runs (Phase 13 Day 162): the registry is filled by an
// explicit call, never by a global constructor, so each test binary that creates devices compiles this file in. A binary
// that leaves it out fails its first device creation with "not registered in this program", never a silent skip.
#include <catch2/catch_test_case_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "rhi/backends/backends.h"

namespace {

class RegisterBackendsListener : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(const Catch::TestRunInfo&) override { engine_placeholder::rhi::RegisterCompiledBackends(); }
    // A test that empties the registry to try fake backends leaves it full again for the next one.
    void testCaseStarting(const Catch::TestCaseInfo&) override { engine_placeholder::rhi::RegisterCompiledBackends(); }
};

} // namespace

CATCH_REGISTER_LISTENER(RegisterBackendsListener)
