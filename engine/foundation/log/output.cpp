#include "log/output.h"

#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "cstr/varargs.h"
#include "log/log_device.h"
#include "string_view.h"

namespace vse::out
{

namespace
{

constexpr char kCutMarker[] = "...[cut]\n";

int FormatAndEmit(int stream, const char* format, VarArgs args)
{
    char buffer[kMaxFormattedBytes];
    const int needed = VFormatTo(buffer, sizeof(buffer), format, args);
    if (needed < 0)
    {
        return needed;
    }
    usize length = static_cast<usize>(needed);
    if (length >= sizeof(buffer))
    {
        const usize marker = sizeof(kCutMarker) - 1;
        length = sizeof(buffer) - 1;
        MemCopy(buffer + length - marker, kCutMarker, marker);
    }
    detail::EmitOutput(StringView(buffer, length), stream);
    return needed;
}

void EmitText(const char* text, usize length, int stream)
{
    // One record per chunk, cut at the last newline inside it so a line is not split without need.
    while (length > 0)
    {
        usize chunk = length < kMaxFormattedBytes ? length : kMaxFormattedBytes;
        if (chunk < length)
        {
            for (usize i = chunk; i > 0; --i)
            {
                if (text[i - 1] == '\n')
                {
                    chunk = i;
                    break;
                }
            }
        }
        detail::EmitOutput(StringView(text, chunk), stream);
        text += chunk;
        length -= chunk;
    }
}

}  // namespace

int Print(const char* format, ...)
{
    VarArgs args;
    ENGINE_VA_START(args, format);
    const int written = FormatAndEmit(1, format, args);
    ENGINE_VA_END(args);
    return written;
}

int PrintError(const char* format, ...)
{
    VarArgs args;
    ENGINE_VA_START(args, format);
    const int written = FormatAndEmit(2, format, args);
    ENGINE_VA_END(args);
    return written;
}

void PrintText(const char* text)
{
    EmitText(text, CStrLength(text), 1);
}

void PrintErrorText(const char* text)
{
    EmitText(text, CStrLength(text), 2);
}

void PrintLine(const char* text)
{
    const usize length = CStrLength(text);
    if (length < kMaxFormattedBytes)
    {
        // Text and newline in one record, so concurrent writers cannot put another line between them.
        char buffer[kMaxFormattedBytes];
        MemCopy(buffer, text, length);
        buffer[length] = '\n';
        detail::EmitOutput(StringView(buffer, length + 1), 1);
        return;
    }
    EmitText(text, length, 1);
    detail::EmitOutput(StringView("\n", 1), 1);
}

usize PrintBytes(const void* data, usize size)
{
    EmitText(static_cast<const char*>(data), size, 1);
    return size;
}

usize PrintErrorBytes(const void* data, usize size)
{
    EmitText(static_cast<const char*>(data), size, 2);
    return size;
}

void Flush()
{
    FlushLogDevices();
}

void FlushError()
{
    FlushLogDevices();
}

}  // namespace vse::out
