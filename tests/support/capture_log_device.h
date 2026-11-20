#pragma once

#include "containers/vector.h"
#include "cstr/mem_ops.h"
#include "log/log_device.h"
#include "string_view.h"
#include "sync/mutex.h"
#include "types.h"

// A log device that keeps every line it receives, for tests that read what was logged (instead of replacing the write
// sink, which the crash and assert tests still do because they test the sink itself).
namespace engine_test
{

using namespace vse;

// Fixed storage instead of a String: a String needs an allocator.
struct Text
{
    char data[5200] = {};
    usize length = 0;
    Text() = default;
    explicit Text(StringView v)
    {
        length = v.Length() < sizeof(data) ? v.Length() : sizeof(data);
        MemCopy(data, v.Data(), length);
    }
    void Append(StringView v)
    {
        for (usize i = 0; i < v.Length() && length < sizeof(data); ++i)
        {
            data[length++] = v[i];
        }
    }
    StringView View() const { return StringView(data, length); }
};

struct Seen
{
    Text channel;
    LogVerbosity level;
    Text message;
    Text line;
    Text trace;
    u64 timestamp;
};

class CaptureLogDevice final : public LogDevice
{
public:
    void Write(const LogRecord& r) override
    {
        LockGuard<Mutex> lock(mutex_);
        seen_.Push(
            Seen{Text(StringView(r.channel)), r.level, Text(r.message), Text(r.line), Text(r.trace), r.timestamp_ns});
    }
    void Flush() override { ++flushes_; }
    usize Count()
    {
        LockGuard<Mutex> lock(mutex_);
        return seen_.Size();
    }
    Seen& At(usize i) { return seen_[i]; }
    void Clear()
    {
        LockGuard<Mutex> lock(mutex_);
        seen_.Clear();
    }
    int flushes_ = 0;

private:
    Mutex mutex_;
    Vector<Seen> seen_;
};

}  // namespace engine_test
