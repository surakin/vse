#include "log/tty_log_device.h"

#include "cstr/cstr.h"
#include "cstr/mem_ops.h"

namespace vse
{

namespace
{

const char* LevelColor(LogVerbosity level)
{
    switch (level)
    {
    case LogVerbosity::Error: return "\x1b[1;31m";
    case LogVerbosity::Warn: return "\x1b[33m";
    case LogVerbosity::Info: return "";
    case LogVerbosity::Debug:
    case LogVerbosity::Trace: return "\x1b[2m";
    }
    return "";
}

usize Append(char* out, usize offset, const char* text, usize length)
{
    MemCopy(out + offset, text, length);
    return offset + length;
}

}  // namespace

void TtyLogDevice::Write(const LogRecord& record)
{
    // The line is at most 512 bytes and a control byte grows to two, so 1100 hold any coloured line.
    constexpr usize kLineRoom = 1100;
    char out[kLineRoom + kLogTraceBytes];
    usize n = 0;
    const char* color = LevelColor(record.level);
    const usize color_length = CStrLength(color);
    n = Append(out, n, color, color_length);

    // Without the newline: the colour is reset before it.
    usize body = record.line.Length();
    if (body > 0 && record.line[body - 1] == '\n')
    {
        --body;
    }
    // Room kept for the reset, the newline and a two-byte escape; a longer line is cut.
    const usize limit = kLineRoom - 8;
    for (usize i = 0; i < body && n + 2 <= limit; ++i)
    {
        const u8 c = static_cast<u8>(record.line[i]);
        if ((c < 0x20 && c != '\t' && c != '\n') || c == 0x7f)
        {
            out[n++] = '^';
            out[n++] = c == 0x7f ? '?' : static_cast<char>(c + 64);
        }
        else
        {
            out[n++] = static_cast<char>(c);
        }
    }
    if (color_length != 0)
    {
        n = Append(out, n, "\x1b[0m", 4);
    }
    out[n++] = '\n';
    const usize trace = record.trace.Length() < kLogTraceBytes ? record.trace.Length() : kLogTraceBytes;
    n = Append(out, n, record.trace.Data(), trace);
    WriteLogConsoleBytes(out, n);
}

}  // namespace vse
