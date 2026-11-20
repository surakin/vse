#include "net/tcp_stream.h"

#include "cstr/mem_ops.h"
#include "lang/status_try.h"
#include "net/time_budget.h"
#include "sync/mutex.h"

namespace vse::net
{

namespace
{

constexpr usize kScratchBytes = 16 * 1024;
constexpr usize kInitialQueueBytes = 4 * 1024;
constexpr usize kDrainBytes = 4096;

}  // namespace

const char* CloseReasonName(CloseReason reason)
{
    switch (reason)
    {
    case CloseReason::None: return "None";
    case CloseReason::PeerClosed: return "PeerClosed";
    case CloseReason::Reset: return "Reset";
    case CloseReason::TimedOut: return "TimedOut";
    case CloseReason::LocalClose: return "LocalClose";
    case CloseReason::ProtocolError: return "ProtocolError";
    }
    return "Unknown";
}

TcpStream::~TcpStream()
{
    if (socket_.IsOpen())
    {
        [[maybe_unused]] const Status closed = Abort();
    }
}

CloseReason TcpStream::SetReason(CloseReason reason)
{
    u8 expected = static_cast<u8>(CloseReason::None);
    if (reason_.CompareExchangeStrong(expected, static_cast<u8>(reason), MemoryOrder::AcqRel, MemoryOrder::Acquire))
    {
        return reason;
    }
    return static_cast<CloseReason>(expected);
}

Status TcpStream::Fail(CloseReason reason, ErrorCode code)
{
    SetReason(reason);
    send_closed_.Store(true, MemoryOrder::Release);
    if (reason != CloseReason::Reset)
    {
        // The peer is not known to be gone: the close that follows is a reset, not a second orderly end.
        [[maybe_unused]] const Status linger = socket_.SetLinger(true, 0);
    }
    // Wakes the operation on the other direction; the descriptor itself stays until Close or Abort.
    [[maybe_unused]] const Status shut = socket_.Shutdown(ShutdownMode::Both);
    return Status::Of(code);
}

Status TcpStream::Adopt(Reactor& reactor, Socket&& socket, const TcpStreamOptions& options)
{
    if (socket_.IsOpen() || !socket.IsOpen() || socket.IsDatagram())
    {
        return Status::Of(ErrorCode::Invalid);
    }
    if (options.max_frame_bytes == 0 || options.receive_buffer_bytes == 0 ||
        options.send_queue_bytes < kFrameHeaderBytes + 1)
    {
        return Status::Of(ErrorCode::LimitExceeded);
    }
    // The buffers are allocated when they are first needed (the read buffer at the first read, the scratch buffer
    // at the first send) and the queue grows by doubling: an idle connection holds almost nothing.
    Vector<byte> queue;
    const usize first_queue = options.send_queue_bytes < kInitialQueueBytes ? options.send_queue_bytes
                                                                            : kInitialQueueBytes;
    if (!queue.TryResizeUninitialized(first_queue))
    {
        return Status::Of(ErrorCode::Other);
    }
    [[maybe_unused]] const Status delay = socket.SetNoDelay(options.no_delay);
    if (options.socket_receive_buffer_bytes != 0)
    {
        [[maybe_unused]] const Status rcv = socket.SetReceiveBuffer(options.socket_receive_buffer_bytes);
    }
    if (options.socket_send_buffer_bytes != 0)
    {
        [[maybe_unused]] const Status snd = socket.SetSendBuffer(options.socket_send_buffer_bytes);
    }
    if (options.keep_alive)
    {
        [[maybe_unused]] const Status alive = socket.SetKeepAlive(true);
        if (options.keep_alive_idle_seconds != 0 || options.keep_alive_interval_seconds != 0 ||
            options.keep_alive_probes != 0)
        {
            [[maybe_unused]] const Status timing = socket.SetKeepAliveTiming(
                options.keep_alive_idle_seconds, options.keep_alive_interval_seconds, options.keep_alive_probes);
        }
    }
    reactor_ = &reactor;
    options_ = options;
    socket_ = Move(socket);
    read_buffer_.Clear();
    read_begin_ = 0;
    read_end_ = 0;
    read_eof_ = false;
    reader_ = FrameReader(options.max_frame_bytes);
    writer_ = FrameWriter(options.max_frame_bytes);
    queue_ = Move(queue);
    queue_capacity_ = options.send_queue_bytes;
    queue_allocated_ = first_queue;
    queue_head_ = 0;
    queue_size_ = 0;
    scratch_.Clear();
    reason_.Store(static_cast<u8>(CloseReason::None), MemoryOrder::Release);
    send_closed_.Store(false, MemoryOrder::Release);
    flushing_.Store(false, MemoryOrder::Release);
    return Status::Success();
}

jobs::Task<Status> TcpStream::Connect(Reactor& reactor, const Endpoint& remote, const TcpStreamOptions& options,
                                      TcpStream* stream, CancelToken* wait)
{
    if (stream->IsOpen())
    {
        co_return Status::Of(ErrorCode::Invalid);
    }
    Socket socket;
    const Status opened = Socket::OpenTcp(remote.GetAddress().Family(), &socket);
    if (opened.Failed())
    {
        co_return opened;
    }
    WaitOptions connect_wait;
    connect_wait.token = wait;
    connect_wait.timeout_nanos = options.connect_timeout_nanos;
    const IoResult connected = co_await net::Connect(reactor, socket, remote, connect_wait);
    if (connected.status.Failed())
    {
        [[maybe_unused]] const Status closed = reactor.Close(socket);
        co_return connected.status;
    }
    const Status adopted = stream->Adopt(reactor, Move(socket), options);
    if (adopted.Failed())
    {
        [[maybe_unused]] const Status closed = reactor.Close(socket);
    }
    co_return adopted;
}

// ---- sending ----

Status TcpStream::Send(ByteSpan payload)
{
    if (!socket_.IsOpen() || send_closed_.Load(MemoryOrder::Acquire))
    {
        return Status::Of(ErrorCode::Closed);
    }
    byte header[kFrameHeaderBytes];
    ENGINE_TRY(writer_.Header(payload.Size(), header));
    const usize need = kFrameHeaderBytes + payload.Size();
    LockGuard<Mutex> guard(queue_lock_);
    if (need > queue_capacity_ - queue_size_)
    {
        return Status::Of(ErrorCode::LimitExceeded);
    }
    if (scratch_.Empty() && !scratch_.TryResizeUninitialized(kScratchBytes))
    {
        return Status::Of(ErrorCode::Other);
    }
    if (need > queue_allocated_ - queue_size_)
    {
        // Grow the ring (doubling, at most to the capacity) and straighten it.
        usize grown = queue_allocated_ * 2;
        while (grown < queue_size_ + need)
        {
            grown *= 2;
        }
        grown = grown > queue_capacity_ ? queue_capacity_ : grown;
        Vector<byte> bigger;
        if (!bigger.TryResizeUninitialized(grown))
        {
            return Status::Of(ErrorCode::Other);
        }
        const usize first = queue_allocated_ - queue_head_ < queue_size_ ? queue_allocated_ - queue_head_ : queue_size_;
        MemCopy(bigger.Data(), queue_.Data() + queue_head_, first);
        MemCopy(bigger.Data() + first, queue_.Data(), queue_size_ - first);
        queue_ = Move(bigger);
        queue_allocated_ = grown;
        queue_head_ = 0;
    }
    usize tail = (queue_head_ + queue_size_) % queue_allocated_;
    const auto put = [this, &tail](const byte* data, usize size)
    {
        usize left = size;
        while (left > 0)
        {
            const usize room = queue_allocated_ - tail;
            const usize chunk = left < room ? left : room;
            MemCopy(queue_.Data() + tail, data, chunk);
            data += chunk;
            left -= chunk;
            tail = (tail + chunk) % queue_allocated_;
        }
    };
    put(header, kFrameHeaderBytes);
    put(payload.Data(), payload.Size());
    queue_size_ += need;
    return Status::Success();
}

usize TcpStream::SendQueueAllocatedBytes() const
{
    LockGuard<Mutex> guard(queue_lock_);
    return queue_allocated_;
}

usize TcpStream::SendQueueBytes() const
{
    LockGuard<Mutex> guard(queue_lock_);
    return queue_size_;
}

usize TcpStream::PeekQueue()
{
    LockGuard<Mutex> guard(queue_lock_);
    usize size = queue_size_ < kScratchBytes ? queue_size_ : kScratchBytes;
    usize first = queue_allocated_ - queue_head_;
    first = first < size ? first : size;
    MemCopy(scratch_.Data(), queue_.Data() + queue_head_, first);
    if (size > first)
    {
        MemCopy(scratch_.Data() + first, queue_.Data(), size - first);
    }
    return size;
}

void TcpStream::DropQueue(usize bytes)
{
    LockGuard<Mutex> guard(queue_lock_);
    queue_head_ = (queue_head_ + bytes) % queue_allocated_;
    queue_size_ -= bytes;
}

bool TcpStream::NextWait(const TimeBudget& budget, WaitOptions* out, bool* idle_governs) const
{
    bool expired = false;
    *out = budget.Next(&expired);
    if (expired)
    {
        return false;
    }
    *idle_governs = false;
    if (options_.idle_timeout_nanos >= 0 &&
        (out->timeout_nanos < 0 || options_.idle_timeout_nanos <= out->timeout_nanos))
    {
        out->timeout_nanos = options_.idle_timeout_nanos;
        *idle_governs = true;
    }
    return true;
}

jobs::Task<Status> TcpStream::Flush(usize keep_queued, WaitOptions options)
{
    if (reactor_ == nullptr)
    {
        co_return Status::Of(ErrorCode::Closed);
    }
    bool expected = false;
    if (!flushing_.CompareExchangeStrong(expected, true, MemoryOrder::AcqRel, MemoryOrder::Acquire))
    {
        co_return Status::Of(ErrorCode::Invalid);
    }
    const TimeBudget budget(*reactor_, options);
    Status result;
    usize piece = 0;    // bytes in the scratch buffer
    usize written = 0;  // of those, the bytes the socket has taken
    for (;;)
    {
        if (written == piece)
        {
            piece = 0;
            written = 0;
            if (SendQueueBytes() <= keep_queued)
            {
                break;
            }
            piece = PeekQueue();
        }
        if (!socket_.IsOpen())
        {
            result = Status::Of(ErrorCode::Closed);
            break;
        }
        usize sent = 0;
        const Status s = socket_.Send(ByteSpan(scratch_.Data() + written, piece - written), &sent);
        written += sent;
        DropQueue(sent);  // the room is free as soon as the socket has taken the bytes
        if (s.Ok() && sent > 0)
        {
            continue;  // progress: the idle time starts again
        }
        if (s.Failed() && !s.Is(ErrorCode::WouldBlock))
        {
            if (s.Is(ErrorCode::Reset) || s.Is(ErrorCode::Closed))
            {
                result = Fail(CloseReason::Reset, ErrorCode::Reset);
            }
            else
            {
                result = Fail(s.Is(ErrorCode::TimedOut) ? CloseReason::TimedOut : CloseReason::Reset, s.Code());
            }
            break;
        }
        // The socket is full: wait until it can take more, for the idle time or what is left of the call.
        WaitOptions next;
        bool idle_governs = false;
        if (!NextWait(budget, &next, &idle_governs))
        {
            result = Status::Of(ErrorCode::TimedOut);
            break;
        }
        const Status waited = co_await reactor_->WaitWritable(socket_, next);
        if (waited.Ok())
        {
            continue;
        }
        if (waited.Is(ErrorCode::TimedOut))
        {
            result = idle_governs ? Fail(CloseReason::TimedOut, ErrorCode::TimedOut) : waited;
        }
        else if (waited.Is(ErrorCode::Cancelled))
        {
            result = waited;
        }
        else
        {
            result = Status::Of(ErrorCode::Closed);  // closed from another side while we waited
        }
        break;
    }
    flushing_.Store(false, MemoryOrder::Release);
    co_return result;
}

jobs::Task<Status> TcpStream::SendFrame(ByteSpan payload, WaitOptions options)
{
    if (reactor_ == nullptr)
    {
        co_return Status::Of(ErrorCode::Closed);
    }
    const TimeBudget budget(*reactor_, options);
    for (;;)
    {
        const Status queued = Send(payload);
        if (queued.Ok())
        {
            break;
        }
        if (!queued.Is(ErrorCode::LimitExceeded) || payload.Size() > options_.max_frame_bytes ||
            kFrameHeaderBytes + payload.Size() > queue_capacity_)
        {
            co_return queued;  // not a matter of waiting
        }
        // No room: wait until the queue has drained enough for this frame.
        bool expired = false;
        const WaitOptions next = budget.Next(&expired);
        if (expired)
        {
            co_return Status::Of(ErrorCode::TimedOut);
        }
        const Status flushed = co_await Flush(queue_capacity_ - (kFrameHeaderBytes + payload.Size()), next);
        if (flushed.Failed())
        {
            co_return flushed;
        }
    }
    bool expired = false;
    const WaitOptions rest = budget.Next(&expired);
    if (expired)
    {
        co_return Status::Of(ErrorCode::TimedOut);
    }
    co_return co_await Flush(0, rest);
}

// ---- receiving ----

jobs::Task<Status> TcpStream::ReceiveFrame(WaitOptions options)
{
    if (reactor_ == nullptr)
    {
        co_return Status::Of(ErrorCode::Closed);
    }
    if (reader_.HasFrame())
    {
        reader_.Pop();
    }
    const TimeBudget budget(*reactor_, options);
    for (;;)
    {
        if (read_begin_ < read_end_)
        {
            usize used = 0;
            const Status decoded = reader_.Consume(ByteSpan(read_buffer_.Data() + read_begin_, read_end_ - read_begin_),
                                                   &used);
            read_begin_ += used;
            if (decoded.Failed())
            {
                co_return Fail(CloseReason::ProtocolError, decoded.Code());
            }
            if (reader_.HasFrame())
            {
                co_return Status::Success();
            }
            continue;
        }
        read_begin_ = 0;
        read_end_ = 0;
        if (read_eof_ || !socket_.IsOpen() || (Reason() != CloseReason::None && Reason() != CloseReason::PeerClosed))
        {
            co_return Status::Of(ErrorCode::Closed);
        }
        WaitOptions next;
        bool idle_governs = false;
        if (!NextWait(budget, &next, &idle_governs))
        {
            co_return Status::Of(ErrorCode::TimedOut);
        }
        if (read_buffer_.Empty() && !read_buffer_.TryResizeUninitialized(options_.receive_buffer_bytes))
        {
            co_return Status::Of(ErrorCode::Other);
        }
        usize want = read_buffer_.Size();
        if (options_.max_read_bytes != 0 && options_.max_read_bytes < want)
        {
            want = options_.max_read_bytes;
        }
        const IoResult received = co_await net::Receive(*reactor_, socket_, MutableByteSpan(read_buffer_.Data(), want),
                                                        next);
        if (received.status.Ok())
        {
            read_end_ = received.count;
            continue;
        }
        const ErrorCode code = received.status.Code();
        if (code == ErrorCode::TimedOut && received.status.OsError() != 0)
        {
            // The system gave up (keep-alive probes unanswered, or retransmits): the connection is dead, whatever the
            // time limits of this call. (A wait that ran out of time carries no system error.)
            co_return Fail(CloseReason::TimedOut, ErrorCode::TimedOut);
        }
        if (code == ErrorCode::TimedOut)
        {
            co_return idle_governs ? Fail(CloseReason::TimedOut, ErrorCode::TimedOut) : received.status;
        }
        if (code == ErrorCode::Cancelled)
        {
            co_return received.status;
        }
        if (!socket_.IsOpen() || Reason() != CloseReason::None)
        {
            co_return Status::Of(ErrorCode::Closed);  // closed or failed from the other side while we waited
        }
        if (code == ErrorCode::Closed)
        {
            read_eof_ = true;
            if (reader_.InFrame())
            {
                co_return Fail(CloseReason::ProtocolError, ErrorCode::Closed);
            }
            SetReason(CloseReason::PeerClosed);
            co_return Status::Of(ErrorCode::Closed);
        }
        co_return Fail(CloseReason::Reset, code == ErrorCode::Reset ? ErrorCode::Reset : code);
    }
}

// ---- closing ----

jobs::Task<Status> TcpStream::DrainPeer(WaitOptions options)
{
    byte sink[kDrainBytes];
    if (read_eof_)
    {
        co_return Status::Success();
    }
    const TimeBudget budget(*reactor_, options);
    for (;;)
    {
        bool expired = false;
        const WaitOptions next = budget.Next(&expired);
        if (expired)
        {
            co_return Status::Of(ErrorCode::TimedOut);
        }
        const IoResult received = co_await net::Receive(*reactor_, socket_, MutableByteSpan(sink, kDrainBytes), next);
        if (received.status.Ok())
        {
            continue;
        }
        if (received.status.Is(ErrorCode::Closed) || received.status.Is(ErrorCode::Reset))
        {
            co_return Status::Success();  // the peer's end (or its reset: nothing more will come either way)
        }
        co_return received.status;
    }
}

jobs::Task<Status> TcpStream::Close()
{
    if (!socket_.IsOpen())
    {
        co_return Status::Success();
    }
    send_closed_.Store(true, MemoryOrder::Release);
    const CloseReason standing = SetReason(CloseReason::LocalClose);
    const TimeBudget budget(*reactor_, WaitOptions{nullptr, options_.linger_nanos});
    Status result;
    // A stream that had already failed has nothing orderly left to do (a peer that only closed its side has).
    const bool failed_already = standing != CloseReason::LocalClose && standing != CloseReason::PeerClosed;
    if (!failed_already)
    {
        bool expired = false;
        const WaitOptions next = budget.Next(&expired);
        Status flushed = expired ? Status::Of(ErrorCode::TimedOut) : co_await Flush(0, next);
        if (flushed.Ok())
        {
            [[maybe_unused]] const Status shut = socket_.Shutdown(ShutdownMode::Write);
            const WaitOptions rest = budget.Next(&expired);
            flushed = expired ? Status::Of(ErrorCode::TimedOut) : co_await DrainPeer(rest);
        }
        result = flushed;
    }
    if (result.Failed() || failed_already)
    {
        [[maybe_unused]] const Status linger = socket_.SetLinger(true, 0);
    }
    [[maybe_unused]] const Status closed = reactor_->Close(socket_);
    co_return result;
}

Status TcpStream::Abort()
{
    if (!socket_.IsOpen())
    {
        return Status::Success();
    }
    send_closed_.Store(true, MemoryOrder::Release);
    SetReason(CloseReason::LocalClose);
    [[maybe_unused]] const Status linger = socket_.SetLinger(true, 0);
    return reactor_->Close(socket_);
}

}  // namespace vse::net
