#pragma once

//! [[net::Endpoint]] (an address and a port) and [[net::EndpointSet]] (a bounded list of them, for resolver results).
//! Values; no syscalls, no allocation.

#include "log/assert.h"
#include "net/address.h"

namespace vse::net
{

/// Text of an endpoint in canonical form (`1.2.3.4:80`, `[::1]:80`). Same shape as [[net::AddressText]], with room for
/// the brackets and the port.
///
/// - Thread-safety: immutable value.
struct EndpointText
{
    /// The most characters an endpoint can take.
    static constexpr usize kCapacity = 71;
    /// The characters, followed by a terminating zero.
    Array<char, kCapacity + 1> chars{};
    /// The number of characters before the terminating zero.
    u8 length = 0;

    /// The text as a view, valid while this value lives.
    StringView View() const { return StringView(chars.Data(), length); }
    /// The text as a C string.
    ///
    /// - Ownership: borrowed from this value; valid while it lives.
    /// - Fails: never.
    const char* CStr() const { return chars.Data(); }
};

/// An [[net::Address]] and a port number (host order; port 0 means "any port" when binding).
///
/// - Thread-safety: immutable value.
class Endpoint
{
public:
    /// Creates an endpoint with no address and port 0.
    constexpr Endpoint() = default;
    /// Creates an endpoint from `address` and `port`.
    Endpoint(const Address& address, u16 port) : address_(address), port_(port) {}

    /// Parses `host:port` for IPv4 and `[host]:port` for IPv6.
    ///
    /// The brackets are required (a bare IPv6 address is ambiguous and refused). The host is an address, not a name
    /// (names go through the resolver). The port is decimal, 0 to 65535, with no sign, no leading zeros and no spaces.
    ///
    /// - Ownership: `text` is only read during the call; nothing is retained.
    /// - Param out: receives the endpoint on success; untouched on failure.
    /// - Fails: [[net::ErrorCode::Invalid]] for anything else; `*out` is not written.
    static Status Parse(StringView text, Endpoint* out);

    /// The address.
    const Address& GetAddress() const { return address_; }
    /// The port.
    u16 Port() const { return port_; }
    /// Returns a copy with `port`.
    Endpoint WithPort(u16 port) const { return Endpoint(address_, port); }

    /// Formats the canonical text. An endpoint with no address formats as `:port`, which is for logs:
    /// [[net::Endpoint::Parse]] refuses it.
    /// - Fails: never.
    EndpointText Format() const;

    /// A well-mixed hash of the address and port.
    usize HashValue() const;

    /// Equal when address and port are equal.
    bool operator==(const Endpoint& other) const { return port_ == other.port_ && address_ == other.address_; }
    /// The negation of `operator==`.
    bool operator!=(const Endpoint& other) const { return !(*this == other); }
    /// Total order: address, then port.
    Ordering operator<=>(const Endpoint& other) const;

private:
    Address address_;
    u16 port_ = 0;
};

/// A bounded list of endpoints, the shape of a resolver's answer. Order is the order added (the resolver puts the
/// preferred family first).
///
/// - Thread-safety: thread-compatible plain data.
class EndpointSet
{
public:
    /// The most endpoints a set holds.
    static constexpr usize kCapacity = 16;

    /// Adds `endpoint` at the end. Returns false, adding nothing, when the set is full.
    ///
    /// - Fails: returns false when the set holds [[net::EndpointSet::kCapacity]] endpoints.
    bool Add(const Endpoint& endpoint)
    {
        if (count_ >= kCapacity)
        {
            return false;
        }
        items_[count_++] = endpoint;
        return true;
    }
    /// The number of endpoints.
    usize Count() const { return count_; }
    /// True when the set holds none.
    ///
    /// - Fails: never.
    bool Empty() const { return count_ == 0; }
    /// The endpoint at `index`.
    ///
    /// - Aborts: `index` is not below [[net::EndpointSet::Count]] (checked in debug builds).
    const Endpoint& operator[](usize index) const
    {
        ENGINE_ASSERT(index < count_, "EndpointSet::operator[]: the index is not below Count()");
        return items_[index];
    }
    /// Removes every endpoint.
    void Clear() { count_ = 0; }

private:
    Array<Endpoint, kCapacity> items_{};
    usize count_ = 0;
};

}  // namespace vse::net

namespace vse
{

/// Hash function for [[net::Endpoint]] keys.
///
/// - Thread-safety: thread-safe.
template <> struct Hash<net::Endpoint>
{
    /// Returns [[net::Endpoint::HashValue]].
    usize operator()(const net::Endpoint& e) const { return e.HashValue(); }
};

}  // namespace vse
