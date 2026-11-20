#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "debug/crash_handler.h"
#include "io/directory.h"
#include "io/file.h"
#include "io/file_system.h"
#include "os/os.h"

using namespace vse;

namespace
{

io::Path SavedDumpsTempDir()
{
    io::Path tmp, dir;
    io::TempDirectory(&tmp);
    REQUIRE(io::CreateTempDirectory(tmp, "re_saved_dumps", &dir).Ok());
    return dir;
}

void SavedDumpsWrite(const io::Path& dir, const char* name)
{
    const char text[] = "x";
    REQUIRE(io::WriteFile(dir.Join(name), text, 1).Ok());
    os::SleepNanos(20'000'000);  // the next file is clearly newer
}

}  // namespace

TEST_CASE("PruneSavedDumps deletes the oldest envelopes beyond the limit and nothing else", "[debug][crash_handler]")
{
    const io::Path dir = SavedDumpsTempDir();
    SavedDumpsWrite(dir, "a.envelope");
    SavedDumpsWrite(dir, "b.envelope");
    SavedDumpsWrite(dir, "notes.txt");
    SavedDumpsWrite(dir, "c.envelope");
    SavedDumpsWrite(dir, "d.envelope");

    CHECK(debug::PruneSavedDumps(dir, 0) == 0);  // 0 keeps everything
    CHECK(debug::PruneSavedDumps(dir, 4) == 0);  // already within the limit
    CHECK(io::Exists(dir.Join("a.envelope")));
    CHECK(debug::PruneSavedDumps(dir, 2) == 2);
    CHECK_FALSE(io::Exists(dir.Join("a.envelope")));
    CHECK_FALSE(io::Exists(dir.Join("b.envelope")));
    CHECK(io::Exists(dir.Join("c.envelope")));
    CHECK(io::Exists(dir.Join("d.envelope")));
    CHECK(io::Exists(dir.Join("notes.txt")));                    // not a dump
    CHECK(debug::PruneSavedDumps(dir.Join("missing"), 1) == 0);  // a missing directory is not an error
    io::RemoveAll(dir);
}
