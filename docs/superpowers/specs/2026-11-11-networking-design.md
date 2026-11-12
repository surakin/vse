# Networking design (Phase 20)

Written 2026-11-11 (Phase 20 Day 266). Plan: `plans/phase-20-networking.md`; `PLAN.md` assumption #26. The decisions below are the planner's defaults, kept or amended by the spikes of section 3 (throwaway programs, not committed, written with `std` and raw POSIX because they are not engine code; `g++ -O2`, Linux 7.2, Ryzen 9 7950X3D, 16 hardware threads, loopback only).

## 1. Intent

Game traffic, tools, remote debugging, the resource system's network backend and a store over the network all need "send these bytes to that peer, reliably or not, without blocking a frame". One module with a socket layer per OS, readiness on the job system, and a `Transport` interface above it means every higher layer is written once against the interface and tested against a deterministic simulated transport.

Goals: IPv4 and IPv6 sockets through one interface on Linux and Windows; no thread per connection; a UDP transport with the reliability a game needs written here and tested hard; a TCP transport; a simulated transport that every higher layer's tests use; hostile input fuzzed; every length bounded before allocation.
Non-goals: replication and prediction, matchmaking and relay servers, NAT traversal, voice, HTTP and WebSocket clients, raw sockets or anything that needs privileges, a browser platform.
Anti-goals: a socket call outside a backend directory; a thread per connection; a length read from the wire used to allocate before it is checked; custom cryptography.

## 2. Decisions

| Topic | Decision | Reason |
|---|---|---|
| Modules | `engine/net`, above `foundation` (incl. `bytes`) and `jobs`; sockets and readiness in `net/linux/` and `net/windows/` (`net/macos/` designed in, built by Phase 26); the `Transport` interface, the simulated transport, the UDP transport and the TCP transport above them. No ECS, storage or renderer type in `net`. Raw `socket`/`bind`/`connect`/`epoll` calls only in backend directories (gate `net_access_check`, Day 268). | The resource system and a network store depend on `net`; it depends on neither. |
| Addresses and errors | `net::Address` / `net::Endpoint` are value types with no syscalls (parse, format, compare, hash, classify); `net::Status` holds a portable `ErrorCode` (`WouldBlock`, `Refused`, `Reset`, `TimedOut`, `Unreachable`, `AddressInUse`, `Closed`, `Cancelled`, `MessageTooLarge`, `Unsupported`, `Other`) plus the OS code and a short message. No exceptions. | Same shape as `io::Status` and `database::Status`; Windows rules and parsing are testable on Linux. |
| Readiness | **A second non-participant thread, `net::Reactor`, beside `jobs::IoThread`, not an extension of it** (Day 269: the application creates it after the scheduler, since `jobs` cannot name epoll and `net` sits above `jobs`; the registry, timers and cancel logic are neutral and only `net::Poller` is per platform). It runs `epoll_wait` (level-triggered and one-shot, so a socket that is ready when armed is reported at once) on Linux and hands each waiting coroutine to `jobs::Scheduler::Schedule`, the call `jobs::Counter` makes, so the continuation resumes on a participant (the same hand-off `IoThread` uses). Timers share the thread (the `epoll_wait` timeout over a timer heap). Windows: `WSAPoll` over a socket-pair wake-up first, behind the same `Reactor` interface; IOCP is the replacement if Day 278's measurement says `WSAPoll` is too slow. No thread per connection. | Spike 1: the pattern costs 0.74-0.76 `epoll_wait` wake-ups per round trip and 2.6-2.7 events per wake-up at 1,000 connections, and reaches 1.0-1.09 million echo round trips per second with four resuming threads. `IoThread` is a FIFO executor of blocking requests; an `epoll_wait` parked in it would block every file read behind it, so the reactor gets its own thread. |
| Awaitables | `AwaitReadable`, `AwaitWritable`, `Accept`, `Connect`, `Receive`, `Send` (loops until all bytes are taken), `ReceiveFrom`, `SendTo`, `Sleep`; each returns a `Status` plus a count, takes an optional `CancelToken` and timeout. Time comes through a `Clock` interface (`os::MonotonicTimeNanos`; a fake clock for the simulated transport). | Cancel and timeout on everything keeps a frame from hanging; the fake clock makes timing tests deterministic. |
| Wake-up races | Four rules, tested on Day 269: registration after the event already happened is level-checked before sleeping; cancel racing completion has exactly one outcome; a socket closed while a waiter is registered completes the waiter with `Closed`; scheduler shutdown completes every waiter with `Cancelled`. | The classic readiness bugs. |
| Packet format (UDP transport) | One datagram = one packet. Header: 1 byte `kind` (connect-request, challenge, challenge-response, accepted, payload, disconnect, keep-alive) with 3 bits of version in the high bits; payload packets carry a 16-bit connection id chosen by the server, a 32-bit packet sequence (the low 16 bits on the wire, extended by the receiver), a 32-bit acknowledge bitfield for the last 32 packets, then frames. Frames: stream-data (channel, message sequence, fragment index and count), ack-ranges, ping, close. All integers little-endian through `ByteWriter`; every field range-checked on read. | Small fixed header; the acknowledge bitfield is cheap and gives the sender its loss signal without a timer per packet. |
| Handshake | Client sends connect-request padded to the MTU (so a spoofed request cannot amplify), server answers a stateless challenge (an HMAC-style cookie over address, time and a rotating server secret, computed with the engine's hash and a per-boot random key; no per-attempt state kept), client echoes it, server allocates the connection. Rate limits per source address on requests and on allocations. Nothing but the handshake is accepted before the connection exists. The cookie is anti-spoofing only; it gives no confidentiality (section 4). | A spoofed source address cannot make the server allocate or send more than it received. |
| Channels | Per connection up to 8 channels, each one of: unreliable, unreliable-sequenced (late packets dropped), reliable-ordered, reliable-unordered. Messages above the payload size are fragmented (up to 64 fragments, then `MessageTooLarge`) and reassembled with a per-connection memory budget; a reliable message is retransmitted until acknowledged or the connection times out. | The four behaviours games ask for; TCP gives only one of them. |
| MTU | **1200 payload bytes** per datagram (the IPv6 minimum MTU of 1280 less the IPv6 header, the UDP header and the transport header, rounded down); no path MTU discovery in this phase. | Fits every path without fragmentation by the network. |
| Congestion control | NewReno-style congestion window counted in packets: slow start, congestion avoidance, halving on loss detected by the acknowledge bitfield or by a retransmission timeout; send pacing spreads one window over one smoothed round-trip time instead of bursting it. RTT: smoothed with variance as in RFC 6298; retransmission timeout = smoothed RTT + 4 x variance, clamped to [50 ms, 2 s]. Unreliable data is subject to the window too and is dropped, not queued, when the window is full. A per-connection bandwidth budget (a setting) caps the rate. | Conservative, documented, testable on the simulated transport; pacing avoids overrunning a receive buffer (spike 2). |
| Keep-alive and timeout | A keep-alive after 1 s without sending; the connection times out after `net.timeout_ms` (default 10 s) without receiving; the close reason is reported to the caller. | Dead peers are found without a thread. |
| Transport interface | `Transport::Listen`, `Connect`, `Poll` (events: opened, closed with a reason, message), `Send(connection, channel, bytes, reliability)`, `Receive`, `Close`, per-connection `Statistics` (RTT, loss, bytes, packets, congestion window); implemented by `SimulatedTransport`, `UdpTransport` and `TcpTransport`; one conformance suite runs against all three. | The simulated transport proves the interface fits more than one backend. |
| Simulated transport | In-process, seeded and deterministic: loss, reordering, duplication, latency, jitter and bandwidth, driven by a fake clock, so a networking bug can be replayed from its seed. | Every higher layer's tests depend on it. |
| Message encoding | `NetWriter` / `NetReader` extend `ByteWriter` / `ByteReader` (Phase 19 Day 256) with bit packing and quantized floats; every read can fail with a `Status`. | One bounds-checked vocabulary for storage and networking. |
| TCP | Length-prefixed frames with a size limit (`net.max_message_bytes`), a bounded send queue that reports full instead of growing, graceful and abortive close, keep-alive and connect timeouts from settings, `TCP_NODELAY` on by default. | Backpressure is a return value, not memory growth. |
| Resolution | The system resolver (`getaddrinfo`, part of the OS) runs on a job so it never blocks a frame; a timeout; results are `Endpoint`s. Dual-stack listeners where the OS allows (`IPV6_V6ONLY` off). | A frame never waits on DNS. |
| Replication hooks | Recorded, not built: every outgoing datagram carries a sequence number the application can read back for snapshot stamping; a per-connection bandwidth budget the application can query; a per-connection interest callback slot (set by the application, called by nothing in `net`). | So replication of ECS state (a later phase) needs no change to the packet format. |
| Settings | Group `net.*`: `net.port`, `net.timeout_ms`, `net.keepalive_ms`, `net.connect_timeout_ms`, `net.recv_buffer_bytes` (default 4 MiB), `net.send_buffer_bytes` (default 1 MiB), `net.max_message_bytes`, `net.max_connections`, `net.connection_budget_bytes`, `net.handshake_rate_per_second`, and the simulated conditions (`net.sim.loss`, `.reorder`, `.duplicate`, `.latency_ms`, `.jitter_ms`, `.bandwidth_kbps`, `.seed`). | Everything configurable is a setting (Phase 11). |
| Tests | Real sockets: label `network`, loopback and ephemeral ports only; when `bind` or loopback is unavailable the test prints the reason and skips (Catch2 `SKIP`), as the GPU and display tests do. Everything else runs on the simulated transport. | Section 3, the sandbox check. |
| Not in this phase | Replication, prediction, matchmaking, relays, NAT traversal, voice, HTTP, WebSocket, raw sockets, path MTU discovery. | Scope. |

## 3. Spikes

### (1) Readiness: an epoll reactor thread resuming workers

Throwaway program: 1,000 loopback TCP connections (both ends in one process, `TCP_NODELAY`), 200 round trips of 16 bytes each; one reactor thread in `epoll_wait` (edge-triggered) pushes ready ids with a timestamp to a queue, four worker threads stand in for job participants and do the read and the echo or the next write. Two runs:

| Measure | Run 1 | Run 2 |
|---|---|---|
| Round trips | 200,000 in 0.199 s = 1,005,000 per second | 200,000 in 0.184 s = 1,089,000 per second |
| `epoll_wait` wake-ups per round trip | 0.762 | 0.739 |
| Events per wake-up | 2.6 | 2.7 |
| Reactor to worker hand-off latency, p50 / p99 / max | 407 us / 1.46 ms / 2.76 ms | 371 us / 1.45 ms / 2.81 ms |

The hand-off latency includes queueing behind the other 999 ready connections (all are in flight at once), so it measures a saturated queue, not an idle resume; Day 269's test measures an idle resume (one connection, expected tens of microseconds) and the saturated figure through the real scheduler. Conclusion: one reactor thread is far from the limit at 1,000 connections; the cost is the hand-off, which is what `jobs::Counter` completion already does for files. The decision (section 2) is a second thread, not an extension of `IoThread`.

**Windows (for Day 278, not measured here):** `WSAPoll` is O(n) per call and has no edge-triggered mode; at 1,000 sockets it is expected to cost tens of microseconds per call, which is acceptable for the first backend; its wake-up needs a loopback socket pair (no `eventfd`). IOCP gives completion rather than readiness, so it would need a different awaitable shape (post a read, complete when done) and is kept as the replacement behind the same `Reactor` interface only if Day 278 shows `WSAPoll` is the bottleneck. A wepoll-style AFD poll is undocumented and is not used.

### (2) UDP on loopback

Paced sender (busy-wait pacing) of 1200-byte datagrams for 1 s, a dedicated receiving thread blocked in `recv`:

| Rate | Receive buffer | Sent | Received | Lost | Reordered |
|---|---|---|---|---|---|
| 10,000/s | default (208 KiB) | 10,000 | 10,000 | 0 | 0 |
| 10,000/s | 4 MiB requested | 10,000 | 10,000 | 0 | 0 |
| 100,000/s | default | 100,000 | 100,000 | 0 | 0 |
| 100,000/s | 4 MiB requested | 100,000 | 100,000 | 0 | 0 |
| 500,000/s | default | 500,000 | 500,000 | 0 | 0 |
| 500,000/s | 4 MiB requested | 500,000 | 500,000 | 0 | 0 |

So loopback with a receiver that keeps up neither loses nor reorders at any of the three rates. Loss comes from a receiver that stalls. A burst of 20,000 back-to-back 1200-byte datagrams against a receiver that is not reading kept:

| `SO_RCVBUF` requested | Kernel buffer (it doubles the request) | Kept of 20,000 | Lost |
|---|---|---|---|
| default | 208 KiB | 92 | 99.5 % |
| 1 MiB | 2 MiB | 910 | 95.5 % |
| 4 MiB | 8 MiB (this machine's `rmem_max` is 7 MiB, so it is capped) | 3,640 | 81.8 % |
| 16 MiB | capped at `rmem_max` | 6,371 | 68.1 % |

Each 1200-byte datagram occupies about 2.3 KiB of buffer (the kernel's per-packet overhead), so the default buffer holds under 100 datagrams. Conclusions: `net.recv_buffer_bytes` defaults to **4 MiB** (the request is reported with what the kernel granted, and a smaller grant is logged once, not an error); the real-socket UDP tests assert zero loss only for a draining receiver at up to 100,000 datagrams per second and never assert on loss for a stalled or faster one; the loss, reordering and duplication tests and the soak test run on the simulated transport, because loopback gives none; the sender is paced (section 2) because an unpaced burst is the way to overrun even a large buffer.

### (3) QUIC, TLS and encryption (the user's decision)

The comparison is from the libraries' published properties and from the transport design here. **Sizes, build times and per-platform status are from memory and from the projects' documentation, not measured in this session, and must be re-checked when a choice is made; the only measured fact is that this machine already has ngtcp2 1.26 and OpenSSL 3.6.5 installed as distro packages.** The decision belongs to the user; Days 267-279 do not depend on it.

| Option | License | Size, build | Ports to Windows / macOS / Android / iOS / consoles | File-layer and thread rules | Cost to test |
|---|---|---|---|---|---|
| (a) **ngtcp2 + a TLS library** (vendored) | ISC (ngtcp2); the TLS library's own (OpenSSL Apache-2.0, ~1 million lines; mbedTLS Apache-2.0, far smaller; wolfSSL GPL or commercial; BoringSSL ISC-like but not versioned for embedding) | ngtcp2 tens of thousands of lines of C, no thread of its own, you drive it; the TLS library is the large part | Builds everywhere C builds; consoles' certification of a TLS library is the unknown | You feed it datagrams and a clock, so it fits the reactor and the no-ad-hoc-thread rule; certificates and a trust store become our problem | Heavy: interoperability, certificates, version negotiation, loss tests |
| (a) **quiche** (Cloudflare) | BSD-2 | Rust: needs `rustc`/`cargo` in every build | Needs a Rust toolchain per platform and console | Own event model | Heavy; breaks "builds from source with a C++ compiler" |
| (a) **msquic as source** | MIT | Large C codebase, own threads and platform layer (worker threads of its own) | Windows (Schannel), Linux (OpenSSL), macOS, Android, iOS | Its own worker threads break the "one job system" rule unless wrapped | Heavy |
| (b) **The OS's own** | OS terms | None to vendor | Windows 11 and Server 2022 ship MsQuic; Apple has Network.framework QUIC; **Linux has none**, Android and consoles vary | An OS API with its own threads and callbacks, different on every platform | A different transport per OS, so the conformance suite proves less |
| (c) **TLS on the TCP transport only** | the TLS library's | Needs the same TLS library as (a) | Same | A TLS facade used only by `TcpTransport` | Moderate; does nothing for UDP game traffic |
| (d) **Authenticated encryption over the UDP transport on an audited primitive** (for example libsodium, ISC, or Monocypher, CC0 or BSD-2 and about 3,000 lines, audited by an outside firm) | permissive | Small (Monocypher is two files); builds on every platform with a C compiler, including consoles | Anywhere C runs | No threads, no I/O; a pure function over a packet (seal, open) plus a nonce and replay window, so it fits the transport's packet path | Light: known-answer tests, tamper and replay tests on the simulated transport |
| (e) **None in this phase**, QUIC deferred | none | None | n/a | n/a | n/a |

**Recommendation: (d) with (e) for QUIC and for TLS.** The UDP transport already provides what QUIC would add for game traffic (connections, channels, reliability, congestion control); what it lacks is confidentiality and authentication, and (d) adds exactly that at the cost of one small audited library and no certificates. Concretely: a connect token issued by a trusted party (a login or matchmaking server, or a tool that holds the key) carries the server address and a per-session key sealed with the server's key (the netcode.io design); every packet after the handshake is sealed with an AEAD (XChaCha20-Poly1305), with the packet sequence as the nonce and a replay window; direct connection without a trusted issuer uses a pre-shared or pinned server public key (a Noise-style handshake, still on the audited primitive). QUIC and TLS are then a recorded deferral with a trigger: the first need to talk to a third-party server over the Internet (HTTP/3, a cloud storage store), at which point (a) with ngtcp2 and a TLS library is decided with that need in front of us. The cost of choosing (d): the engine owns the protocol composition (a design to be reviewed, tested with known-answer and tamper tests, and not invented beyond the cited designs), and there is no PKI, so *who the server is* rests on the token issuer or the pinned key.

**Stop here for the user's decision before Day 280.** Whichever branch is chosen gets its entry in `PLAN.md` assumption #26 and, if it adds a prerequisite, in `BUILD.md`. Never custom cryptography.

### (4) Sandbox check

Loopback TCP, UDP and IPv6 (`::1`) all `bind` and work in this environment (checked with a 10-line script and by the spikes above). The `network` label is applied by `engine_label_tests` like the `gpu` tag convention; a real-socket test that cannot bind prints `skipped: <reason>` through Catch2's `SKIP` and counts as skipped, not failed.

## 4. Threat notes

An unauthenticated UDP port is reachable by anyone: the handshake cookie stops spoofed-source floods and amplification only; it does not authenticate a peer and it does not hide data. Until the encryption outcome of Day 280 is built, the transports are for trusted networks and the guide says so on its first page. Every parser is fuzzed (Day 279); every allocation from a wire length is bounded by a setting first.

## 5. Benchmark baseline and pass conditions (measured against on Day 281)

Baselines from the spikes (a raw epoll reactor and four resuming threads, no engine layers): 1.0-1.09 million echo round trips per second at 1,000 loopback TCP connections; 0.74-0.76 `epoll_wait` wake-ups per round trip; zero loss and reordering at 500,000 paced 1200-byte datagrams per second on loopback with a draining receiver.

Pass conditions for the engine's layers (planner's defaults, to be confirmed or revised by the user when Day 281 is reached):

| Measure | Pass |
|---|---|
| TCP transport echo, 1,000 connections, 4 participants, 16-byte messages | at least 250,000 round trips per second (a quarter of the raw figure leaves room for the coroutine and framing layers) |
| `epoll_wait` wake-ups per round trip through the engine's reactor | at most 1.0 |
| Idle resume of one coroutine (one connection, one byte) | p99 under 200 microseconds |
| Saturated hand-off (1,000 connections in flight) | p99 under 5 ms |
| UDP transport, one connection pair on loopback, reliable-ordered channel | at least 50,000 messages per second of 64 bytes with no message lost or reordered; unreliable channel at 100,000 datagrams per second with zero loss when the receiver drains |
| CPU per idle connection | no wake-ups and no measurable CPU with 10,000 idle simulated connections beyond the keep-alive timers |
| Soak | 10,000 simulated connections for 10 simulated minutes at 30 messages per second each, with 5 % loss, 2 % reordering and 1 % duplication: every reliable message delivered in order exactly once, memory within `net.connection_budget_bytes` x connections |
| Congestion | on a simulated 10 Mbit/s, 50 ms link the sender reaches at least 80 % of the link rate and its queue delay does not grow without bound |

## 6. Hooks recorded for later phases

The resource system (Phase 21) and a network store (Phase 19's `Store` interface) use `Transport` as it is. Replication (not scheduled) reads the packet sequence number, the per-connection bandwidth budget and the interest callback slot of section 2; the spec of that phase decides the rest.
