#pragma once

#include "types.h"

//! Terminal detection: is a descriptor a terminal and how many colours it shows (Maintenance: log devices).
//!
//! The rules follow the usual conventions: `NO_COLOR` (set and not empty) turns colour off, `CLICOLOR_FORCE` (set and
//! not `0`) turns it on even for a pipe, `TERM=dumb` has none, `COLORTERM=truecolor` or `24bit` is 24-bit colour, a
//! `TERM` naming `256color` is 256 colours, any other `TERM` is the basic 16. On Windows the console's virtual
//! terminal processing is switched on for the descriptor, and a console is taken as 24-bit colour when `TERM` is unset.
//! The user's choice in the settings (`log.color`) overrides all of it.
namespace vse::os
{

/// How many colours a terminal shows.
enum class TerminalColors : u8
{
    None,      ///< Not a terminal, or one that must not get escape sequences.
    Basic,     ///< 16 colours.
    Color256,  ///< 256 colours.
    TrueColor  ///< 24-bit colour.
};

/// The environment variables [[ClassifyTerminal]] reads; a null member is an unset variable. Borrowed strings.
struct TerminalEnvironment
{
    const char* no_color = nullptr;        ///< `NO_COLOR`.
    const char* clicolor_force = nullptr;  ///< `CLICOLOR_FORCE`.
    const char* term = nullptr;            ///< `TERM`.
    const char* colorterm = nullptr;       ///< `COLORTERM`.
};

/// Reports whether descriptor `fd` is an interactive terminal (a console on Windows).
///
/// On POSIX this is `isatty`; on Windows `GetConsoleMode`.
/// - Param fd: 0, 1 or 2 (standard descriptors), or a descriptor from [[os::Open]].
/// - Returns: true for a terminal; false for a pipe, a file or an invalid descriptor.
/// - Fails: returns false.
bool IsTerminal(int fd);

/// Makes descriptor `fd` accept ANSI escape sequences, if it needs a switch for that.
///
/// Windows sets `ENABLE_VIRTUAL_TERMINAL_PROCESSING` on the console; POSIX terminals always accept them.
/// - Param fd: a terminal descriptor.
/// - Returns: true when escape sequences will work.
/// - Fails: returns false.
bool EnableTerminalSequences(int fd);

/// Reads the four variables of [[TerminalEnvironment]] from the process environment. The pointers are borrowed from
/// the environment and valid until it changes.
/// - Thread-safety: thread-compatible, as [[os::GetEnvironment]].
TerminalEnvironment ReadTerminalEnvironment();

/// The rules above as a pure function: no system call, so every case is testable.
/// - Param is_terminal: whether the descriptor is a terminal.
/// - Param env: the variables.
/// - Returns: the colours to use, `None` for none.
/// - Fails: never.
TerminalColors ClassifyTerminal(bool is_terminal, const TerminalEnvironment& env);

/// The colours descriptor `fd` shows.
///
/// It is [[IsTerminal]], [[ReadTerminalEnvironment]] and [[ClassifyTerminal]] together; on Windows it also enables
/// virtual terminal processing for a console (`None` if that fails).
/// - Param fd: the descriptor to ask about.
/// - Returns: the colours, `None` for none.
/// - Fails: returns `None`.
TerminalColors DetectTerminalColors(int fd);

}  // namespace vse::os
