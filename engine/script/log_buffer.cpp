#include "script/log_buffer.h"

#include "cstr/cstr.h"
#include "cstr/format.h"

namespace vse::script
{

LogChannel LogScript("LogScript", LogVerbosity::Info);

LogVerbosity LogVerbosityOf(ScriptLogLevel level)
{
    switch (level)
    {
    case ScriptLogLevel::Debug: return LogVerbosity::Debug;
    case ScriptLogLevel::Info: return LogVerbosity::Info;
    case ScriptLogLevel::Warn: return LogVerbosity::Warn;
    case ScriptLogLevel::Error: return LogVerbosity::Error;
    }
    return LogVerbosity::Info;
}

namespace
{

StringView Cut(StringView text)
{
    return text.Length() > kMaxScriptLogText ? StringView(text.Data(), kMaxScriptLogText) : text;
}

}  // namespace

void WriteScriptLog(ScriptLogLevel level, StringView script, u32 line, StringView text)
{
    String<HeapAllocator> out(HeapAllocator::Default());
    out.Append('[');
    out.Append(script.Length() > 0 ? script : StringView("?"));
    if (line != 0)
    {
        char number[16];
        FormatTo(number, sizeof(number), ":%u", static_cast<unsigned>(line));
        out.Append(StringView(number, CStrLength(number)));
    }
    out.Append("] ");
    out.Append(Cut(text));
    Log(LogScript, LogVerbosityOf(level), out.View());
}

LogBuffer::LogBuffer(u32 limit_per_script) : limit_(limit_per_script) {}

LogBuffer::Count* LogBuffer::CountFor(StringView script)
{
    for (usize i = 0; i < counts_.Size(); ++i)
    {
        if (counts_[i].script.View() == script)
        {
            return &counts_[i];
        }
    }
    if (counts_.Size() >= kMaxLogScripts)
    {
        return &counts_[kMaxLogScripts - 1];  // the last counter is shared by every further name
    }
    Count count;
    count.script = String<HeapAllocator>(HeapAllocator::Default());
    count.script.Append(script);
    counts_.Push(Move(count));
    return &counts_[counts_.Size() - 1];
}

bool LogBuffer::Add(ScriptLogLevel level, StringView script, u32 line, StringView text)
{
    Count* count = CountFor(script);
    if (limit_ != 0 && count->used >= limit_)
    {
        ++count->dropped;
        return false;
    }
    ++count->used;
    Entry entry;
    entry.level = level;
    entry.line = line;
    entry.script = String<HeapAllocator>(HeapAllocator::Default());
    entry.script.Append(script);
    entry.text = String<HeapAllocator>(HeapAllocator::Default());
    entry.text.Append(Cut(text));
    entries_.Push(Move(entry));
    return true;
}

u64 LogBuffer::DroppedSinceFlush() const
{
    u64 total = 0;
    for (usize i = 0; i < counts_.Size(); ++i)
    {
        total += counts_[i].dropped;
    }
    return total;
}

usize LogBuffer::Flush()
{
    usize written = 0;
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        WriteScriptLog(entries_[i].level, entries_[i].script.View(), entries_[i].line, entries_[i].text.View());
        ++written;
    }
    for (usize i = 0; i < counts_.Size(); ++i)
    {
        if (counts_[i].dropped != 0)
        {
            char text[96];
            FormatTo(text, sizeof(text), "%u more messages dropped (limit %u per tick)",
                     static_cast<unsigned>(counts_[i].dropped), static_cast<unsigned>(limit_));
            WriteScriptLog(ScriptLogLevel::Warn, counts_[i].script.View(), 0, StringView(text, CStrLength(text)));
            ++written;
        }
    }
    entries_.Clear();
    counts_.Clear();
    return written;
}

usize FlushLogBuffers(Span<LogBuffer* const> buffers)
{
    usize written = 0;
    for (usize i = 0; i < buffers.Size(); ++i)
    {
        if (buffers[i] != nullptr)
        {
            written += buffers[i]->Flush();
        }
    }
    return written;
}

}  // namespace vse::script
