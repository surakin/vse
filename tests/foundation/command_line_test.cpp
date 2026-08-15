#include <catch2/catch_test_macros.hpp>

#include "mem/arena_allocator.h"
#include "os/command_line.h"
#include "string/string.h"
#include "types.h"

using engine_placeholder::ArenaAllocator;
using engine_placeholder::StringView;
using engine_placeholder::usize;
using engine_placeholder::os::CommandLine;

namespace {

// argv-style helper: takes string literals, builds a char* array. The
// returned CommandLine borrows nothing from `literals` after
// construction (it copies into Allocator-owned storage), so the caller
// only needs `literals` to stay alive for the constructor call itself.
template <typename... Args>
CommandLine<ArenaAllocator> MakeCommandLine(ArenaAllocator& allocator, Args... literals) {
    const char* argv[] = {literals...};
    return CommandLine<ArenaAllocator>(allocator, static_cast<int>(sizeof...(Args)), const_cast<char**>(argv));
}

} // namespace

TEST_CASE("CommandLine exposes program name and positional args", "[command_line]") {
    ArenaAllocator arena(4096);
    auto cmd_line = MakeCommandLine(arena, "engine_main", "foo", "bar");

    REQUIRE(cmd_line.ProgramName() == StringView("engine_main"));
    REQUIRE(cmd_line.ArgCount() == 2);
    REQUIRE(cmd_line.Arg(0) == StringView("foo"));
    REQUIRE(cmd_line.Arg(1) == StringView("bar"));
}

TEST_CASE("CommandLine with zero extra args has ArgCount 0 and no flags", "[command_line]") {
    ArenaAllocator arena(4096);
    auto cmd_line = MakeCommandLine(arena, "engine_main");

    REQUIRE(cmd_line.ArgCount() == 0);
    REQUIRE(cmd_line.FlagCount() == 0);
}

TEST_CASE("CommandLine keeps a shell-quoted arg containing a space intact", "[command_line]") {
    ArenaAllocator arena(4096);
    auto cmd_line = MakeCommandLine(arena, "engine_main", "hello world");

    REQUIRE(cmd_line.ArgCount() == 1);
    REQUIRE(cmd_line.Arg(0) == StringView("hello world"));
}

TEST_CASE("CommandLine recognizes a boolean --flag with no value", "[command_line]") {
    ArenaAllocator arena(4096);
    auto cmd_line = MakeCommandLine(arena, "engine_main", "--verbose");

    REQUIRE(cmd_line.ArgCount() == 0);
    REQUIRE(cmd_line.HasFlag(StringView("verbose")));
    StringView value;
    REQUIRE_FALSE(cmd_line.TryGetOption(StringView("verbose"), value));
}

TEST_CASE("CommandLine recognizes a --key=value option", "[command_line]") {
    ArenaAllocator arena(4096);
    auto cmd_line = MakeCommandLine(arena, "engine_main", "--config=foo.json");

    REQUIRE(cmd_line.ArgCount() == 0);
    REQUIRE(cmd_line.HasFlag(StringView("config")));
    StringView value;
    REQUIRE(cmd_line.TryGetOption(StringView("config"), value));
    REQUIRE(value == StringView("foo.json"));
}

TEST_CASE("CommandLine: a repeated flag has the last occurrence win", "[command_line]") {
    ArenaAllocator arena(4096);
    auto cmd_line = MakeCommandLine(arena, "engine_main", "--x=1", "--x=2");

    StringView value;
    REQUIRE(cmd_line.TryGetOption(StringView("x"), value));
    REQUIRE(value == StringView("2"));
}

TEST_CASE("CommandLine treats single-dash and bare -- tokens as positional args", "[command_line]") {
    ArenaAllocator arena(4096);
    auto cmd_line = MakeCommandLine(arena, "engine_main", "-v", "--", "extra");

    REQUIRE(cmd_line.ArgCount() == 3);
    REQUIRE(cmd_line.Arg(0) == StringView("-v"));
    REQUIRE(cmd_line.Arg(1) == StringView("--"));
    REQUIRE(cmd_line.Arg(2) == StringView("extra"));
}

TEST_CASE("CommandLine flags are enumerable by index without knowing names ahead of time", "[command_line]") {
    ArenaAllocator arena(4096);
    auto cmd_line = MakeCommandLine(arena, "engine_main", "--verbose", "--config=foo.json");

    REQUIRE(cmd_line.FlagCount() == 2);
    REQUIRE(cmd_line.FlagName(0) == StringView("verbose"));
    REQUIRE_FALSE(cmd_line.FlagHasValue(0));
    REQUIRE(cmd_line.FlagName(1) == StringView("config"));
    REQUIRE(cmd_line.FlagHasValue(1));
    REQUIRE(cmd_line.FlagValue(1) == StringView("foo.json"));
}

TEST_CASE("CommandLine::FromNative sources args from StashNativeCommandLine", "[command_line]") {
    ArenaAllocator arena(4096);
    const char* argv[] = {"engine_main", "native_arg", "--native_flag"};
    engine_placeholder::os::StashNativeCommandLine(3, const_cast<char**>(argv));

    auto cmd_line = CommandLine<ArenaAllocator>::FromNative(arena);

    REQUIRE(cmd_line.ProgramName() == StringView("engine_main"));
    REQUIRE(cmd_line.ArgCount() == 1);
    REQUIRE(cmd_line.Arg(0) == StringView("native_arg"));
    REQUIRE(cmd_line.HasFlag(StringView("native_flag")));
}
