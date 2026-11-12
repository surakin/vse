#pragma once

//! [[net::RateLimiter]]: token buckets per address and in total, and a ban list with expiry, in memory that never
//! grows.
//!
//! The transports ask it before they spend work or memory on a stranger: a handshake packet (UDP), an accepted stream
//! (TCP). A request is allowed when the address's bucket and the global bucket both hold a token. An address that
//! misbehaves (a cookie that does not check, a stream that breaks the protocol before its handshake is done) is given a
//! strike; enough strikes ban it for a while and everything it sends is refused without further work.
//!
//! **Time.** Buckets refill from the caller's [[net::Clock]] time (nanoseconds, never going back), so a test with a
//! manual clock decides exactly what is allowed. A bucket holds at most `burst` tokens and refills at `rate` per
//! second.
//!
//! **Memory.** The per-address state lives in a table of `max_addresses` entries allocated once. An address hashes to a
//! bucket of 8 entries; a new address takes a free or idle entry of its bucket, else the entry seen longest ago that is
//! not banned. An attacker that rotates through many addresses can therefore push other addresses' state out (they
//! start again with a full bucket) but never grows the table; the global bucket still bounds the total rate, and a ban
//! lasts until it expires unless the bucket holds nothing but bans.

#include "containers/array.h"
#include "containers/vector.h"
#include "net/address.h"
#include "types.h"

namespace engine_placeholder::net
{

/// The key under which the limits count an address: an IPv4 address as it is, an IPv6 address by its /64 (one
/// subscriber's allocation).
/// - Param a: the address.
/// - Returns: the address with the host part of an IPv6 address cleared.
inline Address AddressGroup(const Address& a)
{
    if (!a.IsV6())
    {
        return a;
    }
    Array<u8, 16> bytes = a.Bytes();
    for (usize i = 8; i < 16; ++i)
    {
        bytes[i] = 0;
    }
    return Address::V6(bytes, 0);
}

/// Limits of a [[net::RateLimiter]]; a rate of zero turns that limit off.
///
/// - Thread-safety: thread-compatible plain data.
struct RateLimiterConfig
{
    f64 total_per_second = 0;    ///< Requests per second over all addresses; 0 for no limit.
    f64 total_burst = 0;         ///< Tokens the global bucket holds at most (at least 1 when the rate is not 0).
    f64 address_per_second = 0;  ///< Requests per second of one address; 0 for no limit.
    f64 address_burst = 0;       ///< Tokens one address's bucket holds at most (at least 1 when the rate is not 0).
    usize max_addresses = 4096;  ///< Entries of the per-address table (rounded up to a multiple of 8, at least 8).
    u32 strikes_to_ban = 3;      ///< Strikes that ban an address; 0 never bans.
    u64 strike_window_nanos = 60'000'000'000ull;  ///< Strikes older than this are forgotten.
    u64 ban_nanos = 300'000'000'000ull;           ///< How long a ban lasts.
};

/// What the limiter has refused, for tests and diagnostics.
///
/// - Thread-safety: thread-compatible plain data.
struct RateLimiterCounters
{
    u64 allowed = 0;          ///< Requests allowed.
    u64 refused_total = 0;    ///< Refused by the global bucket.
    u64 refused_address = 0;  ///< Refused by an address's bucket.
    u64 refused_banned = 0;   ///< Refused because the address is banned.
    u64 bans = 0;             ///< Bans started.
};

/// Token buckets and a bounded ban list.
///
/// - Thread-safety: thread-compatible; the thread that owns the transport uses it.
class RateLimiter
{
public:
    /// Creates a limiter and allocates its table.
    /// - Param config: the limits.
    explicit RateLimiter(const RateLimiterConfig& config = {});

    /// Asks whether a request from `address` may proceed, and takes a token from its bucket and from the global one if
    /// so.
    /// - Param address: the sender (the network part is used; see [[net::Address]]).
    /// - Param now_nanos: the current time.
    /// - Returns: true if allowed.
    /// - Fails: never.
    bool Allow(const Address& address, u64 now_nanos);

    /// Records a misbehavior of `address`; the strike that reaches `strikes_to_ban` bans it.
    /// - Param address: the offender.
    /// - Param now_nanos: the current time.
    void Strike(const Address& address, u64 now_nanos);

    /// Whether `address` is banned now.
    /// - Param address: the address.
    /// - Param now_nanos: the current time.
    /// - Returns: true while a ban lasts.
    bool IsBanned(const Address& address, u64 now_nanos) const;

    /// The number of entries in use (addresses with state, banned ones included).
    /// - Param now_nanos: the current time (entries that expired do not count).
    /// - Returns: the count.
    usize ActiveAddresses(u64 now_nanos) const;

    /// The number of addresses banned now.
    /// - Param now_nanos: the current time.
    /// - Returns: the count.
    usize BannedAddresses(u64 now_nanos) const;

    /// The size of the table.
    /// - Returns: the entries allocated; it never changes.
    usize Capacity() const { return entries_.Size(); }

    /// What was allowed and refused.
    /// - Returns: the counters.
    const RateLimiterCounters& Counters() const { return counters_; }

private:
    struct Entry
    {
        Address address;
        bool used = false;
        f64 tokens = 0;
        u64 refilled = 0;
        u64 last_seen = 0;
        u32 strikes = 0;
        u64 first_strike = 0;
        u64 banned_until = 0;
    };

    Entry* Lookup(const Address& address);
    const Entry* Lookup(const Address& address) const;
    Entry* Claim(const Address& address, u64 now);
    void Refill(Entry* e, u64 now) const;
    bool Idle(const Entry& e, u64 now) const;

    RateLimiterConfig config_;
    Vector<Entry> entries_;
    f64 total_tokens_ = 0;
    u64 total_refilled_ = 0;
    bool total_started_ = false;
    RateLimiterCounters counters_;
};

}  // namespace engine_placeholder::net
