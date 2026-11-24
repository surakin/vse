// The per-call bookkeeping every hardware device backend keeps for `DeviceCounters`: how many render calls it made,
// how many took longer than the audio they rendered lasts, and the longest one. One copy for ALSA, PipeWire, WASAPI
// and CoreAudio.
#pragma once

#include "audio/device.h"
#include "os/os.h"
#include "sync/atomic.h"
#include "types.h"

namespace vse::audio
{

/// The render-call counters of a device: written by the device's render thread, read by the control thread.
///
/// - Thread-safety: thread-safe; `Record` is lock-free and allocates nothing (the audio thread's rules), `Fill` and
///   `Reset` may run on any thread.
class CallbackCounters
{
public:
    /// The monotonic clock in microseconds, for timing a render call.
    ///
    /// - Returns: the time.
    /// - Fails: never.
    static u64 NowMicros() { return os::MonotonicTimeNanos() / 1000; }

    /// Clears the counters (when a device opens).
    ///
    /// - Fails: never.
    void Reset()
    {
        callbacks_.Store(0);
        slow_.Store(0);
        longest_.Store(0);
    }

    /// Counts one render call (the device's render thread).
    ///
    /// - Param took_micros: how long the call took.
    /// - Param budget_micros: how long the frames it rendered last; a longer call is counted as slow.
    /// - Fails: never; allocates nothing.
    void Record(u64 took_micros, u64 budget_micros)
    {
        callbacks_.FetchAdd(1);
        if (took_micros > budget_micros)
        {
            slow_.FetchAdd(1);
        }
        u64 longest = longest_.Load();
        while (took_micros > longest && !longest_.CompareExchangeWeak(longest, took_micros))
        {
        }
    }

    /// Writes the render calls, the slow ones and the longest into `out` (the other fields are left as they are).
    ///
    /// - Param out: the counters to fill.
    /// - Fails: never.
    /// - Ownership: `out` is borrowed for the call.
    void Fill(DeviceCounters* out) const
    {
        out->callbacks = callbacks_.Load();
        out->slow_callbacks = slow_.Load();
        out->longest_callback_micros = longest_.Load();
    }

private:
    Atomic<u64> callbacks_{0};
    Atomic<u64> slow_{0};
    Atomic<u64> longest_{0};
};

}  // namespace vse::audio
