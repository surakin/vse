// Post-mortem dumps (debug/post_mortem.h), neutral part: the report says what happened and where the artifact goes.
#include <catch2/catch_test_macros.hpp>

#include "debug/death_test.h"
#include "debug/post_mortem.h"
#include "os/os.h"
#include "support/death_test_skip.h"

#include "cstr/format.h"

using namespace engine_placeholder;
using namespace engine_placeholder::debug;

// Run in a child: enabling raises this process's core limit, which a test program should not keep.
ENGINE_DEATH_TEST_FN(dt_post_mortem_enable) {
    PostMortemOptions options;
    options.size_limit_bytes = 64ull * 1024 * 1024;
    const PostMortemReport report = EnablePostMortemDumps(options);
    char line[1024];
    const int n = FormatTo(line, sizeof(line), "RESULT=%d PREVIOUS=%llu CURRENT=%llu\nDESTINATION=%s\nNOTE=%s\n",
                                static_cast<int>(report.result), static_cast<unsigned long long>(report.previous_limit_bytes),
                                static_cast<unsigned long long>(report.current_limit_bytes), report.destination, report.note);
    os::Write(1, line, static_cast<usize>(n));
}

TEST_CASE("Enabling post-mortem dumps with nothing to raise is reported as limited, with a note", "[postmortem][death][debug]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    const ChildResult r = RunInChild("dt_post_mortem_enable"); // the default child has its core limit set to zero
    REQUIRE(r.exited);
    CHECK(r.StdoutContains("RESULT=1"));
    CHECK_FALSE(r.StdoutContains("NOTE=\n"));
}

TEST_CASE("Enabling post-mortem dumps raises the limit to the requested size and says where the artifact goes", "[postmortem][death][debug]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    ChildOptions options;
    options.disable_core_dumps = false; // the default would set the hard limit to 0 and nothing could be raised
    const ChildResult r = RunInChild("dt_post_mortem_enable", "", options);
    REQUIRE(r.exited);
    CHECK(r.exit_code == 0);
    CHECK(r.StdoutContains("DESTINATION="));
    if (r.StdoutContains("RESULT=0")) { // Enabled
        CHECK(r.StdoutContains("CURRENT=67108864\n"));
    } else {
        // The system's hard limit is below 64 MiB (or zero): reported as limited, with a note.
        CHECK(r.StdoutContains("RESULT=1"));
        CHECK_FALSE(r.StdoutContains("NOTE=\n"));
    }
    CHECK_FALSE(r.StdoutContains("DESTINATION=\n"));
}
