// The pipeline cache file rules every backend shares (rhi/pipeline_cache_file.h, Phase 13 Day 167): reading a file
// whole, refusing what is not a usable file, and writing atomically into a directory that may not exist yet.
#include "cstr/cstr.h"

#include <catch2/catch_test_macros.hpp>

#include "temp_dir.h"
#include "containers/vector.h"
#include "io/file.h"
#include "io/file_system.h"
#include "rhi/pipeline_cache_file.h"
#include "rhi/rhi.h"
#include "cstr/mem_ops.h"

using namespace vse;
using namespace vse::rhi;

namespace pipeline_cache_file_test
{
engine_test::Text Native(const io::Path& path)
{
    return engine_test::MakeText(path.View());
}
}  // namespace pipeline_cache_file_test

using pipeline_cache_file_test::Native;

TEST_CASE("A pipeline cache file is written atomically into a new directory and read back whole",
          "[rhi][pipeline_cache]")
{
    io_test::TempDir dir;
    const io::Path file = dir.Path().Join(StringView("nested/deeper/cache.bin"));
    const byte bytes[5] = {1, 2, 3, 4, 5};
    REQUIRE(WritePipelineCacheFile(Native(file).CStr(), bytes, sizeof(bytes)));
    Vector<byte> read;
    REQUIRE(ReadPipelineCacheFile(Native(file).CStr(), &read));
    REQUIRE(read.Size() == sizeof(bytes));
    CHECK(MemCompare(read.Data(), bytes, sizeof(bytes)) == 0);
}

TEST_CASE("A failed pipeline cache read leaves the output empty, whatever it held", "[rhi][pipeline_cache]")
{
    // Review finding (Day 167): the output kept its old bytes on failure, and both backends take a non-empty output as
    // a file read.
    io_test::TempDir dir;
    Vector<byte> out;
    out.Push(7);
    out.Push(8);
    CHECK_FALSE(ReadPipelineCacheFile(Native(dir.Path().Join(StringView("missing.bin"))).CStr(), &out));
    CHECK(out.Size() == 0);
    out.Push(9);
    CHECK_FALSE(ReadPipelineCacheFile(dir.Native().CStr(), &out));  // a directory, not a file
    CHECK(out.Size() == 0);
    const io::Path empty = dir.Path().Join(StringView("empty.bin"));
    REQUIRE(io::WriteFile(empty, nullptr, 0).Ok());
    out.Push(10);
    CHECK_FALSE(ReadPipelineCacheFile(Native(empty).CStr(), &out));  // an empty file is not a cache
    CHECK(out.Size() == 0);
}

TEST_CASE("The pipeline cache directory comes from the device, then the environment, then the user's cache",
          "[rhi][pipeline_cache]")
{
    DeviceCreateInfo info;
    info.application_name = "cache/test";
    info.pipeline_cache_dir = "explicit-dir";
    char dir[400];
    REQUIRE(ResolvePipelineCacheDir(info, dir, sizeof(dir)));
    CHECK(CStrCompare(dir, "explicit-dir") == 0);
    char tiny[4];
    CHECK_FALSE(ResolvePipelineCacheDir(info, tiny, sizeof(tiny)));  // too long: no cache, never another directory
    CHECK(tiny[0] == '\0');
}
