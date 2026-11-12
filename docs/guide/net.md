# Networking: addresses, endpoints, errors, sockets, the reactor and TCP streams

Status: Phase 20. Built: addresses, endpoints and the error type (Day 267), the socket interface with its Linux backend and the access gate (Day 268), the reactor, the asynchronous operations and timers (Day 269). name resolution, listeners and the connect helpers (Day 270), framed TCP streams (Day 271), the message codec (Day 272), the transport interface, the simulated network and transport, and the conformance suite (Day 273), the UDP transport's packet layer and handshake (Day 274). Next: the UDP transport's channels and reliability (Days 275-276) and the TCP transport (Day 277).

**Audience:** engine programmers who talk to another machine or process. Design, spikes and the benchmark pass conditions:
`docs/superpowers/specs/2026-11-11-networking-design.md`.

**Trust:** until the encryption outcome of Day 280 is built, the engine's transports are for trusted networks only. Nothing here hides or authenticates data.

The module `engine/net` (namespace `net`) is the only place that calls the operating system's socket API (the gate `net_access_check` fails on a socket call or a socket
header anywhere else). The first half of this page covers the part with no system calls at all: values that name a peer, and the way failures are reported.
The second half is `net::Socket`. Because they make no syscalls they behave the same on every
platform and are tested exhaustively on any machine, Windows scope-id rules included.

## The vocabulary

| Type | What it is |
|---|---|
| `Address` | An IPv4 or IPv6 address: four or sixteen bytes in network order and, for IPv6, an optional scope (a numeric interface index or an interface name). |
| `Endpoint` | An `Address` and a `u16` port. Port 0 means "any port" when binding. |
| `EndpointSet` | At most 16 endpoints, in order: the shape of a resolver's answer (Day 270). |
| `AddressText`, `EndpointText` | Fixed buffers holding canonical text. Formatting allocates nothing. |
| `Status`, `ErrorCode` | The result of every fallible call: a portable code and the operating system's error number, kept for logs. |

## Parsing is strict

`Address::Parse` accepts exactly one address and refuses everything else with `ErrorCode::Invalid`, leaving the output untouched:

- IPv4 is four decimal parts of one to three digits, each at most 255, **no leading zero** (`010.0.0.1` is refused, not read as octal), nothing before or after.
- IPv6 follows the RFC 4291 text forms: groups of one to four hex digits in either case, one `::` for at least one group of zeros, an embedded dotted IPv4 tail
  (`::ffff:1.2.3.4`). A pair of brackets around the whole address is accepted.
- A scope follows `%`: digits up to nine are a numeric interface index (`fe80::1%3`), anything else of up to 15 letters, digits, `_`, `-` and `.` is an interface
  name (`fe80::1%eth0`) that a socket backend turns into an index. IPv4 has no scope.

`Endpoint::Parse` takes `a.b.c.d:port` and `[v6]:port`. A bare IPv6 address with a port is ambiguous and refused. The port is decimal, 0 to 65535, with no sign,
space or leading zero. A host name is not an address; names go through the resolver.

Formatting is the canonical form of RFC 5952: lower case, no leading zeros, the longest run of two or more zero groups compressed (the first on a tie, never a
single group), an IPv4-mapped address as `::ffff:a.b.c.d`. Parsing the formatted text always gives back an equal value.

```cpp compile run standalone
#include <cstdio>

#include "net/endpoint.h"

using namespace engine_placeholder;
using namespace engine_placeholder::net;

int main() {
    Address a;
    Status s = Address::Parse("2001:DB8:0:0:1:0:0:1", &a);
    Endpoint e;
    Status t = Endpoint::Parse("[fe80::1%eth0]:5000", &e);
    if (s.Failed() || t.Failed()) {
        return 1;
    }
    std::printf("%s %s %u\n", a.Format().CStr(), e.Format().CStr(), static_cast<unsigned>(e.Port()));
    return 0;
}
```

```output
2001:db8::1:0:0:1 [fe80::1%eth0]:5000 5000
```

## Classification

`IsAny`, `IsLoopback`, `IsLinkLocal`, `IsPrivate`, `IsMulticast` ask which range an address is in (the ranges are in the header's comments). An IPv4-mapped IPv6
address answers as the IPv4 address it wraps, and `Unmapped()` / `Mapped()` convert between the two forms. A server that accepts connections from the Internet
decides with these what counts as local; `Address::Loopback(family)` and `Address::Any(family)` build the two addresses a listener most often wants.

## Ordering and hashing

`Address` and `Endpoint` have a total order (family, bytes, scope, then port) and a `Hash` specialization, so both work as keys of `HashMap` and in sorted
containers. Equality includes the scope: `fe80::1%eth0` and `fe80::1%3` are different values until a backend resolves the name.

## Errors are values

No network call throws or aborts on a network failure. A call returns a `Status` (or a count together with one) whose `ErrorCode` is portable:

| Code | Meaning |
|---|---|
| `WouldBlock` | A non-blocking operation cannot finish now; wait for readiness and retry. |
| `Refused`, `Reset`, `TimedOut`, `Unreachable` | The peer or the path said no, vanished, or did not answer. |
| `AddressInUse`, `AddressNotAvailable` | A local endpoint is bound already, or does not belong to this machine. |
| `NotConnected`, `AlreadyConnected` | The socket is in the wrong state for the call. |
| `MessageTooLarge`, `LimitExceeded` | A size or count above what the socket, the transport or a setting allows. |
| `Closed`, `Cancelled` | The socket or connection was closed; the operation was cancelled before it finished. |
| `Invalid`, `Unsupported`, `Other` | The caller passed something refused; the platform lacks the feature; anything else. |

Branch on the code, never on `OsError()`; the number (`errno` or the Winsock code) is there for logs. The table that maps each operating system's numbers to these
codes lives in the backends (Days 268 and 278) and is tested against a table of the numbers.

`ENGINE_TRY(expr)` returns a failed `Status` from the enclosing function in one line, as it does for `database::Status`.

## Sockets

`net::Socket` is a move-only handle to a TCP stream, a TCP listener or a UDP socket. It is the whole socket API of the engine; the transports of later days
are built on it.

- **Everything is non-blocking**, from the moment the socket exists; accepted sockets too. A call that cannot finish returns `WouldBlock` and has done
  nothing. Until the reactor of Day 269 exists, a test waits by calling again; the reactor will turn the same calls into `co_await`s.
- **Partial reads and writes are results.** `Send` reports how many bytes the system took, `Receive` how many arrived; a count below the request is not an
  error. On a stream, `Receive` reports the end of the stream (the peer shut down its writing half) as `Closed`, after all the data.
- **A connect is two steps.** `Connect` returns success if the connection is made at once and `WouldBlock` if it is under way; when the socket is writable,
  `FinishConnect` reports the outcome, once (`Refused`, `TimedOut`, `Unreachable`, or success).
- **Datagrams are whole.** `SendTo` sends one datagram or none; `ReceiveFrom` returns one datagram and its sender, and drops (and reports as `MessageTooLarge`)
  a datagram larger than the buffer, so a short buffer is never mistaken for a short datagram.
- **No signals.** Writing to a vanished peer reports `Closed` or `Reset`; the process is never sent `SIGPIPE`.
- **Families.** A socket has the family it was opened with. An IPv6 socket accepts IPv4 endpoints (as IPv4-mapped addresses) and `SetDualStack(true)`
  lets an IPv6 listener take IPv4 peers; an IPv4-mapped peer address is reported as IPv4. A scope name in an address (`fe80::1%eth0`) is turned into an
  interface index when the endpoint is used.
- **Misuse is a value.** A closed, moved-from or never-opened socket answers `Invalid` to every call, and closing twice reports `Invalid` the second time;
  nothing crashes. The destructor closes the handle.

```cpp compile standalone
#include "net/socket.h"

using namespace engine_placeholder;
using namespace engine_placeholder::net;

// Starts a loopback connection and returns true if it is made or under way, and the listener sees it once it is pending.
bool StartLoopbackConnection() {
    Socket listener;
    Endpoint at;
    if (Socket::OpenTcp(AddressFamily::V4, &listener).Failed() ||
        listener.Bind(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Failed() ||  // port 0: the system picks a free port
        listener.Listen().Failed() || listener.LocalEndpoint(&at).Failed()) {
        return false;
    }
    Socket client;
    if (Socket::OpenTcp(AddressFamily::V4, &client).Failed()) {
        return false;
    }
    const Status connect = client.Connect(at);  // success, or WouldBlock: wait until writable, then FinishConnect()
    Socket accepted;
    Endpoint peer;
    const Status accept = listener.Accept(&accepted, &peer);  // WouldBlock until the connection is pending
    return (connect.Ok() || connect.Is(ErrorCode::WouldBlock)) && (accept.Ok() || accept.Is(ErrorCode::WouldBlock));
}
```

Options are methods of the socket: `SetReuseAddress`, `SetNoDelay` (TCP only), `SetKeepAlive`, `SetReceiveBuffer` and `SetSendBuffer` (they report what the
system granted: Linux doubles the request and caps it at `net.core.rmem_max`), `SetDualStack` (IPv6 only) and `SetBroadcast` (off by default).

### Porting

`engine/net/BACKEND.md` is the checklist for a new platform: every `Socket` operation with its rules, the error table, and the quirks of Windows, macOS,
mobile and consoles. The test `net_backend_lists` keeps it in step with the header. Linux and macOS share `engine/net/posix/`; Windows gets its own
directory on Day 278.

### Tests that use real sockets

Tests with real sockets are tagged `[network]`, live in the executable `net_tests_sockets` (ctest label `network`) and use loopback with ports the system
chooses. A machine that cannot bind loopback skips them with a printed reason, so a sandbox without networking does not fail the suite.

## The reactor: waiting without a thread per connection

A non-blocking socket says `WouldBlock`; something has to wake the coroutine that wants to try again. `net::Reactor` is that something. One dedicated thread waits in
the platform's poller (epoll on Linux) for the sockets that coroutines wait for and for the nearest timer; when a wait ends it hands the coroutine to the job system's
scheduler, so it resumes on a participant. The reactor thread runs none of your code, and there is no thread per connection.

```cpp
jobs::Scheduler scheduler(config);
net::Reactor reactor(scheduler);          // declared after the scheduler: destroyed (shut down) before it

jobs::Job Echo(net::Reactor* reactor, net::Socket* socket)
{
    byte buffer[512];
    for (;;)
    {
        const net::IoResult got = co_await net::Receive(*reactor, *socket, MutableByteSpan(buffer, sizeof buffer));
        if (got.status.Failed())
        {
            break;                       // Closed at the end of the stream, or the failure
        }
        const net::IoResult sent = co_await net::Send(*reactor, *socket, ByteSpan(buffer, got.count));
        if (sent.status.Failed())
        {
            break;
        }
    }
    const net::Status closed = reactor->Close(*socket);   // also ends any other wait on this socket
}
```

- **Waits** (`WaitReadable`, `WaitWritable`, `Sleep`) are awaitables giving a `Status`: `Ok` (ready; the operation then reports a real error), `TimedOut`, `Cancelled`, `Closed` or `Invalid`.
  Each ends exactly once. A socket that is already ready ends its wait at once, because the poller checks by level; there is no window between "try" and "wait" in which an event is lost.
- **Operations** (`Receive`, `Send`, `ReceiveFrom`, `SendTo`, `Accept`, `Connect`) are `jobs::Task`s that try the socket, wait when it would block and try again, until they finish or the options say stop.
  `Send` returns when every byte has been taken. Their timeout covers the whole operation.
- **Cancellation** is a `CancelToken` in the options: `Cancel()` from any thread ends every wait that uses it with `Cancelled`, whether it is still registering, waiting, or being completed by an event at that instant.
- **Closing** a socket that coroutines may be waiting for goes through `Reactor::Close` (or `Forget` before closing by other means); the waits end with `Closed`. Closing the descriptor behind the reactor's back leaves them waiting.
- **Time** comes from a `Clock`; a test passes a `ManualClock`, advances it and calls `Reactor::Poke`.
- **Shutdown** (`Reactor::Shutdown`, also the destructor) ends every wait with `Cancelled` and joins the thread. The scheduler must still be running, because it resumes those coroutines, so destroy the reactor first.
- **One reader and one writer per socket** at a time; a second wait in the same direction ends with `Invalid`.

`jobs::Task<T>` (`engine/jobs/task.h`) is the general building block: a lazy coroutine returning a value, awaited by a job or another task, handing control back with no trip through the scheduler.

## Names, listeners and connecting

- **`net::Resolve`** turns a host name and a port into at most 16 endpoints. A numeric host (`127.0.0.1`, `::1`, `[::1]`) is parsed in place; a name is queued for the `net::Resolver`, one thread that
  calls the operating system's resolver, which blocks for as long as the name server takes and cannot be cancelled. The coroutine waits on a `net::Signal` that the thread fires, so the timeout and the cancel token
  work as for every wait; a resolution that was given up is finished on the thread and its answer dropped. The resolver's queue is bounded (`LimitExceeded` when full), and `Resolver::Shutdown` ends the queued
  requests with `Cancelled` and waits for the one in progress, so shut it down before the reactor.
- **`net::Signal`** is the general form: a one-shot event any thread can `Fire`, awaited with `Reactor::WaitSignal`. A signal fired before the wait begins ends the wait at once.
- **`net::Listener`** opens, binds and listens, and its `Accept` task applies the policy of an accept loop: when the process is out of descriptors it sleeps (5 ms, doubling to 200 ms) and tries again instead of
  spinning; when `max_unhandled` connections are accepted and not yet reported handled (`Listener::Handled`), it stops accepting and leaves the rest in the kernel's queue. Write the loop yourself, three lines.
- **Dual stack:** a listener on `[::]` also accepts IPv4 clients (mapped addresses) where the system allows it. `Listener::DualStack()` says whether it does; `ListenerOptions::dual_stack = false` asks for IPv6 only.
- **`net::ConnectAny`** tries a list of endpoints one after the other, each for the attempt timeout (a sequential "happy eyeballs": the resolver's order puts the preferred family first), and **`net::ConnectTo`** is
  `Resolve` then `ConnectAny`.

## TCP streams and frames

`net::TcpStream` is a connected socket that sends and receives **frames** (a 4-byte big-endian length, then the payload) with memory that cannot grow past what was configured, and that
says why it ended. `net::FrameWriter` and `net::FrameReader` are the framing on its own, for any byte stream.

- **Sending is a bounded queue.** `Send(frame)` copies the frame into a ring of `send_queue_bytes` and answers `LimitExceeded` when it does not fit: the caller sees the backpressure and nothing
  grows. `Flush(keep_queued)` writes the queue to the socket, waiting whenever the socket is full; awaiting `Flush(0)` means everything is on its way, and `Flush(capacity - n)` means there is
  room for `n` more bytes (the "writable again" wait). `SendFrame` is the form that waits for room, queues and flushes. One coroutine flushes at a time; `Send` can be called from any thread.
- **Receiving reads into a buffer of fixed size** (`receive_buffer_bytes`) and decodes with a `FrameReader`, so a frame may arrive in pieces of any size (the tests cut the stream at every byte). The
  length is checked before the payload is read: zero (`Invalid`) or above `max_frame_bytes` (`LimitExceeded`) ends the stream with `CloseReason::ProtocolError` having spent no memory on it, and
  the payload buffer grows only with bytes that arrived, so a header that promises a megabyte costs nothing until the megabyte comes.
- **Every end has a reason** (`Reason()`): `PeerClosed` (an orderly end at a frame boundary), `Reset`, `TimedOut`, `LocalClose`, `ProtocolError` (bad framing, or the stream ended inside a frame). The first
  reason stays. A failed stream is shut so the other direction's waiting operation returns, and it is reset (not closed politely) when it is closed.
- **Timeouts.** `idle_timeout_nanos` is the longest the stream waits for bytes (or for the peer to take bytes): it restarts with every byte, so a slow frame is fine and a silent peer ends the stream with
  `TimedOut` (this is how a half-open connection is found). The timeout of one call (`WaitOptions`) only ends that call and leaves the stream open. `connect_timeout_nanos` bounds `TcpStream::Connect`.
  TCP keep-alive probes are `keep_alive` and the three timing values.
- **Closing.** `Close()` flushes the queue, shuts the sending side, reads until the peer ends its side (discarding what it still sends) and closes; if `linger_nanos` runs out it gives up, resets and reports
  `TimedOut`. `Abort()` closes at once and the peer sees a reset. Call either when no receive or flush is outstanding (cancel them with their `CancelToken` first).

```cpp compile standalone
#include "jobs/task.h"
#include "net/tcp_stream.h"

using namespace engine_placeholder;
using namespace engine_placeholder::net;

// Sends one frame and waits for the echo of it; returns the echo's size, or 0 on any failure.
jobs::Task<usize> EchoOnce(TcpStream& stream, ByteSpan frame) {
    if ((co_await stream.SendFrame(frame)).Failed()) {  // waits for room in the queue, then for the socket to take it
        co_return 0;
    }
    if ((co_await stream.ReceiveFrame()).Failed()) {  // on failure stream.Reason() says why the stream ended
        co_return 0;
    }
    co_return stream.Frame().Size();  // valid until the next ReceiveFrame
}
```

## Messages: the bit-packed codec

`net::NetWriter` writes into a buffer you own (a packet, say) and `net::NetReader` reads it back; both are in `engine/net/codec.h`. Bits are packed from the low bit of the first byte, so a
32-bit integer written at a byte boundary is the little-endian bytes of `bytes::ByteWriter`, and a ranged integer takes only the bits its range needs.

- **Values.** `WriteBits`, `WriteBool`, `WriteRange(value, min, max)` (the fewest bits; none for a single value), fixed-width integers and raw floats, canonical varints (the Phase 19 encoding, at any bit
  offset) and zigzag signed varints, strings and byte arrays with a **mandatory maximum length** (checked before anything is allocated: a length that promises more than the message holds is `Truncated`), and
  `AlignToByte` (the padding is zero, and the reader insists on it, so a message has one valid encoding).
- **Quantized values.** `Quantize`/`WriteQuantized` map a float in `[min, max]` onto `2^bits` levels; the error is at most half a step, `(max - min) / (2 * (2^bits - 1))`, plus one `f32` rounding. A value outside the
  range (infinities too) clamps, `-0` is `0`, NaN is rejected. Vectors quantize per component; a rotation is "smallest three" (2 bits for the index of the largest component, then the other three in
  `[-1/sqrt 2, 1/sqrt 2]`, error under `4 * sqrt 2 / (2 * (2^bits - 1))` in the four-vector norm); an angle wraps modulo a full turn (error at most `pi / 2^bits`). The arithmetic is done in `f64` with each intermediate
  product stored before it is used again, so no compiler can fuse a multiply and an add differently: the same inputs give the same bits on Linux, Windows and the Mac (the golden bytes in `tests/net/net_codec_test.cpp` pin it).
- **Deltas.** `WriteDeltaInt(value, baseline)` and `WriteDeltaQuantized` write the difference as a signed varint; the reader passes the same baseline. There is no replication logic here, only the encoding.
- **Errors are values** (`CodecError`: `Full`, `Truncated`, `OutOfRange`, `TooLong`, `Invalid`). A failed write or read changes nothing, and the reader stays usable at the same position.

```cpp compile standalone
#include "net/codec.h"

using namespace engine_placeholder;
using namespace engine_placeholder::net;

// Writes a position and a heading into `packet`, reads them back; returns true if they survived within the error bound.
bool RoundTrip(MutableByteSpan packet) {
    NetWriter writer(packet);
    if (writer.WriteBool(true) != CodecError::Ok ||
        writer.WriteVec3(Vec3(12.5f, 0.0f, -80.25f), -512.0f, 512.0f, 16) != CodecError::Ok ||  // about 0.016 per step
        writer.WriteAngle(1.5708f, 10) != CodecError::Ok) {
        return false;
    }
    NetReader reader(writer.Bytes());
    bool flag = false;
    Vec3 position;
    f32 heading = 0.0f;
    return reader.ReadBool(&flag) == CodecError::Ok && reader.ReadVec3(-512.0f, 512.0f, 16, &position) == CodecError::Ok &&
           reader.ReadAngle(10, &heading) == CodecError::Ok && flag;
}
```

## Transports: one interface, polled

`net::Transport` (`engine/net/transport.h`) is what everything above networking uses. It is **polled**: `Poll(&event)` returns the next `net::Event` (`Opened`, `Message`, `Closed`) or false, never blocks and
calls nothing back. A message is delivered whole, never corrupted and never twice; what survives loss depends on the `net::Reliability` chosen per `Send`: `Unreliable`, `UnreliableSequenced` (older than one already
delivered is dropped), `ReliableOrdered` and `ReliableUnordered`, on one of `ChannelCount()` independent channels. `Connect` returns at once and the outcome is an event; `Send` fails with `LimitExceeded` when the
queue is full (backpressure as a value: wait and retry), `MessageTooLarge` above `MaxMessageBytes()`, `Closed` after `Close`. A received message's bytes are valid until the next `Poll`. `Close(connection, code)`
gives the peer the code with `DisconnectReason::PeerClosed`; a connection delivers no event after its `Closed` one and its id is never reused. `Supports` and `DegradesUnreliable` tell what a stream transport cannot do.

### The simulated network and transport

`net::SimNetwork` (`engine/net/sim_network.h`) is a deterministic packet network for tests: per-direction `LinkConfig` (latency, jitter, uniform and burst loss, reordering, duplication, one flipped bit, bandwidth with a
bounded queue, MTU), one seeded generator, 1 ms ticks driven by `Advance`, and an optional trace (`EnableTrace`) that is identical byte for byte for the same seed and calls. `net::SimulatedTransport`
(`sim_transport.h`) is a complete transport over it with its own small reliability protocol (checksummed packets so corruption counts as loss, per-message acknowledgements, retransmission with backoff, a window
of 256, keep-alive and time-out). It is the specification by example of the interface.

### The conformance suite

`tests/net/transport_conformance.h` holds the tests once, as templates over a fixture that makes transports (`MakeServer`, `MakeClient`, `ServerEndpoint`, `DeadEndpoint`, `Destroy`, `Pump`, `Lossy`). The
simulated transport runs them over a clean link and over a hostile one (10% loss, reordering, duplication, corruption); the UDP and TCP transports of Days 274-277 add a fixture each and must pass the same
checks: open and accept, close reasons, a refused connect, reliable streams intact and in order, no duplicates, sequencing, size and channel limits, independent channels, many connections, close during send, no events
after close, and destruction with traffic in flight.

## The UDP transport: packets and the handshake

`net::UdpTransport` (`engine/net/udp_transport.h`) runs over a `net::DatagramPort` (`SocketDatagramPort` on a real non-blocking UDP socket, `SimDatagramPort` on the simulated network) and a `net::Clock`, so the same code is tested
deterministically and over loopback. Day 274 builds the packet layer, the handshake and the connection state machine; Day 275 adds the data plane (next section); congestion control, the round-trip estimate and the application cap are the section after it. The wire format, every integer little-endian, is written out in `engine/net/udp_protocol.h`.

- **Handshake.** `ConnectRequest` (padded to 1200 bytes) -> `Challenge` (17 bytes: the client's salt and a cookie) -> `ChallengeResponse` (padded again, with the cookie) -> `Accepted` (the connection id). The server **keeps nothing**
  until the response validates: the cookie is `SipHash-2-4` under a secret key of the endpoint (address, scope, port), the client's random salt and the time period (`cookie_period_nanos`, a cookie lives two periods). A request
  or response shorter than 1200 bytes is dropped without an answer, and no answer to a stranger is larger than 17 bytes, so the server cannot amplify a flood towards a forged address (tested: a flood of 20,000 spoofed
  packets leaves zero connections and the victim receives under a twentieth of the bytes). Handshake packets repeat with a doubling delay (200 ms to 1 s) until the connect time-out. A replayed response for a live connection is answered again
  without a second connection; for a connection that ended it is dropped (the last 64 are remembered). The connection table is capped (`max_connections`, `max_connections_per_address`); over the cap the client gets `Denied` and a `Refused` close.
- **Packets.** A payload packet carries the connection id, a 16-bit sequence, the newest sequence received and 32 bits for the 32 before it (`udp::ReceiveWindow`, `udp::SendLog`). Comparison is wraparound-safe; a sequence seen before, or more
  than 32 behind the newest, is dropped and counted. A packet from an unknown connection id is answered by a 3-byte `Reset`, which closes the client's connection with `DisconnectReason::Reset`. Acknowledgements ride on payloads; a packet that carried a
  message and nothing else to send is acknowledged by an empty packet after `ack_delay_nanos`, and an idle connection sends one per `keepalive_nanos`. `Close` repeats a `Disconnect` packet `close_repeats` times; silence for `timeout_nanos` is `TimedOut`.
- **Trust.** The cookie stops spoofed-source floods and amplification. It does not authenticate a peer, and nothing is encrypted: until the encryption outcome of Day 280, use the transport on trusted networks only. SipHash is a published keyed hash
  (the published test vectors are in `tests/net/udp_handshake_test.cpp`), not a replacement for the audited primitive Day 280 decides on.

## The UDP transport: channels, reliability and fragmentation

All four `net::Reliability` kinds work on `net::UdpTransport` (`Supports` is true for each). `Send` only queues; the next `Poll` packs and sends, so several small messages share a packet.

- **Frames and packing.** A message becomes one frame (channel, kind, per-channel-and-kind sequence number, length), or fragments of 1024 bytes when it does not fit a packet (`kUdpSingleMax`). A packet takes resent frames first, then the channels in ascending order
  (a lower channel number is a higher priority), up to the 1200-byte MTU. At most 32 packets leave per `Poll` (an acknowledgement covers 33), so a burst cannot outrun its own acknowledgements.
- **Reliability.** A packet's acknowledgement acknowledges its reliable frames. A frame is sent again when its packet is three or more packets behind an acknowledged one, was not acknowledged and is older than the smoothed round trip plus twice its variance (so a
  network that reorders deeply does not get every overtaken packet sent twice), or when its time-out passes (round trip plus four variances, 30 ms to 2 s, doubling per try). At most `window_frames` (256) reliable frames are in flight; `max_queued_frames` bounds the
  rest and `Send` returns `LimitExceeded` when the queue is full. The receiver acknowledges at once after every 8 packets that carried frames, otherwise after `ack_delay_nanos`.
- **Ordering.** `ReliableOrdered` keeps a reorder buffer per channel; `ReliableUnordered` delivers on arrival and remembers the sequence numbers above the lowest missing one; `UnreliableSequenced` drops anything not newer than the newest delivered; `Unreliable` is delivered as received.
  A reliable message more than 1024 sequence numbers ahead of the next expected, or that would exceed the receive byte budget, ends the connection with `DisconnectReason::ProtocolError` (the peer broke a bound it must keep).
- **Fragments are checked exactly before memory is committed.** The first fragment declares the count and the total size; the count, the index, the total (at most `max_message_bytes`), and the exact length of every fragment (1024 except the last, which must make the total) are validated, so fragments cannot
  overlap and a later fragment that disagrees with the first is rejected. The bytes committed for a message are its declared total, counted against `max_receive_buffer_bytes` and `max_reassemblies` (which must be at least the peer's `window_frames`: a legitimate reliable sender can
  have that many messages partial at once). An unreliable message missing a fragment is dropped after `reassembly_timeout_nanos`; for a reliable one a violation ends the connection. Copies of fragments of a message that is already complete are dropped (they cannot start a reassembly nobody would finish).
- **Unconfirmed connections.** A server that accepted a connection and has not yet heard from the client repeats `Accepted` on its own with a growing delay (the client's repeated responses, also lost, are not the only chance), and a client that has not been answered in one cookie period asks for a new cookie.
- **Tested** (`tests/net/udp_channels_test.cpp`): the 13 conformance checks at 0, 5, 30 and 60 percent loss with reordering and duplication; a 1 MiB message intact under each; 400 mixed-size messages once and in order on four channels; a large message on a high channel not delaying small ones on a low channel;
  hand-built hostile fragments (a declared gigabyte, thousands of one-fragment messages, mismatched totals and wrong lengths, a reliable sequence far ahead); and duplicated fragments leaving nothing behind.

## The UDP transport: round trip, congestion control and giving up

- **Round trip.** Every sent packet is a separate record, so a resent frame travels in a new packet with its own send time and an acknowledgement is never ambiguous (the effect of Karn's rule without its bookkeeping). Only the newest packet an acknowledgement reports gives a sample.
  `rtt_seconds` is the smoothed round trip (gain 1/8), `jitter_seconds` its mean deviation (gain 1/4). A frame times out after `1.5 x srtt + 4 x variance` (30 ms to 2 s, doubling per try); a packet is declared lost by three newer acknowledged ones only once it is older than `1.25 x srtt + 4 x variance`.
- **Congestion control** (NewReno-style, in bytes; the unit is one 1200-byte packet). The window starts at 10 packets; below `ssthresh` each acknowledged packet adds its size (slow start), above it `1200 x size / window` (congestion avoidance), and only while the window is at least half full. A loss halves the window (never below 2 packets) and sets
  `ssthresh`, once per round trip (losses of packets sent before the last cut are the same event). A time-out sets `ssthresh` to half and the window to 2 packets. Data leaves only while the bytes in flight are below the window (one packet may always be in flight); acknowledgement-only and keep-alive packets are not held back.
  **Pacing:** once a round trip is known, a token bucket refills at `1.25 x window / srtt` with a burst of 4 packets, so a window opening does not send itself at once. **Application cap:** `max_bandwidth_bytes_per_second` is a second bucket on top (0 for none). The settings system names it `net.max_bandwidth_bytes_per_second` on Day 281.
  The window and flight size are in `ConnectionStats::window_bytes` and `bytes_in_flight`; `UdpConnectionInfo` counts `window_cuts` and `timeouts_fired`.
- **Backpressure.** `Send` returns `LimitExceeded` when `max_queued_frames` is reached. An unreliable message instead discards the oldest queued unreliable frames to make room (counted in `dropped_unreliable`): a reliable message is delayed by a full queue, an unreliable one is not worth waiting for.
- **Silence.** An idle connection sends an empty packet every `keepalive_nanos`; silence for `timeout_nanos` ends it with `TimedOut` (a blackout shorter than that is survived, tested at 5 s against the default 10 s). The path MTU is fixed at 1200 bytes; probing for a larger one is left out.
- **Giving up during the handshake.** A client that closes before it knows the server's connection id sends `Disconnect` with id 0 and the 8-byte salt of its attempt (id 0 is never a real connection); the server closes the connection that peer and salt name. Without it an accepted connection would wait out its time-out.
- **Tested** (`tests/net/udp_congestion_test.cpp`): over 1 Mbit/s and 40 ms with a 20 KB queue a bulk transfer reaches over 90 percent of the cap with under 5 percent of packets dropped by the queue; two connections over one bottleneck share it within a factor of two; loss bursts are recovered; a 5-second blackout is survived and a longer one ends in `TimedOut`;
  the round-trip estimate is within 10 percent over a jittery path; the cap holds within 10 percent; the oldest unreliable messages are discarded; and an exact trace (cuts, resends, packets, time) pins the algorithm.

