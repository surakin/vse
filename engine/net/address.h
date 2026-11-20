#pragma once

//! [[net::Address]]: an IPv4 or IPv6 address as a value, with strict parsing, canonical formatting and classification.
//! No syscalls, no allocation.

#include "containers/array.h"
#include "containers/hash_map.h"
#include "lang/ordering.h"
#include "net/status.h"
#include "string_view.h"
#include "types.h"

namespace vse::net
{

/// The address family of an [[net::Address]].
enum class AddressFamily : u8
{
    None,  ///< A default-constructed address: no family, not usable.
    V4,    ///< IPv4: four bytes.
    V6,    ///< IPv6: sixteen bytes, with an optional scope.
};

/// A fixed-capacity text buffer holding an address or an endpoint in canonical form. No allocation.
///
/// - Thread-safety: immutable value.
struct AddressText
{
    /// The most characters an address can take: 39 for the longest IPv6 form, 1 for `%`, 15 for a scope name, with room
    /// to spare.
    static constexpr usize kCapacity = 63;
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

/// An IPv4 or IPv6 address.
///
/// IPv4 keeps its four bytes; IPv6 keeps sixteen bytes and an optional scope (the zone of a link-local address): a
/// numeric interface index or an interface name of up to 15 characters, which a backend turns into an index. The type
/// has no syscalls, so everything here is testable on any machine, with the rules of any operating system.
///
/// Parsing is strict: dotted decimal IPv4 has four parts of one to three digits each, no leading zero (so none reads as
/// octal), each at most 255; IPv6 follows RFC 4291 text forms (`::`, an embedded dotted IPv4 tail, groups of one to
/// four hex digits), upper or lower case; a single `[` `]` pair around the whole address is accepted; nothing may
/// follow. Formatting is the canonical form of RFC 5952 (lower case, no leading zeros, the longest run of two or more
/// zero groups compressed, the first on a tie, an IPv4-mapped address as `::ffff:a.b.c.d`).
///
/// - Thread-safety: immutable value.
class Address
{
public:
    /// The most characters of a scope name.
    static constexpr usize kMaxScopeNameLength = 15;

    /// Creates an address with no family.
    constexpr Address() = default;

    /// Returns the IPv4 address `a.b.c.d`.
    static Address V4(u8 a, u8 b, u8 c, u8 d);
    /// Returns an IPv6 address from sixteen bytes in network order, with a numeric scope id (zero for none).
    static Address V6(const Array<u8, 16>& bytes, u32 scope_id = 0);
    /// Returns the unspecified address of `family` (`0.0.0.0` or `::`), or an address with no family for
    /// [[net::AddressFamily::None]].
    static Address Any(AddressFamily family);
    /// Returns the loopback address of `family` (`127.0.0.1` or `::1`), or an address with no family for
    /// [[net::AddressFamily::None]].
    static Address Loopback(AddressFamily family);

    /// Parses `text` (forms in the class comment) into `*out`.
    ///
    /// - Param out: receives the address on success; untouched on failure.
    /// - Ownership: `text` is only read during the call; nothing is retained.
    /// - Fails: [[net::ErrorCode::Invalid]] for any text that is not exactly one address; `*out` is not written. Never
    /// aborts, whatever the bytes.
    static Status Parse(StringView text, Address* out);

    /// The family.
    AddressFamily Family() const { return family_; }
    /// True for IPv4.
    ///
    /// - Fails: never.
    bool IsV4() const { return family_ == AddressFamily::V4; }
    /// True for IPv6 (including IPv4-mapped).
    ///
    /// - Fails: never.
    bool IsV6() const { return family_ == AddressFamily::V6; }
    /// True unless the address has no family.
    ///
    /// - Fails: never.
    bool IsValid() const { return family_ != AddressFamily::None; }

    /// The address bytes in network order: four used for IPv4 (the rest zero), sixteen for IPv6.
    const Array<u8, 16>& Bytes() const { return bytes_; }
    /// The numeric scope id of an IPv6 address; zero when there is none or the scope is a name.
    u32 ScopeId() const { return scope_id_; }
    /// The scope name of an IPv6 address (`"eth0"`); empty when there is none or the scope is numeric. Valid while this
    /// address lives.
    StringView ScopeName() const { return StringView(scope_name_.Data(), scope_name_length_); }
    /// Returns a copy with the numeric scope `scope_id` (the name is cleared).
    ///
    /// - Fails: never. A scope on an IPv4 address is ignored.
    Address WithScopeId(u32 scope_id) const;
    /// Writes to `*out` a copy with the scope name `name` (the id is cleared).
    ///
    /// Returns false, with the copy unscoped, when `name` is empty, longer than [[net::Address::kMaxScopeNameLength]],
    /// has a character outside letters, digits, `_`, `-` and `.`, or this is not IPv6.
    ///
    /// - Ownership: `name` is copied; `*out` is written in both cases.
    /// - Fails: returns false as described; never aborts.
    bool WithScopeName(StringView name, Address* out) const;

    /// True for `0.0.0.0` and `::`.
    ///
    /// - Fails: never.
    bool IsAny() const;
    /// True for `127.0.0.0/8`, `::1` and an IPv4-mapped loopback.
    ///
    /// - Fails: never.
    bool IsLoopback() const;
    /// True for `169.254.0.0/16` and `fe80::/10`.
    ///
    /// - Fails: never.
    bool IsLinkLocal() const;
    /// True for `10.0.0.0/8`, `172.16.0.0/12`, `192.168.0.0/16` and the unique-local `fc00::/7` (an IPv4-mapped private
    /// address too).
    ///
    /// - Fails: never.
    bool IsPrivate() const;
    /// True for `224.0.0.0/4` and `ff00::/8`.
    ///
    /// - Fails: never.
    bool IsMulticast() const;
    /// True for an IPv6 address of the form `::ffff:a.b.c.d`.
    ///
    /// - Fails: never.
    bool IsV4Mapped() const;
    /// Returns the IPv4 address inside an IPv4-mapped address, or this address unchanged when it is not one.
    Address Unmapped() const;
    /// Returns the IPv4-mapped IPv6 form of an IPv4 address, or this address unchanged when it is not IPv4.
    Address Mapped() const;

    /// Formats the canonical text (class comment), with `%scope` appended for a scoped IPv6 address. An address with no
    /// family formats as an empty text.
    /// - Fails: never.
    AddressText Format() const;

    /// A well-mixed hash of the family, bytes and scope.
    usize HashValue() const;

    /// Equal when family, bytes and scope (id and name) are equal.
    bool operator==(const Address& other) const;
    /// The negation of `operator==`.
    bool operator!=(const Address& other) const { return !(*this == other); }
    /// Total order: family (none, v4, v6), then bytes, then scope id, then scope name.
    Ordering operator<=>(const Address& other) const;

private:
    AddressFamily family_ = AddressFamily::None;
    u8 scope_name_length_ = 0;
    u32 scope_id_ = 0;
    Array<u8, 16> bytes_{};
    Array<char, kMaxScopeNameLength + 1> scope_name_{};
};

}  // namespace vse::net

namespace vse
{

/// Hash function for [[net::Address]] keys.
///
/// - Thread-safety: thread-safe.
template <> struct Hash<net::Address>
{
    /// Returns [[net::Address::HashValue]].
    usize operator()(const net::Address& a) const { return a.HashValue(); }
};

}  // namespace vse
