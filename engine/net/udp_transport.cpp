#include "net/udp_transport.h"

#include "containers/array.h"
#include "cstr/mem_ops.h"
#include "lang/utility.h"
#include "mem/heap_allocator.h"
#include "os/os.h"

namespace engine_placeholder::net
{

namespace
{

constexpr u8 kUdpFrameMessage = 1;
constexpr usize kUdpFrameHeaderBytes = 1 + 1 + 2;
constexpr usize kReceiveBatch = 256;        // datagrams taken from the port per Poll
constexpr usize kUdpRetiredMemory = 16384;  // closed handshakes remembered while their cookie lives
constexpr usize
    kUdpRetiredPerPrefix = 1024;  // per /24 (IPv4) or /48 (IPv6): sixteen networks are needed to fill the table
constexpr usize kUdpRetiredPerAddress = 64;  // of them, per source address: a churning address is denied alone
constexpr u64 kMaxHandshakeRetry = 1'000'000'000ull;

enum class UdpState : u8
{
    Connecting,
    Connected,
    Closed,
};

enum class Phase : u8
{
    Request,   // client: waiting for the Challenge
    Response,  // client: waiting for Accepted
};

struct UdpConn
{
    ConnectionId id = 0;
    u16 wire_id = 0;  // the server's id for the connection, in every payload header
    Endpoint peer;
    bool incoming = false;
    UdpState state = UdpState::Connecting;
    Phase phase = Phase::Request;
    u64 client_salt = 0;
    u64 cookie = 0;
    u64 epoch = 0;
    u64 created = 0;
    u64 last_heard = 0;
    u64 last_sent = 0;
    u64 last_try = 0;
    u64 retry_nanos = 0;
    bool ack_pending = false;
    u16 send_seq = 0;
    udp::ReceiveWindow recv;
    udp::SendLog log;
    UdpConnectionInfo info;
    ConnectionStats stats;
};

struct UdpEventRec
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

struct UdpLingering
{
    Endpoint peer;
    u16 wire_id = 0;
    u32 code = 0;
    u32 sends_left = 0;
    u64 next_send = 0;
};

struct UdpRetired
{
    u64 key = 0;     // keyed hash of (endpoint, salt): unpredictable to an attacker, so it cannot be made to collide
    Address group;   // the source address, or its /64 for IPv6: the unit of the quota
    Address prefix;  // its /24 (IPv4) or /48 (IPv6): the unit of the wider quota
};

u64 ReadU64(ByteSpan b, usize at)
{
    u64 v = 0;
    for (u32 i = 0; i < 8; ++i)
    {
        v |= static_cast<u64>(static_cast<u8>(b[at + i])) << (8 * i);
    }
    return v;
}

u16 ReadU16(ByteSpan b, usize at)
{
    return static_cast<u16>(static_cast<u8>(b[at]) | (static_cast<u8>(b[at + 1]) << 8));
}

void PutU64(Vector<byte>* out, u64 v)
{
    for (u32 i = 0; i < 8; ++i)
    {
        out->Push(static_cast<byte>((v >> (8 * i)) & 0xff));
    }
}

void PutU16(Vector<byte>* out, u16 v)
{
    out->Push(static_cast<byte>(v & 0xff));
    out->Push(static_cast<byte>(v >> 8));
}

void PutU32(Vector<byte>* out, u32 v)
{
    for (u32 i = 0; i < 4; ++i)
    {
        out->Push(static_cast<byte>((v >> (8 * i)) & 0xff));
    }
}

void PadTo(Vector<byte>* out, usize size)
{
    while (out->Size() < size)
    {
        out->Push(static_cast<byte>(0));
    }
}

}  // namespace

struct UdpTransport::Impl
{
    Impl(DatagramPort& p, const Clock& c, const UdpTransportConfig& cfg)
        : port(&p), clock(&c), config(cfg), self(p.LocalEndpoint()), by_id(HeapAllocator::Default()),
          by_wire(HeapAllocator::Default()), retired_keys(HeapAllocator::Default()),
          retired_per_group(HeapAllocator::Default()), retired_per_prefix(HeapAllocator::Default())
    {
        state = cfg.seed;
        if (config.max_connections > 65535)
        {
            config.max_connections = 65535;  // wire ids are 16 bits
        }
        key0 = Random();
        key1 = Random();
    }

    DatagramPort* port;
    const Clock* clock;
    UdpTransportConfig config;
    Endpoint self;
    bool listening = false;
    u64 state;
    u64 key0 = 0;
    u64 key1 = 0;
    ConnectionId next_id = 1;
    u16 next_wire = 1;
    Vector<UniquePtr<UdpConn>> conns;
    HashMap<ConnectionId, UdpConn*, HeapAllocator> by_id;  // every connection by its local id
    HashMap<u16, UdpConn*, HeapAllocator> by_wire;         // incoming connections by their wire id
    Vector<UdpEventRec> queue;
    usize queue_head = 0;
    UdpEventRec current;
    Vector<UdpLingering> lingering;
    // Remembered handshakes, in two buckets by the period in which their cookie expires (a cookie expires after the
    // period following the one it was made in, and an entry is only kept while that is not over, so the current and the
    // next period are the only two that can be live).
    Vector<UdpRetired> retired[2];
    u64 retired_expiry[2] = {0, 0};
    usize retired_count = 0;
    HashMap<u64, u8, HeapAllocator> retired_keys;
    HashMap<Address, u32, HeapAllocator> retired_per_group;
    HashMap<Address, u32, HeapAllocator> retired_per_prefix;
    UdpCounters counters;
    Array<byte, 2048> buffer;

    u64 Now() const { return clock->NowNanos(); }

    u64 Random()
    {
        if (state == 0 && config.seed == 0)
        {
            u64 v = 0;
            os::FillRandomBytes(&v, sizeof(v));
            return v;
        }
        if (state == 0)
        {
            state = 0x9e3779b97f4a7c15ull;
        }
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }

    UdpConn* FindById(ConnectionId id) const
    {
        UdpConn* const* found = by_id.Find(id);
        return found == nullptr ? nullptr : *found;
    }

    UdpConn* Create(const Endpoint& peer, bool incoming)
    {
        UniquePtr<UdpConn> conn = MakeUnique<UdpConn>();
        conn->id = next_id++;
        conn->peer = peer;
        conn->incoming = incoming;
        conn->created = Now();
        conn->last_heard = conn->created;
        conn->last_sent = conn->created;
        conn->send_seq = config.initial_sequence;
        UdpConn* raw = conn.Get();
        by_id.Insert(raw->id, raw);
        conns.Push(Move(conn));
        return raw;
    }

    // The quota groups IPv6 addresses by their /64, the size of one subscriber's allocation; a wider quota groups IPv4
    // by /24 and IPv6 by /48, so no single network can fill the table.
    static Address GroupOf(const Address& a, usize keep_bytes_v6)
    {
        if (!a.IsV6())
        {
            return a;
        }
        Array<u8, 16> bytes = a.Bytes();
        for (usize i = keep_bytes_v6; i < 16; ++i)
        {
            bytes[i] = 0;
        }
        return Address::V6(bytes, 0);
    }
    static Address GroupOf(const Address& a) { return GroupOf(a, 8); }
    static Address PrefixOf(const Address& a)
    {
        if (a.IsV6())
        {
            return GroupOf(a, 6);
        }
        const Array<u8, 16>& b = a.Bytes();
        return Address::V4(static_cast<u8>(b[0]), static_cast<u8>(b[1]), static_cast<u8>(b[2]), 0);
    }

    u64 HandshakeKey(const Endpoint& from, u64 salt) const { return Cookie(from, salt, ~u64{0}); }

    static void CountUp(HashMap<Address, u32, HeapAllocator>& map, const Address& key)
    {
        u32* count = map.Find(key);
        if (count == nullptr)
        {
            map.Insert(key, 1);
        }
        else
        {
            ++*count;
        }
    }

    static void CountDown(HashMap<Address, u32, HeapAllocator>& map, const Address& key)
    {
        u32* count = map.Find(key);
        if (count != nullptr && --*count == 0)
        {
            map.Remove(key);
        }
    }

    void ClearBucket(usize bucket)
    {
        for (usize i = 0; i < retired[bucket].Size(); ++i)
        {
            const UdpRetired& r = retired[bucket][i];
            retired_keys.Remove(r.key);
            CountDown(retired_per_group, r.group);
            CountDown(retired_per_prefix, r.prefix);
        }
        retired_count -= retired[bucket].Size();
        retired[bucket].Clear();
    }

    // Remembers that this handshake produced a connection that has ended, until its cookie has expired. A cookie that
    // has expired already needs no memory.
    void Retire(const UdpConn* c)
    {
        const u64 expiry = c->epoch + 1;
        if (Epoch() > expiry)
        {
            return;
        }
        const usize bucket = expiry & 1;
        if (retired_expiry[bucket] != expiry)
        {
            ClearBucket(bucket);  // what is left in it expired two periods ago
            retired_expiry[bucket] = expiry;
        }
        UdpRetired r;
        r.key = HandshakeKey(c->peer, c->client_salt);
        r.group = GroupOf(c->peer.GetAddress());
        r.prefix = PrefixOf(c->peer.GetAddress());
        retired[bucket].Push(r);
        ++retired_count;
        retired_keys.Insert(r.key, 1);
        CountUp(retired_per_group, r.group);
        CountUp(retired_per_prefix, r.prefix);
    }

    // Forgets the handshakes whose cookies can no longer be presented.
    void PurgeRetired()
    {
        for (usize bucket = 0; bucket < 2; ++bucket)
        {
            if (!retired[bucket].Empty() && Epoch() > retired_expiry[bucket])
            {
                ClearBucket(bucket);
            }
        }
    }

    void Remove(UdpConn* c)
    {
        by_id.Remove(c->id);
        if (c->incoming)
        {
            by_wire.Remove(c->wire_id);
            Retire(c);
        }
        for (usize i = 0; i < conns.Size(); ++i)
        {
            if (conns[i].Get() == c)
            {
                conns.Remove(i);
                return;
            }
        }
    }

    // ---- sending ----

    void Emit(const Endpoint& to, const Vector<byte>& packet)
    {
        ++counters.datagrams_sent;
        if (port->SendTo(ByteSpan(packet.Data(), packet.Size()), to).Failed())
        {
            ++counters.send_failures;
        }
    }

    u64 Cookie(const Endpoint& from, u64 salt, u64 epoch) const
    {
        Vector<byte> in;
        PutU64(&in, epoch);
        PutU64(&in, salt);
        in.Push(static_cast<byte>(from.GetAddress().Family()));
        const Array<u8, 16>& bytes = from.GetAddress().Bytes();
        for (usize i = 0; i < 16; ++i)
        {
            in.Push(static_cast<byte>(bytes[i]));
        }
        PutU32(&in, from.GetAddress().ScopeId());
        PutU16(&in, from.Port());
        return udp::SipHash24(key0, key1, ByteSpan(in.Data(), in.Size()));
    }

    u64 Epoch() const { return Now() / config.cookie_period_nanos; }

    bool CookieValid(const Endpoint& from, u64 salt, u64 cookie) const
    {
        const u64 epoch = Epoch();
        return cookie == Cookie(from, salt, epoch) || (epoch > 0 && cookie == Cookie(from, salt, epoch - 1));
    }

    void SendRequest(UdpConn* c)
    {
        Vector<byte> p;
        p.Push(static_cast<byte>(udp::FirstByte(udp::PacketKind::ConnectRequest)));
        PutU64(&p, c->client_salt);
        PadTo(&p, udp::kHandshakeBytes);
        Emit(c->peer, p);
    }

    void SendResponse(UdpConn* c)
    {
        Vector<byte> p;
        p.Push(static_cast<byte>(udp::FirstByte(udp::PacketKind::ChallengeResponse)));
        PutU64(&p, c->client_salt);
        PutU64(&p, c->cookie);
        PadTo(&p, udp::kHandshakeBytes);
        Emit(c->peer, p);
    }

    void SendAccepted(UdpConn* c)
    {
        Vector<byte> p;
        p.Push(static_cast<byte>(udp::FirstByte(udp::PacketKind::Accepted)));
        PutU64(&p, c->client_salt);
        PutU16(&p, c->wire_id);
        Emit(c->peer, p);
        c->last_sent = Now();
    }

    void SendDenied(const Endpoint& to, u64 salt, udp::DenyReason reason)
    {
        Vector<byte> p;
        p.Push(static_cast<byte>(udp::FirstByte(udp::PacketKind::Denied)));
        PutU64(&p, salt);
        p.Push(static_cast<byte>(reason));
        ++counters.denied_sent;
        Emit(to, p);
    }

    // A payload packet with one frame, or none (an acknowledgement or a keep-alive).
    void SendPayload(UdpConn* c, bool with_frame, u8 channel, ByteSpan message)
    {
        Vector<byte> p;
        udp::PayloadHeader h;
        h.connection = c->wire_id;
        h.sequence = c->send_seq;
        h.has_ack = c->recv.Any();
        h.ack = c->recv.Latest();
        h.ack_bits = c->recv.Bits();
        udp::WritePayloadHeader(h, &p);
        if (with_frame)
        {
            p.Push(static_cast<byte>(kUdpFrameMessage));
            p.Push(static_cast<byte>(channel));
            PutU16(&p, static_cast<u16>(message.Size()));
            for (usize i = 0; i < message.Size(); ++i)
            {
                p.Push(message[i]);
            }
        }
        c->log.OnSent(c->send_seq);
        ++c->send_seq;
        c->info.next_sequence = c->send_seq;
        c->last_sent = Now();
        c->ack_pending = false;
        ++c->stats.packets_sent;
        Emit(c->peer, p);
    }

    // ---- events ----

    void Queue(UdpEventRec&& rec) { queue.Push(Move(rec)); }

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

    void End(UdpConn* c, DisconnectReason reason, u32 code)
    {
        if (c->state == UdpState::Closed)
        {
            return;
        }
        c->state = UdpState::Closed;
        c->info.connected = false;
        UdpEventRec rec;
        rec.kind = EventKind::Closed;
        rec.connection = c->id;
        rec.reason = reason;
        rec.code = code;
        Queue(Move(rec));
    }

    void Open(UdpConn* c)
    {
        c->state = UdpState::Connected;
        c->info.connected = true;
        c->last_heard = Now();
        UdpEventRec rec;
        rec.kind = EventKind::Opened;
        rec.connection = c->id;
        rec.incoming = c->incoming;
        rec.peer = c->peer;
        Queue(Move(rec));
    }

    // ---- receiving ----

    UdpConn* FindPayloadTarget(const Endpoint& from, u16 wire_id) const
    {
        UdpConn* const* found = by_wire.Find(wire_id);
        if (found != nullptr && (*found)->peer == from)
        {
            return *found;
        }
        for (usize i = 0; i < conns.Size(); ++i)
        {
            UdpConn* c = conns[i].Get();
            if (!c->incoming && c->state == UdpState::Connected && c->wire_id == wire_id && c->peer == from)
            {
                return c;
            }
        }
        return nullptr;
    }

    void OnRequest(const Endpoint& from, ByteSpan packet)
    {
        if (packet.Size() < udp::kHandshakeBytes)
        {
            ++counters.dropped_short;  // not full-size: never answered, so no amplification
            return;
        }
        const u64 salt = ReadU64(packet, 1);
        if (!listening)
        {
            SendDenied(from, salt, udp::DenyReason::NotListening);
            return;
        }
        Vector<byte> p;
        p.Push(static_cast<byte>(udp::FirstByte(udp::PacketKind::Challenge)));
        PutU64(&p, salt);
        PutU64(&p, Cookie(from, salt, Epoch()));
        ++counters.challenges_sent;
        Emit(from, p);
    }

    void OnResponse(const Endpoint& from, ByteSpan packet)
    {
        if (packet.Size() < udp::kHandshakeBytes)
        {
            ++counters.dropped_short;
            return;
        }
        const u64 salt = ReadU64(packet, 1);
        const u64 cookie = ReadU64(packet, 9);
        if (!listening || !CookieValid(from, salt, cookie))
        {
            ++counters.dropped_bad_cookie;
            return;
        }
        usize from_address = 0;
        for (usize i = 0; i < conns.Size(); ++i)
        {
            UdpConn* c = conns[i].Get();
            if (!c->incoming)
            {
                continue;
            }
            if (c->peer == from && c->client_salt == salt)
            {
                if (c->state == UdpState::Connected)
                {
                    SendAccepted(c);  // our answer was lost: say it again
                }
                return;
            }
            from_address += c->state != UdpState::Closed && c->peer.GetAddress() == from.GetAddress() ? 1 : 0;
        }
        PurgeRetired();
        if (retired_keys.Find(HandshakeKey(from, salt)) != nullptr)
        {
            ++counters.dropped_stale;  // a replay of a handshake whose connection already ended
            return;
        }
        // Fail closed: an entry whose cookie can still be presented is never forgotten to make room. One address (or
        // IPv6 /64) that churns through handshakes is denied alone; a full table denies new handshakes until the
        // oldest cookies expire.
        const u32* here = retired_per_group.Find(GroupOf(from.GetAddress()));
        const u32* wider = retired_per_prefix.Find(PrefixOf(from.GetAddress()));
        if ((here != nullptr && *here >= kUdpRetiredPerAddress) || (wider != nullptr && *wider >= kUdpRetiredPerPrefix))
        {
            SendDenied(from, salt, udp::DenyReason::AddressLimit);
            return;
        }
        if (retired_count >= kUdpRetiredMemory)
        {
            SendDenied(from, salt, udp::DenyReason::ServerFull);
            return;
        }
        if (conns.Size() >= config.max_connections)
        {
            SendDenied(from, salt, udp::DenyReason::ServerFull);
            return;
        }
        if (from_address >= config.max_connections_per_address)
        {
            SendDenied(from, salt, udp::DenyReason::AddressLimit);
            return;
        }
        UdpConn* c = Create(from, true);
        c->client_salt = salt;
        c->cookie = cookie;
        c->epoch = Epoch();
        for (u32 tries = 0; tries < 65536; ++tries)
        {
            if (next_wire == 0)
            {
                next_wire = 1;
            }
            if (by_wire.Find(next_wire) == nullptr)
            {
                break;
            }
            ++next_wire;
        }
        c->wire_id = next_wire++;
        by_wire.Insert(c->wire_id, c);
        SendAccepted(c);
        Open(c);
    }

    void OnChallenge(const Endpoint& from, ByteSpan packet)
    {
        if (packet.Size() < 17)
        {
            ++counters.dropped_short;
            return;
        }
        const u64 salt = ReadU64(packet, 1);
        for (usize i = 0; i < conns.Size(); ++i)
        {
            UdpConn* c = conns[i].Get();
            if (!c->incoming && c->state == UdpState::Connecting && c->peer == from && c->client_salt == salt)
            {
                c->cookie = ReadU64(packet, 9);
                c->phase = Phase::Response;
                c->retry_nanos = config.handshake_retry_nanos;
                c->last_try = Now();
                SendResponse(c);
                return;
            }
        }
        ++counters.dropped_stale;
    }

    void OnAccepted(const Endpoint& from, ByteSpan packet)
    {
        if (packet.Size() < 11)
        {
            ++counters.dropped_short;
            return;
        }
        const u64 salt = ReadU64(packet, 1);
        for (usize i = 0; i < conns.Size(); ++i)
        {
            UdpConn* c = conns[i].Get();
            if (!c->incoming && c->state == UdpState::Connecting && c->phase == Phase::Response && c->peer == from &&
                c->client_salt == salt)
            {
                c->wire_id = ReadU16(packet, 9);
                Open(c);
                return;
            }
        }
        ++counters.dropped_stale;
    }

    void OnDenied(const Endpoint& from, ByteSpan packet)
    {
        if (packet.Size() < 10)
        {
            ++counters.dropped_short;
            return;
        }
        const u64 salt = ReadU64(packet, 1);
        for (usize i = 0; i < conns.Size(); ++i)
        {
            UdpConn* c = conns[i].Get();
            if (!c->incoming && c->state == UdpState::Connecting && c->peer == from && c->client_salt == salt)
            {
                End(c, DisconnectReason::Refused, 0);
                return;
            }
        }
    }

    void OnPayload(const Endpoint& from, ByteSpan packet)
    {
        udp::PayloadHeader h;
        ByteSpan body;
        if (!udp::ReadPayloadHeader(packet, &h, &body))
        {
            ++counters.dropped_short;
            return;
        }
        UdpConn* c = FindPayloadTarget(from, h.connection);
        if (c == nullptr)
        {
            for (usize i = 0; i < lingering.Size(); ++i)
            {
                if (lingering[i].wire_id == h.connection && lingering[i].peer == from)
                {
                    return;  // we are telling this peer it is closed: a Reset would race the Disconnect
                }
            }
            if (listening)
            {
                Vector<byte> p;  // tell a client whose connection we lost, instead of leaving it to time out
                p.Push(static_cast<byte>(udp::FirstByte(udp::PacketKind::Reset)));
                PutU16(&p, h.connection);
                ++counters.resets_sent;
                Emit(from, p);
            }
            return;
        }
        if (c->state != UdpState::Connected)
        {
            return;
        }
        const udp::SeqVerdict verdict = c->recv.OnReceive(h.sequence);
        if (verdict == udp::SeqVerdict::Duplicate)
        {
            ++c->info.duplicates;
            ++counters.dropped_duplicate;
            return;
        }
        if (verdict == udp::SeqVerdict::TooOld)
        {
            ++c->info.too_old;
            ++counters.dropped_stale;
            return;
        }
        c->info.any_received = true;
        c->info.latest_received = c->recv.Latest();
        c->last_heard = Now();
        ++c->stats.packets_received;
        if (h.has_ack)
        {
            c->info.packets_acked += c->log.OnAck(h.ack, h.ack_bits, nullptr);
        }
        usize at = 0;
        bool had_frame = false;
        while (at < body.Size())
        {
            if (body.Size() - at < kUdpFrameHeaderBytes || static_cast<u8>(body[at]) != kUdpFrameMessage)
            {
                break;  // a frame we do not know: the rest of the packet cannot be read
            }
            const u8 channel = static_cast<u8>(body[at + 1]);
            const usize length = ReadU16(body, at + 2);
            at += kUdpFrameHeaderBytes;
            if (length == 0 || length > body.Size() - at || channel >= config.channels)
            {
                break;
            }
            UdpEventRec rec;
            rec.kind = EventKind::Message;
            rec.connection = c->id;
            rec.channel = channel;
            rec.reliability = Reliability::Unreliable;
            rec.data.TryResizeUninitialized(length);
            MemCopy(rec.data.Data(), body.Data() + at, length);
            ++c->stats.messages_received;
            c->stats.bytes_received += length;
            Queue(Move(rec));
            at += length;
            had_frame = true;
        }
        c->ack_pending = c->ack_pending ||
                         had_frame;  // an empty packet is never answered, or two peers would ping-pong
    }

    void OnDisconnect(const Endpoint& from, ByteSpan packet)
    {
        if (packet.Size() < 7)
        {
            ++counters.dropped_short;
            return;
        }
        UdpConn* c = FindPayloadTarget(from, ReadU16(packet, 1));
        if (c != nullptr && c->state == UdpState::Connected)
        {
            u32 code = 0;
            for (u32 i = 0; i < 4; ++i)
            {
                code |= static_cast<u32>(static_cast<u8>(packet[3 + i])) << (8 * i);
            }
            End(c, DisconnectReason::PeerClosed, code);
        }
    }

    void OnReset(const Endpoint& from, ByteSpan packet)
    {
        if (packet.Size() < 3)
        {
            ++counters.dropped_short;
            return;
        }
        UdpConn* c = FindPayloadTarget(from, ReadU16(packet, 1));
        if (c != nullptr && !c->incoming && c->state == UdpState::Connected)
        {
            End(c, DisconnectReason::Reset, 0);
        }
    }

    void Receive(const Endpoint& from, ByteSpan packet)
    {
        ++counters.datagrams_received;
        if (packet.Size() > udp::kMtu)
        {
            ++counters.dropped_oversize;
            return;
        }
        udp::PacketKind kind = udp::PacketKind::Payload;
        if (packet.Size() == 0 || !udp::ParseFirstByte(static_cast<u8>(packet[0]), &kind))
        {
            ++counters.dropped_unknown;
            return;
        }
        switch (kind)
        {
        case udp::PacketKind::ConnectRequest: OnRequest(from, packet); break;
        case udp::PacketKind::Challenge: OnChallenge(from, packet); break;
        case udp::PacketKind::ChallengeResponse: OnResponse(from, packet); break;
        case udp::PacketKind::Accepted: OnAccepted(from, packet); break;
        case udp::PacketKind::Denied: OnDenied(from, packet); break;
        case udp::PacketKind::Payload: OnPayload(from, packet); break;
        case udp::PacketKind::Disconnect: OnDisconnect(from, packet); break;
        case udp::PacketKind::Reset: OnReset(from, packet); break;
        }
    }

    // ---- time ----

    void Tick()
    {
        const u64 now = Now();
        for (usize i = 0; i < conns.Size(); ++i)
        {
            UdpConn* c = conns[i].Get();
            if (c->state == UdpState::Connecting)
            {
                if (now - c->created >= config.connect_timeout_nanos)
                {
                    End(c, DisconnectReason::TimedOut, 0);
                }
                else if (now - c->last_try >= c->retry_nanos)
                {
                    c->last_try = now;
                    c->retry_nanos = c->retry_nanos * 2 > kMaxHandshakeRetry ? kMaxHandshakeRetry : c->retry_nanos * 2;
                    if (c->phase == Phase::Request)
                    {
                        SendRequest(c);
                    }
                    else
                    {
                        SendResponse(c);
                    }
                }
            }
            else if (c->state == UdpState::Connected)
            {
                if (now - c->last_heard >= config.timeout_nanos)
                {
                    End(c, DisconnectReason::TimedOut, 0);
                }
                else if ((c->ack_pending && now - c->last_sent >= config.ack_delay_nanos) ||
                         now - c->last_sent >= config.keepalive_nanos)
                {
                    SendPayload(c, false, 0, ByteSpan());
                }
            }
        }
        for (usize i = 0; i < lingering.Size();)
        {
            UdpLingering& l = lingering[i];
            if (now >= l.next_send)
            {
                Vector<byte> p;
                p.Push(static_cast<byte>(udp::FirstByte(udp::PacketKind::Disconnect)));
                PutU16(&p, l.wire_id);
                PutU32(&p, l.code);
                Emit(l.peer, p);
                l.next_send = now + config.close_interval_nanos;
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

    void Service()
    {
        for (usize n = 0; n < kReceiveBatch; ++n)
        {
            usize received = 0;
            Endpoint from;
            const Status s = port->ReceiveFrom(MutableByteSpan(buffer.Data(), buffer.Size()), &received, &from);
            if (s.Is(ErrorCode::WouldBlock))
            {
                break;
            }
            if (s.Is(ErrorCode::MessageTooLarge))
            {
                ++counters.datagrams_received;
                ++counters.dropped_oversize;
                continue;
            }
            if (s.Failed())
            {
                break;
            }
            Receive(from, ByteSpan(buffer.Data(), received));
        }
        Tick();
    }
};

UdpTransport::UdpTransport(DatagramPort& port, const Clock& clock, const UdpTransportConfig& config)
    : config_(config), impl_(MakeUnique<Impl>(port, clock, config))
{
}

UdpTransport::~UdpTransport() = default;

usize UdpTransport::MaxMessageBytes() const
{
    return udp::kMaxPayloadBytes - 4;
}

Status UdpTransport::Listen(const Endpoint& at)
{
    if (!(at == impl_->self) || impl_->listening)
    {
        return Status::Of(ErrorCode::Invalid);
    }
    impl_->listening = true;
    return Status::Success();
}

Status UdpTransport::Connect(const Endpoint& to, ConnectionId* connection)
{
    Impl& m = *impl_;
    if (m.conns.Size() >= m.config.max_connections)
    {
        return Status::Of(ErrorCode::LimitExceeded);
    }
    UdpConn* c = m.Create(to, false);
    c->client_salt = m.Random();
    c->phase = Phase::Request;
    c->retry_nanos = config_.handshake_retry_nanos;
    c->last_try = m.Now();
    m.SendRequest(c);
    *connection = c->id;
    return Status::Success();
}

Status UdpTransport::Send(ConnectionId connection, u8 channel, ByteSpan bytes, Reliability reliability)
{
    Impl& m = *impl_;
    UdpConn* c = m.FindById(connection);
    if (c == nullptr || channel >= config_.channels || bytes.Size() == 0 || static_cast<u8>(reliability) > 3)
    {
        return Status::Of(ErrorCode::Invalid);
    }
    if (c->state == UdpState::Closed)
    {
        return Status::Of(ErrorCode::Closed);
    }
    if (bytes.Size() > MaxMessageBytes())
    {
        return Status::Of(ErrorCode::MessageTooLarge);
    }
    if (reliability != Reliability::Unreliable)
    {
        return Status::Of(ErrorCode::Unsupported);
    }
    if (c->state != UdpState::Connected)
    {
        return Status::Of(ErrorCode::NotConnected);
    }
    ++c->stats.messages_sent;
    c->stats.bytes_sent += bytes.Size();
    m.SendPayload(c, true, channel, bytes);
    return Status::Success();
}

bool UdpTransport::Poll(Event* event)
{
    Impl& m = *impl_;
    if (m.queue_head >= m.queue.Size())
    {
        m.queue.Clear();
        m.queue_head = 0;
        m.Service();  // only when nothing is waiting, so draining many events does not rescan every connection
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
        UdpConn* c = m.FindById(m.current.connection);
        if (c != nullptr)
        {
            m.Remove(c);
        }
    }
    return true;
}

Status UdpTransport::Close(ConnectionId connection, u32 code)
{
    Impl& m = *impl_;
    UdpConn* c = m.FindById(connection);
    if (c == nullptr)
    {
        return Status::Of(ErrorCode::Invalid);
    }
    if (c->state == UdpState::Closed)
    {
        return Status::Of(ErrorCode::Closed);
    }
    if (c->state == UdpState::Connected)
    {
        UdpLingering l;
        l.peer = c->peer;
        l.wire_id = c->wire_id;
        l.code = code;
        l.sends_left = config_.close_repeats;
        l.next_send = m.Now();
        m.lingering.Push(l);
    }
    m.PurgeMessages(connection);
    m.End(c, DisconnectReason::LocalClose, 0);
    return Status::Success();
}

Status UdpTransport::Statistics(ConnectionId connection, ConnectionStats* stats) const
{
    const UdpConn* c = impl_->FindById(connection);
    if (c == nullptr)
    {
        return Status::Of(ErrorCode::Invalid);
    }
    *stats = c->stats;
    return Status::Success();
}

Status UdpTransport::Info(ConnectionId connection, UdpConnectionInfo* info) const
{
    const UdpConn* c = impl_->FindById(connection);
    if (c == nullptr)
    {
        return Status::Of(ErrorCode::Invalid);
    }
    *info = c->info;
    return Status::Success();
}

usize UdpTransport::ConnectionCount() const
{
    return impl_->conns.Size();
}

const UdpCounters& UdpTransport::Counters() const
{
    return impl_->counters;
}

}  // namespace engine_placeholder::net
