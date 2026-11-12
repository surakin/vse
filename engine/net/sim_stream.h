#pragma once

//! [[net::SimStreamNetwork]]: a deterministic in-process model of TCP streams for tests: reliable, in-order bytes with
//! a one-way latency, a bounded send buffer (so writes are partial and block), reads of at most a few bytes at a time
//! on request, an orderly end of stream and refused connections. It has no loss: TCP hides loss from its user, so what
//! a stream user can observe is the delay, the chunking and the end.

#include "containers/shared_ptr.h"
#include "containers/vector.h"
#include "net/clock.h"
#include "net/stream.h"
#include "types.h"

namespace engine_placeholder::net
{

/// What the streams of a [[net::SimStreamNetwork]] do.
///
/// - Thread-safety: thread-compatible plain data.
struct SimStreamConfig
{
    u64 latency_nanos = 5'000'000;        ///< One-way delay of every byte and of a connection attempt.
    usize send_buffer_bytes = 64 * 1024;  ///< Bytes in flight per direction; a write above this is partial or blocks.
    usize max_read_bytes = 0;  ///< If not zero, a read returns at most this many bytes (a stream cut anywhere).
};

/// The simulated network: listeners by endpoint. The time comes from a clock the test moves.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: borrows the clock, which must outlive the network and its providers.
class SimStreamNetwork
{
public:
    /// Creates a network.
    /// - Param clock: the time source.
    /// - Param config: the behavior of its streams.
    SimStreamNetwork(const Clock& clock, const SimStreamConfig& config = {});

    /// An endpoint on the loopback address nobody has taken.
    /// - Returns: a fresh endpoint.
    Endpoint AllocateEndpoint();

    /// Makes a provider whose listener (if it listens) is at `local`.
    /// - Param local: the provider's endpoint.
    /// - Returns: the provider; it unregisters itself when destroyed.
    /// - Ownership: the caller owns the provider; the network must outlive it.
    UniquePtr<StreamProvider> MakeProvider(const Endpoint& local);

private:
    friend class SimStreamProvider;
    struct ListenerEntry
    {
        Endpoint at;
        void* provider = nullptr;
    };

    const Clock* clock_;
    SimStreamConfig config_;
    u16 next_port_ = 30000;
    Vector<ListenerEntry> listeners_;
};

}  // namespace engine_placeholder::net
