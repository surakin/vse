#pragma once

#include "containers/vector.h"
#include "types.h"

//! Non-blocking transfer of clipboard bytes through a pipe, for the Wayland data device: the other program reads or writes the other end at its own pace.

namespace engine_placeholder::os {

/// Writes a byte string into a pipe's write end without ever blocking, a piece at a time from [[os::PipeWriter::Pump]], and closes it when done.
///
/// A peer that reads slowly leaves the pipe full; `Pump` then returns and tries again on the next call. The transfer ends with `Done`, `Failed` (the peer closed early) or
/// `TimedOut` (no byte moved for the timeout), and the descriptor is closed on every ending and on destruction.
///
/// Thread-safety: not-thread-safe; owner-thread only.
/// Ownership: owns the descriptor and a copy of the bytes.
class PipeWriter {
public:
    /// The state of the transfer.
    enum class State : u8 { Idle, Writing, Done, Failed, TimedOut };

    PipeWriter() = default;
    ~PipeWriter() { Close(); }
    PipeWriter(const PipeWriter&) = delete;
    PipeWriter& operator=(const PipeWriter&) = delete;
    PipeWriter(PipeWriter&& other) noexcept;
    PipeWriter& operator=(PipeWriter&& other) noexcept;

    /// Starts a transfer; takes ownership of `fd` (closed on any ending). The descriptor is made non-blocking.
    /// Param fd: the write end of a pipe.
    /// Param bytes: the data; copied.
    /// Param size: its count.
    /// Param now_ns: the time, for the timeout.
    /// Param timeout_ns: how long without progress ends the transfer.
    void Start(int fd, const byte* bytes, usize size, u64 now_ns, u64 timeout_ns);

    /// Writes what the pipe takes now. Returns the state afterwards.
    State Pump(u64 now_ns);

    /// Returns the state without doing anything.
    State GetState() const { return state_; }

    /// Closes the descriptor and drops the data; the state is kept.
    void Close();

private:
    int fd_ = -1;
    Vector<byte> data_;
    usize offset_ = 0;
    u64 last_progress_ns_ = 0;
    u64 timeout_ns_ = 0;
    State state_ = State::Idle;
};

/// Reads a pipe's read end without blocking, a piece at a time from [[os::PipeReader::Pump]], until the peer closes it.
///
/// Ends with `Done` (end of file; the bytes are in `Bytes`), `TooLarge` (more than the cap arrived), `Failed` (a read error) or `TimedOut` (no byte arrived for the timeout).
///
/// Thread-safety: not-thread-safe; owner-thread only.
/// Ownership: owns the descriptor and the bytes read so far.
class PipeReader {
public:
    /// The state of the transfer.
    enum class State : u8 { Idle, Reading, Done, TooLarge, Failed, TimedOut };

    PipeReader() = default;
    ~PipeReader() { Close(); }
    PipeReader(const PipeReader&) = delete;
    PipeReader& operator=(const PipeReader&) = delete;
    PipeReader(PipeReader&& other) noexcept;
    PipeReader& operator=(PipeReader&& other) noexcept;

    /// Starts a transfer; takes ownership of `fd` (closed on any ending). The descriptor is made non-blocking.
    /// Param fd: the read end of a pipe.
    /// Param cap: the most bytes accepted.
    /// Param now_ns: the time, for the timeout.
    /// Param timeout_ns: how long without a byte ends the transfer.
    void Start(int fd, usize cap, u64 now_ns, u64 timeout_ns);

    /// Reads what the pipe holds now. Returns the state afterwards.
    State Pump(u64 now_ns);

    /// Returns the state without doing anything.
    State GetState() const { return state_; }

    /// Returns the bytes read so far.
    Vector<byte>& Bytes() { return data_; }

    /// Closes the descriptor; the bytes and state are kept.
    void Close();

private:
    int fd_ = -1;
    Vector<byte> data_;
    usize cap_ = 0;
    u64 last_progress_ns_ = 0;
    u64 timeout_ns_ = 0;
    State state_ = State::Idle;
};

} // namespace engine_placeholder::os
