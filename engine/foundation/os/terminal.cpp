#include "os/terminal.h"

#include "build_defs.h"
#include "os/console.h"
#include "string_view.h"

namespace vse::os
{

namespace
{

bool IsSetAndNotEmpty(const char* value)
{
    return value != nullptr && value[0] != '\0';
}

}  // namespace

TerminalEnvironment ReadTerminalEnvironment()
{
    TerminalEnvironment env;
    env.no_color = GetEnvironment("NO_COLOR");
    env.clicolor_force = GetEnvironment("CLICOLOR_FORCE");
    env.term = GetEnvironment("TERM");
    env.colorterm = GetEnvironment("COLORTERM");
    return env;
}

TerminalColors ClassifyTerminal(bool is_terminal, const TerminalEnvironment& env)
{
    if (IsSetAndNotEmpty(env.no_color))
    {
        return TerminalColors::None;
    }
    const bool forced = IsSetAndNotEmpty(env.clicolor_force) && StringView(env.clicolor_force) != StringView("0");
    if (!is_terminal && !forced)
    {
        return TerminalColors::None;
    }
    const StringView term = env.term != nullptr ? StringView(env.term) : StringView();
    if (term == StringView("dumb"))
    {
        return TerminalColors::None;
    }
    const StringView colorterm = env.colorterm != nullptr ? StringView(env.colorterm) : StringView();
    if (colorterm == StringView("truecolor") || colorterm == StringView("24bit"))
    {
        return TerminalColors::TrueColor;
    }
    if (term.Contains(StringView("256color")))
    {
        return TerminalColors::Color256;
    }
    return TerminalColors::Basic;
}

TerminalColors DetectTerminalColors(int fd)
{
    const bool terminal = IsTerminal(fd);
    TerminalEnvironment env = ReadTerminalEnvironment();
    if (terminal && !EnableTerminalSequences(fd))
    {
        return TerminalColors::None;
    }
#if ENGINE_PLATFORM_WINDOWS
    if (terminal)
    {
        if (env.term == nullptr && env.colorterm == nullptr)
        {
            env.colorterm = "truecolor";  // Windows consoles and Windows Terminal show 24-bit colour
        }
    }
#endif
    return ClassifyTerminal(terminal, env);
}

}  // namespace vse::os
