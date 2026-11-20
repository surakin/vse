#include <catch2/catch_test_macros.hpp>

#include "os/os.h"
#include "os/terminal.h"

using namespace vse::os;

namespace
{
TerminalEnvironment Env(const char* term, const char* colorterm = nullptr, const char* no_color = nullptr,
                        const char* force = nullptr)
{
    TerminalEnvironment e;
    e.term = term;
    e.colorterm = colorterm;
    e.no_color = no_color;
    e.clicolor_force = force;
    return e;
}
}  // namespace

TEST_CASE("a terminal shows the colours its environment names", "[os][terminal]")
{
    CHECK(ClassifyTerminal(true, Env("xterm-256color")) == TerminalColors::Color256);
    CHECK(ClassifyTerminal(true, Env("xterm")) == TerminalColors::Basic);
    CHECK(ClassifyTerminal(true, Env("xterm", "truecolor")) == TerminalColors::TrueColor);
    CHECK(ClassifyTerminal(true, Env("xterm", "24bit")) == TerminalColors::TrueColor);
    CHECK(ClassifyTerminal(true, Env("screen-256color", "truecolor")) == TerminalColors::TrueColor);
}

TEST_CASE("something that is not a terminal shows no colour", "[os][terminal]")
{
    CHECK(ClassifyTerminal(false, Env("xterm-256color", "truecolor")) == TerminalColors::None);
    CHECK(ClassifyTerminal(true, Env("dumb")) == TerminalColors::None);
}

TEST_CASE("NO_COLOR turns colour off even when forced", "[os][terminal]")
{
    CHECK(ClassifyTerminal(true, Env("xterm-256color", nullptr, "1")) == TerminalColors::None);
    CHECK(ClassifyTerminal(true, Env("xterm-256color", nullptr, "1", "1")) == TerminalColors::None);
    CHECK(ClassifyTerminal(true, Env("xterm-256color", nullptr, "")) == TerminalColors::Color256);  // empty is unset
}

TEST_CASE("CLICOLOR_FORCE turns colour on for a pipe", "[os][terminal]")
{
    CHECK(ClassifyTerminal(false, Env("xterm-256color", nullptr, nullptr, "1")) == TerminalColors::Color256);
    CHECK(ClassifyTerminal(false, Env(nullptr, nullptr, nullptr, "1")) == TerminalColors::Basic);
    CHECK(ClassifyTerminal(false, Env("xterm-256color", nullptr, nullptr, "0")) == TerminalColors::None);
}

TEST_CASE("a terminal without TERM gets the basic colours", "[os][terminal]")
{
    CHECK(ClassifyTerminal(true, Env(nullptr)) == TerminalColors::Basic);
}

TEST_CASE("an invalid descriptor is not a terminal", "[os][terminal]")
{
    CHECK_FALSE(IsTerminal(-1));
    CHECK(DetectTerminalColors(-1) == TerminalColors::None);
}
