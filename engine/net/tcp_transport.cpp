#include "net/tcp_transport.h"

#include "cstr/mem_ops.h"
#include "lang/utility.h"
#include "net/frame.h"

namespace vse::net
{
namespace detail  // named, not anonymous: GCC refuses a class member whose type has internal linkage
{

constexpr u8 kProtocolVersion = 1;
constexpr u8 kTypeHello = 1;
constexpr u8 kTypeData = 2;
constexpr u8 kTypeClose = 3;
constexpr u8 kTypePing = 4;
constexpr u8 kTypePong = 5;
constexpr usize kHelloBytes = 1 + 1 + 1 + 4;
constexpr usize kDataHeaderBytes = 1 + 1 + 1;
constexpr usize kAcceptPerPoll = 64;
constexpr usize kFrameSlack = 16;  // frame bytes beyond the largest message

enum class TcpState : u8
{
    Connecting,  // the stream is being connected
    Handshake,   // connected; waiting for the peer's Hello
    Open,
    Closed,  // ended; the Closed event may not have been polled yet
};

struct TcpEventRec
{
    EventKind kind = EventKind::Message;
    ConnectionId connection = 0;
    bool incoming = false;
    Endpoint peer;
    u8 channel = 0;
    Reliability reliability = Reliability::ReliableOrdered;
    Vector<byte> data;
    DisconnectReason reason = DisconnectReason::None;
    u32 code = 0;
};

struct TcpConn
{
    explicit TcpConn(usize max_frame) : reader(max_frame) {}

    ConnectionId id = 0;
    UniquePtr<StreamIo> io;
    Endpoint peer;
    bool incoming = false;
    TcpState state = TcpState::Connecting;
    FrameReader reader;
    Vector<byte> out;
    usize out_head = 0;
    Vector<u64> ends;  // absolute stream offsets at which queued data messages end
    usize ends_head = 0;
    u64 queued_total = 0;        // bytes ever queued
    u64 written_total = 0;       // bytes ever written
    bool out_failed = false;     // a frame could not be queued (out of memory)
    bool flushing = false;       // closed locally; the last bytes are still being written
    bool shutdown_sent = false;  // the write side is shut; the peer's remaining bytes are read and dropped
    u64 flush_deadline = 0;
    u8 channels = 0;
    usize max_message = 0;
    u64 created = 0;
    u64 last_recv = 0;
    u64 last_ping = 0;
    bool has_rtt = false;
    f64 srtt = 0;
    ConnectionStats stats;
};

void TcpPutU32(Vector<byte>* v, u32 x)
{
    for (u32 i = 0; i < 4; ++i)
    {
        v->Push(static_cast<byte>(x >> (8 * i)));
    }
}

void TcpPutU64(Vector<byte>* v, u64 x)
{
    for (u32 i = 0; i < 8; ++i)
    {
        v->Push(static_cast<byte>(x >> (8 * i)));
    }
}

u32 TcpGetU32(ByteSpan b, usize at)
{
    u32 x = 0;
    for (u32 i = 0; i < 4; ++i)
    {
        x |= static_cast<u32>(static_cast<u8>(b[at + i])) << (8 * i);
    }
    return x;
}

u64 TcpGetU64(ByteSpan b, usize at)
{
    u64 x = 0;
    for (u32 i = 0; i < 8; ++i)
    {
        x |= static_cast<u64>(static_cast<u8>(b[at + i])) << (8 * i);
    }
    return x;
}

}  // namespace detail

using namespace detail;

struct TcpTransport::Impl
{
    Impl(StreamProvider& p, const Clock& c, const TcpTransportConfig& cfg)
        : provider(&p), clock(&c), config(cfg), by_id(HeapAllocator::Default()), accept_limiter(cfg.accept_limits)
    {
    }

    StreamProvider* provider;
    const Clock* clock;
    TcpTransportConfig config;
    bool listening = false;
    ConnectionId next_id = 1;
    Vector<UniquePtr<TcpConn>> conns;
    HashMap<ConnectionId, TcpConn*, HeapAllocator> by_id;
    Vector<UniquePtr<TcpConn>> lingering;  // closed locally, still flushing
    Vector<TcpEventRec> queue;
    usize queue_head = 0;
    TcpEventRec current;
    Vector<byte> read_buffer;
    RateLimiter accept_limiter;
    usize total_out = 0;  // queued output over all connections: exact after a pass, never low until the next

    u64 Now() const { return clock->NowNanos(); }
    // Time since `then`; zero if `then` is later than `now` (a connection made after the pass took its time).
    static u64 Since(u64 now, u64 then) { return now > then ? now - then : 0; }
    usize MaxFrame() const { return config.max_message_bytes + kFrameSlack; }

    TcpConn* FindById(ConnectionId id) const
    {
        TcpConn* const* found = by_id.Find(id);
        return found == nullptr ? nullptr : *found;
    }

    TcpConn* Create(UniquePtr<StreamIo>&& io, const Endpoint& peer, bool incoming)
    {
        UniquePtr<TcpConn> conn = MakeUnique<TcpConn>(kHelloBytes);  // until the Hello, a frame is at most a Hello
        for (;;)
        {
            if (next_id == 0)
            {
                next_id = 1;
            }
            if (by_id.Find(next_id) == nullptr)
            {
                break;
            }
            ++next_id;
        }
        conn->id = next_id++;
        conn->io = Move(io);
        conn->peer = peer;
        conn->incoming = incoming;
        conn->created = Now();
        conn->last_recv = conn->created;
        conn->last_ping = conn->created;
        TcpConn* raw = conn.Get();
        by_id.Insert(raw->id, raw);
        conns.Push(Move(conn));
        return raw;
    }

    void Remove(TcpConn* c)
    {
        by_id.Remove(c->id);
        for (usize i = 0; i < conns.Size(); ++i)
        {
            if (conns[i].Get() == c)
            {
                if (c->flushing && c->io)
                {
                    lingering.Push(Move(conns[i]));
                }
                conns.Remove(i);
                return;
            }
        }
    }

    void Queue(TcpEventRec&& rec) { queue.Push(Move(rec)); }

    void PurgeMessages(ConnectionId id)
    {
        usize out = queue_head;
        for (usize i = queue_head; i < queue.Size(); ++i)
        {
            if (queue[i].kind == EventKind::Message && queue[i].connection == id)
            {
                continue;
            }
            if (out != i)
            {
                queue[out] = Move(queue[i]);
            }
            ++out;
        }
        queue.Resize(out);
    }

    void End(TcpConn* c, DisconnectReason reason, u32 code)
    {
        if (c->state == TcpState::Closed)
        {
            return;
        }
        const bool was_open = c->state == TcpState::Open;
        if (c->incoming && c->state == TcpState::Handshake &&
            (reason == DisconnectReason::ProtocolError || reason == DisconnectReason::TimedOut))
        {
            accept_limiter.Strike(AddressGroup(c->peer.GetAddress()), Now());  // a stranger that never finished
        }
        c->state = TcpState::Closed;
        TcpEventRec rec;
        rec.kind = EventKind::Closed;
        rec.connection = c->id;
        rec.reason = reason;
        rec.code = code;
        Queue(Move(rec));
        if (!(reason == DisconnectReason::LocalClose && was_open))
        {
            c->io.Reset();  // nothing more to say to this peer
        }
    }

    // ---- sending ----

    // Appends one frame to the connection's output.
    void QueueFrame(TcpConn* c, ByteSpan head, ByteSpan body, bool is_message)
    {
        const FrameWriter writer(MaxFrame());
        byte header[kFrameHeaderBytes];
        if (writer.Header(head.Size() + body.Size(), header).Failed())
        {
            return;
        }
        const usize start = c->out.Size();
        for (usize i = 0; i < kFrameHeaderBytes; ++i)
        {
            c->out.Push(header[i]);
        }
        for (usize i = 0; i < head.Size(); ++i)
        {
            c->out.Push(head[i]);
        }
        const usize at = c->out.Size();
        if (!c->out.TryResizeUninitialized(at + body.Size()))
        {
            c->out.Resize(start);  // out of memory: no part of this frame is sent and the connection ends
            c->out_failed = true;
            return;
        }
        if (body.Size() != 0)
        {
            MemCopy(c->out.Data() + at, body.Data(), body.Size());
        }
        c->queued_total += kFrameHeaderBytes + head.Size() + body.Size();
        total_out += kFrameHeaderBytes + head.Size() + body.Size();
        if (is_message)
        {
            c->ends.Push(c->queued_total);
        }
    }

    void QueueHello(TcpConn* c)
    {
        Vector<byte> p;
        p.Push(static_cast<byte>(kTypeHello));
        p.Push(static_cast<byte>(kProtocolVersion));
        p.Push(static_cast<byte>(config.channels));
        TcpPutU32(&p, static_cast<u32>(config.max_message_bytes));
        QueueFrame(c, ByteSpan(p.Data(), p.Size()), ByteSpan(), false);
    }

    void QueuePing(TcpConn* c, u8 type, u64 stamp)
    {
        Vector<byte> p;
        p.Push(static_cast<byte>(type));
        TcpPutU64(&p, stamp);
        QueueFrame(c, ByteSpan(p.Data(), p.Size()), ByteSpan(), false);
    }

    usize Pending(const TcpConn* c) const { return c->out.Size() - c->out_head; }

    // Writes what the stream takes; false if the connection broke.
    bool Write(TcpConn* c)
    {
        while (c->io && Pending(c) != 0)
        {
            usize sent = 0;
            const Status s = c->io->Send(ByteSpan(c->out.Data() + c->out_head, Pending(c)), &sent);
            if (s.Is(ErrorCode::WouldBlock))
            {
                break;
            }
            if (s.Failed())
            {
                return false;
            }
            c->out_head += sent;
            c->written_total += sent;
            if (sent == 0)
            {
                break;
            }
        }
        if (c->out_head == c->out.Size())
        {
            c->out.Clear();
            c->out_head = 0;
        }
        else if (c->out_head > 64 * 1024 && c->out_head * 2 > c->out.Size())
        {
            c->out.RemoveRange(0, c->out_head);
            c->out_head = 0;
        }
        while (c->ends_head < c->ends.Size() && c->ends[c->ends_head] <= c->written_total)
        {
            ++c->ends_head;
        }
        if (c->ends_head > 64 && c->ends_head * 2 > c->ends.Size())
        {
            c->ends.RemoveRange(0, c->ends_head);
            c->ends_head = 0;
        }
        return true;
    }

    // ---- receiving ----

    void Deliver(TcpConn* c, u8 channel, u8 rel, ByteSpan data)
    {
        TcpEventRec rec;
        rec.kind = EventKind::Message;
        rec.connection = c->id;
        rec.channel = channel;
        rec.reliability = static_cast<Reliability>(rel);
        rec.data.TryResizeUninitialized(data.Size());
        MemCopy(rec.data.Data(), data.Data(), data.Size());
        ++c->stats.messages_received;
        c->stats.bytes_received += data.Size();
        Queue(Move(rec));
    }

    void Open(TcpConn* c)
    {
        c->state = TcpState::Open;
        c->reader = FrameReader(c->max_message + kFrameSlack);  // the Hello is done: messages may follow
        c->last_ping = Now();
        TcpEventRec rec;
        rec.kind = EventKind::Opened;
        rec.connection = c->id;
        rec.incoming = c->incoming;
        rec.peer = c->peer;
        Queue(Move(rec));
        QueuePing(c, kTypePing, Now());
    }

    // One frame from the peer; false if it broke the protocol.
    bool OnFrame(TcpConn* c, ByteSpan f)
    {
        ++c->stats.packets_received;
        const u8 type = static_cast<u8>(f[0]);
        if (c->state == TcpState::Handshake)
        {
            if (type != kTypeHello || f.Size() != kHelloBytes || static_cast<u8>(f[1]) != kProtocolVersion)
            {
                return false;
            }
            const u8 their_channels = static_cast<u8>(f[2]);
            const usize their_max = TcpGetU32(f, 3);
            c->channels = their_channels < config.channels ? their_channels : config.channels;
            c->max_message = their_max < config.max_message_bytes ? their_max : config.max_message_bytes;
            if (c->channels == 0 || c->max_message == 0)
            {
                return false;
            }
            if (c->incoming)
            {
                QueueHello(c);
            }
            Open(c);
            return true;
        }
        switch (type)
        {
        case kTypeData:
        {
            if (f.Size() <= kDataHeaderBytes)
            {
                return false;
            }
            const u8 channel = static_cast<u8>(f[1]);
            const u8 rel = static_cast<u8>(f[2]);
            if (channel >= c->channels || rel > 3 || f.Size() - kDataHeaderBytes > c->max_message)
            {
                return false;
            }
            Deliver(c, channel, rel, ByteSpan(f.Data() + kDataHeaderBytes, f.Size() - kDataHeaderBytes));
            return true;
        }
        case kTypeClose:
            if (f.Size() != 5)
            {
                return false;
            }
            End(c, DisconnectReason::PeerClosed, TcpGetU32(f, 1));
            return true;
        case kTypePing:
            if (f.Size() != 9)
            {
                return false;
            }
            if (Pending(c) > config.max_send_queue_bytes)
            {
                return false;  // a peer that asks and does not read the answers
            }
            QueuePing(c, kTypePong, TcpGetU64(f, 1));
            return true;
        case kTypePong:
        {
            if (f.Size() != 9)
            {
                return false;
            }
            const u64 sent = TcpGetU64(f, 1);
            const u64 now = Now();
            if (sent <= now)
            {
                const f64 sample = static_cast<f64>(now - sent) / 1e9;
                c->srtt = c->has_rtt ? 0.875 * c->srtt + 0.125 * sample : sample;
                c->has_rtt = true;
                c->stats.rtt_seconds = c->srtt;
            }
            return true;
        }
        default: return false;
        }
    }

    // Reads what the stream has (up to the per-pass budget) and handles the frames in it.
    void Read(TcpConn* c)
    {
        usize budget = config.read_bytes_per_poll;
        while (c->io && c->state != TcpState::Closed && budget != 0)
        {
            usize got = 0;
            const usize want = budget < read_buffer.Size() ? budget : read_buffer.Size();
            const Status s = c->io->Receive(MutableByteSpan(read_buffer.Data(), want), &got);
            if (s.Is(ErrorCode::WouldBlock))
            {
                return;
            }
            if (s.Failed())
            {
                End(c, DisconnectReason::Reset, 0);  // the end of the stream without a Close frame, or a reset
                return;
            }
            budget -= got;
            c->last_recv = Now();
            usize at = 0;
            while (at < got && c->state != TcpState::Closed)
            {
                usize used = 0;
                if (c->reader.Consume(ByteSpan(read_buffer.Data() + at, got - at), &used).Failed())
                {
                    End(c, DisconnectReason::ProtocolError, 0);
                    return;
                }
                at += used;
                if (c->reader.HasFrame())
                {
                    const bool ok = OnFrame(c, c->reader.Frame());
                    c->reader.Pop();
                    if (!ok)
                    {
                        End(c, DisconnectReason::ProtocolError, 0);
                        return;
                    }
                }
                else if (used == 0)
                {
                    break;
                }
            }
        }
    }

    void Step(TcpConn* c, u64 now)
    {
        if (c->state == TcpState::Connecting)
        {
            const Status s = c->io->FinishConnect();
            if (s.Is(ErrorCode::WouldBlock))
            {
                if (Since(now, c->created) >= config.handshake_timeout_nanos)
                {
                    End(c, DisconnectReason::TimedOut, 0);
                }
                return;
            }
            if (s.Failed())
            {
                End(c, s.Is(ErrorCode::Refused) ? DisconnectReason::Refused : DisconnectReason::TimedOut, 0);
                return;
            }
            c->state = TcpState::Handshake;
            QueueHello(c);
        }
        if (c->state == TcpState::Closed)
        {
            return;
        }
        if (c->out_failed || !Write(c))
        {
            End(c, DisconnectReason::Reset, 0);
            return;
        }
        Read(c);
        if (c->state == TcpState::Closed)
        {
            return;
        }
        if (c->state == TcpState::Handshake && Since(now, c->created) >= config.handshake_timeout_nanos)
        {
            End(c, DisconnectReason::TimedOut, 0);
            return;
        }
        if (c->state == TcpState::Open)
        {
            if (Since(now, c->last_recv) >= config.timeout_nanos)
            {
                End(c, DisconnectReason::TimedOut, 0);
                return;
            }
            if (Since(now, c->last_ping) >= config.ping_interval_nanos)
            {
                c->last_ping = now;
                QueuePing(c, kTypePing, now);
            }
            if (!Write(c))
            {
                End(c, DisconnectReason::Reset, 0);
            }
        }
    }

    // A connection closed locally keeps writing its last bytes; false when it is done.
    bool Flush(TcpConn* c, u64 now)
    {
        if (!c->io)
        {
            return false;
        }
        if (!Write(c) || now >= c->flush_deadline)
        {
            c->io.Reset();
            return false;
        }
        if (Pending(c) != 0)
        {
            return true;
        }
        if (!c->shutdown_sent)
        {
            c->shutdown_sent = true;
            if (c->io->ShutdownWrite().Failed())
            {
                c->io.Reset();
                return false;
            }
        }
        // Closing a socket with unread data resets the stream and can destroy our last bytes in flight: read what the
        // peer still sends until it ends its side (or the linger runs out).
        for (u32 n = 0; n < 64; ++n)
        {
            usize got = 0;
            const Status s = c->io->Receive(MutableByteSpan(read_buffer.Data(), read_buffer.Size()), &got);
            if (s.Is(ErrorCode::WouldBlock))
            {
                return true;
            }
            if (s.Failed())
            {
                c->io.Reset();
                return false;
            }
        }
        return true;
    }

    // Whether a stream from `peer` may stay: not banned, within the rate limits and the per-address cap.
    bool AllowAccept(const Endpoint& peer)
    {
        const Address group = AddressGroup(peer.GetAddress());
        if (!accept_limiter.Allow(group, Now()))
        {
            return false;
        }
        usize same = 0;
        for (usize i = 0; i < conns.Size(); ++i)
        {
            same += AddressGroup(conns[i]->peer.GetAddress()) == group ? 1 : 0;
        }
        return same < config.max_connections_per_address;
    }

    void Service()
    {
        const u64 now = Now();
        if (read_buffer.Empty())
        {
            read_buffer.Resize(16 * 1024);
        }
        if (listening)
        {
            for (usize n = 0; n < kAcceptPerPoll; ++n)
            {
                UniquePtr<StreamIo> io;
                Endpoint peer;
                if (provider->Accept(&io, &peer).Failed())
                {
                    break;
                }
                if (conns.Size() >= config.max_connections || !AllowAccept(peer))
                {
                    continue;  // over a cap or a limit: the stream is closed on arrival
                }
                TcpConn* c = Create(Move(io), peer, true);
                c->state = TcpState::Handshake;
            }
        }
        for (usize i = 0; i < conns.Size(); ++i)
        {
            TcpConn* c = conns[i].Get();
            if (c->state == TcpState::Closed)
            {
                if (c->flushing && !Flush(c, now))
                {
                    c->flushing = false;
                }
                continue;
            }
            Step(c, now);
        }
        for (usize i = 0; i < lingering.Size();)
        {
            if (Flush(lingering[i].Get(), now))
            {
                ++i;
            }
            else
            {
                lingering.Remove(i);
            }
        }
        usize queued = 0;
        for (usize i = 0; i < conns.Size(); ++i)
        {
            queued += Pending(conns[i].Get());
        }
        for (usize i = 0; i < lingering.Size(); ++i)
        {
            queued += Pending(lingering[i].Get());
        }
        total_out = queued;
    }
};

TcpTransport::TcpTransport(StreamProvider& provider, const Clock& clock, const TcpTransportConfig& config)
    : config_(config), impl_(MakeUnique<Impl>(provider, clock, config))
{
    if (config_.channels == 0)
    {
        config_.channels = 1;
        impl_->config.channels = 1;
    }
}

TcpTransport::~TcpTransport() = default;

Status TcpTransport::Listen(const Endpoint& at)
{
    if (impl_->listening)
    {
        return Status::Of(ErrorCode::Invalid);
    }
    const Status s = impl_->provider->Listen(at);
    if (s.Ok())
    {
        impl_->listening = true;
    }
    return s;
}

Status TcpTransport::Connect(const Endpoint& to, ConnectionId* connection)
{
    Impl& m = *impl_;
    if (m.conns.Size() >= m.config.max_connections)
    {
        return Status::Of(ErrorCode::LimitExceeded);
    }
    UniquePtr<StreamIo> io;
    const Status s = m.provider->Connect(to, &io);
    TcpConn* c = m.Create(Move(io), to, false);
    *connection = c->id;
    if (s.Failed())
    {
        // Refused at once (a loopback address with no listener): reported as the Closed event of the connection.
        m.End(c, s.Is(ErrorCode::Refused) ? DisconnectReason::Refused : DisconnectReason::TimedOut, 0);
    }
    return Status::Success();
}

Status TcpTransport::Send(ConnectionId connection, u8 channel, ByteSpan bytes, Reliability reliability)
{
    Impl& m = *impl_;
    TcpConn* c = m.FindById(connection);
    if (c == nullptr || bytes.Size() == 0 || static_cast<u8>(reliability) > 3)
    {
        return Status::Of(ErrorCode::Invalid);
    }
    if (c->state == TcpState::Closed)
    {
        return Status::Of(ErrorCode::Closed);
    }
    if (channel >= config_.channels || bytes.Size() > config_.max_message_bytes)
    {
        return Status::Of(channel >= config_.channels ? ErrorCode::Invalid : ErrorCode::MessageTooLarge);
    }
    if (c->state != TcpState::Open)
    {
        return Status::Of(ErrorCode::NotConnected);
    }
    if (channel >= c->channels || bytes.Size() > c->max_message)
    {
        return Status::Of(channel >= c->channels ? ErrorCode::Invalid : ErrorCode::MessageTooLarge);
    }
    if (m.Pending(c) + bytes.Size() + kFrameHeaderBytes + kDataHeaderBytes > m.config.max_send_queue_bytes ||
        m.total_out + bytes.Size() + kFrameHeaderBytes + kDataHeaderBytes > m.config.max_total_send_queue_bytes)
    {
        return Status::Of(ErrorCode::LimitExceeded);
    }
    // A stream is reliable and ordered: the unreliable kinds are sent as reliable ordered.
    const Reliability sent_as = reliability == Reliability::ReliableUnordered ? Reliability::ReliableUnordered
                                                                              : Reliability::ReliableOrdered;
    const byte head[kDataHeaderBytes] = {static_cast<byte>(kTypeData), static_cast<byte>(channel),
                                         static_cast<byte>(sent_as)};
    m.QueueFrame(c, ByteSpan(head, kDataHeaderBytes), bytes, true);
    if (c->out_failed)
    {
        return Status::Of(ErrorCode::LimitExceeded);
    }
    ++c->stats.messages_sent;
    ++c->stats.packets_sent;
    c->stats.bytes_sent += bytes.Size();
    return Status::Success();
}

bool TcpTransport::Poll(Event* event)
{
    Impl& m = *impl_;
    if (m.queue_head >= m.queue.Size())
    {
        m.queue.Clear();
        m.queue_head = 0;
        m.Service();
        if (m.queue_head >= m.queue.Size())
        {
            return false;
        }
    }
    m.current = Move(m.queue[m.queue_head++]);
    event->kind = m.current.kind;
    event->connection = m.current.connection;
    event->incoming = m.current.incoming;
    event->peer = m.current.peer;
    event->channel = m.current.channel;
    event->reliability = m.current.reliability;
    event->message = ByteSpan(m.current.data.Data(), m.current.data.Size());
    event->reason = m.current.reason;
    event->close_code = m.current.code;
    if (m.current.kind == EventKind::Closed)
    {
        TcpConn* c = m.FindById(m.current.connection);
        if (c != nullptr)
        {
            m.Remove(c);
        }
    }
    return true;
}

Status TcpTransport::Close(ConnectionId connection, u32 code)
{
    Impl& m = *impl_;
    TcpConn* c = m.FindById(connection);
    if (c == nullptr)
    {
        return Status::Of(ErrorCode::Invalid);
    }
    if (c->state == TcpState::Closed)
    {
        return Status::Of(ErrorCode::Closed);
    }
    if (c->state == TcpState::Open)
    {
        byte body[5] = {static_cast<byte>(kTypeClose)};
        for (u32 i = 0; i < 4; ++i)
        {
            body[1 + i] = static_cast<byte>(code >> (8 * i));
        }
        m.QueueFrame(c, ByteSpan(body, 5), ByteSpan(), false);
        c->flushing = true;
        c->flush_deadline = m.Now() + m.config.close_linger_nanos;
    }
    m.PurgeMessages(connection);
    m.End(c, DisconnectReason::LocalClose, 0);
    return Status::Success();
}

Status TcpTransport::Statistics(ConnectionId connection, ConnectionStats* stats) const
{
    const TcpConn* c = impl_->FindById(connection);
    if (c == nullptr)
    {
        return Status::Of(ErrorCode::Invalid);
    }
    *stats = c->stats;
    usize messages = 0;
    for (usize i = c->ends_head; i < c->ends.Size(); ++i)
    {
        messages += c->ends[i] > c->written_total ? 1 : 0;
    }
    stats->send_queue_messages = messages;
    stats->send_queue_bytes = impl_->Pending(c);
    return Status::Success();
}

const RateLimiterCounters& TcpTransport::AcceptLimiterCounters() const
{
    return impl_->accept_limiter.Counters();
}

usize TcpTransport::ConnectionCount() const
{
    return impl_->conns.Size();
}

}  // namespace vse::net
