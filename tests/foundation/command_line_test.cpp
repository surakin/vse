#include <catch2/catch_test_macros.hpp>

#include "mem/arena_allocator.h"
#include "os/command_line.h"
#include "string/string.h"
#include "types.h"

using vse::ArenaAllocator;
using vse::StringView;
using vse::usize;
using vse::os::CommandLine;

namespace
{

// argv-style helper: takes string literals, builds a char* array. The
// returned CommandLine borrows nothing from `literals` after
// construction (it copies into Allocator-owned storage), so the caller
// only needs `literals` to stay alive for the constructor call itself.
template <typename... Args> CommandLine<ArenaAllocator> MakeCommandLine(ArenaAllocator& allocator, Args... literals)
{
    const char* argv[] = {literals...};
    return CommandLine<ArenaAllocator>(allocator, static_cast<int>(sizeof...(Args)), const_cast<char**>(argv));
}

}  // namespace

TEST_CASE("CommandLine exposes program name and positional args", "[command_line]")
{
    ArenaAllocator arena(4096);
    auto cmd_line = MakeCommandLine(arena, "engine_main", "foo", "bar");

    REQUIRE(cmd_line.ProgramName() == StringView("engine_main"));
    REQUIRE(cmd_line.ArgCount() == 2);
    REQUIRE(cmd_line.Arg(0) == StringView("foo"));
    REQUIRE(cmd_line.Arg(1) == StringView("bar"));
}

TEST_CASE("CommandLine with zero extra args has ArgCount 0 and no flags", "[command_line]")
{
    ArenaAllocator arena(4096);
    auto cmd_line = MakeCommandLine(arena, "engine_main");

    REQUIRE(cmd_line.ArgCount() == 0);
    REQUIRE(cmd_line.FlagCount() == 0);
}

TEST_CASE("CommandLine keeps a shell-quoted arg containing a space intact", "[command_line]")
{
    ArenaAllocator arena(4096);
    auto cmd_line = MakeCommandLine(arena, "engine_main", "hello world");

    REQUIRE(cmd_line.ArgCount() == 1);
    REQUIRE(cmd_line.Arg(0) == StringView("hello world"));
}

TEST_CASE("CommandLine recognizes a boolean --flag with no value", "[command_line]")
{
    ArenaAllocator arena(4096);
    auto cmd_line = MakeCommandLine(arena, "engine_main", "--verbose");

    REQUIRE(cmd_line.ArgCount() == 0);
    REQUIRE(cmd_line.HasFlag(StringView("verbose")));
    StringView value;
    REQUIRE_FALSE(cmd_line.TryGetOption(StringView("verbose"), value));
}

TEST_CASE("CommandLine recognizes a --key=value option", "[command_line]")
{
    ArenaAllocator arena(4096);
    auto cmd_line = MakeCommandLine(arena, "engine_main", "--config=foo.json");

    REQUIRE(cmd_line.ArgCount() == 0);
    REQUIRE(cmd_line.HasFlag(StringView("config")));
    StringView value;
    REQUIRE(cmd_line.TryGetOption(StringView("config"), value));
    REQUIRE(value == StringView("foo.json"));
}

TEST_CASE("CommandLine: a repeated flag has the last occurrence win", "[command_line]")
{
    ArenaAllocator arena(4096);
    auto cmd_line = MakeCommandLine(arena, "engine_main", "--x=1", "--x=2");

    StringView value;
    REQUIRE(cmd_line.TryGetOption(StringView("x"), value));
    REQUIRE(value == StringView("2"));
}

TEST_CASE("CommandLine treats single-dash and bare -- tokens as positional args", "[command_line]")
{
    ArenaAllocator arena(4096);
    auto cmd_line = MakeCommandLine(arena, "engine_main", "-v", "--", "extra");

    REQUIRE(cmd_line.ArgCount() == 3);
    REQUIRE(cmd_line.Arg(0) == StringView("-v"));
    REQUIRE(cmd_line.Arg(1) == StringView("--"));
    REQUIRE(cmd_line.Arg(2) == StringView("extra"));
}

TEST_CASE("CommandLine flags are enumerable by index without knowing names ahead of time", "[command_line]")
{
    ArenaAllocator arena(4096);
    auto cmd_line = MakeCommandLine(arena, "engine_main", "--verbose", "--config=foo.json");

    REQUIRE(cmd_line.FlagCount() == 2);
    REQUIRE(cmd_line.FlagName(0) == StringView("verbose"));
    REQUIRE_FALSE(cmd_line.FlagHasValue(0));
    REQUIRE(cmd_line.FlagName(1) == StringView("config"));
    REQUIRE(cmd_line.FlagHasValue(1));
    REQUIRE(cmd_line.FlagValue(1) == StringView("foo.json"));
}

TEST_CASE("CommandLine::FromNative sources args from StashNativeCommandLine", "[command_line]")
{
    ArenaAllocator arena(4096);
    const char* argv[] = {"engine_main", "native_arg", "--native_flag"};
    vse::os::StashNativeCommandLine(3, const_cast<char**>(argv));

    auto cmd_line = CommandLine<ArenaAllocator>::FromNative(arena);

    REQUIRE(cmd_line.ProgramName() == StringView("engine_main"));
    REQUIRE(cmd_line.ArgCount() == 1);
    REQUIRE(cmd_line.Arg(0) == StringView("native_arg"));
    REQUIRE(cmd_line.HasFlag(StringView("native_flag")));
}

namespace
{

using vse::Span;
using vse::os::CommandLineOption;

constexpr CommandLineOption kOptions[] = {
    {"root", true},
    {"quiet", false},
    {"out", true},
    {"keep-going", false},
};

template <typename... Args> CommandLine<ArenaAllocator> MakeDeclared(ArenaAllocator& allocator, Args... literals)
{
    const char* argv[] = {literals...};
    return CommandLine<ArenaAllocator>(allocator, static_cast<int>(sizeof...(Args)), const_cast<char**>(argv),
                                       Span<const CommandLineOption>(kOptions, 4));
}

}  // namespace

TEST_CASE("A declared option takes the next token or an =value", "[command_line][declared]")
{
    ArenaAllocator arena(8192);
    auto line = MakeDeclared(arena, "tool", "check", "--root", "docs dir", "--out=x.txt", "--quiet", "file");
    REQUIRE(line.Ok());
    StringView value;
    REQUIRE(line.TryGetOption("root", value));
    CHECK(value == StringView("docs dir"));
    REQUIRE(line.TryGetOption("out", value));
    CHECK(value == StringView("x.txt"));
    CHECK(line.HasFlag("quiet"));
    CHECK_FALSE(line.TryGetOption("quiet", value));
    REQUIRE(line.ArgCount() == 2);
    CHECK(line.Arg(0) == StringView("check"));
    CHECK(line.Arg(1) == StringView("file"));
}

TEST_CASE("A flag that takes no value does not swallow the next token", "[command_line][declared]")
{
    ArenaAllocator arena(8192);
    auto line = MakeDeclared(arena, "tool", "--quiet", "file");
    REQUIRE(line.Ok());
    CHECK(line.HasFlag("quiet"));
    REQUIRE(line.ArgCount() == 1);
    CHECK(line.Arg(0) == StringView("file"));
}

TEST_CASE("Declared options report the first problem", "[command_line][declared]")
{
    ArenaAllocator arena(8192);
    auto unknown = MakeDeclared(arena, "tool", "--bogus");
    CHECK_FALSE(unknown.Ok());
    CHECK(unknown.Error() == StringView("unknown option --bogus"));
    auto missing = MakeDeclared(arena, "tool", "--root");
    CHECK_FALSE(missing.Ok());
    CHECK(missing.Error() == StringView("--root needs a value"));
    auto extra = MakeDeclared(arena, "tool", "--quiet=yes");
    CHECK_FALSE(extra.Ok());
    CHECK(extra.Error() == StringView("--quiet takes no value"));
    auto first = MakeDeclared(arena, "tool", "--bogus", "--root");
    CHECK(first.Error() == StringView("unknown option --bogus"));  // the first one is kept
}

TEST_CASE("A bare -- ends the flags", "[command_line][declared]")
{
    ArenaAllocator arena(8192);
    auto line = MakeDeclared(arena, "tool", "--quiet", "--", "--root", "x");
    REQUIRE(line.Ok());
    CHECK(line.HasFlag("quiet"));
    CHECK_FALSE(line.HasFlag("root"));
    REQUIRE(line.ArgCount() == 2);
    CHECK(line.Arg(0) == StringView("--root"));
    CHECK(line.Arg(1) == StringView("x"));
}

TEST_CASE("Without declared options nothing is an error and values need =", "[command_line][declared]")
{
    ArenaAllocator arena(8192);
    auto line = MakeCommandLine(arena, "tool", "--root", "x");
    CHECK(line.Ok());
    CHECK(line.Error().Length() == 0);
    CHECK(line.HasFlag("root"));
    CHECK(line.ArgCount() == 1);
}

namespace
{

constexpr CommandLineOption kRepeatOptions[] = {{"link", true, true}, {"-I", true, true}, {"keep", false}};

}  // namespace

TEST_CASE("A repeatable option keeps every value and one-dash options take a value", "[command_line][declared]")
{
    ArenaAllocator arena(8192);
    const char* argv[] = {"tool", "--link", "a", "-I", "inc1", "--link=b", "-Iinc2", "--keep", "x"};
    CommandLine<ArenaAllocator> line(arena, 9, const_cast<char**>(argv),
                                     Span<const CommandLineOption>(kRepeatOptions, 3));
    REQUIRE(line.Ok());
    REQUIRE(line.OptionCount("link") == 2);
    CHECK(line.OptionAt("link", 0) == StringView("a"));
    CHECK(line.OptionAt("link", 1) == StringView("b"));
    REQUIRE(line.OptionCount("-I") == 2);
    CHECK(line.OptionAt("-I", 0) == StringView("inc1"));
    CHECK(line.OptionAt("-I", 1) == StringView("inc2"));
    CHECK(line.HasFlag("keep"));
    REQUIRE(line.ArgCount() == 1);
    CHECK(line.Arg(0) == StringView("x"));
}

TEST_CASE("A one-dash token is positional unless declared, and a declared one needs a value",
          "[command_line][declared]")
{
    ArenaAllocator arena(8192);
    const char* positional[] = {"tool", "-Z", "-1", "-"};
    CommandLine<ArenaAllocator> a(arena, 4, const_cast<char**>(positional),
                                  Span<const CommandLineOption>(kRepeatOptions, 3));
    CHECK(a.Ok());
    REQUIRE(a.ArgCount() == 3);
    CHECK(a.Arg(0) == StringView("-Z"));
    CHECK(a.Arg(1) == StringView("-1"));
    CHECK(a.Arg(2) == StringView("-"));
    const char* missing[] = {"tool", "-I"};
    CommandLine<ArenaAllocator> b(arena, 2, const_cast<char**>(missing),
                                  Span<const CommandLineOption>(kRepeatOptions, 3));
    CHECK_FALSE(b.Ok());
    CHECK(b.Error() == StringView("-I needs a value"));
}

TEST_CASE("OptionOr gives the value or the fallback", "[command_line][declared]")
{
    ArenaAllocator arena(8192);
    auto line = MakeDeclared(arena, "tool", "--root", "here", "--quiet");
    CHECK(StringView(line.OptionOr("root", "elsewhere")) == StringView("here"));
    CHECK(StringView(line.OptionOr("out", "elsewhere")) == StringView("elsewhere"));
    CHECK(line.OptionOr("quiet", nullptr) == nullptr);  // a flag without a value has none
    CHECK(line.OptionOr("out", nullptr) == nullptr);
}
