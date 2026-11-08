#include <catch2/catch_test_macros.hpp>

#include "text/library.h"

using namespace engine_placeholder;
using namespace engine_placeholder::text;

TEST_CASE("FreeType starts through the engine's allocator and reports the pinned version", "[text]") {
    LibraryVersion version;
    LibraryAllocationStats stats;
    REQUIRE(ProbeLibrary(&version, &stats));
    CHECK(version.major == 2);
    CHECK(version.minor == 15);
    CHECK(version.patch == 0);
    CHECK(stats.allocations > 0);        // the library's modules were built from our heap
    CHECK(stats.frees == stats.allocations);
    CHECK(stats.live_bytes_at_end == 0); // and everything went back
    CHECK(stats.peak_bytes > 0);
}

TEST_CASE("The probe accepts null outputs", "[text]") {
    CHECK(ProbeLibrary(nullptr, nullptr));
}
