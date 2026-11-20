#pragma once

//! [[net::TcpStream]]: a connected TCP socket that sends and receives length-prefixed frames with bounded memory, and
//! reports why it ended.
//!
//! Sending is a bounded queue: [[net::TcpStream::Send]] puts a frame in it and answers `LimitExceeded` when it is full
//! instead of growing (the backpressure is a value), and [[net::TcpStream::Flush]] drains it to the socket, waiting
//! whenever the socket is full; awaiting it is the "writable again" signal. Receiving is
//! [[net::TcpStream::ReceiveFrame]]: it reads into a buffer of fixed size and decodes frames with a
//! [[net::FrameReader]], so a frame can arrive in pieces of any size. A frame whose length is zero or above the limit
//! closes the stream with
//! [[net::CloseReason::ProtocolError]] before any memory is spent on it.
//!
//! Every end of a stream has a [[net::CloseReason]]: the peer closed (cleanly, at a frame boundary), the peer reset the
//! connection, a timeout fired (a connection that sends nothing for the idle time is half-open or dead), the owner
//! closed it, or the peer broke the framing. The first reason is kept.
//!
//! One coroutine may receive and one may flush at a time; they may run in parallel with each other.
//! [[net::TcpStream::Send]] may be called from any thread. [[net::TcpStream::Close]] and [[net::TcpStream::Abort]] are
//! for when no receive or flush is outstanding (cancel them with their [[net::CancelToken]] and wait for them first).

#include "containers/span.h"
#include "containers/vector.h"
#include "jobs/task.h"
#include "net/async.h"
#include "net/endpoint.h"
#include "net/frame.h"
#include "net/reactor.h"
#include "net/socket.h"
#include "net/status.h"
#include "sync/atomic.h"
#include "sync/mutex.h"
#include "types.h"

namespace vse::net
{

/// Why a [[net::TcpStream]] ended.
enum class CloseReason : u8
{
    None,           ///< Still open.
    PeerClosed,     ///< The peer closed its sending side at a frame boundary: an orderly end.
    Reset,          ///< The connection was reset or broken (also: a write to a peer that was gone).
    TimedOut,       ///< Nothing was received (or the peer took no data) for the idle time.
    LocalClose,     ///< [[net::TcpStream::Close]] or [[net::TcpStream::Abort]] ended it.
    ProtocolError,  ///< The peer broke the framing: a zero or oversized length, or the stream ended inside a frame.
};

/// The name of a close reason, for logs and tests.
/// - Param reason: the reason.
/// - Ownership: the returned string is static.
/// - Returns: a static string.
const char* CloseReasonName(CloseReason reason);

/// How a [[net::TcpStream]] is set up.
///
/// - Thread-safety: thread-compatible plain data.
struct TcpStreamOptions
{
    usize max_frame_bytes = 1u << 20;        ///< The largest frame payload, both ways.
    usize receive_buffer_bytes = 64 * 1024;  ///< Bytes read from the socket per call; the stream's read buffer.
    usize send_queue_bytes = 256 * 1024;     ///< The capacity of the send queue, headers included; never exceeded.
    usize max_read_bytes = 0;          ///< If not zero, ask the socket for at most this many bytes per read (tests).
    i64 idle_timeout_nanos = -1;       ///< Longest wait for incoming (or outgoing) bytes; negative for none.
    i64 connect_timeout_nanos = -1;    ///< Longest connect; negative for none.
    i64 linger_nanos = 5'000'000'000;  ///< Longest [[net::TcpStream::Close]] waits for the queue and the peer.
    usize socket_receive_buffer_bytes = 0;  ///< If not zero, the size asked of the system for the socket's receive
                                            ///< buffer.
    usize socket_send_buffer_bytes = 0;     ///< Likewise for the send buffer.
    bool no_delay = true;                   ///< `TCP_NODELAY`.
    bool keep_alive = false;                ///< Turn on TCP keep-alive probes.
    u32 keep_alive_idle_seconds = 0;  ///< Probe timing (0 for the system's); see [[net::Socket::SetKeepAliveTiming]].
    u32 keep_alive_interval_seconds = 0;  ///< Seconds between probes (0 for the system's).
    u32 keep_alive_probes = 0;            ///< Unanswered probes before the connection is reset (0 for the system's).
};

/// A framed, bounded TCP stream.
///
/// - Thread-safety: thread-safe for [[net::TcpStream::Send]], [[net::TcpStream::SendQueueBytes]] and
/// [[net::TcpStream::Reason]]; one coroutine receives and one flushes at a time (see the file comment).
/// - Ownership: owns the socket and closes it through the reactor in the destructor (the reactor must outlive the
/// stream). It cannot be moved: the coroutines point at it.
class TcpStream
{
public:
    /// Creates a stream that is not open.
    TcpStream() = default;

    /// Closes the socket (abortively if it is still open).
    ~TcpStream();

    TcpStream(const TcpStream&) = delete;
    TcpStream& operator=(const TcpStream&) = delete;

    /// Makes `stream` an open stream connected to `remote`.
    /// - Param reactor: the reactor to wait on; must outlive the stream.
    /// - Param remote: where to connect.
    /// - Param options: setup; the connect timeout applies here.
    /// - Param stream: receives the connection; must not be open.
    /// - Param wait: the token that can cancel the connect.
    /// - Returns: success, or why not.
    /// - Fails: [[net::ErrorCode::Refused]], [[net::ErrorCode::TimedOut]], [[net::ErrorCode::Cancelled]],
    /// [[net::ErrorCode::Unreachable]], [[net::ErrorCode::Invalid]] for an open `stream`, [[net::ErrorCode::Other]].
    /// - Ownership: `reactor`, `remote`, `options` and `stream` are borrowed until the `co_await` returns.
    static jobs::Task<Status> Connect(Reactor& reactor, const Endpoint& remote, const TcpStreamOptions& options,
                                      TcpStream* stream, CancelToken* wait = nullptr);

    /// Takes over a connected socket (one a [[net::Listener]] accepted, say).
    /// - Param reactor: the reactor to wait on; must outlive the stream.
    /// - Param socket: a connected stream socket; moved from on success.
    /// - Param options: setup.
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::Invalid]] if `socket` is not an open stream socket or this stream is open,
    /// [[net::ErrorCode::LimitExceeded]] for sizes of zero, [[net::ErrorCode::Other]] if memory ran out.
    Status Adopt(Reactor& reactor, Socket&& socket, const TcpStreamOptions& options);

    /// Tells whether the stream holds an open socket (also after it failed, until it is closed).
    /// - Returns: true while open.
    bool IsOpen() const { return socket_.IsOpen(); }

    /// Why the stream ended.
    /// - Returns: [[net::CloseReason::None]] while it is healthy.
    CloseReason Reason() const { return static_cast<CloseReason>(reason_.Load(MemoryOrder::Acquire)); }

    /// Queues one frame for sending.
    /// - Param payload: the frame's bytes; copied.
    /// - Returns: success (nothing is sent until [[net::TcpStream::Flush]] runs).
    /// - Fails: [[net::ErrorCode::LimitExceeded]] if the frame is above `max_frame_bytes` or the queue has no room
    /// for it and its header (the frame is not queued; flush and try again), [[net::ErrorCode::Invalid]] for an empty
    /// frame, [[net::ErrorCode::Closed]] after the stream ended for any reason but [[net::CloseReason::PeerClosed]]
    /// (the peer closing its side does not stop us sending), or once [[net::TcpStream::Close]] or
    /// [[net::TcpStream::Abort]] began.
    Status Send(ByteSpan payload);

    /// The bytes waiting in the send queue.
    /// - Returns: the count, at most the capacity.
    usize SendQueueBytes() const;

    /// The capacity of the send queue.
    /// - Returns: the bytes it holds at most.
    usize SendQueueCapacity() const { return queue_capacity_; }

    /// The memory the send queue holds now: a few KiB for an idle stream, doubling as frames are queued, never above
    /// the capacity.
    /// - Returns: the bytes allocated for the queue.
    usize SendQueueAllocatedBytes() const;

    /// Writes queued bytes to the socket until at most `keep_queued` remain, waiting whenever the socket is full.
    /// Awaiting `Flush(0)` is "everything is on its way"; awaiting `Flush(capacity - n)` is "there is room for `n`
    /// more bytes".
    /// - Param keep_queued: how many bytes may stay in the queue.
    /// - Param options: token and timeout of the whole call (a timeout here leaves the stream open).
    /// - Returns: success, or why not. A socket error ends the stream with its reason; the idle timeout (no byte taken
    /// by the peer for that long) ends it with [[net::CloseReason::TimedOut]].
    /// - Fails: [[net::ErrorCode::TimedOut]], [[net::ErrorCode::Cancelled]], [[net::ErrorCode::Reset]],
    /// [[net::ErrorCode::Closed]], [[net::ErrorCode::Invalid]] if another flush is running.
    jobs::Task<Status> Flush(usize keep_queued = 0, WaitOptions options = {});

    /// Waits for room in the queue, queues the frame and flushes the queue: the form of sending that waits instead of
    /// failing when the queue is full.
    /// - Param payload: the frame's bytes; copied.
    /// - Param options: token and timeout of the whole call.
    /// - Returns: success once the socket has taken every queued byte, this frame included.
    /// - Fails: those of [[net::TcpStream::Send]] (except a full queue) and [[net::TcpStream::Flush]].
    jobs::Task<Status> SendFrame(ByteSpan payload, WaitOptions options = {});

    /// Receives the next frame. The previous frame is released first.
    /// - Param options: token and overall timeout of the call; running out of it returns TimedOut and leaves the stream
    /// open (the idle timeout of the stream, in contrast, ends it).
    /// - Returns: success (the frame is [[net::TcpStream::Frame]]), or why not. The end of the stream at a frame
    /// boundary is `Closed` with [[net::CloseReason::PeerClosed]].
    /// - Fails: [[net::ErrorCode::Closed]], [[net::ErrorCode::Reset]], [[net::ErrorCode::TimedOut]],
    /// [[net::ErrorCode::Cancelled]], [[net::ErrorCode::LimitExceeded]] (oversized frame),
    /// [[net::ErrorCode::Invalid]] (zero-length frame).
    jobs::Task<Status> ReceiveFrame(WaitOptions options = {});

    /// The frame [[net::TcpStream::ReceiveFrame]] last returned.
    /// - Returns: the payload, valid until the next call to [[net::TcpStream::ReceiveFrame]].
    ByteSpan Frame() const { return reader_.Frame(); }

    /// Closes gracefully: flushes the queue, tells the peer no more is coming, reads until the peer does the same
    /// (discarding what it sends), then closes the socket. The wait is bounded by `linger_nanos`; if it runs out the
    /// close becomes abortive.
    /// - Returns: success when the shutdown was orderly, [[net::ErrorCode::TimedOut]] when the linger time ran out
    /// (the socket is closed anyway).
    jobs::Task<Status> Close();

    /// Closes at once: unsent data is dropped and the peer sees a reset.
    /// - Returns: success, or the socket's failure.
    Status Abort();

private:
    // Records the first reason; later ones are ignored. Returns the reason that stands.
    CloseReason SetReason(CloseReason reason);
    // Ends the stream because of `reason`: the reason is kept, the connection is shut so waiting operations return,
    // and it will be reset when closed (unless the peer already did so).
    Status Fail(CloseReason reason, ErrorCode code);
    // The options for the next wait of an operation: the idle time and what is left of the call's budget. Returns
    // false when the call's budget is spent. `idle_governs` tells which of the two ends the wait first.
    bool NextWait(const class TimeBudget& budget, WaitOptions* out, bool* idle_governs) const;
    // Copies the next piece of the queue's head into the scratch buffer; returns its size.
    usize PeekQueue();
    void DropQueue(usize bytes);
    // Reads from the socket and discards, until the peer's end or the time is up.
    jobs::Task<Status> DrainPeer(WaitOptions options);

    Reactor* reactor_ = nullptr;
    Socket socket_;
    TcpStreamOptions options_;
    Atomic<u8> reason_{0};
    Atomic<bool> flushing_{false};
    Atomic<bool> send_closed_{false};
    bool read_eof_ = false;
    // The receive side.
    Vector<byte> read_buffer_;
    usize read_begin_ = 0;
    usize read_end_ = 0;
    FrameReader reader_{0};
    // The send queue: a ring of fixed capacity.
    mutable Mutex queue_lock_;
    Vector<byte> queue_;
    usize queue_capacity_ = 0;   // the most the queue may hold
    usize queue_allocated_ = 0;  // the size of the ring now; grows by doubling up to the capacity
    usize queue_head_ = 0;
    usize queue_size_ = 0;
    Vector<byte> scratch_;  // the piece being written, so the lock is not held while writing
    FrameWriter writer_{0};
};

}  // namespace vse::net
