#pragma once

#include "log/log_device.h"

namespace vse
{

/// The coloured console device, for a colour-capable terminal.
///
/// It writes the line of [[StdoutLogDevice]] with an ANSI colour per level (Error bold red, Warn yellow, Info plain,
/// Debug and Trace dim), reset before the newline.
///
/// Control bytes in a message (ESC and the rest below space, except tab and newline, and DEL) are shown as `^X`, so a
/// logged string cannot send commands to the terminal. The trace follows the line, uncoloured, in the same write.
/// - Thread-safety: thread-safe (one write per line).
class TtyLogDevice final : public LogDevice
{
public:
    void Write(const LogRecord& record) override;
};

}  // namespace vse
