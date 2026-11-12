#pragma once

// The transport conformance suite (Phase 20 Day 273): tests written once against net::Transport and run against every
// implementation (the simulated transport now; the UDP transport on Days 274-276; the TCP transport on Day 277).
//
// A fixture `F` supplies the implementation:
//
//   Transport* MakeServer();            // a transport that listens; its endpoint is ServerEndpoint()
//   Transport* MakeClient();            // a transport that connects
//   Endpoint ServerEndpoint();          // where MakeServer's transport listens
//   Endpoint DeadEndpoint();            // an endpoint where nothing listens (a connect there is refused)
//   void Destroy(Transport* t);         // destroys a transport made by the fixture, with traffic in flight
//   void Pump(u32 milliseconds);        // lets time pass and the implementation make progress
//   bool Lossy();                       // true if the carrier drops, reorders, duplicates or corrupts packets
//
// Each function below is one conformance check; the implementation's test file wraps them in TEST_CASEs.

#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "cstr/mem_ops.h"
#include "lang/utility.h"
#include "net/transport.h"

namespace engine_placeholder::net::conformance
{

struct Rec
{
    EventKind kind = EventKind::Opened;
    ConnectionId connection = 0;
    bool incoming = false;
    u8 channel = 0;
    Reliability reliability = Reliability::Unreliable;
    Vector<byte> data;
    DisconnectReason reason = DisconnectReason::None;
    u32 code = 0;
};

// One transport and every event it has produced so far.
struct Peer
{
    Transport* transport = nullptr;
    Vector<Rec> events;

    void Drain()
    {
        Event e;
        while (transport->Poll(&e))
        {
            Rec r;
            r.kind = e.kind;
            r.connection = e.connection;
            r.incoming = e.incoming;
            r.channel = e.channel;
            r.reliability = e.reliability;
            r.reason = e.reason;
            r.code = e.close_code;
            r.data.TryResizeUninitialized(e.message.Size());
            if (e.message.Size() != 0)
            {
                MemCopy(r.data.Data(), e.message.Data(), e.message.Size());
            }
            events.Push(Move(r));
        }
    }

    usize Count(EventKind kind, ConnectionId connection = 0) const
    {
        usize n = 0;
        for (usize i = 0; i < events.Size(); ++i)
        {
            n += events[i].kind == kind && (connection == 0 || events[i].connection == connection) ? 1 : 0;
        }
        return n;
    }

    // The first event of `kind` on `connection` (0 for any), or null.
    const Rec* First(EventKind kind, ConnectionId connection = 0) const
    {
        for (usize i = 0; i < events.Size(); ++i)
        {
            if (events[i].kind == kind && (connection == 0 || events[i].connection == connection))
            {
                return &events[i];
            }
        }
        return nullptr;
    }
};

inline Peer MakePeer(Transport* t)
{
    Peer p;
    p.transport = t;
    return p;
}

// Lets time pass for `milliseconds`, draining every peer after each step.
template <class F, class... Peers> void Settle(F& f, u32 milliseconds, Peers*... peers)
{
    for (u32 elapsed = 0; elapsed < milliseconds; elapsed += 10)
    {
        f.Pump(10);
        (peers->Drain(), ...);
    }
}

// Pumps until `done()` or the time is up; returns whether it became true.
template <class F, class Done, class... Peers> bool SettleUntil(F& f, u32 milliseconds, Done done, Peers*... peers)
{
    for (u32 elapsed = 0; elapsed < milliseconds; elapsed += 10)
    {
        f.Pump(10);
        (peers->Drain(), ...);
        if (done())
        {
            return true;
        }
    }
    return false;
}

// A message that checks itself: an index, a length and bytes derived from both, so corruption or truncation shows.
inline void MakeMessage(u32 index, usize size, Vector<byte>* out)
{
    out->Clear();
    for (u32 i = 0; i < 4; ++i)
    {
        out->Push(static_cast<byte>((index >> (8 * i)) & 0xff));
    }
    u32 state = index * 2654435761u + 12345u;
    for (usize i = 4; i < size; ++i)
    {
        state = state * 1664525u + 1013904223u;
        out->Push(static_cast<byte>(state >> 24));
    }
}

inline usize MessageSize(u32 index, usize max)
{
    const usize span = max > 8 ? max - 8 : 1;
    return 4 + (static_cast<usize>(index) * 37 + 11) % span;
}

// True if `data` is exactly the message MakeMessage builds for the index it carries; *index receives it.
inline bool CheckMessage(const Vector<byte>& data, u32* index)
{
    if (data.Size() < 4)
    {
        return false;
    }
    u32 value = 0;
    for (u32 i = 0; i < 4; ++i)
    {
        value |= static_cast<u32>(static_cast<u8>(data[i])) << (8 * i);
    }
    Vector<byte> expected;
    MakeMessage(value, data.Size(), &expected);
    *index = value;
    return MemCompare(data.Data(), expected.Data(), data.Size()) == 0;
}

// Connects a client to the server and settles until both sides report the connection open.
template <class F> bool Connect(F& f, Peer* client, Peer* server, ConnectionId* client_conn, ConnectionId* server_conn)
{
    REQUIRE(client->transport->Connect(f.ServerEndpoint(), client_conn).Ok());
    const usize before = server->Count(EventKind::Opened);
    const bool opened = SettleUntil(
        f, 5000, [&]
        { return client->Count(EventKind::Opened, *client_conn) == 1 && server->Count(EventKind::Opened) > before; },
        client, server);
    if (!opened)
    {
        return false;
    }
    // The server's connection is the newest Opened event.
    for (usize i = server->events.Size(); i > 0; --i)
    {
        if (server->events[i - 1].kind == EventKind::Opened)
        {
            *server_conn = server->events[i - 1].connection;
            break;
        }
    }
    return true;
}

template <class F> void ConnectAndAccept(F& f)
{
    Peer server = MakePeer(f.MakeServer());
    Peer client = MakePeer(f.MakeClient());
    ConnectionId cc = 0;
    ConnectionId sc = 0;
    REQUIRE(Connect(f, &client, &server, &cc, &sc));
    CHECK(cc != 0);
    CHECK(sc != 0);
    CHECK(client.First(EventKind::Opened, cc)->incoming == false);
    CHECK(server.First(EventKind::Opened, sc)->incoming == true);
    // Messages flow both ways.
    const byte hello[5] = {static_cast<byte>('h'), static_cast<byte>('e'), static_cast<byte>('l'),
                           static_cast<byte>('l'), static_cast<byte>('o')};
    REQUIRE(client.transport->Send(cc, 0, ByteSpan(hello, 5), Reliability::ReliableOrdered).Ok());
    REQUIRE(server.transport->Send(sc, 1, ByteSpan(hello, 3), Reliability::ReliableOrdered).Ok());
    REQUIRE(SettleUntil(
        f, 5000, [&] { return server.Count(EventKind::Message) == 1 && client.Count(EventKind::Message) == 1; },
        &client, &server));
    CHECK(server.First(EventKind::Message)->data.Size() == 5);
    CHECK(server.First(EventKind::Message)->channel == 0);
    CHECK(client.First(EventKind::Message)->data.Size() == 3);
    CHECK(client.First(EventKind::Message)->channel == 1);
    ConnectionStats stats;
    REQUIRE(client.transport->Statistics(cc, &stats).Ok());
    CHECK(stats.messages_sent == 1);
    CHECK(stats.bytes_sent == 5);
    CHECK(stats.messages_received == 1);
    CHECK(stats.packets_sent >= 1);
}

template <class F> void CloseCarriesReasons(F& f)
{
    Peer server = MakePeer(f.MakeServer());
    Peer client = MakePeer(f.MakeClient());
    ConnectionId cc = 0;
    ConnectionId sc = 0;
    REQUIRE(Connect(f, &client, &server, &cc, &sc));
    CHECK(client.transport->Close(cc, 7).Ok());
    CHECK(client.transport->Close(cc, 7).Is(ErrorCode::Closed));  // already closing
    const byte one[1] = {static_cast<byte>(1)};
    CHECK(client.transport->Send(cc, 0, ByteSpan(one, 1), Reliability::ReliableOrdered).Is(ErrorCode::Closed));
    CHECK(client.transport->Close(987654, 0).Is(ErrorCode::Invalid));
    REQUIRE(SettleUntil(
        f, 20000, [&] { return server.Count(EventKind::Closed, sc) == 1 && client.Count(EventKind::Closed, cc) == 1; },
        &client, &server));
    const Rec* local = client.First(EventKind::Closed, cc);
    const Rec* remote = server.First(EventKind::Closed, sc);
    CHECK(local->reason == DisconnectReason::LocalClose);
    if (!f.Lossy())
    {
        CHECK(remote->reason == DisconnectReason::PeerClosed);
        CHECK(remote->code == 7);
    }
    else
    {
        CHECK((remote->reason == DisconnectReason::PeerClosed || remote->reason == DisconnectReason::TimedOut));
    }
    // A closed connection is gone from the transport once its event was polled.
    CHECK(client.transport->Send(cc, 0, ByteSpan(one, 1), Reliability::ReliableOrdered).Failed());
}

template <class F> void ConnectToNothingIsRefused(F& f)
{
    Peer client = MakePeer(f.MakeClient());
    ConnectionId cc = 0;
    REQUIRE(client.transport->Connect(f.DeadEndpoint(), &cc).Ok());
    REQUIRE(SettleUntil(f, 70000, [&] { return client.Count(EventKind::Closed, cc) == 1; }, &client));
    const Rec* closed = client.First(EventKind::Closed, cc);
    CHECK((closed->reason == DisconnectReason::Refused || closed->reason == DisconnectReason::TimedOut));
    CHECK(client.Count(EventKind::Opened, cc) == 0);
}

// Sends `count` self-checking messages with the given reliability on `channel`; returns the indices that arrived, in
// arrival order, after checking each message's content.
template <class F>
void SendStream(F& f, Peer& from, ConnectionId conn, u8 channel, Reliability rel, u32 first, u32 count, usize max_size)
{
    Vector<byte> message;
    for (u32 i = first; i < first + count; ++i)
    {
        MakeMessage(i, MessageSize(i, max_size), &message);
        Status s = from.transport->Send(conn, channel, ByteSpan(message.Data(), message.Size()), rel);
        // A full send queue is backpressure: let the carrier drain and try again.
        for (int tries = 0; s.Is(ErrorCode::LimitExceeded) && tries < 2000; ++tries)
        {
            Settle(f, 10, &from);
            s = from.transport->Send(conn, channel, ByteSpan(message.Data(), message.Size()), rel);
        }
        REQUIRE(s.Ok());
    }
}

template <class F> void ReliableOrderedArrivesOnceInOrder(F& f)
{
    Peer server = MakePeer(f.MakeServer());
    Peer client = MakePeer(f.MakeClient());
    ConnectionId cc = 0;
    ConnectionId sc = 0;
    REQUIRE(Connect(f, &client, &server, &cc, &sc));
    constexpr u32 kCount = 600;
    const usize max = client.transport->MaxMessageBytes();
    SendStream(f, client, cc, 0, Reliability::ReliableOrdered, 0, kCount, max);
    SendStream(f, server, sc, 2, Reliability::ReliableOrdered, 0, kCount, max);
    REQUIRE(SettleUntil(
        f, f.Lossy() ? 3600000u : 600000u,
        [&] { return server.Count(EventKind::Message) >= kCount && client.Count(EventKind::Message) >= kCount; },
        &client, &server));
    Settle(f, 200, &client, &server);  // anything extra (a duplicate) would show up now
    for (Peer* p : {&server, &client})
    {
        CHECK(p->Count(EventKind::Message) == kCount);
        u32 expected = 0;
        bool good = true;
        for (usize i = 0; i < p->events.Size(); ++i)
        {
            if (p->events[i].kind != EventKind::Message)
            {
                continue;
            }
            u32 index = 0;
            good = good && CheckMessage(p->events[i].data, &index) && index == expected &&
                   p->events[i].reliability == Reliability::ReliableOrdered;
            ++expected;
        }
        CHECK(good);
    }
    // The last acknowledgements may still be on their way (or lost, and the message sent again): wait for them.
    ConnectionStats stats;
    REQUIRE(SettleUntil(
        f, 60000, [&] { return client.transport->Statistics(cc, &stats).Ok() && stats.send_queue_messages == 0; },
        &client, &server));
    CHECK(stats.messages_sent == kCount);
}

template <class F> void ReliableUnorderedArrivesOnce(F& f)
{
    Peer server = MakePeer(f.MakeServer());
    Peer client = MakePeer(f.MakeClient());
    ConnectionId cc = 0;
    ConnectionId sc = 0;
    REQUIRE(Connect(f, &client, &server, &cc, &sc));
    constexpr u32 kCount = 400;
    SendStream(f, client, cc, 1, Reliability::ReliableUnordered, 0, kCount, client.transport->MaxMessageBytes());
    REQUIRE(SettleUntil(
        f, f.Lossy() ? 3600000u : 600000u, [&] { return server.Count(EventKind::Message) >= kCount; }, &client,
        &server));
    Settle(f, 200, &client, &server);
    CHECK(server.Count(EventKind::Message) == kCount);
    Vector<u8> seen;
    seen.Resize(kCount);
    bool good = true;
    for (usize i = 0; i < server.events.Size(); ++i)
    {
        if (server.events[i].kind != EventKind::Message)
        {
            continue;
        }
        u32 index = 0;
        good = good && CheckMessage(server.events[i].data, &index) && index < kCount && seen[index] == 0;
        if (index < kCount)
        {
            seen[index] = 1;
        }
    }
    CHECK(good);
}

template <class F> void UnreliableIsNeverCorruptedOrDuplicated(F& f)
{
    Peer server = MakePeer(f.MakeServer());
    Peer client = MakePeer(f.MakeClient());
    ConnectionId cc = 0;
    ConnectionId sc = 0;
    REQUIRE(Connect(f, &client, &server, &cc, &sc));
    if (!client.transport->Supports(Reliability::Unreliable))
    {
        SUCCEED("this transport does not carry unreliable messages as such");
        return;
    }
    constexpr u32 kCount = 500;
    // Paced, so a lossless carrier is not asked to hold everything at once.
    for (u32 batch = 0; batch < kCount; batch += 10)
    {
        SendStream(f, client, cc, 3, Reliability::Unreliable, batch, 10, client.transport->MaxMessageBytes());
        Settle(f, 10, &client, &server);
    }
    // A carrier with congestion control takes a while to carry it all; a lossy one may drop some.
    if (f.Lossy())
    {
        Settle(f, 2000, &client, &server);
    }
    else
    {
        SettleUntil(f, 20000, [&] { return server.Count(EventKind::Message) >= kCount; }, &client, &server);
    }
    Vector<u8> seen;
    seen.Resize(kCount);
    bool good = true;
    for (usize i = 0; i < server.events.Size(); ++i)
    {
        if (server.events[i].kind != EventKind::Message)
        {
            continue;
        }
        u32 index = 0;
        good = good && CheckMessage(server.events[i].data, &index) && index < kCount && seen[index] == 0;
        if (index < kCount)
        {
            seen[index] = 1;
        }
    }
    CHECK(good);  // no corrupted message, none delivered twice
    if (!f.Lossy())
    {
        CHECK(server.Count(EventKind::Message) == kCount);
    }
    else
    {
        CHECK(server.Count(EventKind::Message) > 0);
    }
}

template <class F> void UnreliableSequencedNeverGoesBack(F& f)
{
    Peer server = MakePeer(f.MakeServer());
    Peer client = MakePeer(f.MakeClient());
    ConnectionId cc = 0;
    ConnectionId sc = 0;
    REQUIRE(Connect(f, &client, &server, &cc, &sc));
    if (!client.transport->Supports(Reliability::UnreliableSequenced))
    {
        SUCCEED("this transport does not carry sequenced messages as such");
        return;
    }
    constexpr u32 kCount = 500;
    for (u32 batch = 0; batch < kCount; batch += 10)
    {
        SendStream(f, client, cc, 4, Reliability::UnreliableSequenced, batch, 10, client.transport->MaxMessageBytes());
        Settle(f, 10, &client, &server);
    }
    Settle(f, 2000, &client, &server);
    bool first = true;
    u32 last = 0;
    bool good = true;
    for (usize i = 0; i < server.events.Size(); ++i)
    {
        if (server.events[i].kind != EventKind::Message)
        {
            continue;
        }
        u32 index = 0;
        good = good && CheckMessage(server.events[i].data, &index) && (first || index > last);
        first = false;
        last = index;
    }
    CHECK(good);
    CHECK(server.Count(EventKind::Message) > 0);
}

template <class F> void MessageSizeAndChannelLimits(F& f)
{
    Peer server = MakePeer(f.MakeServer());
    Peer client = MakePeer(f.MakeClient());
    ConnectionId cc = 0;
    ConnectionId sc = 0;
    REQUIRE(Connect(f, &client, &server, &cc, &sc));
    Transport& t = *client.transport;
    const usize max = t.MaxMessageBytes();
    REQUIRE(max >= 64);
    REQUIRE(t.ChannelCount() >= 1);
    Vector<byte> big;
    MakeMessage(5, max, &big);
    CHECK(t.Send(cc, 0, ByteSpan(big.Data(), big.Size()), Reliability::ReliableOrdered).Ok());
    Vector<byte> too_big;
    MakeMessage(6, max + 1, &too_big);
    CHECK(t.Send(cc, 0, ByteSpan(too_big.Data(), too_big.Size()), Reliability::ReliableOrdered)
              .Is(ErrorCode::MessageTooLarge));
    CHECK(t.Send(cc, 0, ByteSpan(), Reliability::ReliableOrdered).Is(ErrorCode::Invalid));
    CHECK(t.Send(cc, t.ChannelCount(), ByteSpan(big.Data(), 8), Reliability::ReliableOrdered).Is(ErrorCode::Invalid));
    CHECK(t.Send(424242, 0, ByteSpan(big.Data(), 8), Reliability::ReliableOrdered).Is(ErrorCode::Invalid));
    // The largest message arrives intact.
    REQUIRE(SettleUntil(f, 30000, [&] { return server.Count(EventKind::Message) >= 1; }, &client, &server));
    u32 index = 0;
    CHECK(server.First(EventKind::Message)->data.Size() == max);
    CHECK(CheckMessage(server.First(EventKind::Message)->data, &index));
    CHECK(index == 5);
    // The last channel works too.
    const u8 last_channel = static_cast<u8>(t.ChannelCount() - 1);
    CHECK(t.Send(cc, last_channel, ByteSpan(big.Data(), 8), Reliability::ReliableOrdered).Ok());
}

template <class F> void ChannelsAreIndependent(F& f)
{
    Peer server = MakePeer(f.MakeServer());
    Peer client = MakePeer(f.MakeClient());
    ConnectionId cc = 0;
    ConnectionId sc = 0;
    REQUIRE(Connect(f, &client, &server, &cc, &sc));
    const u8 channels = client.transport->ChannelCount() < 4 ? client.transport->ChannelCount() : 4;
    constexpr u32 kPerChannel = 100;
    for (u8 ch = 0; ch < channels; ++ch)
    {
        SendStream(f, client, cc, ch, Reliability::ReliableOrdered, ch * 1000, kPerChannel,
                   client.transport->MaxMessageBytes());
    }
    REQUIRE(SettleUntil(
        f, f.Lossy() ? 3600000u : 600000u, [&] { return server.Count(EventKind::Message) >= channels * kPerChannel; },
        &client, &server));
    Settle(f, 100, &client, &server);
    CHECK(server.Count(EventKind::Message) == channels * kPerChannel);
    bool good = true;
    u32 next[4] = {0, 1000, 2000, 3000};
    for (usize i = 0; i < server.events.Size(); ++i)
    {
        if (server.events[i].kind != EventKind::Message)
        {
            continue;
        }
        u32 index = 0;
        const u8 ch = server.events[i].channel;
        good = good && ch < channels && CheckMessage(server.events[i].data, &index) && index / 1000 == ch &&
               index == next[ch];
        if (ch < channels)
        {
            ++next[ch];
        }
    }
    CHECK(good);  // per channel: in order, and nothing from another channel
}

template <class F> void ManyConnectionsAtOnce(F& f)
{
    constexpr usize kClients = 40;
    Peer server = MakePeer(f.MakeServer());
    Vector<Peer> clients;
    Vector<ConnectionId> ids;
    for (usize i = 0; i < kClients; ++i)
    {
        clients.Push(MakePeer(f.MakeClient()));
    }
    for (usize i = 0; i < kClients; ++i)
    {
        ConnectionId id = 0;
        REQUIRE(clients[i].transport->Connect(f.ServerEndpoint(), &id).Ok());
        ids.Push(id);
    }
    const auto all_open = [&]
    {
        if (server.Count(EventKind::Opened) < kClients)
        {
            return false;
        }
        for (usize i = 0; i < kClients; ++i)
        {
            if (clients[i].Count(EventKind::Opened) != 1)
            {
                return false;
            }
        }
        return true;
    };
    const auto drain_all = [&]
    {
        server.Drain();
        for (usize i = 0; i < kClients; ++i)
        {
            clients[i].Drain();
        }
    };
    for (u32 t = 0; t < (f.Lossy() ? 90000u : 30000u) && !all_open(); t += 10)
    {
        f.Pump(10);
        drain_all();
    }
    REQUIRE(all_open());
    CHECK(server.Count(EventKind::Opened) == kClients);
    // Each client sends its number; the server answers each on its own connection.
    for (usize i = 0; i < kClients; ++i)
    {
        Vector<byte> m;
        MakeMessage(static_cast<u32>(i), 16, &m);
        REQUIRE(clients[i].transport->Send(ids[i], 0, ByteSpan(m.Data(), m.Size()), Reliability::ReliableOrdered).Ok());
    }
    const auto all_asked = [&] { return server.Count(EventKind::Message) >= kClients; };
    for (u32 t = 0; t < 60000 && !all_asked(); t += 10)
    {
        f.Pump(10);
        drain_all();
    }
    REQUIRE(all_asked());
    for (usize i = 0; i < server.events.Size(); ++i)
    {
        if (server.events[i].kind == EventKind::Message)
        {
            REQUIRE(server.transport
                        ->Send(server.events[i].connection, 0,
                               ByteSpan(server.events[i].data.Data(), server.events[i].data.Size()),
                               Reliability::ReliableOrdered)
                        .Ok());
        }
    }
    const auto all_answered = [&]
    {
        for (usize i = 0; i < kClients; ++i)
        {
            if (clients[i].Count(EventKind::Message) != 1)
            {
                return false;
            }
        }
        return true;
    };
    for (u32 t = 0; t < 60000 && !all_answered(); t += 10)
    {
        f.Pump(10);
        drain_all();
    }
    REQUIRE(all_answered());
    for (usize i = 0; i < kClients; ++i)
    {
        u32 index = 99999;
        CHECK(CheckMessage(clients[i].First(EventKind::Message)->data, &index));
        CHECK(index == i);  // the answer came back on the right connection
    }
}

template <class F> void CloseDuringSend(F& f)
{
    Peer server = MakePeer(f.MakeServer());
    Peer client = MakePeer(f.MakeClient());
    ConnectionId cc = 0;
    ConnectionId sc = 0;
    REQUIRE(Connect(f, &client, &server, &cc, &sc));
    constexpr u32 kCount = 100;
    for (u32 i = 0; i < kCount; ++i)
    {
        Vector<byte> m;
        MakeMessage(i, MessageSize(i, client.transport->MaxMessageBytes()), &m);
        if (client.transport->Send(cc, 0, ByteSpan(m.Data(), m.Size()), Reliability::ReliableOrdered).Failed())
        {
            break;  // a full queue is fine: the point is the close that follows
        }
    }
    REQUIRE(client.transport->Close(cc, 3).Ok());
    REQUIRE(SettleUntil(f, 30000, [&] { return server.Count(EventKind::Closed, sc) == 1; }, &client, &server));
    // Whatever arrived before the close is an in-order prefix and intact, and nothing comes after the Closed event.
    u32 expected = 0;
    bool good = true;
    bool after_close = false;
    bool closed = false;
    for (usize i = 0; i < server.events.Size(); ++i)
    {
        const Rec& r = server.events[i];
        if (r.kind == EventKind::Closed)
        {
            closed = true;
        }
        else if (r.kind == EventKind::Message)
        {
            after_close = after_close || closed;
            u32 index = 0;
            good = good && CheckMessage(r.data, &index) && index == expected;
            ++expected;
        }
    }
    CHECK(good);
    CHECK_FALSE(after_close);
    CHECK(client.Count(EventKind::Message) == 0);
    CHECK(client.Count(EventKind::Closed, cc) == 1);
}

template <class F> void NoEventsAfterClose(F& f)
{
    Peer server = MakePeer(f.MakeServer());
    Peer client = MakePeer(f.MakeClient());
    ConnectionId cc = 0;
    ConnectionId sc = 0;
    REQUIRE(Connect(f, &client, &server, &cc, &sc));
    for (u32 i = 0; i < 20; ++i)
    {
        Vector<byte> m;
        MakeMessage(i, 32, &m);
        REQUIRE(server.transport->Send(sc, 0, ByteSpan(m.Data(), m.Size()), Reliability::ReliableOrdered).Ok());
    }
    // Close before the client has polled anything of it: the messages are discarded, the Closed event comes once.
    REQUIRE(client.transport->Close(cc, 0).Ok());
    Settle(f, 3000, &client, &server);
    CHECK(client.Count(EventKind::Message) == 0);
    CHECK(client.Count(EventKind::Closed, cc) == 1);
    Settle(f, 1000, &client, &server);
    CHECK(client.Count(EventKind::Closed, cc) == 1);
    CHECK(client.events[client.events.Size() - 1].kind == EventKind::Closed);
    ConnectionStats stats;
    CHECK(client.transport->Statistics(cc, &stats).Failed());  // gone once its Closed event was polled
}

template <class F> void DestroyingATransportWithTrafficInFlight(F& f)
{
    {
        Transport* server_t = f.MakeServer();
        Transport* client_t = f.MakeClient();
        Peer server = MakePeer(server_t);
        Peer client = MakePeer(client_t);
        ConnectionId cc = 0;
        ConnectionId sc = 0;
        REQUIRE(Connect(f, &client, &server, &cc, &sc));
        for (u32 i = 0; i < 50; ++i)
        {
            Vector<byte> m;
            MakeMessage(i, 200, &m);
            REQUIRE(client_t->Send(cc, 0, ByteSpan(m.Data(), m.Size()), Reliability::ReliableOrdered).Ok());
            REQUIRE(server_t->Send(sc, 0, ByteSpan(m.Data(), m.Size()), Reliability::Unreliable).Ok());
        }
        f.Destroy(client_t);  // with its packets on the way
        client.transport = nullptr;
        for (u32 t = 0; t < 500; t += 10)
        {
            f.Pump(10);
            server.Drain();
        }
        f.Destroy(server_t);  // and the server too, with a connection that never closed
    }
    f.Pump(100);
    SUCCEED("nothing crashed and nothing was delivered to a destroyed transport");
}

}  // namespace engine_placeholder::net::conformance
