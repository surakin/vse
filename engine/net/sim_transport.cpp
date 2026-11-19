#include "net/sim_transport.h"

#include "containers/deque.h"
#include "containers/hash_set.h"
#include "bytes/byte_reader.h"
#include "bytes/byte_writer.h"
#include "cstr/mem_ops.h"
#include "lang/utility.h"

namespace engine_placeholder::net
{

namespace
{

enum PacketKind : u8
{
    kConnect = 1,
    kAccept = 2,
    kRefuse = 3,
    kData = 4,
    kAck = 5,
    kPing = 6,
    kPong = 7,
    kClose = 8,
    kCancel = 9,  // a client gave up on a connect: carries its connection id and nonce
};

constexpr usize kWindow = 256;    // reliable messages in flight per connection
constexpr u32 kLookahead = 1024;  // how far ahead of the next expected message one is accepted (both reliable kinds)
constexpr usize
    kRetiredMemory = 64;  // closed incoming connections remembered, to ignore a late repeat of their request
constexpr u64 kConnectRetryNanos = 200'000'000;
constexpr u64 kMinRto = 20'000'000;
constexpr u64 kMaxRto = 2'000'000'000;
constexpr u64 kInitialRto = 200'000'000;

u32 Checksum(const byte* data, usize size)
{
    u32 hash = 2166136261u;
    for (usize i = 0; i < size; ++i)
    {
        hash ^= static_cast<u8>(data[i]);
        hash *= 16777619u;
    }
    return hash;
}

bool IsReliable(u8 rel)
{
    return rel == static_cast<u8>(Reliability::ReliableOrdered) ||
           rel == static_cast<u8>(Reliability::ReliableUnordered);
}

Vector<byte> CopyOf(ByteSpan bytes)
{
    Vector<byte> copy;
    if (bytes.Size() != 0)
    {
        copy.TryResizeUninitialized(bytes.Size());
        MemCopy(copy.Data(), bytes.Data(), bytes.Size());
    }
    return copy;
}

struct RecvSpace
{
    u32 next = 0;  // ordered: the next expected; unordered: every seq below it has arrived
    // By sequence number (container audit Day 10: vectors rescanned from the start after every delivery).
    HashSet<u32, HeapAllocator> above{HeapAllocator::Default()};  // unordered: arrived at or above `next`
    HashMap<u32, Vector<byte>, HeapAllocator> buffered{HeapAllocator::Default()};  // ordered: arrived ahead of `next`
    bool any = false;  // unreliable kinds: something was delivered
    u32 highest = 0;
    u64 mask = 0;  // unreliable: bit i set = seq (highest - 1 - i) seen
};

struct ChannelState
{
    u32 send_next[4] = {};
    RecvSpace recv[4];
};

struct InFlight
{
    u8 channel = 0;
    u8 rel = 0;
    u32 seq = 0;
    Vector<byte> data;
    u64 first_sent = 0;
    u64 last_sent = 0;
    u32 tries = 0;
};

struct Pending
{
    u8 channel = 0;
    u8 rel = 0;
    u32 seq = 0;
    Vector<byte> data;
};

enum class State : u8
{
    Connecting,
    Open,
    Closed,
};

struct Conn
{
    ConnectionId id = 0;
    ConnectionId remote_id = 0;
    u64 nonce = 0;  // client side: chosen at Connect; server side: the client's, from its request
    Endpoint peer;
    State state = State::Connecting;
    bool incoming = false;
    u64 created = 0;
    u64 last_heard = 0;
    u64 last_sent = 0;
    u64 connect_last_try = 0;
    Vector<ChannelState> channels;
    Vector<InFlight> inflight;
    Vector<Pending> pending;
    usize pending_head = 0;
    bool has_rtt = false;
    f64 srtt = 0;
    f64 rttvar = 0;
    u64 data_tx = 0;
    u64 data_retx = 0;
    ConnectionStats stats;
};

struct Retired
{
    Endpoint peer;
    u64 nonce = 0;
};

struct EventRec
{
    EventKind kind = EventKind::Opened;
    ConnectionId connection = 0;
    bool incoming = false;
    Endpoint peer;
    u8 channel = 0;
    Reliability reliability = Reliability::Unreliable;
    Vector<byte> data;
    DisconnectReason reason = DisconnectReason::None;
    u32 code = 0;
};

struct Lingering
{
    u8 kind = kClose;  // kClose, or kCancel for a connect that was never answered
    Endpoint peer;
    ConnectionId remote_id = 0;  // for kCancel: our own id
    u64 nonce = 0;
    u32 code = 0;
    u32 sends_left = 0;
    u64 next_send = 0;
};

}  // namespace

struct SimulatedTransport::Impl
{
    Impl(SimNetwork& network, const Endpoint& local, const SimTransportConfig& config)
        : net(&network), self(local), cfg(config), index(HeapAllocator::Default())
    {
    }

    SimNetwork* net;
    Endpoint self;
    SimTransportConfig cfg;
    bool listening = false;
    ConnectionId next_id = 1;
    Vector<UniquePtr<Conn>> conns;
    HashMap<ConnectionId, Conn*, HeapAllocator> index;
    Vector<EventRec> queue;
    usize queue_head = 0;
    EventRec current;
    Vector<Lingering> lingering;
    Deque<Retired> retired;  // newest last, at most kRetiredMemory (a Deque: the oldest leaves from the front)

    u64 Now() const { return net->NowNanos(); }

    Conn* Find(ConnectionId id) const
    {
        Conn* const* found = index.Find(id);
        return found == nullptr ? nullptr : *found;
    }

    Conn* Create(const Endpoint& peer, bool incoming)
    {
        UniquePtr<Conn> conn = MakeUnique<Conn>();
        conn->id = next_id++;
        conn->peer = peer;
        conn->incoming = incoming;
        conn->created = Now();
        conn->last_heard = conn->created;
        conn->last_sent = conn->created;
        conn->channels.Resize(cfg.channels);
        Conn* raw = conn.Get();
        index.Insert(raw->id, raw);
        conns.Push(Move(conn));
        return raw;
    }

    void Remove(Conn* conn)
    {
        if (conn->incoming)
        {
            if (retired.Size() >= kRetiredMemory)
            {
                retired.PopFront();
            }
            Retired r;
            r.peer = conn->peer;
            r.nonce = conn->nonce;
            retired.PushBack(r);
        }
        index.Remove(conn->id);
        for (usize i = 0; i < conns.Size(); ++i)
        {
            if (conns[i].Get() == conn)
            {
                conns.Remove(i);
                return;
            }
        }
    }

    // ---- sending packets ----

    void Emit(const Endpoint& to, const bytes::ByteWriter& body)
    {
        bytes::ByteWriter packet;
        packet.WriteU32(Checksum(body.Bytes().Data(), body.Bytes().Size()));
        packet.WriteBytes(body.Bytes());
        net->Transmit(self, to, packet.Bytes());
    }

    void SendConnect(Conn* c)
    {
        bytes::ByteWriter w;
        w.WriteU8(kConnect);
        w.WriteU32(c->id);
        w.WriteU64(c->nonce);
        Emit(c->peer, w);
        c->connect_last_try = Now();
        c->last_sent = Now();
    }

    void SendData(Conn* c, u8 channel, u8 rel, u32 seq, ByteSpan payload)
    {
        bytes::ByteWriter w;
        w.WriteU8(kData);
        w.WriteU32(c->remote_id);
        w.WriteU8(channel);
        w.WriteU8(rel);
        w.WriteU32(seq);
        w.WriteBytes(payload);
        Emit(c->peer, w);
        c->last_sent = Now();
        ++c->stats.packets_sent;
    }

    void SendSimple(Conn* c, u8 kind, u32 value_a, u8 channel, u8 rel, u32 seq)
    {
        bytes::ByteWriter w;
        w.WriteU8(kind);
        w.WriteU32(c->remote_id);
        if (kind == kAck)
        {
            w.WriteU8(channel);
            w.WriteU8(rel);
            w.WriteU32(seq);
        }
        else
        {
            w.WriteU32(value_a);
        }
        Emit(c->peer, w);
        c->last_sent = Now();
        ++c->stats.packets_sent;
    }

    void SendPing(Conn* c)
    {
        bytes::ByteWriter w;
        w.WriteU8(kPing);
        w.WriteU32(c->remote_id);
        w.WriteU64(Now());
        Emit(c->peer, w);
        c->last_sent = Now();
        ++c->stats.packets_sent;
    }

    // ---- events ----

    void Queue(EventRec&& rec) { queue.Push(Move(rec)); }

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

    void EndConnection(Conn* c, DisconnectReason reason, u32 code)
    {
        if (c->state == State::Closed)
        {
            return;
        }
        c->state = State::Closed;
        c->inflight.Clear();
        c->pending.Clear();
        c->pending_head = 0;
        EventRec rec;
        rec.kind = EventKind::Closed;
        rec.connection = c->id;
        rec.reason = reason;
        rec.code = code;
        Queue(Move(rec));
    }

    void DeliverMessage(Conn* c, u8 channel, u8 rel, ByteSpan payload)
    {
        EventRec rec;
        rec.kind = EventKind::Message;
        rec.connection = c->id;
        rec.channel = channel;
        rec.reliability = static_cast<Reliability>(rel);
        rec.data = CopyOf(payload);
        ++c->stats.messages_received;
        c->stats.bytes_received += payload.Size();
        Queue(Move(rec));
    }

    void OpenConnection(Conn* c)
    {
        c->state = State::Open;
        c->last_heard = Now();
        EventRec rec;
        rec.kind = EventKind::Opened;
        rec.connection = c->id;
        rec.incoming = c->incoming;
        rec.peer = c->peer;
        Queue(Move(rec));
        Pump(c);
    }

    // ---- reliability ----

    u64 Rto(const Conn* c) const
    {
        if (!c->has_rtt)
        {
            return kInitialRto;
        }
        f64 rto = 2.0 * c->srtt + 4.0 * c->rttvar;
        u64 nanos = static_cast<u64>(rto * 1e9);
        return nanos < kMinRto ? kMinRto : (nanos > kMaxRto ? kMaxRto : nanos);
    }

    void RttSample(Conn* c, f64 seconds)
    {
        if (!c->has_rtt)
        {
            c->has_rtt = true;
            c->srtt = seconds;
            c->rttvar = seconds / 2;
        }
        else
        {
            const f64 diff = c->srtt > seconds ? c->srtt - seconds : seconds - c->srtt;
            c->rttvar = 0.75 * c->rttvar + 0.25 * diff;
            c->srtt = 0.875 * c->srtt + 0.125 * seconds;
        }
        c->stats.rtt_seconds = c->srtt;
    }

    // Moves waiting messages into the window and sends them.
    void Pump(Conn* c)
    {
        if (c->state != State::Open)
        {
            return;
        }
        while (c->pending_head < c->pending.Size())
        {
            Pending& p = c->pending[c->pending_head];
            const bool reliable = IsReliable(p.rel);
            if (reliable && c->inflight.Size() >= kWindow)
            {
                break;
            }
            SendData(c, p.channel, p.rel, p.seq, ByteSpan(p.data.Data(), p.data.Size()));
            if (reliable)
            {
                InFlight f;
                f.channel = p.channel;
                f.rel = p.rel;
                f.seq = p.seq;
                f.data = Move(p.data);
                f.first_sent = Now();
                f.last_sent = f.first_sent;
                f.tries = 1;
                ++c->data_tx;
                c->inflight.Push(Move(f));
            }
            ++c->pending_head;
        }
        if (c->pending_head > 64 && c->pending_head * 2 > c->pending.Size())
        {
            c->pending.RemoveRange(0, c->pending_head);
            c->pending_head = 0;
        }
        else if (c->pending_head == c->pending.Size())
        {
            c->pending.Clear();
            c->pending_head = 0;
        }
    }

    void Retransmit(Conn* c)
    {
        const u64 now = Now();
        const u64 base = Rto(c);
        for (usize i = 0; i < c->inflight.Size(); ++i)
        {
            InFlight& f = c->inflight[i];
            u64 rto = base << (f.tries > 5 ? 4 : f.tries - 1);
            rto = rto > kMaxRto ? kMaxRto : rto;
            if (now - f.last_sent >= rto)
            {
                SendData(c, f.channel, f.rel, f.seq, ByteSpan(f.data.Data(), f.data.Size()));
                f.last_sent = now;
                ++f.tries;
                ++c->data_tx;
                ++c->data_retx;
            }
        }
        c->stats.loss = c->data_tx == 0 ? 0 : static_cast<f64>(c->data_retx) / static_cast<f64>(c->data_tx);
    }

    // ---- receiving ----

    void OnData(Conn* c, u8 channel, u8 rel, u32 seq, ByteSpan payload)
    {
        ChannelState& ch = c->channels[channel];
        RecvSpace& rs = ch.recv[rel];
        switch (static_cast<Reliability>(rel))
        {
        case Reliability::Unreliable:
        {
            if (!rs.any)
            {
                rs.any = true;
                rs.highest = seq;
                rs.mask = 0;
                DeliverMessage(c, channel, rel, payload);
            }
            else if (seq > rs.highest)
            {
                const u32 shift = seq - rs.highest;
                rs.mask = shift >= 64 ? 0 : (rs.mask << shift);
                if (shift <= 64)
                {
                    rs.mask |= u64{1} << (shift - 1);
                }
                rs.highest = seq;
                DeliverMessage(c, channel, rel, payload);
            }
            else
            {
                const u32 d = rs.highest - seq;
                if (d == 0 || d > 64 || ((rs.mask >> (d - 1)) & 1) != 0)
                {
                    break;  // a duplicate, or too old to tell
                }
                rs.mask |= u64{1} << (d - 1);
                DeliverMessage(c, channel, rel, payload);
            }
            break;
        }
        case Reliability::UnreliableSequenced:
        {
            if (!rs.any || seq > rs.highest)
            {
                rs.any = true;
                rs.highest = seq;
                DeliverMessage(c, channel, rel, payload);
            }
            break;
        }
        case Reliability::ReliableUnordered:
        {
            if (seq >= rs.next && seq - rs.next > kLookahead)
            {
                break;  // further ahead than an honest sender's window allows: neither delivered nor acknowledged
            }
            SendSimple(c, kAck, 0, channel, rel, seq);
            if (seq < rs.next)
            {
                break;
            }
            if (!rs.above.Insert(seq))
            {
                return;
            }
            DeliverMessage(c, channel, rel, payload);
            while (rs.above.Remove(rs.next))
            {
                ++rs.next;
            }
            break;
        }
        case Reliability::ReliableOrdered:
        {
            if (seq >= rs.next && seq - rs.next > kLookahead)
            {
                break;  // not acknowledged either: the sender must not believe it was received
            }
            SendSimple(c, kAck, 0, channel, rel, seq);
            if (seq < rs.next)
            {
                break;
            }
            if (seq != rs.next)
            {
                if (rs.buffered.Contains(seq))
                {
                    return;
                }
                rs.buffered.Insert(seq, CopyOf(payload));
                break;
            }
            DeliverMessage(c, channel, rel, payload);
            ++rs.next;
            for (Vector<byte>* next = rs.buffered.Find(rs.next); next != nullptr; next = rs.buffered.Find(rs.next))
            {
                const Vector<byte> message = Move(
                    *next);  // out of the map first: DeliverMessage runs the receiver's code
                rs.buffered.Remove(rs.next);
                DeliverMessage(c, channel, rel, ByteSpan(message.Data(), message.Size()));
                ++rs.next;  // after the delivery, as before
            }
            break;
        }
        }
    }

    void OnAck(Conn* c, u8 channel, u8 rel, u32 seq)
    {
        for (usize i = 0; i < c->inflight.Size(); ++i)
        {
            const InFlight& f = c->inflight[i];
            if (f.channel == channel && f.rel == rel && f.seq == seq)
            {
                if (f.tries == 1)
                {
                    RttSample(c, static_cast<f64>(Now() - f.first_sent) / 1e9);
                }
                c->inflight.Remove(i);
                Pump(c);
                return;
            }
        }
    }

    void Receive(const Endpoint& from, ByteSpan packet)
    {
        if (packet.Size() < 5)
        {
            return;
        }
        bytes::ByteReader r(packet);
        u32 sum = 0;
        if (r.ReadU32(&sum) != bytes::ByteError::Ok || Checksum(packet.Data() + 4, packet.Size() - 4) != sum)
        {
            return;  // corrupted: dropped like a lost packet
        }
        u8 kind = 0;
        if (r.ReadU8(&kind) != bytes::ByteError::Ok)
        {
            return;
        }
        if (kind == kConnect)
        {
            u32 client_id = 0;
            u64 nonce = 0;
            if (r.ReadU32(&client_id) != bytes::ByteError::Ok || r.ReadU64(&nonce) != bytes::ByteError::Ok)
            {
                return;
            }
            OnConnect(from, client_id, nonce);
            return;
        }
        if (kind == kCancel)
        {
            u32 client_id = 0;
            u64 nonce = 0;
            u32 code = 0;
            if (r.ReadU32(&client_id) == bytes::ByteError::Ok && r.ReadU64(&nonce) == bytes::ByteError::Ok &&
                r.ReadU32(&code) == bytes::ByteError::Ok)
            {
                for (usize i = 0; i < conns.Size(); ++i)
                {
                    Conn* c = conns[i].Get();
                    if (c->incoming && c->peer == from && c->remote_id == client_id && c->nonce == nonce)
                    {
                        EndConnection(c, DisconnectReason::PeerClosed, code);
                        break;
                    }
                }
            }
            return;
        }
        u32 dest = 0;
        if (r.ReadU32(&dest) != bytes::ByteError::Ok)
        {
            return;
        }
        Conn* c = Find(dest);
        if (c == nullptr || !(c->peer == from) || c->state == State::Closed)
        {
            return;
        }
        if (kind == kAccept)
        {
            u32 server_id = 0;
            if (r.ReadU32(&server_id) == bytes::ByteError::Ok && c->state == State::Connecting && !c->incoming)
            {
                c->remote_id = server_id;
                OpenConnection(c);
            }
            return;
        }
        if (kind == kRefuse)
        {
            if (c->state == State::Connecting)
            {
                EndConnection(c, DisconnectReason::Refused, 0);
            }
            return;
        }
        if (c->state != State::Open)
        {
            return;
        }
        c->last_heard = Now();
        ++c->stats.packets_received;
        switch (kind)
        {
        case kData:
        {
            u8 channel = 0;
            u8 rel = 0;
            u32 seq = 0;
            if (r.ReadU8(&channel) != bytes::ByteError::Ok || r.ReadU8(&rel) != bytes::ByteError::Ok ||
                r.ReadU32(&seq) != bytes::ByteError::Ok || channel >= cfg.channels || rel > 3)
            {
                return;
            }
            const usize size = r.Remaining();
            if (size == 0 || size > cfg.max_message_bytes)
            {
                return;
            }
            OnData(c, channel, rel, seq, ByteSpan(packet.Data() + r.Position(), size));
            break;
        }
        case kAck:
        {
            u8 channel = 0;
            u8 rel = 0;
            u32 seq = 0;
            if (r.ReadU8(&channel) == bytes::ByteError::Ok && r.ReadU8(&rel) == bytes::ByteError::Ok &&
                r.ReadU32(&seq) == bytes::ByteError::Ok)
            {
                OnAck(c, channel, rel, seq);
            }
            break;
        }
        case kPing:
        {
            u64 sent = 0;
            if (r.ReadU64(&sent) == bytes::ByteError::Ok)
            {
                bytes::ByteWriter w;
                w.WriteU8(kPong);
                w.WriteU32(c->remote_id);
                w.WriteU64(sent);
                Emit(c->peer, w);
                c->last_sent = Now();
                ++c->stats.packets_sent;
            }
            break;
        }
        case kPong:
        {
            u64 sent = 0;
            if (r.ReadU64(&sent) == bytes::ByteError::Ok && sent <= Now())
            {
                RttSample(c, static_cast<f64>(Now() - sent) / 1e9);
            }
            break;
        }
        case kClose:
        {
            u32 code = 0;
            if (r.ReadU32(&code) == bytes::ByteError::Ok)
            {
                EndConnection(c, DisconnectReason::PeerClosed, code);
            }
            break;
        }
        default: break;
        }
    }

    void OnConnect(const Endpoint& from, u32 client_id, u64 nonce)
    {
        bytes::ByteWriter w;
        // A retransmitted request for a connection we already made: answer again.
        for (usize i = 0; i < conns.Size(); ++i)
        {
            Conn* c = conns[i].Get();
            if (c->incoming && c->peer == from && c->remote_id == client_id && c->nonce == nonce)
            {
                if (c->state != State::Closed)
                {
                    SendAccept(c);
                }
                return;
            }
        }
        // A late repeat of a request whose connection already ended and was reported: ignore it.
        for (usize i = 0; i < retired.Size(); ++i)
        {
            if (retired[i].peer == from && retired[i].nonce == nonce)
            {
                return;
            }
        }
        if (!listening || conns.Size() >= cfg.max_connections)
        {
            w.WriteU8(kRefuse);
            w.WriteU32(client_id);
            Emit(from, w);
            return;
        }
        Conn* c = Create(from, true);
        c->remote_id = client_id;
        c->nonce = nonce;
        SendAccept(c);
        OpenConnection(c);
    }

    void SendAccept(Conn* c)
    {
        bytes::ByteWriter w;
        w.WriteU8(kAccept);
        w.WriteU32(c->remote_id);
        w.WriteU32(c->id);
        Emit(c->peer, w);
        c->last_sent = Now();
    }

    void Tick(u64 now)
    {
        for (usize i = 0; i < conns.Size(); ++i)
        {
            Conn* c = conns[i].Get();
            if (c->state == State::Connecting)
            {
                if (now - c->created >= cfg.connect_timeout_nanos)
                {
                    EndConnection(c, DisconnectReason::TimedOut, 0);
                }
                else if (now - c->connect_last_try >= kConnectRetryNanos)
                {
                    SendConnect(c);
                }
            }
            else if (c->state == State::Open)
            {
                if (now - c->last_heard >= cfg.timeout_nanos)
                {
                    EndConnection(c, DisconnectReason::TimedOut, 0);
                    continue;
                }
                Retransmit(c);
                if (now - c->last_sent >= cfg.keepalive_nanos)
                {
                    SendPing(c);
                }
            }
        }
        for (usize i = 0; i < lingering.Size();)
        {
            Lingering& l = lingering[i];
            if (now >= l.next_send)
            {
                bytes::ByteWriter w;
                w.WriteU8(l.kind);
                w.WriteU32(l.remote_id);
                if (l.kind == kCancel)
                {
                    w.WriteU64(l.nonce);
                }
                w.WriteU32(l.code);
                Emit(l.peer, w);
                l.next_send = now + 50'000'000;
                --l.sends_left;
            }
            if (l.sends_left == 0)
            {
                lingering.Remove(i);
            }
            else
            {
                ++i;
            }
        }
    }
};

SimulatedTransport::SimulatedTransport(SimNetwork& network, const Endpoint& local, const SimTransportConfig& config)
    : network_(&network), local_(local), config_(config), impl_(MakeUnique<Impl>(network, local, config))
{
    attached_ = network.Attach(local, this).Ok();
}

SimulatedTransport::~SimulatedTransport()
{
    if (attached_)
    {
        network_->Detach(local_);  // only what this transport attached: the endpoint may belong to another sink
    }
}

void SimulatedTransport::OnPacket(const Endpoint& from, ByteSpan packet)
{
    impl_->Receive(from, packet);
}

void SimulatedTransport::OnTick(u64 now_nanos)
{
    impl_->Tick(now_nanos);
}

void SimulatedTransport::OnUnreachable(const Endpoint& to)
{
    // Nothing is attached at `to`: a connect attempt there is refused, the way an unused port answers.
    for (usize i = 0; i < impl_->conns.Size(); ++i)
    {
        Conn* c = impl_->conns[i].Get();
        if (c->state == State::Connecting && c->peer == to)
        {
            impl_->EndConnection(c, DisconnectReason::Refused, 0);
        }
    }
}

Status SimulatedTransport::Listen(const Endpoint& at)
{
    if (!(at == local_) || impl_->listening)
    {
        return Status::Of(ErrorCode::Invalid);
    }
    impl_->listening = true;
    return Status::Success();
}

Status SimulatedTransport::Connect(const Endpoint& to, ConnectionId* connection)
{
    if (impl_->conns.Size() >= config_.max_connections)
    {
        return Status::Of(ErrorCode::LimitExceeded);
    }
    Conn* c = impl_->Create(to, false);
    c->nonce = network_->NewNonce();
    impl_->SendConnect(c);
    *connection = c->id;
    return Status::Success();
}

Status SimulatedTransport::Send(ConnectionId connection, u8 channel, ByteSpan bytes, Reliability reliability)
{
    Conn* c = impl_->Find(connection);
    if (c == nullptr || channel >= config_.channels || bytes.Size() == 0 || static_cast<u8>(reliability) > 3)
    {
        return Status::Of(ErrorCode::Invalid);
    }
    if (c->state == State::Closed)
    {
        return Status::Of(ErrorCode::Closed);
    }
    if (bytes.Size() > config_.max_message_bytes)
    {
        return Status::Of(ErrorCode::MessageTooLarge);
    }
    const u8 rel = static_cast<u8>(reliability);
    const bool reliable = IsReliable(rel);
    const usize waiting = c->pending.Size() - c->pending_head;
    if ((reliable && waiting + c->inflight.Size() >= config_.max_queued_messages) ||
        (!reliable && c->state == State::Connecting && waiting >= config_.max_queued_messages))
    {
        return Status::Of(ErrorCode::LimitExceeded);
    }
    const u32 seq = c->channels[channel].send_next[rel]++;
    ++c->stats.messages_sent;
    c->stats.bytes_sent += bytes.Size();
    if (!reliable && c->state == State::Open)
    {
        impl_->SendData(c, channel, rel, seq, bytes);
        return Status::Success();
    }
    Pending p;
    p.channel = channel;
    p.rel = rel;
    p.seq = seq;
    p.data = CopyOf(bytes);
    c->pending.Push(Move(p));
    impl_->Pump(c);
    return Status::Success();
}

bool SimulatedTransport::Poll(Event* event)
{
    Impl& m = *impl_;
    if (m.queue_head >= m.queue.Size())
    {
        m.queue.Clear();
        m.queue_head = 0;
        return false;
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
        Conn* c = m.Find(m.current.connection);
        if (c != nullptr)
        {
            m.Remove(c);
        }
    }
    return true;
}

Status SimulatedTransport::Close(ConnectionId connection, u32 code)
{
    Conn* c = impl_->Find(connection);
    if (c == nullptr)
    {
        return Status::Of(ErrorCode::Invalid);
    }
    if (c->state == State::Closed)
    {
        return Status::Of(ErrorCode::Closed);
    }
    if (c->state == State::Open || (c->state == State::Connecting && !c->incoming))
    {
        Lingering l;
        l.kind = c->state == State::Open ? kClose : kCancel;  // the server may have accepted already
        l.peer = c->peer;
        l.remote_id = c->state == State::Open ? c->remote_id : c->id;
        l.nonce = c->nonce;
        l.code = code;
        l.sends_left = 3;
        l.next_send = impl_->Now();
        impl_->lingering.Push(l);
    }
    impl_->PurgeMessages(connection);
    impl_->EndConnection(c, DisconnectReason::LocalClose, 0);
    return Status::Success();
}

Status SimulatedTransport::Statistics(ConnectionId connection, ConnectionStats* stats) const
{
    const Conn* c = impl_->Find(connection);
    if (c == nullptr)
    {
        return Status::Of(ErrorCode::Invalid);
    }
    *stats = c->stats;
    stats->send_queue_messages = c->inflight.Size() + (c->pending.Size() - c->pending_head);
    usize bytes = 0;
    for (usize i = 0; i < c->inflight.Size(); ++i)
    {
        bytes += c->inflight[i].data.Size();
    }
    for (usize i = c->pending_head; i < c->pending.Size(); ++i)
    {
        if (IsReliable(c->pending[i].rel))
        {
            bytes += c->pending[i].data.Size();
        }
    }
    stats->send_queue_bytes = bytes;
    return Status::Success();
}

}  // namespace engine_placeholder::net
