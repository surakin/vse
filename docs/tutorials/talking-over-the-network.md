# Tutorial: Talk Over the Network

**Goal:** Start a server and a client in one process, connect them, send messages on two channels, answer from the server and close with a code, first over the simulated network (no sockets, fully deterministic) and then with the same code over real loopback TCP.

**You need:** [Run Work in Parallel with Jobs](parallel-jobs.md) is not required; the transports are polled from your own loop. Background: [Networking](../guide/net.md) and [Settings](../guide/settings.md).

## Steps

1. **Pick a carrier.** A `net::Transport` is the whole interface: `Listen`, `Connect`, `Send`, `Poll`, `Close`. `TcpTransport` runs it over a `StreamProvider`; here the provider is the simulated one
   (`SimStreamNetwork`), so the time is a clock the loop moves by hand.
2. **Listen and connect.** The server calls `Listen(endpoint)`; the client calls `Connect(endpoint, &id)` and gets a `ConnectionId` at once. Nothing has happened yet: a connection exists when its
   `Opened` event arrives.
3. **Poll.** Every call to `Poll(&event)` returns the next thing that happened: `Opened`, `Message`, `Closed`. A frame loop calls it until it returns false, once per frame.
4. **Send.** `Send(id, channel, bytes, Reliability::ReliableOrdered)` queues a message; it never blocks. A full queue returns `LimitExceeded` (back-pressure: try again later).
5. **Close.** `Close(id, code)` ends the connection; the peer sees `Closed` with `PeerClosed` and your code.
6. **Swap the carrier.** For real sockets replace `SimStreamNetwork` with `SocketStreamProvider` and the simulated clock with `MonotonicClock`; for UDP use `UdpTransport` over a `DatagramPort`. The
   rest of the code does not change, because it only knows `net::Transport`.

## Complete code

```cpp compile run standalone
#include "containers/unique_ptr.h"
#include "net/sim_network.h"
#include "net/sim_stream.h"
#include "net/tcp_transport.h"
#include "os/console.h"

using namespace engine_placeholder;
using namespace engine_placeholder::net;

int main() {
    // Step 1: a simulated network whose time we move ourselves.
    SimNetwork time(1);
    SimStreamNetwork streams(time.GetClock());
    const Endpoint server_at = streams.AllocateEndpoint();
    UniquePtr<StreamProvider> server_provider = streams.MakeProvider(server_at);
    UniquePtr<StreamProvider> client_provider = streams.MakeProvider(streams.AllocateEndpoint());
    TcpTransport server(*server_provider, time.GetClock());
    TcpTransport client(*client_provider, time.GetClock());

    // Step 2: listen and connect.
    if (server.Listen(server_at).Failed()) return 1;
    ConnectionId to_server = 0;
    if (client.Connect(server_at, &to_server).Failed()) return 1;

    ConnectionId from_client = 0;
    bool sent = false;
    bool answered = false;
    bool closed = false;
    u32 server_saw = 0;
    u32 close_code = 0;
    for (u32 step = 0; step < 2000 && !closed; ++step) {
        time.Advance(1'000'000);  // one millisecond passes

        // Step 3: poll both sides.
        Event e;
        while (server.Poll(&e)) {
            if (e.kind == EventKind::Opened) from_client = e.connection;
            if (e.kind == EventKind::Message) {
                ++server_saw;
                if (server_saw == 2 && !answered) {
                    answered = true;
                    const byte reply[2] = {static_cast<byte>('o'), static_cast<byte>('k')};
                    (void)server.Send(from_client, 1, ByteSpan(reply, 2), Reliability::ReliableOrdered);
                }
            }
            if (e.kind == EventKind::Closed) {
                closed = true;
                close_code = e.close_code;
            }
        }
        while (client.Poll(&e)) {
            if (e.kind == EventKind::Opened && !sent) {
                // Step 4: two messages on two channels.
                sent = true;
                const byte hello[5] = {static_cast<byte>('h'), static_cast<byte>('e'), static_cast<byte>('l'),
                                       static_cast<byte>('l'), static_cast<byte>('o')};
                (void)client.Send(to_server, 0, ByteSpan(hello, 5), Reliability::ReliableOrdered);
                (void)client.Send(to_server, 1, ByteSpan(hello, 3), Reliability::ReliableOrdered);
            }
            if (e.kind == EventKind::Message) {
                // Step 5: the answer arrived; close with a code.
                (void)client.Close(to_server, 42);
            }
        }
    }
    os::Print("server saw %u messages, closed with code %u\n", server_saw, close_code);
    return (server_saw == 2 && closed && close_code == 42) ? 0 : 1;
}
```

## What to try next

- Run `game/net_demo` (`net_demo --simulated`, or without the flag for real loopback sockets) to see the same conversation over the simulated, UDP and TCP transports.
- Change the carrier to `UdpTransport` over `SimDatagramPort` and set `SimNetwork::SetDefaultLink` to lose, reorder and duplicate packets: reliable messages still arrive once, in order.
- Use `tools/net_probe` (`net_probe echo`, `net_probe ping`) for a manual look at latency over real sockets.
